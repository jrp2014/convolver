// SPDX-License-Identifier: GPL-2.0-or-later
// Inspection reuses the loader's parser, resolver, selection and decoder, and the
// engine's own validation/accounting, so a report cannot disagree with a render.
#include "convolver/inspection.hpp"
#include "config_parse.hpp"
#include <memory>
#include <stdexcept>

namespace convolver {
namespace {
using detail::ConfigurationError;
using detail::ParsedDocument;

SourcePosition position(const detail::SourceLocation& l) {
    return {l.file, l.line, l.column};
}
std::vector<SourcePosition> chain(const detail::IncludeChain& c) {
    std::vector<SourcePosition> result;
    for (const auto& l : c)
        result.push_back(position(l));
    return result;
}
Diagnostic diagnostic(const ConfigurationError& e) {
    return {e.code(), DiagnosticSeverity::error, e.reason(), position(e.location()),
            chain(e.include_chain())};
}
std::string mismatch(int rate, std::size_t inputs, const InspectionOptions& o) {
    const bool bad_rate = o.expected_rate && o.expected_rate != rate;
    const bool bad_inputs = o.expected_inputs && o.expected_inputs != inputs;
    if (bad_rate && bad_inputs)
        return "rate-and-input-mismatch";
    if (bad_rate)
        return "rate-mismatch";
    return bad_inputs ? "input-mismatch" : "";
}
std::string selectors(const InspectionOptions& o) {
    std::string text;
    if (o.expected_rate)
        text = "sample rate " + std::to_string(o.expected_rate) + " Hz";
    if (o.expected_inputs)
        text += (text.empty() ? "" : " and ") + std::to_string(o.expected_inputs) +
                " input channel" + (o.expected_inputs == 1 ? "" : "s");
    return text;
}

class Inspector {
  public:
    Inspector(const std::filesystem::path& path, const InspectionOptions& options)
        : options_(options) {
        report_.path = path;
        report_.options = options;
    }
    InspectionReport run() {
        if (options_.syntax_only)
            syntax_only();
        else
            resolve();
        bool errors = false;
        for (const auto& d : report_.diagnostics)
            errors = errors || d.severity == DiagnosticSeverity::error;
        report_.valid = !errors && (options_.all_configurations ? !report_.configurations.empty()
                                                                : report_.selected.has_value());
        return std::move(report_);
    }

