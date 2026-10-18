// SPDX-License-Identifier: GPL-2.0-or-later
// Loader orchestration: list traversal and selection, reference resolution and IR
// decoding. Text grammar lives in config_parse.cpp.
#include "config_parse.hpp"
#include "convolver/io.hpp"
#include <algorithm>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace convolver::detail {
namespace {
void read_impulses(AudioReader& reader, std::vector<FilterPath>& paths,
                   std::optional<std::size_t> selected, std::size_t& budget,
                   const SourceLocation& where) {
    const auto channels = reader.info().channels;
    if (selected && *selected >= channels)
        throw ConfigurationError("ir-channel", where,
                                 "filter channel " + std::to_string(*selected) +
                                     " out of range (file has " + std::to_string(channels) +
                                     " channels)");
    // Reject oversized files before allocating for their samples.
    if (const auto declared = reader.declared_frames()) {
        if (*declared > (max_decoded_samples - budget) / channels)
            throw ConfigurationError("limit", where, "decoded impulse data exceeds 64 Mi samples");
        if (selected)
            paths.back().impulse.reserve(*declared);
        else
            for (auto& p : paths)
                p.impulse.reserve(*declared);
    }
    std::vector<float> buffer(4096 * channels);
    while (const auto frames = reader.read(buffer)) {
        const auto samples = frames * channels;
        if (samples > max_decoded_samples - budget)
            throw ConfigurationError("limit", where, "decoded impulse data exceeds 64 Mi samples");
        budget += samples;
        if (selected) {
            for (std::size_t f = 0; f < frames; ++f)
                paths.back().impulse.push_back(buffer[f * channels + *selected]);
        } else {
            for (std::size_t c = 0; c < channels; ++c)
                for (std::size_t f = 0; f < frames; ++f)
                    paths[c].impulse.push_back(buffer[f * channels + c]);
        }
    }
    reader.close();
    if (paths.back().impulse.empty())
        throw ConfigurationError("empty-ir", where, "empty impulse response");
}
std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
#ifndef _WIN32
bool drive_letter(const std::string& name) {
    return name.size() >= 2 && name[1] == ':' &&
           ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z'));
}
// The longest trailing part of a Windows reference that exists beside owner,
// offered as a suggested replacement. It is never loaded implicitly.
std::string existing_suffix(const std::filesystem::path& owner, std::string name,
                            const FileExists& exists) {
    std::replace(name.begin(), name.end(), '\\', '/');
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= name.size()) {
        const auto end = std::min(name.find('/', start), name.size());
        if (end > start)
            parts.push_back(name.substr(start, end - start));
        start = end + 1;
    }
    if (!parts.empty() && drive_letter(parts.front()))
        parts.front().erase(0, 2);
    if (!parts.empty() && parts.front().empty())
        parts.erase(parts.begin());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        std::string candidate;
        for (std::size_t j = i; j < parts.size(); ++j)
            candidate += (candidate.empty() ? "" : "/") + parts[j];
        const auto path = owner.parent_path() / utf8_path(candidate);
        std::error_code ec;
        if (exists ? exists(path) : std::filesystem::is_regular_file(path, ec))
            return candidate;
    }
    return {};
}
#endif
} // namespace

std::string read_configuration_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw ConfigurationError("unreadable", {path, 0, 0}, "cannot read " + utf8_string(path));
    std::string text(max_text_bytes + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (file.bad())
        throw ConfigurationError("unreadable", {path, 0, 0}, "read failed: " + utf8_string(path));
    text.resize(static_cast<std::size_t>(file.gcount()));
    return text;
}

std::filesystem::path resolve_reference(const std::filesystem::path& owner,
                                        const ParsedReference& ref,
                                        [[maybe_unused]] const FileExists& exists) {
    // The historical format originated on Windows. Treat both separators alike
    // in file references, without interpreting drive letters as POSIX filenames.
    const auto& name = ref.spelling;
#ifndef _WIN32
    if (drive_letter(name) || (!name.empty() && name.front() == '\\')) {
        const auto kind = drive_letter(name)         ? "drive-letter"
                          : name.starts_with("\\\\") ? "UNC network"
                                                     : "backslash-rooted";
        std::string why = std::string("Windows drive/rooted paths must be changed to relative "
                                      "paths on this platform: ") +
                          name + " is a " + kind + " path with no meaning here; ";
        const auto suggestion = existing_suffix(owner, name, exists);
        if (!suggestion.empty())
            why += "a file exists at '" + suggestion +
                   "' beside this configuration; change the reference to that";
        else
            why += "copy the file into or beside this configuration's folder and give its path "
                   "relative to this configuration (for example irs/filter.wav)";
        throw ConfigurationError("nonportable-path", ref.location, why);
    }
#endif
    auto normalized = name;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    const auto p = utf8_path(normalized);
    return p.is_absolute() ? p : owner.parent_path() / p;
}

