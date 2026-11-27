// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-9: a filter file chosen in a plug-in's window and refused, through
// read_filter_file() and HostFilter::reject() as the Audio Unit and LV2 call
// them. The short status text (the Status parameter, the window's first
// line and Copy diagnostics) must give the refused filter's own rate and
// counts, never "0 in, 0 out" (jrp's GarageBand run, 30 October 2026: a
// mono WAV refused on a stereo track read "the filter has 0 in, 0 out").
#include "advice.hpp"
#include "b_format.hpp"
#include "cases.hpp"
#include "convolver/host.hpp"
#include "convolver/io.hpp"
#include "filter_file.hpp"
#include "host_log.hpp"
#include "host_report.hpp"
#include "sound_files.hpp"
#include "utf8.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
using namespace convolver;
using convolver::test::b_format_wave;
namespace {
void require(bool b, const std::string& message) {
    if (!b)
        throw std::runtime_error(message);
}
bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}
void impulse(const fs::path& path, int rate, std::size_t channels) {
    AudioWriter writer(path, {rate, channels}, {Container::wav, SampleType::pcm16});
    std::vector<float> data(channels * 8, 0.f);
    for (std::size_t c = 0; c < channels; ++c)
        data[c] = .5f;
    writer.write(data);
    writer.close();
}
// A stereo filter playing, then `path` chosen for `host` and refused.
std::string refused(const fs::path& path, const HostLayout& host, HostFailureKind expected) {
    HostFilter filter;
    HostState playing;
    playing.configuration = Configuration{host.sample_rate, 2, 2, 0, {}, {}, {}};
    for (std::size_t ch = 0; ch < 2; ++ch)
        playing.configuration->paths.push_back({{1.f}, {{ch, 1}}, {{ch, 1}}});
    filter.load(std::move(playing), host);
    FilterFileFailure why;
    const auto text = detail::utf8_string(path);
    const auto name = detail::utf8_string(path.filename());
    const auto next = read_filter_file(text, host, detail::libsndfile_files(), why);
    require(!next, name + " was accepted");
    require(why.kind == expected, name + ": wrong failure kind");
    require(has(why.message.problem, name), name + ": not named in " + why.message.problem);
    filter.reject(why.kind, why.message, {HostFilterSource::file, text, 0}, why.filter());
    const auto line = host_status(filter, host).text;
    std::cout << name << ": " << line << '\n';
    require(has(line, "Load failed, previous filter kept: "), "status: " + line);
    require(!has(line, "0 in, 0 out") && !has(line, " 0 Hz"), "unknown values shown: " + line);
    return line;
}
void run(const fs::path& dir) {
    const HostLayout stereo{44100, 2, 2};
    impulse(dir / "mono.wav", 44100, 1);
    impulse(dir / "three.wav", 44100, 3);
    impulse(dir / "fast.wav", 48000, 2);
    const auto channels = refused(dir / "three.wav", stereo, HostFailureKind::channels);
    require(has(channels, "the filter has 3 in, 3 out, the host 2 in, 2 out."),
            "channel counts: " + channels);
    const auto rate = refused(dir / "fast.wav", stereo, HostFailureKind::sample_rate);
    require(has(rate, "the filter is for 48000 Hz, the host runs at 44100 Hz."), "rate: " + rate);
    // PLUG-25: a chosen file refused for its rate carries the agreed line in
    // its fix (decisions log, 8 November 2026); one refused for its channels
    // does not.
    {
        FilterFileFailure why;
        require(!read_filter_file((dir / "fast.wav").string(), stereo, detail::libsndfile_files(),
                                  why) &&
                    has(why.message.fix, "set the host's project or audio device to 48000 Hz. "
                                         "The host may play at a different rate from the one it "
                                         "exports at."),
                "chosen file's rate refusal without the playback-rate line: " + why.message.fix);
        require(!read_filter_file((dir / "three.wav").string(), stereo, detail::libsndfile_files(),
                                  why) &&
                    !has(why.message.fix, "exports at"),
                "chosen file's channel refusal names the playback rate: " + why.message.fix);
    }

    // VST3-9: a host rate that is not whole hertz is kept exactly, and no
    // filter suits it, not even one for its whole part.
    HostLayout pulled{0, 2, 2};
    set_host_rate(pulled, 48000 / 1.001);
    require(pulled.sample_rate == 47952 && pulled.fractional_rate == 48000 / 1.001 &&
                host_rate_text(pulled) == "47952.04795 Hz",
            "a fractional rate not kept exactly");
    HostLayout rounded{0, 2, 2};
    set_host_rate(rounded, std::nextafter(48000.0, 49000.0));
    require(rounded.sample_rate == 48000 && rounded.fractional_rate == 0 &&
                host_rate_text(rounded) == "48000 Hz" && !same_rate(rounded, pulled),
            "floating-point rounding of 48000 Hz not taken as 48000 Hz");
    HostLayout nearby{0, 2, 2};
    set_host_rate(nearby, 48000.001);
    require(nearby.sample_rate == 48000 && nearby.fractional_rate == 48000.001 &&
                !same_rate(nearby, rounded),
            "48000.001 Hz taken as 48000 Hz");
    // Closer than ten digits show, but not rounding: never shown as 48000 Hz.
    HostLayout closest{0, 2, 2};
    set_host_rate(closest, 48000.000001);
    require(closest.fractional_rate != 0 && host_rate_text(closest) == "48000.000001 Hz",
            "a rate just above 48000 Hz shown as 48000 Hz: " + host_rate_text(closest));
    // The window's Sample rate fact shows the exact rate.
    {
        HostFilter empty;
        HostReportFacts facts;
        facts.host = pulled;
        const auto report = host_report(empty, facts);
        require(std::any_of(report.audio.begin(), report.audio.end(),
                            [](const HostFact& f) {
                                return f.label == "Host sample rate" && f.value == "47952.04795 Hz";
                            }),
                "the report's Sample rate fact is not the exact rate");
    }
    {
        FilterFileFailure why;
        require(!read_filter_file((dir / "fast.wav").string(), pulled, detail::libsndfile_files(),
                                  why) &&
                    why.kind == HostFailureKind::sample_rate &&
                    has(why.message.problem,
                        "the filter is for 48000 Hz, and the host runs at 47952.04795 Hz") &&
                    why.message.fix ==
                        "Set the host's project or audio device to 48000 Hz. The host may play at "
                        "a different rate from the one it exports at.",
                "a file at a fractional rate not refused with both rates");
        // A filter for the rate's whole part is refused when the file is
        // read, so it cannot slip in before the host is known (when a load
        // is not checked against the host).
        impulse(dir / "whole part.wav", 47952, 2);
        require(!read_filter_file((dir / "whole part.wav").string(), pulled,
                                  detail::libsndfile_files(), why) &&
                    why.kind == HostFailureKind::sample_rate && why.filter() &&
                    why.filter()->sample_rate == 47952 &&
                    has(why.message.problem,
                        "the filter is for 47952 Hz, and the host runs at 47952.04795 Hz"),
                "a filter for the whole part accepted at a fractional rate");
        HostFilter filter;
        filter.reject(why.kind, why.message, {HostFilterSource::file, "whole part.wav", 0},
                      why.filter());
        const auto line = host_status(filter, pulled).text;
        std::cout << "whole part.wav: " << line << '\n';
        require(has(line, "the filter is for 47952 Hz, the host runs at 47952.04795 Hz."),
                "status: " + line);
        // Its channels do not fit either: still a rate refusal, as no
        // filter fits the rate.
        impulse(dir / "whole part mono.wav", 47952, 1);
        require(!read_filter_file((dir / "whole part mono.wav").string(), pulled,
                                  detail::libsndfile_files(), why) &&
                    why.kind == HostFailureKind::sample_rate &&
                    why.message.fix ==
                        "Set the host's project or audio device to 47952 Hz. The host may play at "
                        "a different rate from the one it exports at.",
                "a mono filter for the whole part not refused for its rate");
    }

    // The full explanation speaks to someone in a host, not at a command
    // line (jrp's GarageBand run: it said to omit --output-channels).
    const auto explanation = [&](const fs::path& path, const HostLayout& host) {
        FilterFileFailure why;
        require(!read_filter_file(path.string(), host, detail::libsndfile_files(), why),
                path.filename().string() + " was accepted");
        const auto text = detail::advice_text(why.message);
        std::cout << path.filename().string() << ", explained: " << text << "\n\n";
        require(!has(text, "--"), "command-line advice: " + text);
        return text;
    };
    // It names no window: the LV2 plug-in has none (review of 8177017).
    const auto three = explanation(dir / "three.wav", stereo);
    require(has(three, "Use an IR file with one channel per input channel (2), a four-channel "
                       "true-stereo file or one channel for all of them") &&
                !has(three, "window"),
            "the channel advice: " + three);
    require(has(explanation(dir / "fast.wav", stereo), "set the host's project or audio device"),
            "the rate advice names the host");
    {
        std::ofstream cfg(dir / "crossover.cfg");
        cfg << "44100 2 6 0\n0 0\n0 0 0 0 0 0\nmono.wav\n0\n0\n0\n";
    }
    require(has(explanation(dir / "crossover.cfg", stereo),
                "Give the plug-in 2 input channels and 6 output channels in the host's track or "
                "routing settings"),
            "the output-count advice names the host");
    {
        std::ofstream list(dir / "list.txt");
        list << "crossover.cfg\n";
    }
    require(has(explanation(dir / "list.txt", stereo), "in the host's track or routing settings"),
            "the list advice names the host");
    // A list's summary does not report one entry as "the filter": its
    // entries differ (review of 8177017). With an entry at the host's rate it
    // is refused for channels, otherwise for the rate.
    const HostLayout fast_stereo{48000, 2, 2};
    impulse(dir / "fast-three.wav", 48000, 3);
    {
        std::ofstream list(dir / "mixed.txt");
        list << "three.wav\nfast-three.wav\n";
    }
    const auto mixed = refused(dir / "mixed.txt", fast_stereo, HostFailureKind::channels);
    require(has(mixed, "the filter does not fit the host's 2 in, 2 out.") && !has(mixed, "44100"),
            "list with an entry at the host's rate: " + mixed);
    {
        std::ofstream list(dir / "slow.txt");
        list << "mono.wav\n";
    }
    const auto slow = refused(dir / "slow.txt", fast_stereo, HostFailureKind::sample_rate);
    require(has(slow, "the filter is not for the host's 48000 Hz.") && !has(slow, "1 in"),
            "list with no entry at the host's rate: " + slow);

    // VST3-11: an accepted file gives every file it was read from, for the
    // window's watch; a refused one gives none.
    {
        std::ofstream cfg(dir / "stereo.cfg");
        cfg << "44100 2 2 0\n0 0\n0 0\nmono.wav\n0\n0\n0\nmono.wav\n0\n1\n1\n";
    }
    std::vector<std::string> files{"stale"};
    FilterFileFailure why;
    require(read_filter_file((dir / "stereo.cfg").string(), stereo, detail::libsndfile_files(), why,
                             &files)
                .has_value(),
            "stereo.cfg refused: " + detail::advice_text(why.message));
    const auto named = [&](const std::string& name) {
        return std::any_of(files.begin(), files.end(),
                           [&](const std::string& f) { return fs::path(f).filename() == name; });
    };
    require(!files.empty() && fs::path(files.front()).filename() == "stereo.cfg" &&
                named("mono.wav") && !named("stale"),
            "files read: " + std::to_string(files.size()));
    require(!read_filter_file((dir / "fast.wav").string(), stereo, detail::libsndfile_files(), why,
                              &files) &&
                files.empty(),
            "a refused file's files");
    // A list gives the lists and configurations it visited and the chosen
    // entry's IRs, not the IRs of an entry passed over, which need not be
    // installed: a missing one there must not stop the window reloading.
    {
        std::ofstream fast(dir / "fast-missing.cfg");
        fast << "48000 2 2 0\n0 0\n0 0\nnot-installed.wav\n0\n0\n0\n";
        std::ofstream list(dir / "watched.txt");
        list << "fast-missing.cfg\nstereo.cfg\n";
    }
    require(read_filter_file((dir / "watched.txt").string(), stereo, detail::libsndfile_files(),
                             why, &files)
                .has_value(),
            "watched.txt refused: " + detail::advice_text(why.message));
    require(files.size() >= 4 && fs::path(files[0]).filename() == "watched.txt" &&
                fs::path(files[1]).filename() == "fast-missing.cfg" &&
                fs::path(files[2]).filename() == "stereo.cfg" && named("mono.wav") &&
                !named("not-installed.wav"),
            "a list's files: " + std::to_string(files.size()));

    // The command line keeps its own advice.
    try {
        load_configuration(dir / "crossover.cfg", ConfigurationSelection{44100, 2, 2});
        require(false, "crossover.cfg accepted for 2 outputs");
    } catch (const std::runtime_error& e) {
        require(has(e.what(), "--output-channels"), std::string("CLI advice: ") + e.what());
    }

    // A rejection that carries no filter says only what is known.
    for (const auto kind : {HostFailureKind::channels, HostFailureKind::sample_rate}) {
        HostFilter filter;
        filter.reject(kind, {"unreadable"}, {});
        const auto line = host_status(filter, stereo).text;
        std::cout << "no filter carried: " << line << '\n';
        require(!has(line, "0 in, 0 out") && !has(line, " 0 Hz"), "unknown values shown: " + line);
        require(has(line, kind == HostFailureKind::channels ? "the host's 2 in, 2 out"
                                                            : "the host's 44100 Hz"),
                "the host's values: " + line);
    }
}

