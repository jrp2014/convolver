// SPDX-License-Identifier: GPL-2.0-or-later
// Inspection reuses the loader's parser, resolver, selection and decoder, and the
// engine's own validation/accounting, so a report cannot disagree with a render.
#include "convolver/inspection.hpp"
#include "config_parse.hpp"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

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
std::string mismatch(int rate, std::size_t inputs, std::size_t outputs,
                     const InspectionOptions& o) {
    std::string text;
    auto add = [&](bool bad, const char* name) {
        if (bad)
            text += (text.empty() ? "" : "-and-") + std::string(name);
    };
    add(o.expected_rate && o.expected_rate != rate, "rate");
    add(o.expected_inputs && o.expected_inputs != inputs, "input");
    add(o.expected_outputs && o.expected_outputs != outputs, "output");
    return text.empty() ? text : text + "-mismatch";
}
ConfigurationSelection selection(const InspectionOptions& o) {
    return {o.expected_rate, o.expected_inputs, o.expected_outputs};
}
std::string selectors(const InspectionOptions& o) {
    std::string text;
    auto add = [&](const std::string& part) { text += (text.empty() ? "" : " and ") + part; };
    if (o.expected_rate)
        add("sample rate " + std::to_string(o.expected_rate) + " Hz");
    if (o.expected_inputs)
        add(std::to_string(o.expected_inputs) + " input channel" +
            (o.expected_inputs == 1 ? "" : "s"));
    if (o.expected_outputs)
        add(std::to_string(o.expected_outputs) + " output channel" +
            (o.expected_outputs == 1 ? "" : "s"));
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
        if (dropped_)
            report_.diagnostics.push_back({"diagnostics-truncated",
                                           DiagnosticSeverity::warning,
                                           std::to_string(dropped_) +
                                               " further diagnostics were not reported (limit " +
                                               std::to_string(max_diagnostics) + ")",
                                           {},
                                           {}});
        report_.valid = !errors_ && (options_.all_configurations ? !report_.configurations.empty()
                                                                 : report_.selected.has_value());
        return std::move(report_);
    }

  private:
    void add(Diagnostic d) {
        if (d.severity == DiagnosticSeverity::error)
            ++errors_;
        if (report_.diagnostics.size() < max_diagnostics)
            report_.diagnostics.push_back(std::move(d));
        else
            ++dropped_;
    }
    void fail(const ConfigurationError& e) {
        add(diagnostic(e));
        report_.traversal.push_back({e.location().file, chain(e.include_chain()), "failed",
                                     std::nullopt, std::nullopt, std::nullopt});
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
        const auto errors = errors_;
        InspectedConfiguration result;
        int rate = 0;
        std::size_t inputs = 0, outputs = 0;
        if (l.audio) {
            const auto info = l.audio->info();
            rate = info.sample_rate;
            inputs = outputs = info.channels;
            result = sound_leaf(l.path, info, l.chain);
        } else {
            rate = l.document->configuration.sample_rate;
            inputs = l.document->configuration.input_channels;
            outputs = l.document->configuration.output_channels;
            result = leaf(*l.document, l.chain);
        }
        SelectionStep step{l.path, chain(l.chain), "", rate, inputs, outputs};
        candidates_.push_back({l.path, l.audio != nullptr, rate, inputs, outputs});
        if (!options_.all_configurations) {
            step.outcome = mismatch(rate, inputs, outputs, options_);
            if (!step.outcome.empty()) {
                // As in rendering, an unselected entry's IRs are not opened.
                report_.traversal.push_back(std::move(step));
                return false;
            }
            report_.selected = l.path;
        }
        step.outcome = options_.all_configurations ? "validated" : "selected";
        report_.traversal.push_back(std::move(step));
        if (l.document)
            portability(*l.document, l.chain);
        // Rendering decodes one leaf, so each validated entry gets the full budget.
        if (options_.all_configurations)
            budget_ = 0;
        try {
            if (l.audio) {
                auto cfg = detail::decode_audio_filter(*l.audio, budget_, {l.path, 0, 0});
                for (auto& f : result.paths) {
                    f.ir_sample_rate = rate;
                    f.ir_file_channels = inputs;
                    f.ir_frames = cfg.paths[f.ir_channel].impulse.size();
                }
                verify(result, std::move(cfg));
            } else {
                auto cfg = detail::configuration_graph(l.document->configuration);
                const auto decoded = detail::decode_impulses(*l.document, cfg, budget_,
                                                             options_.raw_filter_encoding);
                for (std::size_t i = 0; i < decoded.size(); ++i) {
                    auto& f = result.paths[i];
                    f.resolved = decoded[i].resolved;
                    f.ir_sample_rate = decoded[i].info.sample_rate;
                    f.ir_file_channels = decoded[i].info.channels;
                    f.ir_frames = decoded[i].frames;
                    f.raw_encoding = decoded[i].raw;
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
        result.valid = errors_ == errors;
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
        detail::Resolver resolver(
            [this](const detail::Resolver::Leaf& l) { return visit(l); }, on_error,
            [this](const ParsedDocument& doc, const detail::IncludeChain& c) {
                report_.traversal.push_back(
                    {doc.source, chain(c), "list", std::nullopt, std::nullopt, std::nullopt});
                portability(doc, c);
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
            const bool list =
                !report_.traversal.empty() && report_.traversal.front().outcome == "list";
            add({"no-match",
                 DiagnosticSeverity::error,
                 detail::explain_no_match(report_.path, list, candidates_, selection(options_),
                                          "the requested selection"),
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
            report_.traversal.push_back(
                {path, {}, "list", std::nullopt, std::nullopt, std::nullopt});
            scope("this is a configuration list");
            return;
        }
        const auto outcome = mismatch(p.sample_rate, p.input_channels, p.output_channels, options_);
        report_.traversal.push_back({path,
                                     {},
                                     outcome.empty() ? "selected" : outcome,
                                     p.sample_rate,
                                     p.input_channels,
                                     p.output_channels});
        if (!outcome.empty())
            add({"no-match",
                 DiagnosticSeverity::error,
                 detail::explain_no_match(
                     path, false,
                     {{path, false, p.sample_rate, p.input_channels, p.output_channels}},
                     selection(options_), "the requested selection"),
                 {path, p.header_line, 0},
                 {}});
        else
            report_.selected = path;
        auto result = leaf(doc, {});
        portability(doc, {});
        result.valid = !errors_;
        report_.configurations.push_back(std::move(result));
    }

    // InspectionProfile::portable_paths: warn about references that work here but
    // will not travel. Nothing is rewritten and selection is unaffected.
    void portability(const ParsedDocument& doc, const detail::IncludeChain& c) {
        if (options_.profile != InspectionProfile::portable_paths)
            return;
        const auto& refs = doc.kind == ParsedDocument::Kind::list ? doc.entries : [&] {
            std::vector<detail::ParsedReference> irs;
            for (const auto& p : doc.configuration.paths)
                irs.push_back(p.ir);
            return irs;
        }();
        for (const auto& ref : refs)
            for (auto& [code, message] : portability_problems(doc.source, ref.spelling))
                add({code, DiagnosticSeverity::warning, message, position(ref.location), chain(c)});
        if (doc.kind != ParsedDocument::Kind::configuration)
            return;
        const auto encoding = raw_encoding_name(options_.raw_filter_encoding);
        const std::string others =
            "other programs that read .cfg files expect sound files such as WAV";
        for (const auto& p : doc.configuration.paths) {
            std::string message;
            // Syntax-only inspection opens no filter, so the name is all it has.
            if (options_.syntax_only) {
                if (detail::raw_filter_name(lexical_reference(p.ir.spelling)))
                    message = "'" + p.ir.spelling +
                              "' is named like a raw filter (.pcm or .raw); if it has no "
                              "sound-file header, Convolver reads it as mono " +
                              encoding + ", but " + others +
                              " (--syntax-only did not open it to check)";
            } else if (raw_filter_reference(doc.source, p.ir))
                message = "'" + p.ir.spelling + "' is a raw filter; Convolver reads it as mono " +
                          encoding + ", but " + others;
            if (!message.empty())
                add({"raw-filter", DiagnosticSeverity::warning, message, position(p.ir.location),
                     chain(c)});
        }
    }
    static std::filesystem::path lexical_reference(std::string spelling) {
        std::replace(spelling.begin(), spelling.end(), '\\', '/');
        return std::filesystem::path(std::u8string(spelling.begin(), spelling.end()));
    }
    // Opens the file to look for a sound-file header: not for syntax-only use.
    static bool raw_filter_reference(const std::filesystem::path& owner,
                                     const detail::ParsedReference& ref) {
        try {
            return detail::raw_filter(detail::resolve_reference(owner, ref));
        } catch (const ConfigurationError&) {
            return false; // A nonportable path is reported on its own.
        }
    }
    std::vector<std::pair<std::string, std::string>>
    portability_problems(const std::filesystem::path& owner, const std::string& spelling) const {
        std::vector<std::pair<std::string, std::string>> found;
        const auto quoted = "'" + spelling + "'";
        for (const char bad : std::string_view("<>\"|?*"))
            if (spelling.find(bad) != std::string::npos) {
                found.emplace_back("windows-invalid-name",
                                   quoted + " contains a character Windows filenames cannot use");
                break;
            }
        const auto lexical = lexical_reference(spelling);
        if (lexical.has_root_name() || lexical.has_root_directory()) {
            found.emplace_back("absolute-reference",
                               quoted + " is an absolute path, which only resolves on this "
                                        "machine; give the path relative to the configuration");
            return found;
        }
        std::error_code ec;
        const auto root =
            std::filesystem::absolute(report_.path, ec).parent_path().lexically_normal();
        const auto target =
            std::filesystem::absolute(owner.parent_path() / lexical, ec).lexically_normal();
        const auto inside = target.lexically_relative(root);
        if (!ec && (inside.empty() || *inside.begin() == ".."))
            found.emplace_back("outside-tree",
                               quoted + " points outside the folder of " +
                                   detail::utf8_string(report_.path.filename()) +
                                   "; a copy of that folder alone will not include it");
        // Compare each component with the stored directory entry, so case differences
        // are found even where this filesystem ignores case.
        auto dir = owner.parent_path();
        for (const auto& part : lexical) {
            const auto name = part.u8string();
            if (name.empty() || name == u8".")
                continue;
            if (name == u8"..") {
                dir /= part;
                continue;
            }
            std::optional<std::u8string> folded;
            bool exact = false;
            std::size_t seen = 0;
            for (auto it = std::filesystem::directory_iterator(dir.empty() ? "." : dir, ec);
                 !ec && it != std::filesystem::directory_iterator() && ++seen < 100000;
                 it.increment(ec)) {
                const auto entry = it->path().filename().u8string();
                if (entry == name) {
                    exact = true;
                    break;
                }
                if (!folded && entry.size() == name.size() &&
                    std::equal(entry.begin(), entry.end(), name.begin(), [](char8_t a, char8_t b) {
                        auto lower = [](char8_t x) {
                            return x >= u8'A' && x <= u8'Z' ? static_cast<char8_t>(x + 32) : x;
                        };
                        return lower(a) == lower(b);
                    }))
                    folded = entry;
            }
            if (exact) {
                dir /= part;
                continue;
            }
            if (folded)
                found.emplace_back("case-mismatch",
                                   quoted + " differs in letter case from the disk: '" +
                                       std::string(name.begin(), name.end()) + "' is '" +
                                       std::string(folded->begin(), folded->end()) +
                                       "'; case-sensitive filesystems will not find it");
            break;
        }
        return found;
    }
    static constexpr std::size_t max_diagnostics = 256;
    InspectionOptions options_;
    InspectionReport report_;
    std::size_t errors_ = 0, dropped_ = 0;
    std::size_t budget_ = 0;
    std::vector<detail::SelectionCandidate> candidates_; // Leaves visited, for no-match help.
};
} // namespace

InspectionReport inspect_configuration(const std::filesystem::path& path,
                                       const InspectionOptions& options) {
    if (options.expected_rate < 0 || options.expected_rate > 768000 ||
        options.expected_inputs > 256 || options.expected_outputs > 256)
        throw std::invalid_argument("expected audio properties out of range");
    if (options.raw_filter_encoding.sample == SampleType::pcm8)
        throw std::invalid_argument("raw filters have no 8-bit sample type");
    if (!options.block_frames || options.block_frames > (1u << 20))
        throw std::invalid_argument("block frames must be between 1 and 1048576");
    if (options.all_configurations &&
        (options.expected_rate || options.expected_inputs || options.expected_outputs))
        throw std::invalid_argument(
            "all-configuration validation cannot be combined with rate or channel selectors");
    if (options.all_configurations && options.syntax_only)
        throw std::invalid_argument("syntax-only and all-configuration modes are exclusive");
    if (path.empty() || path == "-")
        throw std::invalid_argument("configuration must be a file");
    return Inspector(path, options).run();
}
} // namespace convolver
