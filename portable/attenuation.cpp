// SPDX-License-Identifier: GPL-2.0-or-later
// Offline gain estimates; the historical engine called this "optimum attenuation".
#include "convolver/engine.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace convolver {
namespace {
double attenuation_db(double peak) {
    return peak > 0 ? -20 * std::log10(peak) : 0;
}
std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
} // namespace
// Exact peak bound: output o is sum_i (k_oi * x_i) with k_oi the sum over paths of
// out_gain * in_gain * h. Its largest magnitude for |x| <= 1 is sum_i sum_n |k_oi[n]|.
double worst_case_peak(const Configuration& c) {
    std::size_t longest = 0;
    for (const auto& p : c.paths)
        longest = std::max(longest, p.impulse.size());
    std::vector<double> kernel(longest);
    double peak = 0;
    for (std::size_t o = 0; o < c.output_channels; ++o) {
        double total = 0;
        for (std::size_t i = 0; i < c.input_channels; ++i) {
            std::size_t used = 0; // kernel[0, used) holds this input's sum.
            for (const auto& p : c.paths) {
                double gain_in = 0, gain_out = 0;
                for (const auto& r : p.inputs)
                    if (r.channel == i)
                        gain_in += r.gain;
                for (const auto& r : p.outputs)
                    if (r.channel == o)
                        gain_out += r.gain;
                const double gain = gain_in * gain_out;
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
                    kernel[n] += gain * p.impulse[n];
            }
            for (std::size_t n = 0; n < used; ++n)
                total += std::abs(kernel[n]);
        }
        peak = std::max(peak, total);
    }
    return peak;
}
AttenuationEstimate estimate_attenuation(const Configuration& c, std::uint64_t seed,
                                         std::size_t lengths) {
    if (lengths == 0 || lengths > 1000)
        throw std::invalid_argument("attenuation noise length must be 1..1000 filter lengths");
    validate_configuration(c);
    std::size_t longest = 0;
    for (const auto& p : c.paths)
        longest = std::max(longest, p.impulse.size());
    // A block near the filter length keeps the partition count, and so the cost
    // per frame, small for long filters; the result does not depend on it beyond
    // float rounding.
    const auto block = std::clamp<std::size_t>(std::bit_ceil(longest), 256, 65536);
    Engine engine(c, block);
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
            in[i] = static_cast<float>(splitmix64(state) >> 40) * 0x1p-23f - 1.0f;
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
    // std::max ignores NaN, so overflow inside the convolution (finite but enormous
    // coefficients) is refused here rather than reported as a smaller peak. The
    // bound below is computed in double from finite floats and cannot overflow.
    if (!finite_output || !std::isfinite(peak))
        throw std::overflow_error(
            "cannot estimate the attenuation: filtering full-scale noise gave infinite or "
            "undefined output, so the filter coefficients are too large for single-precision "
            "processing. Check how the filter files are scaled");
    AttenuationEstimate result;
    result.noise_peak = peak;
    result.noise_attenuation_db = attenuation_db(peak);
    result.worst_case_peak = worst_case_peak(c);
    result.worst_case_attenuation_db = attenuation_db(result.worst_case_peak);
    return result;
}
} // namespace convolver
