// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include "fft.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace convolver {
namespace {
#if defined(_MSC_VER)
#define CONVOLVER_RESTRICT __restrict
#else
#define CONVOLVER_RESTRICT __restrict__
#endif
// Bound both dimensions and total prepared storage before any large allocation.
constexpr std::size_t max_channels = 256, max_block = 1U << 20, max_samples = 1U << 26;
constexpr std::size_t max_paths = 4096, max_routes = 4096, max_storage = 1U << 28;
std::size_t checked_add(std::size_t a, std::size_t b) {
    if (b > std::numeric_limits<std::size_t>::max() - a)
        throw std::length_error("sample count overflow");
    return a + b;
}
std::size_t checked_mul(std::size_t a, std::size_t b) {
    if (a && b > std::numeric_limits<std::size_t>::max() / a)
        throw std::length_error("sample count overflow");
    return a * b;
}
struct Delay {
    std::vector<float> samples;
    std::size_t position = 0;
    explicit Delay(std::size_t delay) : samples(delay + 1) {}
    float push(float value) noexcept {
        samples[position] = value;
        if (++position == samples.size())
            position = 0;
        return samples[position];
    }
    void reset() noexcept {
        std::fill(samples.begin(), samples.end(), 0.f);
        position = 0;
    }
};
std::size_t delay_at(const std::vector<std::size_t>& delays, std::size_t channel) {
    return delays.empty() ? 0 : delays[channel];
}
// Prepared spectra are stored split per partition: bins real parts, then bins
// imaginary parts. Split storage lets the multiply-accumulate vectorize with plain
// loads on SSE/AVX and NEON instead of shuffling interleaved complex pairs.
void deinterleave(const float* CONVOLVER_RESTRICT from, float* CONVOLVER_RESTRICT to,
                  std::size_t bins, float scale) noexcept {
    for (std::size_t j = 0; j < bins; ++j) {
        to[j] = from[2 * j] * scale;
        to[bins + j] = from[2 * j + 1] * scale;
    }
}
void multiply(float* CONVOLVER_RESTRICT re, float* CONVOLVER_RESTRICT im,
              const float* CONVOLVER_RESTRICT x, const float* CONVOLVER_RESTRICT h,
              std::size_t bins) noexcept {
    const float *xr = x, *xi = x + bins, *hr = h, *hi = h + bins;
    for (std::size_t j = 0; j < bins; ++j) {
        re[j] = xr[j] * hr[j] - xi[j] * hi[j];
        im[j] = xr[j] * hi[j] + xi[j] * hr[j];
    }
}
void multiply_accumulate(float* CONVOLVER_RESTRICT re, float* CONVOLVER_RESTRICT im,
                         const float* CONVOLVER_RESTRICT x, const float* CONVOLVER_RESTRICT h,
                         std::size_t bins) noexcept {
    const float *xr = x, *xi = x + bins, *hr = h, *hi = h + bins;
    for (std::size_t j = 0; j < bins; ++j) {
        re[j] += xr[j] * hr[j] - xi[j] * hi[j];
        im[j] += xr[j] * hi[j] + xi[j] * hr[j];
    }
}
// Shared by validation, tail queries, analysis and Engine construction so the
// accounting formulas exist once. No FFT planning or spectral allocation here.
ConfigurationAnalysis validate(const Configuration& c, std::size_t b) {
    if (!b || b > max_block)
        throw std::invalid_argument("block frames must be between 1 and 1048576");
    if (c.sample_rate <= 0 || c.sample_rate > 768000)
        throw std::invalid_argument("sample rate must be between 1 and 768000");
    if (!c.input_channels || !c.output_channels || c.input_channels > max_channels ||
        c.output_channels > max_channels)
        throw std::invalid_argument("channel count must be between 1 and 256");
    if ((!c.input_delays.empty() && c.input_delays.size() != c.input_channels) ||
        (!c.output_delays.empty() && c.output_delays.size() != c.output_channels))
        throw std::invalid_argument("delay count must match channel count");
    if (c.paths.empty() || c.paths.size() > max_paths)
        throw std::invalid_argument("filter path count must be between 1 and 4096");
    const auto n = detail::fft_size(b), bins = n / 2 + 1;
    std::size_t storage = 0;
    auto account = [&](std::size_t size) {
        storage = checked_add(storage, size);
        if (storage > max_storage)
            throw std::length_error("prepared engine exceeds storage limit");
    };
    account(checked_mul(checked_add(c.input_channels, c.output_channels), b));
    account(detail::fft_storage(n)); // Backend-owned real, spectrum and scratch buffers.
    for (std::size_t channel = 0; channel < c.input_channels + c.output_channels; ++channel) {
        const auto d = channel < c.input_channels
                           ? delay_at(c.input_delays, channel)
                           : delay_at(c.output_delays, channel - c.input_channels);
        if (d > max_samples)
            throw std::length_error("delay exceeds 67108864 frames");
        account(d + 1);
    }
    ConfigurationAnalysis result;
    result.latency_frames = b;
    result.fft_frames = n;
    result.partitions_per_path.reserve(c.paths.size());
    for (const auto& p : c.paths) {
        if (p.impulse.empty() || p.impulse.size() > max_samples)
            throw std::invalid_argument("impulse length must be between 1 and 67108864");
        if (!std::all_of(p.impulse.begin(), p.impulse.end(),
                         [](float v) { return std::isfinite(v); }))
            throw std::invalid_argument("impulse contains nonfinite samples");
        auto routes = [&](const std::vector<Route>& values, std::size_t channels) {
            if (values.empty() || values.size() > max_routes)
                throw std::invalid_argument("route count must be between 1 and 4096");
            for (const auto& r : values)
                if (r.channel >= channels || !std::isfinite(r.gain))
                    throw std::invalid_argument("invalid route channel or gain");
            // Include route storage in the aggregate budget, rounded to floats.
            account(
                checked_mul(values.size(), (sizeof(Route) + sizeof(float) - 1) / sizeof(float)));
        };
        routes(p.inputs, c.input_channels);
        routes(p.outputs, c.output_channels);
        const auto partitions = 1 + (p.impulse.size() - 1) / b;
        // Filter and input history, complex pairs. Paths sharing an input mix share
        // history, so this per-path count is an upper bound.
        result.partitions_per_path.push_back(partitions);
        account(checked_mul(checked_mul(partitions, bins), 4));
        account(b); // overlap.
        bool has_input = false, has_output = false;
        std::size_t input_delay = 0, output_delay = 0;
        for (const auto& r : p.inputs)
            if (r.gain != 0) {
                has_input = true;
                input_delay = std::max(input_delay, delay_at(c.input_delays, r.channel));
            }
        for (const auto& r : p.outputs)
            if (r.gain != 0) {
                has_output = true;
                output_delay = std::max(output_delay, delay_at(c.output_delays, r.channel));
            }
        if (has_input && has_output)
            result.tail_frames =
                std::max(result.tail_frames,
                         checked_add(p.impulse.size() - 1, checked_add(input_delay, output_delay)));
    }
    result.accounted_buffer_bytes = checked_mul(storage, sizeof(float));
    return result;
}
} // namespace
ConfigurationAnalysis analyze_configuration(const Configuration& c, std::size_t block) {
    return validate(c, block);
}
std::size_t configuration_tail_frames(const Configuration& c, std::size_t block) {
    return validate(c, block).tail_frames;
}
void validate_configuration(const Configuration& c, std::size_t block) {
    (void)validate(c, block);
}
// Paths whose ordered input routes are identical (channel and gain bits) produce
// the same mixed block, so they share one forward transform and spectrum history.
bool same_mix(const std::vector<Route>& a, const std::vector<Route>& b) noexcept {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const Route& x, const Route& y) {
        return x.channel == y.channel &&
               std::bit_cast<std::uint32_t>(x.gain) == std::bit_cast<std::uint32_t>(y.gain);
    });
}
struct Engine::Impl {
    struct Mix {
        std::vector<Route> routes;
        std::size_t partitions = 0, head = 0; // History ring covers the longest member path.
        std::vector<float> history;
    };
    struct Path {
        std::vector<Route> outputs;
        std::size_t mix, partitions;
        std::vector<float> filters, overlap;
        Path(const FilterPath& p, std::size_t m, std::size_t b, std::size_t bins)
            : outputs(p.outputs), mix(m), partitions(1 + (p.impulse.size() - 1) / b),
              filters(partitions * bins * 2), overlap(b) {}
    };
    std::size_t inputs, outputs, block, tail, n, bins, position = 0;
    std::vector<float> input_block, output_block;
    std::vector<Delay> input_delays, output_delays;
    std::vector<Mix> mixes;
    std::vector<Path> paths;
    detail::RealFft fft;
    float *time, *frequency, *sum;
    Impl(const Configuration& c, std::size_t b, const ConfigurationAnalysis& v, FftOptions options)
        : inputs(c.input_channels), outputs(c.output_channels), block(b), tail(v.tail_frames),
          n(detail::fft_size(b)), bins(n / 2 + 1), input_block(inputs * b),
          output_block(outputs * b), fft(n, options), time(fft.time()), frequency(fft.frequency()),
          sum(fft.sum()) {
        input_delays.reserve(inputs);
        output_delays.reserve(outputs);
        paths.reserve(c.paths.size());
        for (std::size_t i = 0; i < inputs; ++i)
            input_delays.emplace_back(delay_at(c.input_delays, i));
        for (std::size_t i = 0; i < outputs; ++i)
            output_delays.emplace_back(delay_at(c.output_delays, i));
        const float normalization = 1.0f / static_cast<float>(n);
        for (const auto& source : c.paths) {
            std::size_t m = 0;
            while (m < mixes.size() && !same_mix(mixes[m].routes, source.inputs))
                ++m;
            if (m == mixes.size())
                mixes.push_back({source.inputs, 0, 0, {}});
            paths.emplace_back(source, m, b, bins);
            auto& path = paths.back();
            mixes[m].partitions = std::max(mixes[m].partitions, path.partitions);
            for (std::size_t p = 0; p < path.partitions; ++p) {
                std::fill_n(time, n, 0.f);
                const auto count = std::min(b, source.impulse.size() - p * b);
                std::copy_n(source.impulse.data() + p * b, count, time);
                fft.forward();
                deinterleave(frequency, path.filters.data() + p * 2 * bins, bins, normalization);
            }
        }
        for (auto& mix : mixes)
            mix.history.assign(mix.partitions * bins * 2, 0.f);
    }
    void compute() noexcept {
        const auto stride = 2 * bins;
        std::fill(output_block.begin(), output_block.end(), 0.f);
        // One forward transform per distinct ordered input mix, in route order.
        for (auto& mix : mixes) {
            std::fill_n(time, n, 0.f);
            for (const auto& route : mix.routes)
                for (std::size_t t = 0; t < block; ++t)
                    time[t] += input_block[t * inputs + route.channel] * route.gain;
            fft.forward();
            deinterleave(frequency, mix.history.data() + mix.head * stride, bins, 1.f);
        }
        // Paths keep their configured order, so output accumulation order is unchanged.
        for (auto& path : paths) {
            const auto& mix = mixes[path.mix];
            // Spectra are in history, so frequency is the split accumulator.
            float* re = frequency;
            float* im = frequency + bins;
            multiply(re, im, mix.history.data() + mix.head * stride, path.filters.data(), bins);
            for (std::size_t p = 1; p < path.partitions; ++p) {
                const auto history = (mix.head + mix.partitions - p) % mix.partitions;
                multiply_accumulate(re, im, mix.history.data() + history * stride,
                                    path.filters.data() + p * stride, bins);
            }
            for (std::size_t j = 0; j < bins; ++j) {
                sum[2 * j] = re[j];
                sum[2 * j + 1] = im[j];
            }
            fft.inverse();
            for (std::size_t t = 0; t < block; ++t) {
                const float value = time[t] + path.overlap[t];
                for (const auto& route : path.outputs)
                    output_block[t * outputs + route.channel] += value * route.gain;
                path.overlap[t] = time[block + t];
            }
        }
        for (auto& mix : mixes)
            if (++mix.head == mix.partitions)
                mix.head = 0;
    }
    void reset() noexcept {
        position = 0;
        std::fill(input_block.begin(), input_block.end(), 0.f);
        std::fill(output_block.begin(), output_block.end(), 0.f);
        for (auto& d : input_delays)
            d.reset();
        for (auto& d : output_delays)
            d.reset();
        for (auto& m : mixes) {
            m.head = 0;
            std::fill(m.history.begin(), m.history.end(), 0.f);
        }
        for (auto& p : paths)
            std::fill(p.overlap.begin(), p.overlap.end(), 0.f);
    }
};
void tune_fft(std::size_t b, FftPlanning planning) {
    if (b == 0 || b > (1u << 20))
        throw std::invalid_argument("FFT block size must be between 1 and 1048576");
    if (planning != FftPlanning::measure && planning != FftPlanning::patient)
        throw std::invalid_argument("FFT tuning requires measure or patient planning");
    detail::RealFft fft(detail::fft_size(b), {planning, false});
}
Engine::Engine(const Configuration& c, std::size_t b, FftOptions options)
    : impl_(std::make_unique<Impl>(c, b, validate(c, b), options)) {}
Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;
std::size_t Engine::input_channels() const noexcept {
    return impl_ ? impl_->inputs : 0;
}
std::size_t Engine::output_channels() const noexcept {
    return impl_ ? impl_->outputs : 0;
}
std::size_t Engine::latency_frames() const noexcept {
    return impl_ ? impl_->block : 0;
}
std::size_t Engine::tail_frames() const noexcept {
    return impl_ ? impl_->tail : 0;
}
void Engine::reset() noexcept {
    if (impl_)
        impl_->reset();
}
void Engine::process(std::span<const float> input, std::span<float> output, std::size_t frames) {
    if (!impl_)
        throw std::invalid_argument("cannot process moved-from engine");
    auto& s = *impl_;
    const auto in_samples = checked_mul(frames, s.inputs),
               out_samples = checked_mul(frames, s.outputs);
    if ((!input.empty() && input.size() < in_samples) || output.size() < out_samples)
        throw std::invalid_argument("processing span is too small");
    if (frames == 0)
        return;
    if (!input.empty()) {
        const auto in_bytes = checked_mul(in_samples, sizeof(float)),
                   out_bytes = checked_mul(out_samples, sizeof(float));
        const auto in_start = reinterpret_cast<std::uintptr_t>(input.data()),
                   out_start = reinterpret_cast<std::uintptr_t>(output.data());
        // Subtraction avoids overflowing end addresses and is defined across allocations.
        const bool overlap = in_start <= out_start ? out_start - in_start < in_bytes
                                                   : in_start - out_start < out_bytes;
        if (overlap)
            throw std::invalid_argument("processing input and output overlap");
        if (!std::all_of(input.begin(), input.begin() + in_samples,
                         [](float v) { return std::isfinite(v); }))
            throw std::invalid_argument("input contains nonfinite samples");
    }
    for (std::size_t t = 0; t < frames; ++t) {
        for (std::size_t c = 0; c < s.outputs; ++c)
            output[t * s.outputs + c] =
                s.output_delays[c].push(s.output_block[s.position * s.outputs + c]);
        for (std::size_t c = 0; c < s.inputs; ++c)
            s.input_block[s.position * s.inputs + c] =
                s.input_delays[c].push(input.empty() ? 0 : input[t * s.inputs + c]);
        if (++s.position == s.block) {
            s.compute();
            s.position = 0;
        }
    }
}
} // namespace convolver
