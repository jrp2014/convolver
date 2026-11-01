// SPDX-License-Identifier: GPL-2.0-or-later
#include "compare_cli.hpp"
#include "convolver/io.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace convolver::cli {
namespace {
constexpr auto usage = R"(Usage: convolver --compare OUTPUT EXPECTED
Compare a rendered sound file with the file it should match, sample for
sample, for example a host's render of input.wav with a first-use example's
expected.wav. The files must have the same sample rate and channel count.
They match when no sample differs by more than 0.0001 times the expected
file's peak (-80 dB), or by two 16-bit steps (0.000061) if that is larger,
which allows for rounding and 16-bit dither. When they differ, Convolver says
whether the output is silent, shifted in time, shorter, louder or quieter, or
inverted.
Exit status: 0 the files match, 1 they differ or cannot be read, 2 invalid usage.
)";
// 64 Mi samples (256 MiB) per file: over ten minutes of 48 kHz stereo. Both
// files are held in memory, so this bounds the comparison at about 512 MiB.
constexpr std::size_t max_samples = std::size_t{1} << 26;
constexpr auto limit_words = "0.0001 of the expected peak, -80 dB, or two 16-bit steps";
struct Usage : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

struct Sound {
    AudioInfo info{};
    std::vector<float> samples;
    [[nodiscard]] std::size_t frames() const {
        return samples.size() / info.channels;
    }
};

std::string text(const std::filesystem::path& p) {
    const auto s = p.u8string();
    return {s.begin(), s.end()};
}
std::filesystem::path utf8_path(std::string_view s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
std::string number(const char* format, double value) {
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), format, value);
    return buffer.data();
}
std::string milliseconds(std::size_t frames, int rate) {
    return number("%.2f", 1000.0 * static_cast<double>(frames) / rate) + " ms";
}

Sound read(const std::filesystem::path& path) {
    if (path == "-")
        throw Usage("--compare needs two files, not standard input");
    AudioReader reader(path);
    Sound sound{reader.info(), {}};
    std::vector<float> block(65536 * sound.info.channels);
    while (const auto frames = reader.read(block)) {
        const auto count = frames * sound.info.channels;
        if (sound.samples.size() + count > max_samples)
            throw std::runtime_error(text(path) + ": too long to compare (more than " +
                                     std::to_string(max_samples) + " samples)");
        sound.samples.insert(sound.samples.end(), block.begin(),
                             block.begin() + static_cast<std::ptrdiff_t>(count));
    }
    return sound;
}

double peak(std::span<const float> samples) {
    double result = 0;
    for (const float s : samples)
        result = std::max(result, static_cast<double>(std::fabs(s)));
    return result;
}
std::string decibels(double ratio) {
    return number("%.2f", 20 * std::log10(ratio)) + " dB";
}
} // namespace

Difference difference(std::span<const float> output, std::span<const float> expected,
                      std::size_t channels, std::ptrdiff_t shift, double gain) {
    const auto out_frames = static_cast<std::ptrdiff_t>(output.size() / channels);
    const auto expected_frames = static_cast<std::ptrdiff_t>(expected.size() / channels);
    Difference result;
    // Expected frame i lines up with output frame i + shift.
    const auto first = std::min<std::ptrdiff_t>(0, -shift);
    const auto last = std::max(expected_frames, out_frames - shift);
    for (auto i = first; i < last; ++i) {
        const auto j = i + shift;
        for (std::size_t c = 0; c < channels; ++c) {
            const double e = i >= 0 && i < expected_frames
                                 ? gain * expected[static_cast<std::size_t>(i) * channels + c]
                                 : 0.0;
            const double o =
                j >= 0 && j < out_frames ? output[static_cast<std::size_t>(j) * channels + c] : 0.0;
            if (const auto d = std::fabs(o - e); d > result.largest)
                result = {d, c, static_cast<std::size_t>(std::max<std::ptrdiff_t>(i, 0))};
        }
    }
    return result;
}

std::optional<std::size_t> onset(std::span<const float> samples, std::size_t channels,
                                 double level) {
    for (std::size_t i = 0; i < samples.size(); ++i)
        if (std::fabs(samples[i]) > level)
            return i / channels;
    return std::nullopt;
}

bool is_comparison(int argc, char** argv) {
    return argc > 1 && std::string_view(argv[1]) == "--compare";
}