// PLUG-33: a chosen file follows the rule a preset follows (PLUG-23): the
// host's outputs exactly and at least the filter's inputs. REAPER gives the
// VST3 plug-in on a six-channel track 6 inputs, where the 2-in, 6-out
// crossover was refused when chosen, though its preset played.
void wider_bus(const fs::path& dir) {
    impulse(dir / "wide.wav", 44100, 1);
    const auto write = [&](const std::string& name, const std::string& text) {
        std::ofstream(dir / name) << text;
    };
    write("wide-1x6.cfg", "44100 1 6 0\n0\n0 0 0 0 0 0\nwide.wav\n0\n0\n0\n");
    write("wide-2x6.cfg", "44100 2 6 0\n0 0\n0 0 0 0 0 0\nwide.wav\n0\n1\n5\n");
    write("wide-6x6.cfg", "44100 6 6 0\n0 0 0 0 0 0\n0 0 0 0 0 0\nwide.wav\n0\n0\n0\n");
    write("wide-48k.cfg", "48000 2 6 0\n0 0\n0 0 0 0 0 0\nwide.wav\n0\n0\n0\n");
    // Entries in list order: fewer inputs first, so order alone does not
    // decide; the entry for another rate is passed over.
    write("wide-fewer.txt", "wide-48k.cfg\nwide-1x6.cfg\nwide-2x6.cfg\n");
    write("wide-exact.txt", "wide-2x6.cfg\nwide-6x6.cfg\n");
    const auto chosen = [&](const std::string& name, const HostLayout& host) {
        FilterFileFailure why;
        std::vector<std::string> files{"stale"};
        std::string entry = "stale";
        auto state = read_filter_file(detail::utf8_string(dir / name), host,
                                      detail::libsndfile_files(), why, &files, &entry);
        if (state) {
            require(!files.empty() && fs::path(files.front()).filename() == name &&
                        files.back() != "stale",
                    name + ": files read");
            // As a preset of the same filter: accepted by the control side.
            HostFilter filter;
            try {
                filter.load(*state, host);
            } catch (const std::invalid_argument& e) {
                require(false, name + ": read, but refused on the host's buses: " + e.what());
            }
        }
        return std::tuple{std::move(state), std::move(why), std::move(entry)};
    };
    // `entry`: the list entry named as the wider-bus choice, or none.
    const auto inputs = [&](const std::string& name, const HostLayout& host,
                            const std::string& entry = {}) {
        auto [state, why, named] = chosen(name, host);
        require(state.has_value(),
                name + " refused on a wider bus: " + detail::advice_text(why.message));
        require(entry.empty() ? named.empty() : fs::path(named).filename() == entry,
                name + ": the wider-bus entry named is \"" + named + "\"");
        const auto& c = *state->configuration;
        require(c.sample_rate == host.sample_rate && c.output_channels == host.outputs,
                name + ": another rate or output count chosen");
        return c.input_channels;
    };
    const HostLayout six{44100, 6, 6};
    require(inputs("wide-2x6.cfg", six) == 2, "the crossover on 6 inputs");
    require(inputs("wide-1x6.cfg", six) == 1, "a 1-in filter on 6 inputs");
    // A list: an exact entry comes first, then the most inputs below the
    // host's, whatever the order of the entries.
    // The entry is named so the log and the window can say which plays.
    require(inputs("wide-fewer.txt", six, "wide-2x6.cfg") == 2,
            "the list's 2-in entry not chosen on 6 inputs");
    require(inputs("wide-fewer.txt", HostLayout{44100, 2, 6}) == 2, "the exact 2-in entry");
    require(inputs("wide-fewer.txt", HostLayout{44100, 1, 6}) == 1, "the exact 1-in entry");
    require(inputs("wide-exact.txt", six) == 6, "an exact entry passed over for a smaller one");
    require(inputs("wide-exact.txt", HostLayout{44100, 4, 6}, "wide-2x6.cfg") == 2,
            "the 2-in entry on 4 inputs");
    // EDGE-3: the default routings come after PLUG-33, so a list that
    // played its entry for fewer inputs before plays it still: a mono entry
    // later in the list does not play on each channel instead.
    write("wide-1x1.cfg", "44100 1 1 0\n0\n0\nwide.wav\n0\n0\n0\n");
    write("wide-1x2.cfg", "44100 1 2 0\n0\n0 0\nwide.wav\n0\n0\n0\nwide.wav\n0\n0\n1\n");
    write("wide-mono-last.txt", "wide-1x2.cfg\nwide-1x1.cfg\n");
    write("wide-crossover-last.txt", "wide-1x1.cfg\nwide-2x6.cfg\n");
    require(inputs("wide-mono-last.txt", HostLayout{44100, 2, 2}, "wide-1x2.cfg") == 1,
            "the 1-in, 2-out entry passed over for a mono one on each channel");
    require(inputs("wide-crossover-last.txt", six, "wide-2x6.cfg") == 2,
            "the crossover passed over for a mono entry on each of 6 channels");
    // With no entry for fewer inputs, the mono entry plays on each channel.
    write("wide-mono-only.txt", "wide-48k.cfg\nwide-1x1.cfg\n");
    {
        auto [state, why, named] = chosen("wide-mono-only.txt", HostLayout{44100, 2, 2});
        require(state && state->configuration->input_channels == 1 &&
                    state->configuration->output_channels == 1 && named.empty(),
                "a list's mono entry on 2 channels: " + detail::advice_text(why.message));
    }
    // Still refused as before: fewer inputs than the filter, other outputs,
    // another rate, and a rate that is not whole hertz.
    const auto refusal = [&](const std::string& name, const HostLayout& host,
                             HostFailureKind kind) {
        auto [state, why, named] = chosen(name, host);
        require(!state && why.kind == kind && named.empty(),
                name + " accepted where it does not fit");
    };
    refusal("wide-2x6.cfg", HostLayout{44100, 1, 6}, HostFailureKind::channels);
    refusal("wide-2x6.cfg", HostLayout{44100, 6, 8}, HostFailureKind::channels);
    refusal("wide-2x6.cfg", HostLayout{44100, 8, 2}, HostFailureKind::channels);
    refusal("wide-2x6.cfg", HostLayout{48000, 6, 6}, HostFailureKind::sample_rate);
    refusal("wide-fewer.txt", HostLayout{44100, 6, 2}, HostFailureKind::channels);
    HostLayout pulled{0, 6, 6};
    set_host_rate(pulled, 48000 / 1.001);
    refusal("wide-48k.cfg", pulled, HostFailureKind::sample_rate);
    // jrp (12 November 2026): the choice must be clear to the user. The log
    // and the window's reference lines name the entry and say why it plays.
    {
        FilterFileFailure why;
        HostRequest request{HostFilterSource::file, detail::utf8_string(dir / "wide-fewer.txt"), 0};
        auto state = read_filter_file(request.path, six, detail::libsndfile_files(), why,
                                      &request.files, &request.wider_entry);
        require(state.has_value(), "wide-fewer.txt refused: " + detail::advice_text(why.message));
        HostFilter filter;
        filter.load(std::move(*state), six, {}, request);
        const auto line = loaded_line(filter, std::optional<HostLayout>(six));
        std::cout << "wider bus: " << line << '\n';
        require(has(line, "No entry in the list has the host's 6 inputs, so ") &&
                    has(line, "wide-2x6.cfg is used, the entry with the most inputs below 6.") &&
                    has(line, "Inputs 3–6 are ignored: the filter reads inputs 1–2."),
                "the log line does not explain the choice: " + line);
        HostReportFacts facts;
        facts.host = six;
        const auto report = host_report(filter, facts);
        require(std::any_of(
                    report.reference.begin(), report.reference.end(),
                    [](const auto& fact) {
                        return fact.first == "List entry" && has(fact.second, "wide-2x6.cfg") &&
                               has(fact.second,
                                   "when the list was chosen, no entry had the host's input count");
                    }),
                "the window's reference lines do not name the list entry");
    }
    // The command line still selects exactly.
    try {
        load_configuration(dir / "wide-2x6.cfg", ConfigurationSelection{44100, 6, 6});
        require(false, "the command line took a 2-in filter for 6 inputs");
    } catch (const std::runtime_error&) {
    }
}

