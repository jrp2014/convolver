// SPDX-License-Identifier: GPL-2.0-or-later
// Loader orchestration: list traversal and selection, reference resolution and IR
// decoding. Text grammar lives in config_parse.cpp.
#include "config_parse.hpp"
#include "convolver/io.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace convolver::detail {
namespace {
void check_ir_channel(std::size_t selected, std::size_t channels, const SourceLocation& where) {
    if (selected >= channels)
        throw ConfigurationError(
            "ir-channel", where,
            "filter channel " + std::to_string(selected) + " out of range (the IR file has " +
                std::to_string(channels) + (channels == 1 ? " channel" : " channels") +
                ", numbered from 0, so use " +
                (channels == 1 ? std::string("0") : "0 to " + std::to_string(channels - 1)) + ")");
}
[[noreturn]] void decoded_limit(const SourceLocation& where) {
    throw ConfigurationError("limit", where, "decoded impulse data exceeds 64 Mi samples");
}
// Decodes a whole file into the channels whose target is set (one target per
// file channel) and returns its frame count. Every decoded sample counts
// against budget, used or not.
std::size_t read_channels(SoundFile& reader, const std::vector<std::vector<float>*>& targets,
                          std::size_t& budget, const SourceLocation& where) {
    const auto channels = reader.info().channels;
    // Reject oversized files before allocating for their samples.
    if (const auto declared = reader.declared_frames()) {
        if (*declared > (max_decoded_samples - budget) / channels)
            decoded_limit(where);
        for (auto* target : targets)
            if (target)
                target->reserve(*declared);
    }
    std::vector<float> buffer(4096 * channels);
    std::size_t total = 0;
    while (const auto frames = reader.read(buffer)) {
        const auto samples = frames * channels;
        if (samples > max_decoded_samples - budget)
            decoded_limit(where);
        budget += samples;
        total += frames;
        for (std::size_t c = 0; c < channels; ++c)
            if (auto* target = targets[c])
                for (std::size_t f = 0; f < frames; ++f)
                    target->push_back(buffer[f * channels + c]);
    }
    reader.close();
    if (total == 0)
        throw ConfigurationError("empty-ir", where, "empty impulse response");
    return total;
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

bool matches(int sample_rate, std::size_t input_channels, std::size_t output_channels,
             const ConfigurationSelection& s) {
    return (!s.sample_rate || s.sample_rate == sample_rate) &&
           (!s.input_channels || s.input_channels == input_channels) &&
           (!s.output_channels || s.output_channels == output_channels);
}

namespace {
std::string channels(std::size_t n, const char* kind = "") {
    return std::to_string(n) + ' ' + kind + (n == 1 ? "channel" : "channels");
}
std::string hertz(int rate) {
    return std::to_string(rate) + " Hz";
}
std::string wanted_text(const ConfigurationSelection& w) {
    std::string text;
    auto add = [&](const std::string& part) { text += (text.empty() ? "" : ", ") + part; };
    if (w.sample_rate)
        add(hertz(w.sample_rate));
    if (w.input_channels)
        add(channels(w.input_channels, "input "));
    if (w.output_channels)
        add(channels(w.output_channels, "output ") + " requested");
    return text;
}
std::string candidate_text(const SelectionCandidate& c) {
    if (c.sound_file)
        return "sound file, " + hertz(c.sample_rate) + ", " + channels(c.input_channels);
    return "configuration, " + hertz(c.sample_rate) + ", " + std::to_string(c.input_channels) +
           " in, " + std::to_string(c.output_channels) + " out";
}
// Each property that differs, as "the sample rate is 44100 Hz, not 48000 Hz".
std::vector<std::string> differences(const SelectionCandidate& c, const ConfigurationSelection& w) {
    std::vector<std::string> result;
    if (w.sample_rate && w.sample_rate != c.sample_rate)
        result.push_back("its sample rate is " + hertz(c.sample_rate) + ", not " +
                         hertz(w.sample_rate));
    if (w.input_channels && w.input_channels != c.input_channels)
        result.push_back("it takes " + channels(c.input_channels, "input ") + ", not " +
                         std::to_string(w.input_channels));
    if (w.output_channels && w.output_channels != c.output_channels)
        result.push_back("it produces " + channels(c.output_channels, "output ") + ", not " +
                         std::to_string(w.output_channels));
    return result;
}
std::string joined(const std::vector<std::string>& parts, const char* separator) {
    std::string text;
    for (const auto& p : parts)
        text += (text.empty() ? "" : separator) + p;
    return text;
}
std::string resample_hint(int wanted, const std::string& available, bool list, Chooser who) {
    return "Convolver never resamples. " +
           std::string(list ? "Add an entry for " : "Use a filter made for ") + hertz(wanted) +
           " (with IRs recorded or designed at that rate), or " +
           (who == Chooser::host      ? "set the host's project or audio device to " + available
            : who == Chooser::options ? "choose " + available + " with --sample-rate"
                                      : "convert the audio to " + available + " first") +
           "." +
           (list ? ""
                 : " A configuration list can hold one configuration per sample rate and "
                   "pick the matching one automatically.");
}
// A ready-to-use configuration applying a mono IR to every channel.
std::string mono_example(const SelectionCandidate& c, std::size_t n) {
    const auto name = utf8_string(c.path.filename());
    std::string zeros;
    for (std::size_t i = 0; i < n; ++i)
        zeros += (i ? " 0" : "0");
    std::string text = "// sample rate, input channels, output channels, speaker mask (0: none)\n" +
                       std::to_string(c.sample_rate) + ' ' + std::to_string(n) + ' ' +
                       std::to_string(n) + " 0\n// input delays in milliseconds\n" + zeros +
                       "\n// output delays in milliseconds\n" + zeros + '\n';
    for (std::size_t i = 0; i < n; ++i)
        text += "// path " + std::to_string(i + 1) +
                ": IR file, IR channel, input channel, output channel (from 0)\n" + name + "\n0\n" +
                std::to_string(i) + '\n' + std::to_string(i) + '\n';
    return text;
}
std::string indented(const std::string& text) {
    std::string result;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = std::min(text.find('\n', start), text.size());
        result += "    " + text.substr(start, end - start) + '\n';
        start = end + 1;
    }
    return result;
}
// `who` chose the wanted properties (explain_no_match()). For a plug-in host
// the advice names the host's settings, not command-line options. It names no
// window: the LV2 plug-in has none, and its host's file chooser loads the file.
std::string explain_one(const std::filesystem::path& filter, const SelectionCandidate& c,
                        const ConfigurationSelection& w, std::string_view subject, Chooser who) {
    const bool plugin = who == Chooser::host;
    const auto name = utf8_string(filter);
    const auto diffs = differences(c, w);
    std::string text;
    if (c.sound_file)
        text = name + " is a sound-file filter (" + hertz(c.sample_rate) + ", " +
               channels(c.input_channels) +
               "). A sound file used directly as the filter applies its first channel to the "
               "first input channel, its second to the second, and so on, so it needs audio "
               "with exactly " +
               channels(c.input_channels) + " at " + hertz(c.sample_rate) + ".";
    else
        text = name + " is a configuration for " + hertz(c.sample_rate) + " audio with " +
               channels(c.input_channels, "input ") + " and " +
               channels(c.output_channels, "output ") +
               " (the first three numbers on its first non-comment line).";
    text += " It cannot be used with " + std::string(subject) + " (" + wanted_text(w) +
            "): " + joined(diffs, "; ") + ".";
    if (w.sample_rate && w.sample_rate != c.sample_rate)
        text += "\n" + resample_hint(w.sample_rate, hertz(c.sample_rate), false, who);
    if (w.input_channels && w.input_channels != c.input_channels) {
        if (c.sound_file && c.input_channels == 1 && w.input_channels <= 8)
            text += "\nTo apply this mono IR to all " + std::to_string(w.input_channels) +
                    " channels, save a configuration such as the following beside " +
                    utf8_string(c.path.filename()) + " (for example as filter.cfg) and " +
                    (plugin                    ? "load it in Convolver"
                     : who == Chooser::options ? "make the preset from it"
                                               : "pass it as the filter") +
                    " instead:\n" + indented(mono_example(c, w.input_channels)) +
                    "Lines starting with // are comments and can be kept or removed.";
        else if (c.sound_file)
            text += "\nUse an IR file with one channel per input channel (" +
                    std::to_string(w.input_channels) +
                    "), or a configuration (.cfg) file that says which IR channel filters which "
                    "input and where the result goes.";
        else if (who == Chooser::options)
            text += "\n--input-channels asked for " + channels(w.input_channels, "input ") +
                    ". Use a configuration with that input count (its second header number), "
                    "or omit --input-channels.";
        else
            text += "\nThe audio must have exactly as many channels as the configuration's "
                    "input count. Use a configuration written for " +
                    channels(w.input_channels, "input ") +
                    ", or a configuration list containing one for each channel layout you use.";
    }
    // A host always sets both counts: for a sound file whose input advice
    // already covers equal counts, the output paragraph would only repeat it.
    const bool covered = c.sound_file && w.input_channels != c.input_channels &&
                         w.input_channels == w.output_channels;
    if (w.output_channels && w.output_channels != c.output_channels && plugin && !covered)
        text += "\nThe host gives the plug-in " + channels(w.output_channels, "output ") + ". " +
                (c.sound_file ? "A sound-file filter always has as many outputs as inputs: use "
                                "a configuration (.cfg) file to produce a different number, or "
                                "give the plug-in as many outputs as inputs in the host's track "
                                "or routing settings."
                              : "Use a configuration with that output count (its third header "
                                "number), or give the plug-in " +
                                    channels(c.output_channels, "output ") +
                                    " in the host's track or routing settings.");
    else if (w.output_channels && w.output_channels != c.output_channels && !plugin)
        text += c.sound_file
                    ? "\n--output-channels asked for " + channels(w.output_channels, "output ") +
                          ", but a sound-file filter always has as many outputs as inputs. "
                          "Use a configuration (.cfg) file to produce a different number of "
                          "outputs, or omit --output-channels."
                    : "\n--output-channels asked for " + channels(w.output_channels, "output ") +
                          ". Use a configuration with that output count (its third header "
                          "number), or omit --output-channels.";
    return text;
}
} // namespace

std::string explain_no_match(const std::filesystem::path& filter, bool list,
                             const std::vector<SelectionCandidate>& candidates,
                             const ConfigurationSelection& w, std::string_view subject,
                             Chooser who) {
    if (!list && candidates.size() == 1)
        return explain_one(filter, candidates.front(), w, subject, who);
    const bool plugin = who == Chooser::host;
    const auto name = utf8_string(filter);
    if (candidates.empty())
        return "the configuration list " + name + " names no sound file or configuration.";
    std::string criteria = "sample rate and input channel count";
    if (w.output_channels)
        criteria = "sample rate, input channel count and output channel count";
    std::string text =
        (candidates.size() == 1
             ? std::string("the only filter in the configuration list ") + name + " does not match "
             : "none of the " + std::to_string(candidates.size()) +
                   " filters in the configuration list " + name + " matches ") +
        std::string(subject) + " (" + wanted_text(w) + "). A list uses the first entry whose " +
        criteria + " equal these. Entries checked, in order:";
    constexpr std::size_t shown = 10;
    for (std::size_t i = 0; i < candidates.size() && i < shown; ++i) {
        const auto& c = candidates[i];
        text += "\n  " + utf8_string(c.path) + " (" + candidate_text(c) +
                "): " + joined(differences(c, w), "; ");
    }
    if (candidates.size() > shown)
        text += "\n  ... and " + std::to_string(candidates.size() - shown) + " more";
    const auto any = [&](auto predicate) {
        return std::any_of(candidates.begin(), candidates.end(), predicate);
    };
    if (w.sample_rate &&
        !any([&](const SelectionCandidate& c) { return c.sample_rate == w.sample_rate; })) {
        std::vector<int> rates;
        for (const auto& c : candidates)
            if (std::find(rates.begin(), rates.end(), c.sample_rate) == rates.end())
                rates.push_back(c.sample_rate);
        std::string available;
        for (std::size_t i = 0; i < rates.size(); ++i)
            available += (i == 0 ? "" : i + 1 == rates.size() ? " or " : ", ") + hertz(rates[i]);
        text += "\nNo entry is for " + hertz(w.sample_rate) + ". " +
                resample_hint(w.sample_rate, available, true, who);
    } else if (w.input_channels && !any([&](const SelectionCandidate& c) {
                   return matches(c.sample_rate, c.input_channels, c.output_channels,
                                  {w.sample_rate, w.input_channels, 0});
               })) {
        text += "\nNo entry takes " + channels(w.input_channels, "input ") +
                (w.sample_rate ? " at " + hertz(w.sample_rate) : std::string()) +
                ". Add one to the list, or " +
                (plugin ? "give the plug-in as many inputs as an entry takes in the host's "
                          "track or routing settings."
                 : who == Chooser::options ? "choose an entry's input count with --input-channels."
                                           : "use audio with a channel count an entry accepts.");
    } else if (w.output_channels) {
        text += "\nEntries exist for the sample rate and input channels, but none produces " +
                channels(w.output_channels, "output ") + ". Add one to the list, or " +
                (plugin ? "give the plug-in an entry's output count in the host's track or "
                          "routing settings."
                        : "omit --output-channels to take the first entry that matches the "
                          "input.");
    }
    return text;
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

bool raw_filter_name(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".pcm" || extension == ".raw";
}
// sound_file_header decides whether the content is a sound file, so a .pcm or
// .raw file with a header is read through that header, as it always was.
bool raw_filter(const std::filesystem::path& resolved, const SoundFiles& reader) {
    return raw_filter_name(resolved) && !reader.recognised(resolved);
}

namespace {
// A filter file decoded once for every path of one configuration that names it
// (PERF-6). Its channels are handed to the paths only after every path has
// passed its checks, so nothing is copied for a configuration that fails.
struct DecodedFile {
    AudioInfo info{};
    std::optional<std::size_t> declared;
    std::size_t frames = 0;
    bool raw = false;
    std::vector<std::vector<float>> channels; // Empty for channels no path names.
    std::vector<std::size_t> uses;            // Paths that take each channel.
};
} // namespace

std::vector<DecodedIr> decode_impulses(const ParsedDocument& doc, Configuration& cfg,
                                       std::size_t& budget, const SoundFiles& reader,
                                       RawEncoding raw_encoding) {
    std::vector<DecodedIr> result;
    const auto& parsed = doc.configuration;
    // The paths naming each file. A reference that cannot be resolved names
    // none, and fails with its own diagnostic when its turn comes.
    std::map<std::filesystem::path, std::vector<std::size_t>> named;
    for (std::size_t i = 0; i < parsed.paths.size(); ++i) {
        try {
            named[resolve_reference(doc.source, parsed.paths[i].ir)].push_back(i);
        } catch (const ConfigurationError&) {
        }
    }
    std::map<std::filesystem::path, DecodedFile> files;
    std::vector<DecodedFile*> sources(parsed.paths.size()); // Each path's file.
    for (std::size_t i = 0; i < parsed.paths.size(); ++i) {
        const auto& path = parsed.paths[i];
        const auto resolved = resolve_reference(doc.source, path.ir);
        if (const auto found = files.find(resolved); found != files.end()) {
            // Checked and charged as if the file were decoded again, so the
            // limits, and the configurations they reject, stay as they were.
            auto& file = found->second;
            const auto channels = file.info.channels;
            check_ir_channel(path.ir_channel, channels, path.channel_location);
            if ((file.declared && *file.declared > (max_decoded_samples - budget) / channels) ||
                file.frames > (max_decoded_samples - budget) / channels)
                decoded_limit(path.ir.location);
            budget += file.frames * channels;
            sources[i] = &file;
            result.push_back({resolved, file.info, file.frames,
                              file.raw ? std::optional<RawEncoding>(raw_encoding) : std::nullopt});
            continue;
        }
        std::unique_ptr<SoundFile> ir;
        const bool raw = raw_filter(resolved, reader);
        // Appended to errors about a file whose name says raw but whose content
        // was read as a sound file, so the user knows why.
        const std::string read_as_sound =
            !raw && raw_filter_name(resolved)
                ? " (the name ends like a raw filter's, but the content starts like a sound "
                  "file, so Convolver read it as one; if it is headerless samples that happen "
                  "to look like a header, convert it to WAV, for example with SoX)"
                : "";
        try {
            ir = raw ? reader.open_raw(resolved, RawSpec{cfg.sample_rate, 1}, raw_encoding)
                     : reader.open(resolved);
        } catch (const std::exception& e) {
            std::error_code ec; // On failure absolute() returns an empty path.
            if (raw && std::filesystem::is_regular_file(resolved, ec))
                throw ConfigurationError("damaged-asset", path.ir.location,
                                         "raw filter '" + path.ir.spelling + "': " + e.what() +
                                             " (raw filters are read as mono " +
                                             raw_encoding_name(raw_encoding) + ")");
            // The file is there but the reader cannot read it (a codec its
            // build lacks, for example): the path hint below would mislead. The code
            // stays missing-asset on purpose: codes are stable, and
            // tests/compatibility/cases.json relies on this mapping.
            if (!raw && std::filesystem::is_regular_file(resolved, ec))
                throw ConfigurationError("missing-asset", path.ir.location,
                                         "filter '" + path.ir.spelling + "': " + e.what() +
                                             read_as_sound);
            // Includes a bare "-": standard input cannot be a filter.
            throw ConfigurationError(
                "missing-asset", path.ir.location,
                "filter '" + path.ir.spelling + "': " + e.what() + " (looked for " +
                    utf8_string(std::filesystem::absolute(resolved, ec)) +
                    "; IR file names are relative to the folder containing the configuration, "
                    "and letter case must match on case-sensitive file systems)");
        }
        const auto info = ir->info();
        if (info.sample_rate != cfg.sample_rate)
            throw ConfigurationError("rate-mismatch", path.ir.location,
                                     "filter sample rate differs from configuration (" +
                                         std::to_string(info.sample_rate) + " Hz file, " +
                                         std::to_string(cfg.sample_rate) +
                                         " Hz configuration). Every IR a configuration names "
                                         "must have the sample rate on its first line; "
                                         "Convolver never resamples IRs" +
                                         read_as_sound);
        check_ir_channel(path.ir_channel, info.channels, path.channel_location);
        // Decode every channel that this and later paths name, so the file is
        // read once. A later path naming a channel the file lacks fails when
        // its turn comes, as before.
        DecodedFile file{info, ir->declared_frames(), 0, raw, {}, {}};
        file.channels.resize(info.channels);
        file.uses.resize(info.channels);
        for (const auto j : named[resolved])
            if (parsed.paths[j].ir_channel < info.channels)
                ++file.uses[parsed.paths[j].ir_channel];
        file.uses[path.ir_channel] = std::max<std::size_t>(file.uses[path.ir_channel], 1);
        std::vector<std::vector<float>*> targets(info.channels);
        for (std::size_t c = 0; c < info.channels; ++c)
            if (file.uses[c])
                targets[c] = &file.channels[c];
        try {
            file.frames = read_channels(*ir, targets, budget, path.ir.location);
        } catch (const ConfigurationError&) {
            throw;
        } catch (const std::runtime_error& e) {
            throw ConfigurationError("damaged-asset", path.ir.location,
                                     "filter '" + path.ir.spelling + "': " + e.what());
        }
        result.push_back({resolved, info, file.frames,
                          raw ? std::optional<RawEncoding>(raw_encoding) : std::nullopt});
        sources[i] = &files.emplace(resolved, std::move(file)).first->second;
    }
    // Every path passed: copy each channel to its paths, the last taking it.
    for (std::size_t i = 0; i < sources.size(); ++i) {
        auto& file = *sources[i];
        const auto channel = parsed.paths[i].ir_channel;
        auto& samples = file.channels[channel];
        cfg.paths[i].impulse = file.uses[channel] > 1 ? samples : std::exchange(samples, {});
        if (file.uses[channel])
            --file.uses[channel];
    }
    return result;
}

Configuration decode_audio_filter(SoundFile& audio, std::size_t& budget,
                                  const SourceLocation& where) {
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
    try {
        std::vector<std::vector<float>*> targets;
        for (auto& p : cfg.paths)
            targets.push_back(&p.impulse);
        read_channels(audio, targets, budget, where);
    } catch (const ConfigurationError&) {
        throw;
    } catch (const std::runtime_error& e) {
        throw ConfigurationError("damaged-asset", where, e.what());
    }
    return cfg;
}

Resolver::Sources Resolver::from(const SoundFiles& reader) {
    Sources files;
    files.open_audio = [&reader](const std::filesystem::path& p) -> std::unique_ptr<SoundFile> {
        try {
            return reader.open(p);
        } catch (const std::exception&) {
            return nullptr; // Bounded text validation distinguishes configs from damaged audio.
        }
    };
    return files;
}

Resolver::Resolver(Visitor visitor, ErrorHandler on_error, ListObserver on_list, Sources files)
    : visitor_(std::move(visitor)), on_error_(std::move(on_error)), on_list_(std::move(on_list)),
      files_(std::move(files)) {
    if (!files_.open_audio)
        throw std::invalid_argument("the configuration resolver needs a sound-file reader");
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

std::filesystem::path Resolver::canonical(const std::filesystem::path& path,
                                          const SourceLocation& where) {
    try {
        return files_.canonical(path);
    } catch (const std::filesystem::filesystem_error& e) {
        throw ConfigurationError("unreadable", where, e.what());
    }
}

bool Resolver::visit(const std::filesystem::path& path) {
    if (++visits_ > max_list_visits || active_.size() >= max_list_depth)
        throw ConfigurationError("limit", {path, 0, 0},
                                 "configuration list nesting/entry limit exceeded");
    const auto resolved = canonical(path, {path, 0, 0});
    sources_.push_back(resolved);
    chosen_sources_.push_back(resolved);
    if (!active_.insert(resolved).second)
        throw ConfigurationError("cycle", {path, 0, 0},
                                 "configuration list cycle: " + utf8_string(path));
    struct Pop {
        std::set<std::filesystem::path>& active;
        std::filesystem::path path;
        ~Pop() {
            active.erase(path);
        }
    } pop{active_, resolved};
    if (const auto audio = files_.open_audio(path))
        return visitor_({path, chain_, nullptr, audio.get()});
    const auto text = files_.read_text(path);
    if (text.size() > max_total_text_bytes - text_bytes_)
        throw ConfigurationError("limit", {path, 0, 0},
                                 "configuration text read for one operation exceeds 16 MiB");
    text_bytes_ += text.size();
    const auto doc = parse_configuration_text(text, path);
    if (doc.kind == ParsedDocument::Kind::configuration) {
        const auto irs = sources_.size();
        for (const auto& p : doc.configuration.paths)
            sources_.push_back(
                canonical(resolve_reference(path, p.ir, files_.exists), p.ir.location));
        if (!visitor_({path, chain_, &doc, nullptr}))
            return false;
        chosen_sources_.insert(chosen_sources_.end(), sources_.begin() + irs, sources_.end());
        return true;
    }
    if (on_list_)
        on_list_(doc, chain_);
    // A handler sees the whole chain of enclosing lists; a rethrown error gains
    // the outer entries as it propagates.
    const auto handled = [&](const ConfigurationError& e, std::size_t outer) {
        if (!on_error_)
            return false;
        auto full = e;
        for (std::size_t i = outer; i-- > 0;)
            full.included_from(chain_[i]);
        return on_error_(full);
    };
    for (const auto& entry : doc.entries) {
        std::filesystem::path child;
        try {
            child = resolve_reference(path, entry, files_.exists);
        } catch (const ConfigurationError& e) {
            // Located at the entry itself, so this list is not added to the chain.
            if (handled(e, chain_.size()))
                continue;
            throw;
        }
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
            if (handled(e, chain_.size() - 1))
                continue;
            throw;
        }
    }
    return false;
}
} // namespace convolver::detail

namespace convolver::detail {
Configuration load_configuration(const std::filesystem::path& path,
                                 const ConfigurationSelection& selection, const SoundFiles& files,
                                 std::vector<std::filesystem::path>* source_paths,
                                 std::vector<std::filesystem::path>* raw_filters, bool plugin,
                                 bool options, std::vector<std::filesystem::path>* chosen_paths) {
    if (raw_filters)
        raw_filters->clear(); // Also empty for a sound file or on failure.
    if (selection.sample_rate < 0 || selection.sample_rate > 768000 ||
        selection.input_channels > 256 || selection.output_channels > 256)
        throw std::invalid_argument("expected audio properties out of range");
    if (selection.raw_filter_encoding.sample == SampleType::pcm8)
        throw std::invalid_argument("raw filters have no 8-bit sample type");
    if (path == "-")
        throw std::invalid_argument("configuration must be a file");
    std::optional<Configuration> selected;
    std::size_t budget = 0; // Decoded samples across the whole operation.
    std::vector<detail::SelectionCandidate> candidates;
    bool list = false;
    const auto visit = [&](const detail::Resolver::Leaf& leaf) {
        if (leaf.audio) {
            const auto info = leaf.audio->info();
            candidates.push_back({leaf.path, true, info.sample_rate, info.channels, info.channels});
            // A sound file maps each channel to the same output channel.
            if (!detail::matches(info.sample_rate, info.channels, info.channels, selection))
                return false;
            selected = detail::decode_audio_filter(*leaf.audio, budget, {leaf.path, 0, 0});
            return true;
        }
        const auto& parsed = leaf.document->configuration;
        candidates.push_back(
            {leaf.path, false, parsed.sample_rate, parsed.input_channels, parsed.output_channels});
        // Lists select by stream properties. Unrelated entries' text is validated,
        // but their IR assets need not be installed and are not decoded.
        if (!detail::matches(parsed.sample_rate, parsed.input_channels, parsed.output_channels,
                             selection))
            return false;
        auto cfg = detail::configuration_graph(parsed);
        const auto decoded = detail::decode_impulses(*leaf.document, cfg, budget, files,
                                                     selection.raw_filter_encoding);
        if (raw_filters) {
            for (const auto& ir : decoded)
                if (ir.raw && std::find(raw_filters->begin(), raw_filters->end(), ir.resolved) ==
                                  raw_filters->end())
                    raw_filters->push_back(ir.resolved);
        }
        selected = std::move(cfg);
        return true;
    };
    detail::Resolver resolver(
        visit, {}, [&](const detail::ParsedDocument&, const detail::IncludeChain&) { list = true; },
        detail::Resolver::from(files));
    if (resolver.run(path) && selected) {
        if (source_paths)
            *source_paths = resolver.sources();
        if (chosen_paths)
            *chosen_paths = resolver.chosen_sources();
        return std::move(*selected);
    }
    const auto who = plugin ? Chooser::host : options ? Chooser::options : Chooser::audio;
    auto explanation =
        detail::explain_no_match(path, list, candidates, selection,
                                 who == Chooser::host      ? "the plug-in in its host"
                                 : who == Chooser::options ? "the requested selection"
                                                           : "the audio to be filtered",
                                 who);
    throw detail::NoMatch(explanation, list, std::move(candidates));
}
} // namespace convolver::detail