bool matches(int sample_rate, std::size_t input_channels, int rate, std::size_t inputs) {
    return (!rate || rate == sample_rate) && (!inputs || inputs == input_channels);
}

Configuration configuration_graph(const ParsedConfiguration& parsed) {
    Configuration cfg;
    cfg.sample_rate = parsed.sample_rate;
    cfg.input_channels = parsed.input_channels;
    cfg.output_channels = parsed.output_channels;
    cfg.channel_mask = parsed.channel_mask;
    for (const auto& d : parsed.input_delays)
        cfg.input_delays.push_back(d.frames);
    for (const auto& d : parsed.output_delays)
        cfg.output_delays.push_back(d.frames);
    for (const auto& p : parsed.paths) {
        FilterPath filter;
        for (const auto& r : p.inputs)
            filter.inputs.push_back({r.channel, r.gain});
        for (const auto& r : p.outputs)
            filter.outputs.push_back({r.channel, r.gain});
        cfg.paths.push_back(std::move(filter));
    }
    return cfg;
}

std::vector<DecodedIr> decode_impulses(const ParsedDocument& doc, Configuration& cfg,
                                       std::size_t& budget) {
    std::vector<DecodedIr> result;
    const auto& parsed = doc.configuration;
    for (std::size_t i = 0; i < parsed.paths.size(); ++i) {
        const auto& path = parsed.paths[i];
        const auto resolved = resolve_reference(doc.source, path.ir);
        std::optional<AudioReader> ir;
        try {
            ir.emplace(resolved);
        } catch (const std::runtime_error& e) {
            throw ConfigurationError("missing-asset", path.ir.location,
                                     "filter '" + path.ir.spelling + "': " + e.what());
        }
        const auto info = ir->info();
        if (info.sample_rate != cfg.sample_rate)
            throw ConfigurationError("rate-mismatch", path.ir.location,
                                     "filter sample rate differs from configuration (" +
                                         std::to_string(info.sample_rate) + " Hz file, " +
                                         std::to_string(cfg.sample_rate) + " Hz configuration)");
        // read_impulses targets the last path, so decode into a single path and
        // transfer only its coefficients after all routing has been validated.
        std::vector<FilterPath> decoded(1);
        try {
            read_impulses(*ir, decoded, path.ir_channel, budget,
                          path.ir_channel < info.channels ? path.ir.location
                                                          : path.channel_location);
        } catch (const ConfigurationError&) {
            throw;
        } catch (const std::runtime_error& e) {
            throw ConfigurationError("damaged-asset", path.ir.location,
                                     "filter '" + path.ir.spelling + "': " + e.what());
        }
        result.push_back({resolved, info, decoded.front().impulse.size()});
        cfg.paths[i].impulse = std::move(decoded.front().impulse);
    }
    return result;
}

Configuration decode_audio_filter(AudioReader& audio, std::size_t& budget) {
    const auto info = audio.info();
    Configuration cfg;
    cfg.sample_rate = info.sample_rate;
    cfg.input_channels = cfg.output_channels = info.channels;
    cfg.input_delays.resize(info.channels);
    cfg.output_delays.resize(info.channels);
    cfg.paths.resize(info.channels);
    for (std::size_t c = 0; c < info.channels; ++c) {
        cfg.paths[c].inputs.push_back({c, 1});
        cfg.paths[c].outputs.push_back({c, 1});
    }
    read_impulses(audio, cfg.paths, {}, budget, {});
    return cfg;
}

