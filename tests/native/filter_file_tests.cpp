// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-9: a filter file chosen in a plug-in's window and refused, through
// read_filter_file() and HostFilter::reject() as the Audio Unit and LV2 call
// them. The short status text (the Status parameter, the window's first
// line and Copy diagnostics) must give the refused filter's own rate and
// counts, never "0 in, 0 out" (jrp's GarageBand run, 30 October 2026: a
// mono WAV refused on a stereo track read "the filter has 0 in, 0 out").
#include "convolver/host.hpp"
#include "convolver/io.hpp"
#include "filter_file.hpp"
#include "sound_files.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
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
    const auto next = read_filter_file(path.string(), host, detail::libsndfile_files(), why);
    require(!next, path.filename().string() + " was accepted");
    require(why.kind == expected, path.filename().string() + ": wrong failure kind");
    filter.reject(why.kind, why.message, {HostFilterSource::file, path.string(), 0}, why.filter());
    const auto line = host_status(filter, host).text;
    std::cout << path.filename().string() << ": " << line << '\n';
    require(has(line, "Load failed, previous filter kept: "), "status: " + line);
    require(!has(line, "0 in, 0 out") && !has(line, " 0 Hz"), "unknown values shown: " + line);
    return line;
}
void run(const fs::path& dir) {
    const HostLayout stereo{44100, 2, 2};
    impulse(dir / "mono.wav", 44100, 1);
    impulse(dir / "fast.wav", 48000, 2);
    const auto channels = refused(dir / "mono.wav", stereo, HostFailureKind::channels);
    require(has(channels, "the filter has 1 in, 1 out, the plug-in 2 in, 2 out."),
            "channel counts: " + channels);
    const auto rate = refused(dir / "fast.wav", stereo, HostFailureKind::sample_rate);
    require(has(rate, "the filter is for 48000 Hz, the host runs at 44100 Hz."), "rate: " + rate);

    // The full explanation speaks to someone in a host, not at a command
    // line (jrp's GarageBand run: it said to omit --output-channels).
    const auto explanation = [&](const fs::path& path, const HostLayout& host) {
        FilterFileFailure why;
        require(!read_filter_file(path.string(), host, detail::libsndfile_files(), why),
                path.filename().string() + " was accepted");
        std::cout << path.filename().string() << ", explained: " << why.message << "\n\n";
        require(!has(why.message, "--"), "command-line advice: " + why.message);
        return why.message;
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
                "give the plug-in 6 output channels in the host's track or routing settings"),
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
    require(has(mixed, "the filter does not suit the plug-in's 2 in, 2 out.") &&
                !has(mixed, "44100"),
            "list with an entry at the host's rate: " + mixed);
    {
        std::ofstream list(dir / "slow.txt");
        list << "mono.wav\n";
    }
    const auto slow = refused(dir / "slow.txt", fast_stereo, HostFailureKind::sample_rate);
    require(has(slow, "the filter is not for the host's 48000 Hz.") && !has(slow, "1 in"),
            "list with no entry at the host's rate: " + slow);

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
        filter.reject(kind, "unreadable", {});
        const auto line = host_status(filter, stereo).text;
        std::cout << "no filter carried: " << line << '\n';
        require(!has(line, "0 in, 0 out") && !has(line, " 0 Hz"), "unknown values shown: " + line);
        require(has(line, kind == HostFailureKind::channels ? "the plug-in's 2 in, 2 out"
                                                            : "the host's 44100 Hz"),
                "the host's values: " + line);
    }
}
} // namespace

int main() {
    const auto dir = fs::temp_directory_path() /
                     ("convolver-filter-file-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(dir);
        run(dir);
        fs::remove_all(dir);
        std::cout << "filter file tests passed\n";
    } catch (const std::exception& e) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