int run_comparison(int argc, char** argv) {
    for (int i = 2; i < argc; ++i)
        if (std::string_view(argv[i]) == "--help") {
            std::cout << usage;
            return 0;
        }
    if (argc != 4) {
        std::cerr << "convolver: --compare takes two files: OUTPUT EXPECTED\n" << usage;
        return 2;
    }
    try {
        const auto output_path = utf8_path(argv[2]);
        const auto expected_path = utf8_path(argv[3]);
        const auto output = read(output_path);
        const auto expected = read(expected_path);
        const auto describe = [](const Sound& s) {
            return std::to_string(s.info.sample_rate) + " Hz, " + std::to_string(s.info.channels) +
                   (s.info.channels == 1 ? " channel, " : " channels, ") +
                   std::to_string(s.frames()) + " frames (" +
                   milliseconds(s.frames(), s.info.sample_rate) + ")";
        };
        std::cout << "Output:   " << text(output_path) << ": " << describe(output) << '\n'
                  << "Expected: " << text(expected_path) << ": " << describe(expected) << '\n';
        if (output.info.sample_rate != expected.info.sample_rate) {
            std::cout << "Different: the output is at " << output.info.sample_rate
                      << " Hz and the expected file at " << expected.info.sample_rate
                      << " Hz. Render at the expected file's rate; Convolver never resamples.\n";
            return 1;
        }
        if (output.info.channels != expected.info.channels) {
            const auto count = [](std::size_t n) {
                return std::to_string(n) + (n == 1 ? " channel" : " channels");
            };
            std::cout << "Different: the output has " << count(output.info.channels)
                      << " and the expected file " << count(expected.info.channels)
                      << ". Render the same channels as the expected file.\n";
            return 1;
        }
        const auto channels = expected.info.channels;
        const auto rate = expected.info.sample_rate;
        const double expected_peak = peak(expected.samples);
        const double output_peak = peak(output.samples);
        const double limit = compare_limit(expected_peak);
        const auto found = difference(output.samples, expected.samples, channels);
        std::cout << "Largest difference: " << number("%.3g", found.largest);
        if (found.largest > 0 && expected_peak > 0)
            std::cout << " (" << decibels(found.largest / expected_peak)
                      << " relative to the expected peak of " << number("%.3g", expected_peak)
                      << ")";
        if (found.largest > 0)
            std::cout << ", channel " << found.channel + 1 << ", frame " << found.frame << " ("
                      << milliseconds(found.frame, rate) << ")";
        std::cout << '\n';
        if (found.largest <= limit) {
            std::cout << "Match: no sample differs by more than the limit (" << limit_words
                      << ").\n";
            return 0;
        }
        std::cout << "Different: samples differ by more than the limit (" << limit_words << ").\n";
        // Explain the usual causes, most specific first.
        if (output_peak == 0) {
            std::cout << (output.frames() == 0 ? "The output is empty. " : "The output is silent. ")
                      << "Check that the render includes the track, and that the filter "
                         "loaded: a plug-in plays silence when its filter is for another rate "
                         "or channel count.\n";
            return 1;
        }
        // The frames the output covers of the expected file, when moved `shift`
        // frames earlier: what is missing beyond them is a short render.
        const auto covered = [&](std::ptrdiff_t shift) {
            const auto frames = static_cast<std::ptrdiff_t>(output.frames()) - shift;
            return static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
                frames, 0, static_cast<std::ptrdiff_t>(expected.frames())));
        };
        const auto matches = [&](std::ptrdiff_t shift, double gain) {
            const std::span<const float> head(expected.samples.data(), covered(shift) * channels);
            return difference(output.samples, head, channels, shift, gain).largest <=
                   limit * std::fabs(gain);
        };
        const auto short_words = [&](std::ptrdiff_t shift) {
            const auto missing = expected.frames() - covered(shift);
            return missing == 0 ? std::string()
                                : " It also ends " + std::to_string(missing) + " frames (" +
                                      milliseconds(missing, rate) +
                                      ") too soon: render a longer tail after the input ends.";
        };
        const auto out_start = onset(output.samples, channels, 1e-3 * output_peak);
        const auto expected_start = onset(expected.samples, channels, 1e-3 * expected_peak);
        if (out_start && expected_start && *out_start != *expected_start) {
            const auto shift = static_cast<std::ptrdiff_t>(*out_start) -
                               static_cast<std::ptrdiff_t>(*expected_start);
            if (matches(shift, 1)) {
                const auto frames = static_cast<std::size_t>(std::abs(shift));
                std::cout << "The output would match if it started " << frames << " frames ("
                          << milliseconds(frames, rate) << ") " << (shift > 0 ? "earlier" : "later")
                          << ". "
                          << (shift > 0 ? "A host that does not make up for a plug-in's latency "
                                          "delays its output this way: turn on the host's delay "
                                          "(latency) compensation, or render from the start of the "
                                          "input."
                                        : "Check that the render starts where the input does.")
                          << short_words(shift) << '\n';
                return 1;
            }
        }
        if (output.frames() < expected.frames() && matches(0, 1)) {
            const auto missing = expected.frames() - output.frames();
            std::cout << "The output matches as far as it goes, but ends " << missing << " frames ("
                      << milliseconds(missing, rate)
                      << ") too soon: it lacks the end of the filter's tail. Render a longer tail "
                         "after the input ends.\n";
            return 1;
        }
        // A level or polarity difference: the best gain for the output.
        double cross = 0, energy = 0;
        const auto common = std::min(output.samples.size(), expected.samples.size());
        for (std::size_t i = 0; i < common; ++i) {
            cross += static_cast<double>(output.samples[i]) * expected.samples[i];
            energy += static_cast<double>(expected.samples[i]) * expected.samples[i];
        }
        if (energy > 0 && cross != 0 && matches(0, cross / energy)) {
            const double gain = cross / energy;
            if (gain < 0)
                std::cout << "The output would match if inverted: its polarity is reversed"
                          << (std::fabs(std::fabs(gain) - 1) > 1e-3
                                  ? " and it is " + decibels(-gain) +
                                        " relative to the expected file"
                                  : std::string())
                          << ". Check the track's and the host's polarity (phase) settings.\n";
            else
                std::cout << "The output would match at a different level: it is " << decibels(gain)
                          << " relative to the expected file. Check the track's volume, pan and "
                             "gain settings.\n";
            return 1;
        }
        std::cout << "Check that the output was rendered from the example's input.wav with its "
                     "filter, and that nothing else on the track changed the sound.\n";
        return 1;
    } catch (const Usage& error) {
        std::cerr << "convolver: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "convolver: " << error.what() << '\n';
        return 1;
    }
}
} // namespace convolver::cli