Resolver::Resolver(Visitor visitor, ErrorHandler on_error, ListObserver on_list, Sources files)
    : visitor_(std::move(visitor)), on_error_(std::move(on_error)), on_list_(std::move(on_list)),
      files_(std::move(files)) {
    if (!files_.open_audio)
        files_.open_audio = [](const std::filesystem::path& p) -> std::unique_ptr<AudioReader> {
            try {
                return std::make_unique<AudioReader>(p);
            } catch (const std::runtime_error&) {
                return nullptr; // Bounded text validation distinguishes configs from damaged audio.
            }
        };
    if (!files_.read_text)
        files_.read_text = read_configuration_text;
    if (!files_.canonical)
        files_.canonical = [](const std::filesystem::path& p) {
            return std::filesystem::weakly_canonical(p);
        };
}

bool Resolver::run(const std::filesystem::path& path) {
    try {
        return visit(path);
    } catch (const ConfigurationError& e) {
        if (on_error_ && on_error_(e))
            return false;
        throw;
    }
}

bool Resolver::visit(const std::filesystem::path& path) {
    if (++visits_ > max_list_visits || active_.size() >= max_list_depth)
        throw ConfigurationError("limit", {path, 0, 0},
                                 "configuration list nesting/entry limit exceeded");
    const auto canonical = files_.canonical(path);
    sources_.push_back(canonical);
    if (!active_.insert(canonical).second)
        throw ConfigurationError("cycle", {path, 0, 0},
                                 "configuration list cycle: " + utf8_string(path));
    struct Pop {
        std::set<std::filesystem::path>& active;
        std::filesystem::path path;
        ~Pop() {
            active.erase(path);
        }
    } pop{active_, canonical};
    if (const auto audio = files_.open_audio(path))
        return visitor_({path, chain_, nullptr, audio.get()});
    const auto text = files_.read_text(path);
    if (text.size() > max_total_text_bytes - text_bytes_)
        throw ConfigurationError("limit", {path, 0, 0},
                                 "configuration text read for one operation exceeds 16 MiB");
    text_bytes_ += text.size();
    const auto doc = parse_configuration_text(text, path);
    if (doc.kind == ParsedDocument::Kind::configuration) {
        for (const auto& p : doc.configuration.paths)
            sources_.push_back(files_.canonical(resolve_reference(path, p.ir, files_.exists)));
        return visitor_({path, chain_, &doc, nullptr});
    }
    if (on_list_)
        on_list_(doc, chain_);
    for (const auto& entry : doc.entries) {
        const auto child = resolve_reference(path, entry, files_.exists);
        chain_.push_back(entry.location);
        struct PopChain {
            IncludeChain& chain;
            ~PopChain() {
                chain.pop_back();
            }
        } pop_chain{chain_};
        try {
            if (visit(child))
                return true;
        } catch (ConfigurationError& e) {
            e.included_from(entry.location);
            if (on_error_ && on_error_(e))
                continue;
            throw;
        }
    }
    return false;
}
} // namespace convolver::detail

namespace convolver {
Configuration load_configuration(const std::filesystem::path& path, int expected_rate,
                                 std::size_t expected_inputs,
                                 std::vector<std::filesystem::path>* source_paths) {
    if (expected_rate < 0 || expected_rate > 768000 || expected_inputs > 256)
        throw std::invalid_argument("expected audio properties out of range");
    if (path == "-")
        throw std::invalid_argument("configuration must be a file");
    std::optional<Configuration> selected;
    std::size_t budget = 0; // Decoded samples across the whole operation.
    detail::Resolver resolver([&](const detail::Resolver::Leaf& leaf) {
        if (leaf.audio) {
            const auto info = leaf.audio->info();
            if (!detail::matches(info.sample_rate, info.channels, expected_rate, expected_inputs))
                return false;
            selected = detail::decode_audio_filter(*leaf.audio, budget);
            return true;
        }
        const auto& parsed = leaf.document->configuration;
        // Lists select by stream properties. Unrelated entries' text is validated,
        // but their IR assets need not be installed and are not decoded.
        if (!detail::matches(parsed.sample_rate, parsed.input_channels, expected_rate,
                             expected_inputs))
            return false;
        auto cfg = detail::configuration_graph(parsed);
        detail::decode_impulses(*leaf.document, cfg, budget);
        selected = std::move(cfg);
        return true;
    });
    if (resolver.run(path) && selected) {
        if (source_paths)
            *source_paths = resolver.sources();
        return std::move(*selected);
    }
    throw std::runtime_error("no configuration matches the input sample rate and channel count");
}
} // namespace convolver