// DIAG-7: a name with characters from no single Windows ANSI code page
// (Greek, Cyrillic and Chinese), passed as UTF-8 as every window passes it.
// On Windows, path::string() throws for such a name and path(std::string)
// misreads it, so before DIAG-7 these failed there with a conversion error.
void non_ansi_names(const fs::path& dir) {
    const std::string name = "\xce\xa9\xd0\x96\xe6\xbf\xbe"; // Omega, Zhe, a CJK character.
    const auto folder = dir / detail::utf8_path(name);
    fs::create_directory(folder);
    const HostLayout stereo{44100, 2, 2};
    // An IR file loads directly; a .cfg naming an IR whose extension holds
    // such characters loads too (raw_filter_name() reads every IR's extension).
    impulse(folder / detail::utf8_path(name + ".wav"), 44100, 2);
    impulse(folder / detail::utf8_path("ir." + name), 44100, 2);
    {
        std::ofstream cfg(folder / detail::utf8_path(name + ".cfg"), std::ios::binary);
        cfg << "44100 2 2 0\n0 0\n0 0\nir." << name << "\n0\n0\n0\nir." << name << "\n1\n1\n1\n";
    }
    for (const auto& file : {name + ".wav", name + ".cfg"}) {
        const auto text = detail::utf8_string(folder / detail::utf8_path(file));
        FilterFileFailure why;
        std::vector<std::string> files;
        const auto next = read_filter_file(text, stereo, detail::libsndfile_files(), why, &files);
        require(next.has_value(), file + " was refused: " + why.message.problem);
        require(!files.empty() && has(files.front(), name) && has(files.back(), name),
                file + ": the files read are not named in UTF-8");
    }
    // Refusals name the file in UTF-8: a filter for other channels, and a
    // damaged WAV (validate_wave()'s message).
    impulse(folder / detail::utf8_path(name + "-three.wav"), 44100, 3);
    refused(folder / detail::utf8_path(name + "-three.wav"), stereo, HostFailureKind::channels);
    {
        const auto whole = folder / detail::utf8_path(name + ".wav");
        const auto cut = folder / detail::utf8_path(name + "-cut.wav");
        fs::copy_file(whole, cut);
        fs::resize_file(cut, fs::file_size(whole) - 6);
        FilterFileFailure why;
        const auto next =
            read_filter_file(detail::utf8_string(cut), stereo, detail::libsndfile_files(), why);
        // DIAG-6: with the reader's reason, not "neither a configuration".
        require(!next && has(why.message.problem, name + "-cut.wav") &&
                    has(why.message.problem, "RIFF/WAVE"),
                "a damaged file with a non-ANSI name: " + why.message.problem);
    }
}
// PLUG-27: before the host plays audio through the plug-in, its rate and
// channels may be placeholders (Audacity 4.0.2 sets its window's copy up at
// 44.1 kHz whatever the project's rate), so a chosen file is read with no
// host: a single filter as it is, with every file it was read from, and a
// list refused with jrp's words (20 November 2026), since choosing its entry
// needs them. The adapter keeps the single filter and checks it when the
// host starts (vst3.adapter chosen_before_host_plays).
void before_host_settles(const fs::path& dir) {
    impulse(dir / "early.wav", 48000, 1);
    impulse(dir / "early-slow.wav", 44100, 1);
    {
        std::ofstream cfg(dir / "early.cfg");
        cfg << "48000 2 2 0\n0 0\n0 0\nearly.wav\n0\n0\n0\nearly.wav\n0\n1\n1\n";
        std::ofstream slow(dir / "early-slow.cfg");
        slow << "44100 2 2 0\n0 0\n0 0\nearly-slow.wav\n0\n0\n0\nearly-slow.wav\n0\n1\n1\n";
        std::ofstream list(dir / "early-list.txt");
        list << "early-slow.cfg\nearly.cfg\n";
    }
    std::vector<std::string> files;
    FilterFileFailure why;
    auto cfg = read_filter_file((dir / "early.cfg").string(), std::nullopt,
                                detail::libsndfile_files(), why, &files);
    require(cfg && cfg->configuration && cfg->configuration->sample_rate == 48000 &&
                cfg->configuration->input_channels == 2 && cfg->configuration->output_channels == 2,
            "a single .cfg not read as it is: " + detail::advice_text(why.message));
    require(files.size() >= 2 && fs::path(files[0]).filename() == "early.cfg" &&
                fs::path(files.back()).filename() == "early.wav",
            "a single .cfg's files: " + std::to_string(files.size()));
    auto wav = read_filter_file((dir / "early.wav").string(), std::nullopt,
                                detail::libsndfile_files(), why, &files);
    require(wav && wav->configuration && wav->configuration->sample_rate == 48000 &&
                wav->configuration->input_channels == 1,
            "an IR file not read as it is: " + detail::advice_text(why.message));
    files = {"stale"};
    require(!read_filter_file((dir / "early-list.txt").string(), std::nullopt,
                              detail::libsndfile_files(), why, &files),
            "a list's entry chosen before the host's rate is known");
    require(files.empty() && why.kind == HostFailureKind::settings_unknown && !why.filter(),
            "a waiting list's failure");
    require(has(why.message.problem, "early-list.txt: no entry selected") &&
                why.message.fix ==
                    "Play audio once so Convolver can check the host's sample rate and channel "
                    "counts, then choose the configuration list again.",
            "a waiting list's message: " + detail::advice_text(why.message));
    // Once the host has started at 48 kHz, the list chooses that entry, also
    // while it is not playing (stopped after playing)...
    const HostLayout started{48000, 2, 2};
    for (const bool settled : {true, false}) {
        auto chosen = read_filter_file((dir / "early-list.txt").string(), started,
                                       detail::libsndfile_files(), why, nullptr, nullptr, settled);
        require(chosen && chosen->configuration && chosen->configuration->sample_rate == 48000,
                "the list's 48 kHz entry not chosen once the host started");
    }
    // ... while a single filter for another rate is read as it is, to be
    // kept and checked when the host starts, unless the host is playing.
    auto slow = read_filter_file((dir / "early-slow.cfg").string(), started,
                                 detail::libsndfile_files(), why, nullptr, nullptr, false);
    require(slow && slow->configuration && slow->configuration->sample_rate == 44100,
            "a single filter for another rate refused while the host is not playing");
    require(!read_filter_file((dir / "early-slow.cfg").string(), started,
                              detail::libsndfile_files(), why) &&
                why.kind == HostFailureKind::sample_rate,
            "a single filter for another rate accepted while the host plays");
    // jrp (21 November 2026): a list with no entry for a configuration the
    // host has started with but is not playing (Audacity's placeholder
    // 44.1 kHz) is refused as a normal mismatch naming that rate, and the fix
    // adds what to do if the host has not played at the project's rate yet;
    // while the host plays, the fix is the normal one alone.
    {
        std::ofstream list(dir / "early-48k.txt");
        list << "early.cfg\n";
    }
    const std::string play_once =
        "If the host has not played audio at your project's rate yet, play audio once so "
        "Convolver can check the host's sample rate and channel counts, then choose the "
        "configuration list again.";
    const HostLayout placeholder{44100, 2, 2};
    for (const bool settled : {false, true}) {
        require(!read_filter_file((dir / "early-48k.txt").string(), placeholder,
                                  detail::libsndfile_files(), why, nullptr, nullptr, settled) &&
                    why.kind == HostFailureKind::sample_rate &&
                    has(why.message.problem, "44100 Hz") &&
                    has(why.message.fix, play_once) == !settled && why.message.fix != play_once,
                "a list with no entry for the host's rate, settled " + std::to_string(settled) +
                    ": " + detail::advice_text(why.message));
    }
    // Something that is not a filter is refused as before, naming the file.
    {
        std::ofstream junk(dir / "early-junk.cfg");
        junk << "not a configuration\n";
    }
    require(!read_filter_file((dir / "early-junk.cfg").string(), std::nullopt,
                              detail::libsndfile_files(), why) &&
                has(why.message.problem, "early-junk.cfg"),
            "an unreadable file: " + detail::advice_text(why.message));
}
// Whether a processor for two channels plays the mono 0.5 tap of each.wav
// on each, when `playback` builds it, replaces what it played, or is
// prepared and published in its place (PLUG-57: each lays the filter out):
// 1 and 2 in give 0.5 and 1 out, where one channel's input reaching the
// other, or a channel left unfiltered, would differ.
enum class Reached { built, replaced, prepared };
bool plays_each_channel(const HostPlayback& playback, Reached how) {
    constexpr std::size_t frames = 64;
    const auto stereo = filter_playback(Configuration{
        48000, 2, 2, 0, {}, {}, {{{1.f}, {{0, 1}}, {{0, 1}}}, {{1.f}, {{1, 1}}, {{1, 1}}}}});
    HostProcessor processor(how == Reached::built ? playback : stereo, frames);
    if (how == Reached::replaced)
        processor.replace(playback);
    else if (how == Reached::prepared)
        processor.publish(processor.prepare(playback));
    std::vector<float> left(frames, 1.f), right(frames, 2.f), out_left(frames), out_right(frames);
    const std::array<const float*, 2> in{left.data(), right.data()};
    const std::array<float*, 2> out{out_left.data(), out_right.data()};
    for (std::size_t done = 0; done < 2 * processor.latency_frames() + 8192; done += frames)
        if (!processor.process(in, out, frames))
            return false;
    return out_left.back() == .5f && out_right.back() == 1.f;
}
bool plays_each_channel(const HostPlayback& playback) {
    return plays_each_channel(playback, Reached::built) &&
           plays_each_channel(playback, Reached::replaced) &&
           plays_each_channel(playback, Reached::prepared);
}
// EDGE-3 (jrp, 25 November 2026): a mono IR file on a stereo track plays on
// each channel, kept as it is so that it suits a mono track too; a
// four-channel one plays as true stereo, read so whenever the host's two
// channels are known, also while it is not playing.
void default_layouts(const fs::path& dir) {
    const HostLayout stereo{48000, 2, 2}, mono{48000, 1, 1};
    impulse(dir / "each.wav", 48000, 1);
    impulse(dir / "hall.wav", 48000, 4);
    const auto fact = [](const HostReport& report, const std::string& label) {
        for (const auto& f : report.audio)
            if (f.label == label)
                return f;
        return HostFact{};
    };
    for (const bool settled : {true, false}) {
        FilterFileFailure why;
        auto state = read_filter_file((dir / "each.wav").string(), stereo,
                                      detail::libsndfile_files(), why, nullptr, nullptr, settled);
        require(state && state->configuration && state->configuration->input_channels == 1 &&
                    state->configuration->output_channels == 1,
                "a mono IR file on a stereo track not kept as it is: " +
                    detail::advice_text(why.message));
        HostFilter filter;
        std::optional<HostPlayback> installed;
        filter.load(std::move(*state), stereo, [&](const HostPlayback& p) { installed = p; },
                    {HostFilterSource::file, (dir / "each.wav").string(), 0});
        // PLUG-57: what plays shares the mono filter, laid out on each
        // channel only by the processor built from it.
        require(installed && installed->filter == filter.filter() && installed->each_channel == 2 &&
                    plays_each_channel(*installed),
                "the mono filter installed on a stereo track is not on each channel");
        require(filter.filter()->input_channels == 1, "the saved filter is not the mono one");
        const auto played = filter.activation(stereo);
        require(!played.failure && played.playback.filter == filter.filter() &&
                    plays_each_channel(played.playback),
                "a start on a stereo track does not play the mono filter on each channel");
        const auto one = filter.activation(mono);
        require(!one.failure && one.playback.filter->input_channels == 1 &&
                    !one.playback.each_channel,
                "the mono filter does not play as it is on a mono track");
        const auto status = host_status(filter, stereo).text;
        require(has(status, "Filter loaded: 48000 Hz, 1 in, 1 out; on each of 2 channels."),
                "status: " + status);
        HostReportFacts facts;
        facts.host = stereo;
        const auto routing = fact(host_report(filter, facts), "Routing");
        require(routing.value == "In 1 → Out 1, In 2 → Out 2" &&
                    routing.note == "(the one-channel filter on each channel)",
                "routing: " + routing.value + " " + routing.note);
        const auto host_channels = fact(host_report(filter, facts), "Host channels");
        require(host_channels.note.empty(), "an input reported ignored: " + host_channels.note);
        const auto line = loaded_line(filter, std::optional<HostLayout>(stereo));
        require(has(line, "it plays on each of the host's 2 channels"), "log: " + line);
    }
    // True stereo, once the host's channels are known (settled or not).
    for (const bool settled : {true, false}) {
        FilterFileFailure why;
        auto state = read_filter_file((dir / "hall.wav").string(), stereo,
                                      detail::libsndfile_files(), why, nullptr, nullptr, settled);
        require(state && state->configuration && state->configuration->input_channels == 2 &&
                    state->configuration->paths.size() == 4 &&
                    true_stereo_routing(*state->configuration),
                "a four-channel IR file not read as true stereo: " +
                    detail::advice_text(why.message));
        HostFilter filter;
        filter.load(std::move(*state), stereo);
        const auto status = host_status(filter, stereo).text;
        require(has(status, "2 in, 2 out; true stereo."), "status: " + status);
        HostReportFacts facts;
        facts.host = stereo;
        require(fact(host_report(filter, facts), "Routing").note == "(true stereo)",
                "the true-stereo routing not named");
    }
    // On four channels it filters each channel; before any start it is read
    // as it is, and a stereo start says to choose it again.
    {
        FilterFileFailure why;
        auto quad = read_filter_file((dir / "hall.wav").string(), HostLayout{48000, 4, 4},
                                     detail::libsndfile_files(), why);
        require(quad && quad->configuration->input_channels == 4 &&
                    !true_stereo_routing(*quad->configuration),
                "a four-channel IR file on four channels not read as it is");
        auto early = read_filter_file((dir / "hall.wav").string(), std::nullopt,
                                      detail::libsndfile_files(), why);
        require(early && early->configuration->input_channels == 4,
                "a four-channel IR file before the host's channels not read as it is");
        HostFilter filter;
        filter.load(std::move(*early), std::nullopt);
        const auto start = filter.activation(stereo);
        require(start.failure && has(start.failure->message.note, "choose it again"),
                "a four-channel file read before the host started gives no advice");
    }
    // jrp's review of a404df0: a B-format file chosen on a stereo host is
    // refused with its reason, whether the host plays or not and whether or
    // not a filter that suits it plays (PLUG-42 keeps other unsuited files),
    // never kept as four channels.
    b_format_wave(dir / "b-format.wav");
    for (const bool settled : {true, false})
        for (const bool keep : {true, false}) {
            FilterFileFailure why;
            const auto state =
                read_filter_file((dir / "b-format.wav").string(), stereo,
                                 detail::libsndfile_files(), why, nullptr, nullptr, settled, keep);
            const auto text = detail::advice_text(why.message);
            require(!state && has(text, "Ambisonics B-format") && !why.carried &&
                        why.kind == HostFailureKind::channels,
                    "a B-format file on a stereo host, settled " + std::to_string(settled) +
                        ", keep " + std::to_string(keep) +
                        ", not refused with its reason: " + text);
        }
    // Where true stereo could not apply (2 in, 6 out), it is any four-channel
    // file: kept as it is while the host is stopped (review of a044d1d).
    {
        FilterFileFailure why;
        const auto kept =
            read_filter_file((dir / "b-format.wav").string(), HostLayout{48000, 2, 6},
                             detail::libsndfile_files(), why, nullptr, nullptr, false);
        require(kept && kept->configuration->input_channels == 4,
                "a B-format file on a stopped 2-in, 6-out host not kept as it is: " +
                    detail::advice_text(why.message));
    }
}
} // namespace

int main() {
    const auto dir = fs::temp_directory_path() /
                     ("convolver-filter-file-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(dir);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    const auto failed = convolver::test::run_cases({
        {"run", [&] { run(dir); }},
        {"non_ansi_names", [&] { non_ansi_names(dir); }},
        {"wider_bus", [&] { wider_bus(dir); }},
        {"before_host_settles", [&] { before_host_settles(dir); }},
        {"default_layouts", [&] { default_layouts(dir); }},
    });
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (failed)
        return 1;
    std::cout << "filter file tests passed\n";
}
