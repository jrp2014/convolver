// SPDX-License-Identifier: GPL-2.0-or-later
// PERF-4 divergence check: the AVX2+FMA kernels change rounding, so the output
// differs from the baseline kernels' output. This test measures how much on
// room-correction workloads and fails on a divergence that could matter
// (owner's decision, 24 October 2026: "add FMA, but keep an eye out for
// significant output divergences"). For each workload it renders the whole
// signal with both kernel sets, compares them sample by sample, and compares
// both with a direct double-precision convolution at frames where the whole
// filter is engaged. Limits, fixed here and not to be widened without
// investigation:
//   - FMA against baseline: at most 1e-6 of the output's peak, and at most
//     -120 dBFS (1e-6 of full scale, 1.0) for inputs at or above -120 dBFS.
//     Each workload is gain-staged as a real chain would be: the input is scaled
//     so that the full-level signal's output peaks at -0.1 dBFS (these synthetic
//     filters have up to about +11 dB of gain, which would clip, and the absolute
//     differences scale with the level). The quiet inputs are that level less
//     120 dB, or 1e-35 of it: output peaking near 1e-35, where about a quarter
//     of the output samples and many products are subnormal (below FLT_MIN,
//     1.2e-38), so FMA's single rounding and the baseline's two meet reduced
//     precision. The test requires that case to have subnormal output and
//     differing samples, so it cannot pass vacuously (at 1e-37 of the level,
//     for example, the sets agree exactly because subnormal steps swamp the
//     rounding difference).
//   - Each set against the oracle: within the engine tests' oracle tolerance,
//     2e-5 * (1 + |expected|), and the FMA set's worst error at most 1.5 times
//     the baseline set's (FMA must not make the result noticeably worse).
// Release builds (NDEBUG) also run the 128k-512k filters; other builds run the
// 64k workloads only, to keep sanitizer runs short.
#include "convolver/engine.hpp"
#include "kernels.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace convolver;
namespace {
void require(bool b, const std::string& message) {
    if (!b)
        throw std::runtime_error(message);
}
// A room-correction filter: a main peak in the middle (as from linear-phase
// tools) with exponentially decaying noise around it.
std::vector<float> room_filter(std::size_t length, std::uint32_t seed) {
    std::mt19937 random(seed);
    std::normal_distribution<float> noise(0, 1);
    std::vector<float> h(length);
    const double centre = double(length) / 2, decay = double(length) / 16;
    for (std::size_t k = 0; k < length; ++k)
        h[k] = float(0.02 * noise(random) * std::exp(-std::abs(double(k) - centre) / decay));
    h[length / 2] += 0.5f;
    return h;
}
// A Blackman-windowed band-pass from low to high Hz at 48 kHz, centred like room_filter.
std::vector<float> band_filter(std::size_t length, double low, double high) {
    std::vector<float> h(length);
    const double centre = double(length / 2), pi = std::numbers::pi;
    const auto sinc = [&](double f, double t) {
        return t == 0 ? 2 * f : std::sin(2 * pi * f * t) / (pi * t);
    };
    for (std::size_t k = 0; k < length; ++k) {
        const double t = double(k) - centre, w = 2 * pi * double(k) / double(length - 1);
        const double window = 0.42 - 0.5 * std::cos(w) + 0.08 * std::cos(2 * w);
        h[k] = float(window * (sinc(high / 48000, t) - sinc(low / 48000, t)));
    }
    return h;
}
enum class Signal { noise, sine, quiet, near_denormal };
struct Workload {
    const char* name;
    std::size_t taps, outputs;
    bool crossover;
    Signal signal;
    bool long_filter; // Release builds only
};
constexpr Workload workloads[] = {
    {"64k stereo, white noise", 65536, 2, false, Signal::noise, false},
    {"64k stereo, full-scale sine", 65536, 2, false, Signal::sine, false},
    {"64k stereo, noise at -120 dBFS", 65536, 2, false, Signal::quiet, false},
    {"64k stereo, near-denormal noise", 65536, 2, false, Signal::near_denormal, false},
    {"64k 2->6 crossover, white noise", 65536, 6, true, Signal::noise, false},
    {"128k stereo, white noise", 131072, 2, false, Signal::noise, true},
    {"256k stereo, white noise", 262144, 2, false, Signal::noise, true},
    {"512k stereo, white noise", 524288, 2, false, Signal::noise, true},
    {"512k stereo, full-scale sine", 524288, 2, false, Signal::sine, true},
};
// Input amplitude relative to full scale, for the -120 dBFS limit.
double amplitude(Signal s) {
    switch (s) {
    case Signal::quiet:
        return 1e-6;
    case Signal::near_denormal:
        return 1e-35;
    default:
        return 1;
    }
}
std::vector<float> input_signal(Signal s, std::size_t frames, double level) {
    std::vector<float> x(2 * frames);
    std::mt19937 random(99);
    std::uniform_real_distribution<float> uniform(-1, 1);
    for (std::size_t t = 0; t < frames; ++t)
        for (std::size_t c = 0; c < 2; ++c) {
            const double phase = 2 * std::numbers::pi * 997.0 * double(t) / 48000 + double(c);
            x[2 * t + c] = s == Signal::sine ? float(level * std::sin(phase))
                                             : float(level * amplitude(s) * uniform(random));
        }
    return x;
}
std::vector<float> render(const Configuration& c, std::size_t block, const std::vector<float>& in,
                          bool baseline) {
    detail::force_baseline_kernels(baseline);
    Engine engine(c, block);
    detail::force_baseline_kernels(false);
    const auto frames = in.size() / c.input_channels;
    std::vector<float> out(frames * c.output_channels);
    for (std::size_t done = 0; done < frames; done += block) {
        const auto n = std::min(block, frames - done);
        engine.process(
            std::span<const float>(in).subspan(done * c.input_channels, n * c.input_channels),
            std::span<float>(out).subspan(done * c.output_channels, n * c.output_channels), n);
    }
    return out;
}
// Direct double-precision convolution at one output frame (the formula of
// oracle.hpp, which computes every frame and would take hours for 512k taps).
double direct(const Configuration& c, const std::vector<float>& in, std::size_t latency,
              std::size_t frame, std::size_t output) {
    double sum = 0;
    for (const auto& p : c.paths)
        for (const auto& i : p.inputs)
            for (const auto& o : p.outputs) {
                if (o.channel != output)
                    continue;
                for (std::size_t k = 0; k < p.impulse.size(); ++k) {
                    const auto t = frame - latency - k; // frame >= latency + taps - 1
                    sum += double(in[t * c.input_channels + i.channel]) * i.gain * p.impulse[k] *
                           o.gain;
                }
            }
    return sum;
}
double db(double x) {
    return x > 0 ? 20 * std::log10(x) : -999;
}
} // namespace
int main() {
    try {
#if defined(NDEBUG)
        const bool release = true;
#else
        const bool release = false;
#endif
        const std::string fast = detail::kernels().name, base = detail::baseline_kernels().name;
        std::cout << "kernel divergence: " << fft_backend_name() << ", " << fast << " against "
                  << base << ", B=256\n";
        if (fast == base)
            std::cout << "(this CPU or build has only the baseline set; the FMA comparison is "
                         "trivially zero, the oracle comparison still runs)\n";
        std::cout << "| Workload | input gain (dB) | FMA-baseline max abs | rel. to peak | dBFS | "
                     "samples differing | subnormal outputs | baseline-oracle max abs | "
                     "FMA-oracle max abs | FMA-oracle rel. |\n";
        constexpr std::size_t block = 256, points = 256;
        for (const auto& w : workloads) {
            if (w.long_filter && !release)
                continue;
            Configuration c{48000, 2, w.outputs, 0, {}, {}, {}};
            for (std::size_t out = 0; out < w.outputs; ++out) {
                FilterPath path;
                if (w.crossover) {
                    constexpr double edges[] = {10, 250, 2500, 23990};
                    const auto band = out / 2;
                    path.impulse = band_filter(w.taps, edges[band], edges[band + 1]);
                    const auto room = room_filter(w.taps, std::uint32_t(out));
                    for (std::size_t k = 0; k < w.taps; ++k)
                        path.impulse[k] += 0.1f * (room[k] - (k == w.taps / 2 ? 0.5f : 0.f));
                } else {
                    path.impulse = room_filter(w.taps, std::uint32_t(out));
                }
                path.inputs = {{out % 2, 1}};
                path.outputs = {{out, 1}};
                c.paths.push_back(std::move(path));
            }
            // Enough input that the whole filter is engaged for the last 8 blocks.
            const auto frames = w.taps + block + 8 * block;
            // Level: the full-level signal (noise for the quiet inputs) peaks at -0.1 dBFS.
            const auto loud = w.signal == Signal::sine ? Signal::sine : Signal::noise;
            double loud_peak = 0;
            for (float y : render(c, block, input_signal(loud, frames, 1), true))
                loud_peak = std::max(loud_peak, double(std::abs(y)));
            const double level = 0.9886 / loud_peak; // -0.1 dBFS
            const auto in = input_signal(w.signal, frames, level);
            const auto a = render(c, block, in, false), b = render(c, block, in, true);
            double diff = 0, peak = 0;
            std::size_t differing = 0, subnormal = 0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                require(std::isfinite(a[i]) && std::isfinite(b[i]), "non-finite output");
                diff = std::max(diff, double(std::abs(a[i] - b[i])));
                peak = std::max(peak, double(std::abs(b[i])));
                differing += a[i] != b[i] ? 1 : 0;
                subnormal += std::fpclassify(b[i]) == FP_SUBNORMAL ? 1 : 0;
            }
            double oracle_fast = 0, oracle_base = 0, oracle_peak = 0;
            const auto first = block + w.taps - 1;
            for (std::size_t p = 0; p < points; ++p) {
                const auto frame = first + (frames - 1 - first) * p / (points - 1);
                for (std::size_t o = 0; o < w.outputs; ++o) {
                    const double e = direct(c, in, block, frame, o);
                    const double ea = std::abs(a[frame * w.outputs + o] - e);
                    const double eb = std::abs(b[frame * w.outputs + o] - e);
                    oracle_fast = std::max(oracle_fast, ea);
                    oracle_base = std::max(oracle_base, eb);
                    oracle_peak = std::max(oracle_peak, std::abs(e));
                    require(ea <= 2e-5 * (1 + std::abs(e)) && eb <= 2e-5 * (1 + std::abs(e)),
                            std::string(w.name) + ": outside the oracle tolerance");
                }
            }
            const double relative = peak > 0 ? diff / peak : 0;
            char line[256];
            std::snprintf(
                line, sizeof line,
                "| %s | %.1f | %.3g | %.3g | %.1f | %.1f%% | %.1f%% | %.3g | %.3g | %.3g |\n",
                w.name, db(level), diff, relative, db(diff),
                100.0 * double(differing) / double(a.size()),
                100.0 * double(subnormal) / double(a.size()), oracle_base, oracle_fast,
                oracle_peak > 0 ? oracle_fast / oracle_peak : 0);
            std::cout << line << std::flush;
            require(relative <= 1e-6, std::string(w.name) + ": FMA and baseline differ by more "
                                                            "than 1e-6 of the peak");
            if (amplitude(w.signal) >= 1e-6)
                require(diff <= 1e-6,
                        std::string(w.name) + ": FMA and baseline differ by more than -120 dBFS");
            if (w.signal == Signal::near_denormal) {
                require(subnormal > 0, std::string(w.name) + ": no subnormal output to test");
                require(fast == base || differing > 0,
                        std::string(w.name) +
                            ": the sets agree exactly, so the case tests nothing");
            }
            require(oracle_fast <= 1.5 * oracle_base,
                    std::string(w.name) + ": FMA is noticeably further from the oracle");
        }
        std::cout << "kernel divergence tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
