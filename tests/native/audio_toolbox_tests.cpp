// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: the Audio Unit reads filter files with macOS's AudioToolbox, the tools
// with libsndfile. Every filter both can read must give the same configuration,
// sample for sample, and the files either refuses must be refused by both,
// with the same diagnostic code. On other systems this file compares
// libsndfile with itself, which only checks the test.
#include "config_parse.hpp"
#include "convolver/io.hpp"
#include "sound_files.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace convolver;
using namespace std::string_literals;
namespace {
const detail::SoundFiles& under_test() {
#ifdef __APPLE__
    return detail::audio_toolbox_files();
#else
    return detail::libsndfile_files();
#endif
}
void require(bool b, const std::string& message) {
    if (!b)
        throw std::runtime_error(message);
}
void text_file(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
    if (!f)
        throw std::runtime_error("fixture write failed");
}
// Three channels of different, deterministic values across the full range,
// with the extremes and a few small values each sample type must keep.
std::vector<float> samples(std::size_t channels, std::size_t frames) {
    std::vector<float> data(channels * frames);
    std::uint32_t state = 12345;
    for (auto& x : data) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<int>(state >> 8) - (1 << 23)) / float(1 << 23);
    }
    data[0] = 1.0f - 1.0f / 32768;
    data[1] = -1.0f;
    data[2] = 1.0f / 32768;
    return data;
}
// A mono IEEE-float WAV of `values`, as 32 or 64-bit samples, written by hand
// so that it can hold values a writer would refuse.
void float_wav(const fs::path& path, const std::vector<double>& values, int bits) {
    std::string out;
    const auto put = [&](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out += static_cast<char>((value >> (8 * i)) & 0xffU);
    };
    const auto width = static_cast<std::uint32_t>(bits / 8);
    const auto data = static_cast<std::uint32_t>(values.size()) * width;
    out += "RIFF";
    put(4 + 26 + 12 + 8 + data, 4);
    out += "WAVEfmt ";
    put(18, 4);
    put(3, 2); // IEEE float
    put(1, 2);
    put(48000, 4);
    put(48000 * width, 4);
    put(width, 2);
    put(static_cast<std::uint64_t>(bits), 2);
    put(0, 2);
    out += "fact";
    put(4, 4);
    put(values.size(), 4);
    out += "data";
    put(data, 4);
    for (const auto x : values)
        put(bits == 32 ? std::uint64_t{std::bit_cast<std::uint32_t>(static_cast<float>(x))}
                       : std::bit_cast<std::uint64_t>(x),
            static_cast<int>(width));
    text_file(path, out);
}
void sound_file(const fs::path& path, AudioInfo info, OutputEncoding encoding) {
    AudioWriter writer(path, info, encoding);
    writer.write(samples(info.channels, 1000));
    writer.close();
}
// The largest difference between two loads, in units of the last place of
// the larger magnitude; throws when anything but the samples differ.
double compare(const Configuration& a, const Configuration& b, const std::string& what) {
    require(a.sample_rate == b.sample_rate && a.input_channels == b.input_channels &&
                a.output_channels == b.output_channels && a.input_delays == b.input_delays &&
                a.output_delays == b.output_delays && a.paths.size() == b.paths.size(),
            what + ": the readers disagree about the configuration");
    double worst = 0;
    for (std::size_t i = 0; i < a.paths.size(); ++i) {
        const auto &x = a.paths[i], &y = b.paths[i];
        require(x.impulse.size() == y.impulse.size(),
                what + ": path " + std::to_string(i) + " has " + std::to_string(x.impulse.size()) +
                    " samples from libsndfile, " + std::to_string(y.impulse.size()) +
                    " from the reader under test");
        for (std::size_t n = 0; n < x.impulse.size(); ++n) {
            const auto p = x.impulse[n], q = y.impulse[n];
            if (p == q)
                continue;
            const auto ulp = std::nextafter(std::max(std::fabs(p), std::fabs(q)), 2.0f) -
                             std::max(std::fabs(p), std::fabs(q));
            worst = std::max(worst, double(std::fabs(p - q)) / ulp);
        }
    }
    return worst;
}
Configuration load(const fs::path& path, const detail::SoundFiles& files) {
    return detail::load_configuration(path, {}, files);
}
// The diagnostic code of a refused file, or empty when it loads.
std::string refusal(const fs::path& path, const detail::SoundFiles& files) {
    try {
        load(path, files);
    } catch (const detail::ConfigurationError& e) {
        return e.code();
    } catch (const std::exception& e) {
        return std::string("other: ") + e.what();
    }
    return "";
}

