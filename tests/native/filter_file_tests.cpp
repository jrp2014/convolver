// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-9: a filter file chosen in a plug-in's window and refused, through
// read_filter_file() and HostFilter::reject() as the Audio Unit and LV2 call
// them. The short status text (the Status parameter, the window's first
// line and Copy diagnostics) must give the refused filter's own rate and
// counts, never "0 in, 0 out" (jrp's GarageBand run, 30 October 2026: a
// mono WAV refused on a stereo track read "the filter has 0 in, 0 out").
#include "advice.hpp"
#include "cases.hpp"
#include "convolver/host.hpp"
#include "convolver/io.hpp"
#include "filter_file.hpp"
#include "host_log.hpp"
#include "host_report.hpp"
#include "sound_files.hpp"
#include "utf8.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
using namespace convolver;
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
    impulse(dir / "fast.wav", 48000, 2);
    const auto channels = refused(dir / "mono.wav", stereo, HostFailureKind::channels);
    require(has(channels, "the filter has 1 in, 1 out, the host 2 in, 2 out."),
            "channel counts: " + channels);
    const auto rate = refused(dir / "fast.wav", stereo, HostFailureKind::sample_rate);
    require(has(rate, "the filter is for 48000 Hz, the host runs at 44100 Hz."), "rate: " + rate);

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
                    why.message.fix == "Set the host's project or audio device to 48000 Hz.",
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
                    why.message.fix == "Set the host's project or audio device to 47952 Hz.",
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
    const auto mono = explanation(dir / "mono.wav", stereo);
    require(has(mono, "load it in Convolver instead") && !has(mono, "window"),
            "the mono example: " + mono);
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
    impulse(dir / "fast-mono.wav", 48000, 1);
    {
        std::ofstream list(dir / "mixed.txt");
        list << "mono.wav\nfast-mono.wav\n";
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
                    has(line, "wide-2x6.cfg plays, the entry with the most inputs below 6.") &&
                    has(line, "Inputs 3–6 are ignored: the filter reads inputs 1–2."),
                "the log line does not explain the choice: " + line);
        HostReportFacts facts;
        facts.host = six;
        const auto report = host_report(filter, facts);
        require(std::any_of(report.reference.begin(), report.reference.end(),
                            [](const auto& fact) {
                                return fact.first == "List entry" &&
                                       has(fact.second, "wide-2x6.cfg") &&
                                       has(fact.second, "no entry has the host's 6 inputs");
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
    impulse(folder / detail::utf8_path(name + "-mono.wav"), 44100, 1);
    refused(folder / detail::utf8_path(name + "-mono.wav"), stereo, HostFailureKind::channels);
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
    });
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (failed)
        return 1;
    std::cout << "filter file tests passed\n";
}