  private:
    void add(Diagnostic d) {
        report_.diagnostics.push_back(std::move(d));
    }
    void fail(const ConfigurationError& e) {
        add(diagnostic(e));
        report_.traversal.push_back(
            {e.location().file, chain(e.include_chain()), "failed", std::nullopt, std::nullopt});
    }
    InspectedConfiguration leaf(const ParsedDocument& doc, const detail::IncludeChain& c) {
        const auto& p = doc.configuration;
        InspectedConfiguration result;
        result.file = doc.source;
        result.include_chain = chain(c);
        result.sample_rate = p.sample_rate;
        result.input_channels = p.input_channels;
        result.output_channels = p.output_channels;
        result.channel_mask = p.channel_mask;
        for (const auto& d : p.input_delays)
            result.input_delays.push_back({d.token, d.milliseconds, d.frames});
        for (const auto& d : p.output_delays)
            result.output_delays.push_back({d.token, d.milliseconds, d.frames});
        for (const auto& path : p.paths) {
            InspectedFilterPath f;
            f.reference = path.ir.spelling;
            f.location = position(path.ir.location);
            f.ir_channel = path.ir_channel;
            try {
                f.resolved = detail::resolve_reference(doc.source, path.ir);
            } catch (const ConfigurationError& e) {
                // Only syntax-only inspection reaches this: the resolver already
                // resolved every reference of a visited configuration.
                add(diagnostic(e));
            }
            for (const auto& r : path.inputs)
                f.inputs.push_back({r.token, r.channel, r.gain});
            for (const auto& r : path.outputs)
                f.outputs.push_back({r.token, r.channel, r.gain});
            result.paths.push_back(std::move(f));
        }
        return result;
    }
    InspectedConfiguration sound_leaf(const std::filesystem::path& path, const AudioInfo& info,
                                      const detail::IncludeChain& c) {
        InspectedConfiguration result;
        result.file = path;
        result.include_chain = chain(c);
        result.sound_file = true;
        result.sample_rate = info.sample_rate;
        result.input_channels = result.output_channels = info.channels;
        result.input_delays.resize(info.channels);
        result.output_delays.resize(info.channels);
        for (std::size_t ch = 0; ch < info.channels; ++ch) {
            InspectedFilterPath f;
            f.reference = detail::utf8_string(path);
            f.location = {path, 0, 0};
            f.resolved = path;
            f.ir_channel = ch;
            f.inputs.push_back({std::nullopt, ch, 1});
            f.outputs.push_back({std::nullopt, ch, 1});
            result.paths.push_back(std::move(f));
        }
        return result;
    }
    // Open and decode the IRs and run the engine's validation and accounting.
    void verify(InspectedConfiguration& leaf, Configuration cfg) {
        try {
            const auto analysis = analyze_configuration(cfg, options_.block_frames);
            for (std::size_t i = 0; i < leaf.paths.size(); ++i)
                leaf.paths[i].partitions = analysis.partitions_per_path[i];
            leaf.analysis = analysis;
            leaf.coefficients_verified = true;
        } catch (const std::invalid_argument& e) {
            add({"engine", DiagnosticSeverity::error, e.what(), {leaf.file, 0, 0}, {}});
        } catch (const std::length_error& e) {
            add({"limit", DiagnosticSeverity::error, e.what(), {leaf.file, 0, 0}, {}});
        }
    }
    bool visit(const detail::Resolver::Leaf& l) {
        const auto errors = report_.diagnostics.size();
        InspectedConfiguration result;
        int rate = 0;
        std::size_t inputs = 0;
        if (l.audio) {
            const auto info = l.audio->info();
            rate = info.sample_rate;
            inputs = info.channels;
            result = sound_leaf(l.path, info, l.chain);
        } else {
            rate = l.document->configuration.sample_rate;
            inputs = l.document->configuration.input_channels;
            result = leaf(*l.document, l.chain);
        }
        SelectionStep step{l.path, chain(l.chain), "", rate, inputs};
        if (!options_.all_configurations) {
            step.outcome = mismatch(rate, inputs, options_);
            if (!step.outcome.empty()) {
                // As in rendering, an unselected entry's IRs are not opened.
                report_.traversal.push_back(std::move(step));
                return false;
            }
            report_.selected = l.path;
        }
        step.outcome = options_.all_configurations ? "validated" : "selected";
        report_.traversal.push_back(std::move(step));
        try {
            if (l.audio) {
                auto cfg = detail::decode_audio_filter(*l.audio, budget_);
                for (auto& f : result.paths) {
                    f.ir_sample_rate = rate;
                    f.ir_file_channels = inputs;
                    f.ir_frames = cfg.paths[f.ir_channel].impulse.size();
                }
                verify(result, std::move(cfg));
            } else {
                auto cfg = detail::configuration_graph(l.document->configuration);
                const auto decoded = detail::decode_impulses(*l.document, cfg, budget_);
                for (std::size_t i = 0; i < decoded.size(); ++i) {
                    auto& f = result.paths[i];
                    f.resolved = decoded[i].resolved;
                    f.ir_sample_rate = decoded[i].info.sample_rate;
                    f.ir_file_channels = decoded[i].info.channels;
                    f.ir_frames = decoded[i].frames;
                }
                verify(result, std::move(cfg));
            }
        } catch (ConfigurationError& e) {
            // The resolver adds list context to exceptions it sees; this one is
            // handled here, so attach this leaf's chain directly.
            for (auto it = l.chain.rbegin(); it != l.chain.rend(); ++it)
                e.included_from(*it);
            add(diagnostic(e));
        }
        result.valid = report_.diagnostics.size() == errors;
        report_.configurations.push_back(std::move(result));
        // Selected rendering stops at the first match; all-entry mode continues.
        return !options_.all_configurations;
    }
    void resolve() {
        detail::Resolver::ErrorHandler on_error;
        if (options_.all_configurations)
            on_error = [this](const ConfigurationError& e) {
                fail(e);
                return true;
            };
        detail::Resolver resolver([this](const detail::Resolver::Leaf& l) { return visit(l); },
                                  on_error,
                                  [this](const ParsedDocument& doc, const detail::IncludeChain& c) {
                                      report_.traversal.push_back({doc.source, chain(c), "list",
                                                                   std::nullopt, std::nullopt});
                                  });
        bool stopped = false;
        try {
            stopped = resolver.run(report_.path);
        } catch (const ConfigurationError& e) {
            fail(e);
            return;
        }
        if (options_.all_configurations) {
            report_.selection_reason = "every entry validated (--all-configurations)";
            return;
        }
        const auto wanted = selectors(options_);
        if (!stopped) {
            add({"no-match",
                 DiagnosticSeverity::error,
                 "no configuration matches " + (wanted.empty() ? "" : wanted),
                 {report_.path, 0, 0},
                 {}});
            report_.selection_reason = "no entry matches " + wanted;
            return;
        }
        const bool list = !report_.traversal.empty() && report_.traversal.front().outcome == "list";
        if (!list)
            report_.selection_reason =
                wanted.empty() ? "single configuration" : "single configuration matching " + wanted;
        else
            report_.selection_reason = wanted.empty() ? "first entry (no selectors given)"
                                                      : "first entry matching " + wanted;
    }
    void syntax_only() {
        const auto& path = report_.path;
        report_.selection_reason = "syntax-only check of one configuration file";
        std::unique_ptr<AudioReader> audio;
        try {
            audio = std::make_unique<AudioReader>(path);
        } catch (const std::runtime_error&) { /* Not a sound file: parse as text. */
        }
        auto scope = [&](const char* what) {
            add({"syntax-only-scope",
                 DiagnosticSeverity::error,
                 std::string(what) +
                     "; --syntax-only checks one configuration text file. Inspect without "
                     "--syntax-only to verify sound files and lists",
                 {path, 0, 0},
                 {}});
        };
        if (audio) {
            scope("this is a sound file");
            return;
        }
        ParsedDocument doc;
        try {
            doc = detail::parse_configuration_text(detail::read_configuration_text(path), path);
        } catch (const ConfigurationError& e) {
            fail(e);
            return;
        }
        const auto& p = doc.configuration;
        if (doc.kind == ParsedDocument::Kind::list) {
            report_.traversal.push_back({path, {}, "list", std::nullopt, std::nullopt});
            scope("this is a configuration list");
            return;
        }
        const auto outcome = mismatch(p.sample_rate, p.input_channels, options_);
        report_.traversal.push_back(
            {path, {}, outcome.empty() ? "selected" : outcome, p.sample_rate, p.input_channels});
        if (!outcome.empty())
            add({"no-match",
                 DiagnosticSeverity::error,
                 "configuration does not match " + selectors(options_),
                 {path, p.header_line, 0},
                 {}});
        else
            report_.selected = path;
        auto result = leaf(doc, {});
        bool errors = false;
        for (const auto& d : report_.diagnostics)
            errors = errors || d.severity == DiagnosticSeverity::error;
        result.valid = !errors;
        report_.configurations.push_back(std::move(result));
    }

    InspectionOptions options_;
    InspectionReport report_;
    std::size_t budget_ = 0;
};
} // namespace

InspectionReport inspect_configuration(const std::filesystem::path& path,
                                       const InspectionOptions& options) {
    if (options.expected_rate < 0 || options.expected_rate > 768000 ||
        options.expected_inputs > 256)
        throw std::invalid_argument("expected audio properties out of range");
    if (!options.block_frames || options.block_frames > (1u << 20))
        throw std::invalid_argument("block frames must be between 1 and 1048576");
    if (options.all_configurations && (options.expected_rate || options.expected_inputs))
        throw std::invalid_argument(
            "all-configuration validation cannot be combined with rate or input selectors");
    if (options.all_configurations && options.syntax_only)
        throw std::invalid_argument("syntax-only and all-configuration modes are exclusive");
    if (path.empty() || path == "-")
        throw std::invalid_argument("configuration must be a file");
    return Inspector(path, options).run();
}
} // namespace convolver
