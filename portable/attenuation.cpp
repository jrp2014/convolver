// SPDX-License-Identifier: GPL-2.0-or-later
// Offline gain estimates; the historical engine called this "optimum attenuation".
#include "attenuation.hpp"
#include "bytes.hpp"
#include "convolver/engine.hpp"
#include "offline_engine.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace convolver {
namespace {
// A tap as processing reads it at least: below the smallest normal float,
// zero (PERF-15). (A slightly larger tap in an FFT stage can also be lost to
// the spectrum's normalisation; the figures still count it.) Without this, a filter of subnormal
// taps, which plays silence, had figures of +738 dB (bound) and +753 dB (noise), worked out from
// taps that processing never uses (ROBUST-11).
float played(float tap) noexcept {
    return std::abs(tap) < std::numeric_limits<float>::min() ? 0.f : tap;
}
double attenuation_db(double peak) {
    return peak > 0 ? -20 * std::log10(peak) : 0;
}
// The power of two at or below a positive finite value.
int exponent_of(double v) {
    return std::ilogb(v);
}
// FFT-5: where unit_scaled() puts each level, as powers of two: a path's
// mixed input (from the absolute sum of its input gains), its taps and the
// loudest output (from absolute sums over routes and paths). Each leaves
// room both ways: the mixed input's FFT, at most 2^18 points, and the sum
// over taps and routes stay far below the largest float (2^128), and a route
// whose gain cancels against a far louder one's on the same channel keeps
// its gain above the smallest normal float (2^-126) unless its share is
// below about 2^-150 of that louder route's, where float rounding loses it.
constexpr int input_level = 32, taps_level = 0, output_level = 64;
// FFT-5: `c` with each path's input gains, taps and output gains scaled by
// powers of two to the levels above, and the output exactly 2^-shift times
// `c`'s. Each stage is linear and scaling by a power of two rounds the same,
// so the scaled run gives the same floats times 2^-shift, unless a value is
// subnormal; but no FFT library or plan can overflow inside it, as it could
// with coefficients near the largest float (one tap of 3e38 overflowed in
// one build and not in others).
Configuration unit_scaled(const Configuration& c, int& shift) {
    // Absolute sums, so cancellation between routes or paths cannot hide a
    // large intermediate value.
    const auto absolute_sum = [](const auto& values, auto value) {
        double total = 0;
        for (const auto& v : values)
            total += std::abs(static_cast<double>(value(v)));
        return total;
    };
    std::vector<double> inputs(c.paths.size()), taps(c.paths.size());
    std::vector<double> outputs(c.output_channels);
    for (std::size_t p = 0; p < c.paths.size(); ++p) {
        const auto& path = c.paths[p];
        inputs[p] = absolute_sum(path.inputs, [](const Route& r) { return r.gain; });
        taps[p] = absolute_sum(path.impulse, [](float v) { return played(v); });
        for (const auto& r : path.outputs)
            outputs[r.channel] += inputs[p] * taps[p] * std::abs(r.gain);
    }
    double level = 0;
    for (const double l : outputs)
        level = std::max(level, l);
    Configuration scaled = c;
    shift = level > 0 ? exponent_of(level) - output_level : 0;
    for (std::size_t p = 0; p < c.paths.size(); ++p) {
        if (!(inputs[p] > 0 && taps[p] > 0))
            continue; // Silent: its output is zero at any scale.
        auto& path = scaled.paths[p];
        const int in = exponent_of(inputs[p]) - input_level;
        const int h = exponent_of(taps[p]) - taps_level;
        for (auto& r : path.inputs)
            r.gain = std::ldexp(r.gain, -in);
        for (auto& v : path.impulse)
            v = std::ldexp(played(v), -h);
        // |gain| * inputs * taps <= level < 2^(shift + output_level + 1), so
        // the path's output stays below 2^(output_level + 1) and the gain
        // below 2^(output_level - input_level - taps_level + 1).
        for (auto& r : path.outputs)
            r.gain = std::ldexp(r.gain, in + h - shift);
    }
    return scaled;
}
} // namespace
// Exact peak bound: output o is sum_i (k_oi * x_i) with k_oi the sum over paths of
// out_gain * in_gain * h. Its largest magnitude for |x| <= 1 is sum_i sum_n |k_oi[n]|.
// PERF-27: the noise gain's squares are summed in the same traversal, each
// total in its own order, so neither figure changes.
detail::KernelSums detail::kernel_sums(const Configuration& c) {
    std::size_t longest = 0;
    for (const auto& p : c.paths)
        longest = std::max(longest, p.impulse.size());
    std::vector<double> kernel; // Sized only when two paths share a pair.
    const auto path_gain = [](const FilterPath& p, std::size_t i, std::size_t o) {
        double gain_in = 0, gain_out = 0;
        for (const auto& r : p.inputs)
            if (r.channel == i)
                gain_in += r.gain;
        for (const auto& r : p.outputs)
            if (r.channel == o)
                gain_out += r.gain;
        return gain_in * gain_out;
    };
    KernelSums sums;
    double loudest = 0; // The largest output's sum of squares.
    for (std::size_t o = 0; o < c.output_channels; ++o) {
        double total = 0, energy = 0;
        for (std::size_t i = 0; i < c.input_channels; ++i) {
            // PERF-31: one path joining this input and output, as in nearly
            // every real filter, is summed directly. 0 + gain * tap is the
            // product itself, so both sums are the kernel's, bit for bit
            // unless the compiler fuses multiplies and adds by its own choice
            // (GCC's default with FMA): then the squares' sum may differ in
            // its last bit.
            const FilterPath* only = nullptr;
            double only_gain = 0;
            std::size_t joining = 0;
            for (const auto& p : c.paths)
                if (const double gain = path_gain(p, i, o); gain != 0 && ++joining == 1) {
                    only = &p;
                    only_gain = gain;
                }
            if (joining == 1) {
                for (const float tap : only->impulse) {
                    const double v = only_gain * played(tap);
                    total += std::abs(v);
                    energy += v * v;
                }
                continue;
            }
            if (joining == 0)
                continue;
            if (kernel.empty())
                kernel.resize(longest);
            std::size_t used = 0; // kernel[0, used) holds this input's sum.
            for (const auto& p : c.paths) {
                const double gain = path_gain(p, i, o);
                if (gain == 0)
                    continue;
                // Only the part this output and input use is cleared, so the
                // cost follows the taps, not outputs x inputs x the longest.
                if (p.impulse.size() > used) {
                    std::fill(kernel.begin() + static_cast<std::ptrdiff_t>(used),
                              kernel.begin() + static_cast<std::ptrdiff_t>(p.impulse.size()), 0.0);
                    used = p.impulse.size();
                }
                for (std::size_t n = 0; n < p.impulse.size(); ++n)
                    kernel[n] += gain * played(p.impulse[n]);
            }
            // The whole kernel first: paths that cancel cancel here, before
            // either sum sees them.
            for (std::size_t n = 0; n < used; ++n) {
                total += std::abs(kernel[n]);
                energy += kernel[n] * kernel[n];
            }
        }
        sums.peak = std::max(sums.peak, total);
        loudest = std::max(loudest, energy);
    }
    sums.noise_gain = std::sqrt(loudest);
    return sums;
}
bool detail::plays_silence(const Configuration& c) noexcept {
    for (const auto& p : c.paths)
        for (const float tap : p.impulse)
            if (played(tap) != 0)
                return false;
    return true;
}
double worst_case_peak(const Configuration& c) {
    // IO-21: validated as every other public function taking a
    // Configuration, so a non-finite tap or a route to a channel the filter
    // lacks is refused rather than giving a figure.
    validate_configuration(c);
    return detail::kernel_sums(c).peak;
}
namespace {
void check_lengths(std::size_t lengths) {
    if (lengths == 0 || lengths > 1000)
        throw std::invalid_argument("attenuation noise length must be 1..1000 filter lengths");
}
// The estimate of a filter both entry points below have validated, once
// each: the public one before its worst-case peak, the other for a caller
// that has not (cheap beside the noise run here).
AttenuationEstimate estimate_validated(const Configuration& c, double worst_peak,
                                       std::uint64_t seed, std::size_t lengths) {
    std::size_t longest = 0;
    for (const auto& p : c.paths)
        longest = std::max(longest, p.impulse.size());
    // PERF-23: a latency near the filter length, up to the offline engine's
    // largest block, lets it sum a long filter in large blocks, which keeps
    // the partition count, and so the cost per frame, small; the result does
    // not depend on it beyond float rounding.
    const auto block = std::clamp<std::size_t>(std::bit_ceil(longest), 256, 65536);
    int shift = 0;
    auto engine = detail::OfflineEngine::make(unit_scaled(c, shift), block);
    constexpr std::size_t chunk = 4096;
    std::vector<float> in(chunk * c.input_channels), out(chunk * c.output_channels);
    std::uint64_t state = seed;
    double peak = 0;
    bool finite_output = true;
    const auto scan = [&](std::size_t frames) {
        for (std::size_t i = 0; i < frames * c.output_channels; ++i) {
            finite_output = finite_output && std::isfinite(out[i]);
            peak = std::max(peak, static_cast<double>(std::abs(out[i])));
        }
    };
    // The historical engine used exactly ten filter lengths; a floor keeps short
    // filters from being judged on a handful of samples.
    constexpr std::size_t minimum_frames = 65536;
    for (std::size_t remaining = std::max(longest * lengths, minimum_frames); remaining;) {
        const auto frames = std::min(chunk, remaining);
        for (std::size_t i = 0; i < frames * c.input_channels; ++i)
            // 24 random bits: exactly representable floats in [-1, 1).
            in[i] = static_cast<float>(detail::splitmix64(state) >> 40) * 0x1p-23f - 1.0f;
        engine.process(std::span<const float>(in).first(frames * c.input_channels), out, frames);
        scan(frames);
        remaining -= frames;
    }
    for (auto remaining = engine.latency_frames() + engine.tail_frames(); remaining;) {
        const auto frames = std::min(chunk, remaining);
        engine.process({}, out, frames);
        scan(frames);
        remaining -= frames;
    }
    // std::max ignores NaN, so a non-finite output is refused here rather than
    // reported as a smaller peak (jrp's review of 0.4.43). At unit scale it
    // would be a fault in the convolution, not the filter's level.
    if (!finite_output || !std::isfinite(peak))
        throw std::overflow_error("cannot estimate the attenuation: filtering noise at unit "
                                  "scale gave infinite or undefined output");
    AttenuationEstimate result;
    result.worst_case_peak = worst_peak;
    result.worst_case_attenuation_db = attenuation_db(result.worst_case_peak);
    // Back to the filter's own level, exactly. The peak cannot exceed the
    // bound; float rounding could put it a few units above, so the bound caps
    // it, and a filter whose bound is within the largest float always has an
    // estimate. Only the level of the output decides overflow, in double, the
    // same in every build: whether single-precision processing could carry
    // the noise response, as the CLI's no-clip refuses a bound beyond it. So
    // the estimate has a figure whenever the bound has one.
    peak = std::min(std::ldexp(peak, shift), result.worst_case_peak);
    // Output below the smallest normal float is flushed to zero while
    // Convolver processes (PERF-15), so such a filter plays silence: 0 dB, as
    // an unscaled run gave, rather than a gain beyond the largest float.
    if (peak < std::numeric_limits<float>::min())
        peak = 0;
    if (peak > std::numeric_limits<float>::max())
        throw std::overflow_error(
            "cannot estimate the attenuation: filtering full-scale noise peaks beyond the "
            "largest single-precision value, so the filter coefficients are too large. Check "
            "how the filter files are scaled");
    result.noise_peak = peak;
    result.noise_attenuation_db = attenuation_db(peak);
    return result;
}
} // namespace
AttenuationEstimate estimate_attenuation(const Configuration& c, std::uint64_t seed,
                                         std::size_t lengths) {
    check_lengths(lengths);
    validate_configuration(c);
    return estimate_validated(c, detail::kernel_sums(c).peak, seed, lengths);
}
AttenuationEstimate detail::estimate_attenuation(const Configuration& c, double worst_peak,
                                                 std::uint64_t seed, std::size_t lengths) {
    check_lengths(lengths);
    validate_configuration(c);
    return estimate_validated(c, worst_peak, seed, lengths);
}
} // namespace convolver