void run(const fs::path& dir) {
    const auto& reference = detail::libsndfile_files();
    const auto& reader = under_test();
    struct Case {
        const char* name;
        OutputEncoding encoding;
        double tolerance; // In units of the last place.
    };
    const std::vector<Case> cases{
        {"pcm16.wav", {Container::wav, SampleType::pcm16}, 0},
        {"pcm24.wav", {Container::wav, SampleType::pcm24}, 0},
        // A 32-bit integer has more bits than a float; both readers round,
        // perhaps differently at a tie.
        {"pcm32.wav", {Container::wav, SampleType::pcm32}, 1},
        {"float.wav", {Container::wav, SampleType::float32}, 0},
        {"double.wav", {Container::wav, SampleType::float64}, 0},
        {"pcm16.aiff", {Container::aiff, SampleType::pcm16}, 0},
        {"pcm24.aiff", {Container::aiff, SampleType::pcm24}, 0},
        {"float.caf", {Container::caf, SampleType::float32}, 0},
        {"pcm24.caf", {Container::caf, SampleType::pcm24}, 0},
        {"pcm24.w64", {Container::w64, SampleType::pcm24}, 0},
        {"float.rf64", {Container::rf64, SampleType::float32}, 0},
        {"pcm16.flac", {Container::flac, SampleType::pcm16}, 0},
        {"pcm24.flac", {Container::flac, SampleType::pcm24}, 0},
    };
    for (const auto& c : cases) {
        if (!available(c.encoding.container))
            continue; // A libsndfile without FLAC cannot write the fixture.
        const auto path = dir / c.name;
        sound_file(path, {48000, 3}, c.encoding);
        const auto worst = compare(load(path, reference), load(path, reader), c.name);
        std::cout << c.name << ": worst difference " << worst << " ulp\n";
        require(worst <= c.tolerance, std::string(c.name) + ": the readers' samples differ by " +
                                          std::to_string(worst) + " ulp");
    }

    // A configuration naming a WAV and a headerless float filter, and a .pcm
    // that is really a WAV (read through its header by both).
    sound_file(dir / "left.wav", {44100, 1}, {Container::wav, SampleType::pcm24});
    {
        const auto mono = samples(1, 777);
        std::ofstream raw(dir / "right.pcm", std::ios::binary);
        for (const auto x : mono) {
            const auto bits = std::bit_cast<std::uint32_t>(x);
            for (int b = 0; b < 4; ++b)
                raw.put(static_cast<char>((bits >> (8 * b)) & 0xffU));
        }
    }
    sound_file(dir / "header.pcm", {44100, 1}, {Container::wav, SampleType::pcm16});
    text_file(dir / "filter.cfg", "44100 2 2 0\n0 0\n0 0\n"
                                  "left.wav\n0\n0\n0\n"
                                  "right.pcm\n0\n1\n1\n"
                                  "header.pcm\n0\n0\n1\n");
    std::vector<fs::path> raw_reference, raw_tested;
    const auto from_reference =
        detail::load_configuration(dir / "filter.cfg", {}, reference, nullptr, &raw_reference);
    const auto from_reader =
        detail::load_configuration(dir / "filter.cfg", {}, reader, nullptr, &raw_tested);
    require(compare(from_reference, from_reader, "filter.cfg") == 0,
            "filter.cfg: the readers' samples differ");
    require(raw_reference == raw_tested && raw_tested.size() == 1 &&
                raw_tested[0].filename() == "right.pcm",
            "filter.cfg: only right.pcm is a raw filter, for both readers");
    require(from_reader.paths[1].impulse.size() == 777 &&
                from_reader.paths[2].impulse.size() == 1000,
            "filter.cfg: the raw filter and the WAV named .pcm keep their lengths");
    // The same through a configuration list, choosing by rate.
    text_file(dir / "list.txt", "pcm16.wav\nfilter.cfg\n");
    require(compare(detail::load_configuration(dir / "list.txt", {44100, 2, 2}, reference),
                    detail::load_configuration(dir / "list.txt", {44100, 2, 2}, reader),
                    "list.txt") == 0,
            "list.txt: the readers chose or read differently");

    // Refusals: the same diagnostic codes from both readers.
    text_file(dir / "missing.cfg", "44100 1 1 0\n0\n0\nnot-there.wav\n0\n0\n0\n");
    {
        std::ifstream whole(dir / "pcm16.wav", std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(whole)), {});
        text_file(dir / "truncated.wav", bytes.substr(0, bytes.size() - 100));
        text_file(dir / "damaged.wav", "RIFF\x24\0\0\0WAVEjunk"s);
    }
    text_file(dir / "short.pcm", "abc");
    text_file(dir / "short.cfg", "44100 1 1 0\n0\n0\nshort.pcm\n0\n0\n0\n");
    text_file(dir / "rate.cfg", "44100 1 1 0\n0\n0\npcm16.wav\n0\n0\n0\n");
    // Nonfinite samples, stored or made by converting a double too large
    // for a float (the fourth review: a reader that clamped would load it).
    float_wav(dir / "nan.wav", {0.5, std::nan(""), 0.25}, 32);
    float_wav(dir / "huge.wav", {0.5, 1e300, 0.25}, 64);
    // A headerless filter chosen directly rather than named in a .cfg: no
    // reader may take its samples for some other format (the fourth review).
    // The bytes start like an MPEG frame, the case AudioToolbox could guess.
    {
        std::string raw = "\xff\xfb\x90\x64"s;
        for (const auto x : samples(1, 4096))
            for (int b = 0; b < 4; ++b)
                raw += static_cast<char>((std::bit_cast<std::uint32_t>(x) >> (8 * b)) & 0xffU);
        text_file(dir / "direct.pcm", raw);
    }
    for (const auto* name : {"missing.cfg", "truncated.wav", "damaged.wav", "short.cfg", "rate.cfg",
                             "nan.wav", "huge.wav", "direct.pcm"}) {
        const auto expected = refusal(dir / name, reference);
        const auto got = refusal(dir / name, reader);
        std::cout << name << ": refused as " << got << '\n';
        require(!expected.empty() && expected == got,
                std::string(name) + ": libsndfile refuses it as '" + expected +
                    "', the reader under test as '" + got + "'");
    }
    // A file refused after it was opened is closed again (jrp's review: each
    // refused filter left a descriptor open). Twenty refusals of a WAV whose
    // header claims 800 kHz, counting the descriptors open before and after.
    {
        std::ifstream whole(dir / "pcm16.wav", std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(whole)), {});
        const std::uint32_t rate = 800000; // Little-endian, at the WAV header's offset 24.
        for (int b = 0; b < 4; ++b)
            bytes[24 + b] = static_cast<char>((rate >> (8 * b)) & 0xffU);
        text_file(dir / "fast.wav", bytes);
    }
    const auto open_descriptors = [] {
        std::size_t n = 0;
        for ([[maybe_unused]] const auto& entry : fs::directory_iterator("/dev/fd"))
            ++n;
        return n;
    };
    const auto before = open_descriptors();
    for (int i = 0; i < 20; ++i) {
        std::string message;
        try {
            reader.open(dir / "fast.wav");
        } catch (const std::exception& e) {
            message = e.what();
        }
#ifdef __APPLE__
        // The rate is checked after ExtAudioFileOpenURL, the path that leaked.
        const bool after_opening = message.find("800000") != std::string::npos;
#else
        const bool after_opening = !message.empty();
#endif
        require(after_opening,
                "fast.wav is refused for its rate, after it was opened: '" + message + "'");
    }
    const auto after = open_descriptors();
    std::cout << "fast.wav: refused 20 times, open descriptors " << before << " then " << after
              << '\n';
    require(after <= before, "refused files leave descriptors open: " + std::to_string(before) +
                                 " before, " + std::to_string(after) + " after");
    // The reader's own message names the missing file.
    try {
        load(dir / "missing.cfg", reader);
    } catch (const std::exception& e) {
        require(std::string(e.what()).find("not-there.wav") != std::string::npos,
                std::string("the missing filter is named: ") + e.what());
    }
}
} // namespace

int main() {
    const auto dir = fs::temp_directory_path() /
                     ("convolver-audio-toolbox-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(dir);
        run(dir);
        fs::remove_all(dir);
        std::cout << "sound-file reader tests passed\n";
    } catch (const std::exception& e) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
