// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include "denormals.hpp"
#include "fft.hpp"
#include "kernels.hpp"
#include "partitions.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace convolver {
namespace {
// Bound both dimensions and total prepared storage before any large allocation.
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
// Shared by validation, tail queries, analysis and Engine construction so the
// accounting formulas exist once. No FFT planning or spectral allocation here.
ConfigurationAnalysis validate(const Configuration& c, std::size_t latency) {
    if (latency > max_block_frames)
        throw std::invalid_argument("latency must be between 0 and " +
                                    std::to_string(max_block_frames) + " frames");
    if (c.sample_rate <= 0 || c.sample_rate > max_sample_rate)
        throw std::invalid_argument("sample rate must be between 1 and " +
                                    std::to_string(max_sample_rate));
    if (!c.input_channels || !c.output_channels || c.input_channels > max_channel_count ||
        c.output_channels > max_channel_count)
        throw std::invalid_argument("channel count must be between 1 and " +
                                    std::to_string(max_channel_count));
    if ((!c.input_delays.empty() && c.input_delays.size() != c.input_channels) ||
        (!c.output_delays.empty() && c.output_delays.size() != c.output_channels))
        throw std::invalid_argument("delay count must match channel count");
    if (c.paths.empty() || c.paths.size() > max_paths)
        throw std::invalid_argument("filter path count must be between 1 and 4096");
    std::size_t longest = 0;
    for (const auto& p : c.paths) {
        if (p.impulse.empty() || p.impulse.size() > max_impulse_frames)
            throw std::invalid_argument("impulse length must be between 1 and " +
                                        std::to_string(max_impulse_frames));
        longest = std::max(longest, p.impulse.size());
    }
    const auto layout = detail::partition(longest, latency);
    std::size_t storage = 0;
    auto account = [&](std::size_t size) {
        storage = checked_add(storage, size);
        if (storage > max_storage)
            throw std::length_error("prepared engine exceeds storage limit");
    };
    const auto first = detail::partition_layout().first;
    // Delayed input for one chunk, the direct sum's chunk, and the output buffers.
    account(checked_mul(c.input_channels, first));
    account(first);
    account(checked_mul(c.output_channels, layout.output_ring));
    ConfigurationAnalysis result;
    for (const auto& stage : layout.stages) {
        // Backend-owned real, spectrum and scratch buffers, and the accumulator.
        account(detail::fft_storage(2 * stage.block));
        account(2 * (stage.block + 1));
        result.fft_frames = std::max(result.fft_frames, 2 * stage.block);
    }
    for (std::size_t channel = 0; channel < c.input_channels + c.output_channels; ++channel) {
        const auto d = channel < c.input_channels
                           ? delay_at(c.input_delays, channel)
                           : delay_at(c.output_delays, channel - c.input_channels);
        if (d > max_engine_delay_frames)
            throw std::length_error("delay exceeds " + std::to_string(max_engine_delay_frames) +
                                    " frames");
        account(checked_add(d, channel < c.input_channels ? 1 : layout.extra + 1));
    }
    result.latency_frames = latency;
    result.partitions_per_path.reserve(c.paths.size());
    for (const auto& p : c.paths) {
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
        // Filter spectra and input history, complex pairs per partition. Paths
        // sharing an input mix share history, the mixed input and the direct
        // sum's history, so this per-path count is an upper bound.
        std::size_t partitions = 0;
        for (std::size_t s = 0; s < layout.stages.size(); ++s) {
            const auto count = layout.partitions(s, p.impulse.size());
            partitions += count;
            account(checked_mul(checked_mul(count, layout.stages[s].block + 1), 4));
        }
        result.partitions_per_path.push_back(partitions);
        const auto head = layout.head_taps(p.impulse.size());
        account(head);
        account(layout.input_ring);
        if (head)
            account(head - 1 + first);
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
detail::StageWorkProbe stage_work_probe = nullptr;
} // namespace

void detail::probe_stage_work(StageWorkProbe p) noexcept {
    stage_work_probe = p;
}

ConfigurationAnalysis analyze_configuration(const Configuration& c, std::size_t latency) {
    return validate(c, latency);
}
std::size_t configuration_tail_frames(const Configuration& c, std::size_t latency) {
    return validate(c, latency).tail_frames;
}
void validate_configuration(const Configuration& c, std::size_t latency) {
    (void)validate(c, latency);
}
// Paths whose ordered input routes are identical (channel and gain bits) produce
// the same mixed signal, so they share its forward transforms and spectrum history.
bool same_mix(const std::vector<Route>& a, const std::vector<Route>& b) noexcept {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const Route& x, const Route& y) {
        return x.channel == y.channel &&
               std::bit_cast<std::uint32_t>(x.gain) == std::bit_cast<std::uint32_t>(y.gain);
    });
}
// PERF-2: non-uniformly partitioned convolution (see partitions.hpp). Output is
// gathered in a ring per output channel: the direct sum adds to the frames
// being produced, and each stage adds its blocks' results ahead of time, so a
// frame leaves the ring complete. The output order of additions differs from
// the uniform engine's, which changes results by float rounding only.
struct Engine::Impl {
    // Per stage, the spectra of one input mix's recent blocks.
    struct MixStage {
        std::size_t partitions = 0; // History ring: the longest member path's count.
        // Spectra written since construction or reset, at most the ring size.
        // The partitions beyond them would meet the silence before the first
        // block, so a path multiplies only the valid ones. The products skipped
        // are zeros, and the output sum discards the sign of a zero, so the
        // output is bit-identical to visiting every partition (PERF-5). That
        // assumes finite filter spectra: with an infinite one (a finite IR
        // whose transform overflows), 0 x inf would have made the first blocks
        // NaN; now the NaN comes when audio reaches that partition.
        std::size_t valid = 0;
        std::vector<float> history;
    };
    struct Mix {
        std::vector<Route> routes;
        std::size_t head = 0;      // Taps of the longest member's direct sum.
        std::vector<float> recent; // The mixed signal, input_ring frames.
        // The last head - 1 frames before the current chunk, then the chunk.
        std::vector<float> head_history;
        std::vector<MixStage> stages;
    };
    struct PathStage {
        std::size_t partitions = 0;
        std::vector<float> filters;
    };
    struct Path {
        std::vector<Route> outputs;
        std::size_t mix;
        std::vector<float> head; // Taps summed directly.
        std::vector<PathStage> stages;
    };
    struct Stage {
        std::size_t block = 0, bins = 0, offset = 0;
        std::unique_ptr<detail::RealFft> fft;
        std::vector<float> re, im;
        // Cost weights in floating-point operations: a real transform of 2n
        // points is about 5 n log2(2n); a partition's complex multiply-add is
        // 8 per bin. They spread a block's work evenly over its slack.
        double transform_cost = 0, partition_cost = 0;
        // The block being computed, and how far the work has gone.
        bool whole = false, active = false;
        std::size_t job = 0, boundaries = 0, boundary = 0;
        std::size_t next_mix = 0, next_path = 0, next_partition = 0;
        double total = 0, done = 0;
    };
    std::size_t inputs, outputs, latency, tail, first, planned, ring_mask, recent_mask;
    std::uint64_t time = 0;          // Frames processed since construction or reset.
    std::vector<float> delayed;      // Delayed input for the current chunk, per channel.
    std::vector<float> output_rings; // outputs x output_ring.
    std::vector<float> direct;       // One path's direct sum for a chunk.
    std::vector<Delay> input_delays, output_delays;
    std::vector<Mix> mixes;
    std::vector<Path> paths;
    std::vector<Stage> stages;
    const detail::Kernels& k = detail::kernels();
    const detail::StageWorkProbe probe = stage_work_probe; // Tests only.
    Impl(const Configuration& c, std::size_t l, const ConfigurationAnalysis& v, FftOptions options)
        : inputs(c.input_channels), outputs(c.output_channels), latency(l), tail(v.tail_frames),
          first(detail::partition_layout().first) {
        std::size_t longest = 0;
        for (const auto& p : c.paths)
            longest = std::max(longest, p.impulse.size());
        const auto layout = detail::partition(longest, l);
        planned = layout.planned;
        ring_mask = layout.output_ring - 1;
        recent_mask = layout.input_ring - 1;
        delayed.assign(inputs * first, 0.f);
        output_rings.assign(outputs * layout.output_ring, 0.f);
        direct.assign(first, 0.f);
        input_delays.reserve(inputs);
        output_delays.reserve(outputs);
        for (std::size_t i = 0; i < inputs; ++i)
            input_delays.emplace_back(delay_at(c.input_delays, i));
        for (std::size_t i = 0; i < outputs; ++i)
            output_delays.emplace_back(delay_at(c.output_delays, i) + layout.extra);
        stages.reserve(layout.stages.size());
        for (const auto& s : layout.stages) {
            // Member by member, so each allocation is owned as soon as it is made.
            auto& stage = stages.emplace_back();
            const auto n = static_cast<double>(s.block);
            stage.block = s.block;
            stage.bins = s.block + 1;
            stage.offset = s.offset;
            stage.whole = s.whole;
            stage.transform_cost = 5.0 * n * std::log2(2.0 * n);
            stage.partition_cost = 8.0 * static_cast<double>(stage.bins);
            stage.re.assign(stage.bins, 0.f);
            stage.im.assign(stage.bins, 0.f);
            stage.fft = std::make_unique<detail::RealFft>(2 * s.block, options);
        }
        paths.reserve(c.paths.size());
        for (const auto& source : c.paths) {
            std::size_t m = 0;
            while (m < mixes.size() && !same_mix(mixes[m].routes, source.inputs))
                ++m;
            if (m == mixes.size()) {
                mixes.push_back({source.inputs, 0, {}, {}, {}});
                mixes.back().stages.resize(stages.size());
            }
            auto& mix = mixes[m];
            Path path{source.outputs, m, {}, {}};
            const auto taps = source.impulse.size();
            const auto head = layout.head_taps(taps);
            path.head.assign(source.impulse.begin(),
                             source.impulse.begin() + static_cast<std::ptrdiff_t>(head));
            mix.head = std::max(mix.head, head);
            path.stages.resize(stages.size());
            for (std::size_t s = 0; s < stages.size(); ++s) {
                auto& stage = stages[s];
                auto& own = path.stages[s];
                own.partitions = layout.partitions(s, taps);
                mix.stages[s].partitions = std::max(mix.stages[s].partitions, own.partitions);
                own.filters.assign(own.partitions * 2 * stage.bins, 0.f);
                float* time_data = stage.fft->time();
                const float normalization = 1.0f / static_cast<float>(2 * stage.block);
                for (std::size_t p = 0; p < own.partitions; ++p) {
                    std::fill_n(time_data, 2 * stage.block, 0.f);
                    // Output time offset + p * block + i carries tap (that - latency).
                    const auto from = stage.offset + p * stage.block - layout.planned;
                    const auto count = std::min(stage.block, taps - from);
                    std::copy_n(source.impulse.data() + from, count, time_data);
                    stage.fft->forward();
                    k.deinterleave(stage.fft->frequency(), own.filters.data() + p * 2 * stage.bins,
                                   stage.bins, normalization);
                }
            }
            paths.push_back(std::move(path));
        }
        for (auto& mix : mixes) {
            mix.recent.assign(layout.input_ring, 0.f);
            if (mix.head)
                mix.head_history.assign(mix.head - 1 + first, 0.f);
            for (std::size_t s = 0; s < stages.size(); ++s)
                mix.stages[s].history.assign(mix.stages[s].partitions * 2 * stages[s].bins, 0.f);
        }
    }

    // Unit work of a stage's block `job`: the forward transform of one mix,
    // one partition's multiply-add for one path, or one path's inverse
    // transform and its addition to the output rings.
    void forward(Stage& stage, std::size_t s, Mix& mix, std::size_t job) noexcept {
        auto& own = mix.stages[s];
        float* data = stage.fft->time();
        const auto n = stage.block;
        const auto from = static_cast<std::size_t>(job * n) & recent_mask;
        const auto first_part = std::min(n, mix.recent.size() - from);
        std::copy_n(mix.recent.data() + from, first_part, data);
        std::copy_n(mix.recent.data(), n - first_part, data + first_part);
        std::fill_n(data + n, n, 0.f);
        stage.fft->forward();
        k.deinterleave(stage.fft->frequency(),
                       own.history.data() + (job % own.partitions) * 2 * stage.bins, stage.bins,
                       1.f);
        if (own.valid < own.partitions)
            ++own.valid;
    }
    std::size_t used(std::size_t s, const Path& path) const noexcept {
        return std::min(path.stages[s].partitions, mixes[path.mix].stages[s].valid);
    }
    void multiply(Stage& stage, std::size_t s, const Path& path, std::size_t job,
                  std::size_t p) noexcept {
        const auto& own = mixes[path.mix].stages[s];
        const auto stride = 2 * stage.bins;
        const auto slot = (job + own.partitions - p % own.partitions) % own.partitions;
        const float* x = own.history.data() + slot * stride;
        const float* h = path.stages[s].filters.data() + p * stride;
        if (p == 0)
            k.multiply(stage.re.data(), stage.im.data(), x, h, stage.bins);
        else
            k.multiply_accumulate(stage.re.data(), stage.im.data(), x, h, stage.bins);
    }
    void inverse(Stage& stage, const Path& path, std::size_t job) noexcept {
        k.interleave(stage.re.data(), stage.im.data(), stage.fft->sum(), stage.bins);
        stage.fft->inverse();
        const float* data = stage.fft->time();
        const std::size_t ring = ring_mask + 1;
        const auto at = static_cast<std::size_t>(job * stage.block + stage.offset);
        for (const auto& route : path.outputs) {
            float* out = output_rings.data() + route.channel * ring;
            std::size_t i = 0;
            while (i < 2 * stage.block) {
                const auto position = (at + i) & ring_mask;
                const auto count = std::min(2 * stage.block - i, ring - position);
                for (std::size_t j = 0; j < count; ++j)
                    out[position + j] += data[i + j] * route.gain;
                i += count;
            }
        }
    }
    // A block computed whole (the first size, or one no larger than the
    // latency without slack to spread it): all of it when it completes.
    void run(Stage& stage, std::size_t s, std::size_t job) noexcept {
        for (auto& mix : mixes)
            if (mix.stages[s].partitions)
                forward(stage, s, mix, job);
        for (const auto& path : paths) {
            const auto count = used(s, path);
            if (!count)
                continue;
            const auto& own = mixes[path.mix].stages[s];
            k.accumulate(stage.re.data(), stage.im.data(), own.history.data(), job % own.partitions,
                         own.partitions, path.stages[s].filters.data(), count, stage.bins);
            inverse(stage, path, job);
        }
    }
    void begin(Stage& stage, std::size_t s, std::size_t job) noexcept {
        stage.active = true;
        stage.job = job;
        stage.boundaries = stage.block / first;
        stage.boundary = 0;
        stage.next_mix = stage.next_path = stage.next_partition = 0;
        stage.done = 0;
        stage.total = 0;
        for (const auto& mix : mixes)
            if (mix.stages[s].partitions)
                stage.total += stage.transform_cost;
        for (const auto& path : paths) {
            const auto& own = mixes[path.mix].stages[s];
            const auto count =
                std::min(path.stages[s].partitions, std::min(own.valid + 1, own.partitions));
            if (count)
                stage.total +=
                    static_cast<double>(count) * stage.partition_cost + stage.transform_cost;
        }
    }
    // Do the units whose cumulative cost fits the share of the block due by
    // this boundary; the last boundary before the next block finishes it.
    void advance(Stage& stage, std::size_t s) noexcept {
        ++stage.boundary;
        const bool last = stage.boundary == stage.boundaries;
        const double target = stage.total * static_cast<double>(stage.boundary) /
                              static_cast<double>(stage.boundaries);
        const auto fits = [&](double cost) { return last || stage.done + cost <= target; };
        while (stage.next_mix < mixes.size()) {
            auto& mix = mixes[stage.next_mix];
            if (mix.stages[s].partitions) {
                if (!fits(stage.transform_cost))
                    return;
                forward(stage, s, mix, stage.job);
                stage.done += stage.transform_cost;
            }
            ++stage.next_mix;
        }
        while (stage.next_path < paths.size()) {
            const auto& path = paths[stage.next_path];
            const auto count = used(s, path);
            if (stage.next_partition < count) {
                if (!fits(stage.partition_cost))
                    return;
                multiply(stage, s, path, stage.job, stage.next_partition++);
                stage.done += stage.partition_cost;
                continue;
            }
            if (count) {
                if (!fits(stage.transform_cost))
                    return;
                inverse(stage, path, stage.job);
                stage.done += stage.transform_cost;
            }
            ++stage.next_path;
            stage.next_partition = 0;
        }
        stage.active = false;
    }
    // Tests only: the cost of the block run() has just computed.
    double whole_cost(const Stage& stage, std::size_t s) const noexcept {
        double cost = 0;
        for (const auto& mix : mixes)
            if (mix.stages[s].partitions)
                cost += stage.transform_cost;
        for (const auto& path : paths)
            if (const auto count = used(s, path))
                cost += static_cast<double>(count) * stage.partition_cost + stage.transform_cost;
        return cost;
    }
    void boundary() noexcept {
        for (std::size_t s = 0; s < stages.size(); ++s) {
            auto& stage = stages[s];
            if (time % stage.block)
                continue;
            const auto job = static_cast<std::size_t>(time / stage.block - 1);
            if (stage.whole) {
                run(stage, s, job);
                if (probe) {
                    const auto cost = whole_cost(stage, s);
                    probe({s, stage.block, job, 1, 1, true, cost, cost,
                           std::max(stage.transform_cost, stage.partition_cost)});
                }
            } else {
                begin(stage, s, job);
            }
        }
        for (std::size_t s = 0; s < stages.size(); ++s) {
            auto& stage = stages[s];
            if (!stage.active)
                continue;
            const auto before = stage.done;
            advance(stage, s);
            if (probe)
                probe({s, stage.block, stage.job, stage.boundary, stage.boundaries, false,
                       stage.done - before, stage.total,
                       std::max(stage.transform_cost, stage.partition_cost)});
        }
    }
    // Frames up to the next boundary of the first block size.
    void chunk(std::span<const float> input, std::span<float> output, std::size_t at,
               std::size_t frames) noexcept {
        for (std::size_t t = 0; t < frames; ++t)
            for (std::size_t c = 0; c < inputs; ++c)
                delayed[c * first + t] =
                    input_delays[c].push(input.empty() ? 0 : input[(at + t) * inputs + c]);
        const auto from = static_cast<std::size_t>(time) & recent_mask;
        for (auto& mix : mixes) {
            float* x = mix.recent.data() + from;
            std::fill_n(x, frames, 0.f);
            for (const auto& route : mix.routes)
                for (std::size_t t = 0; t < frames; ++t)
                    x[t] += delayed[route.channel * first + t] * route.gain;
            if (mix.head)
                std::copy_n(x, frames, mix.head_history.data() + mix.head - 1);
        }
        const std::size_t ring = ring_mask + 1;
        const auto position = static_cast<std::size_t>(time) & ring_mask;
        for (const auto& path : paths) {
            const auto taps = path.head.size();
            if (!taps)
                continue;
            const auto& mix = mixes[path.mix];
            k.direct(direct.data(), mix.head_history.data() + mix.head - 1, path.head.data(), taps,
                     frames);
            // Tap k plays at output time k + planned (planned < first here).
            const auto at = static_cast<std::size_t>(time + planned) & ring_mask;
            const auto before_wrap = std::min(frames, ring - at);
            for (const auto& route : path.outputs) {
                float* out = output_rings.data() + route.channel * ring;
                for (std::size_t t = 0; t < before_wrap; ++t)
                    out[at + t] += direct[t] * route.gain;
                for (std::size_t t = before_wrap; t < frames; ++t)
                    out[at + t - ring] += direct[t] * route.gain;
            }
        }
        for (auto& mix : mixes)
            if (mix.head > 1)
                std::copy(mix.head_history.begin() + static_cast<std::ptrdiff_t>(frames),
                          mix.head_history.begin() +
                              static_cast<std::ptrdiff_t>(frames + mix.head - 1),
                          mix.head_history.begin());
        for (std::size_t c = 0; c < outputs; ++c) {
            float* out = output_rings.data() + c * ring + position;
            for (std::size_t t = 0; t < frames; ++t) {
                output[(at + t) * outputs + c] = output_delays[c].push(out[t]);
                out[t] = 0;
            }
        }
        time += frames;
        if (time % first == 0)
            boundary();
    }
    void reset() noexcept {
        time = 0;
        std::fill(output_rings.begin(), output_rings.end(), 0.f);
        for (auto& d : input_delays)
            d.reset();
        for (auto& d : output_delays)
            d.reset();
        // Invalidating the history instead of clearing it keeps the cost of a
        // reset independent of the filter length.
        for (auto& m : mixes) {
            std::fill(m.recent.begin(), m.recent.end(), 0.f);
            std::fill(m.head_history.begin(), m.head_history.end(), 0.f);
            for (auto& s : m.stages)
                s.valid = 0;
        }
        for (auto& s : stages)
            s.active = false;
    }
};
void tune_fft(std::size_t b, FftPlanning planning) {
    tune_fft(b, FftOptions{planning, false, 0});
}
void tune_fft(std::size_t b, FftOptions options) {
    if (b > max_block_frames)
        throw std::invalid_argument("FFT block size must be between 0 and " +
                                    std::to_string(max_block_frames));
    if (options.planning == FftPlanning::estimate || options.wisdom_only)
        throw std::invalid_argument(
            "FFT tuning requires measure, patient or exhaustive planning, without wisdom-only");
    // The engine's transforms are those of its stages, the same for every
    // latency (PERF-2): plan each size a long filter would use.
    (void)b;
    const auto& layout = detail::partition_layout();
    for (std::size_t n = layout.first;; n = std::min(n * layout.growth, layout.largest)) {
        detail::RealFft fft(2 * n, options);
        if (n == layout.largest)
            break;
    }
}
// PERF-15: preparation flushes subnormals too. A long filter's decaying tail
// has subnormal samples and spectra, and transforming them without flushing
// made a 30 s filter's preparation about three times slower.
Engine::Engine(const Configuration& c, std::size_t b, FftOptions options)
    : impl_([&] {
          const detail::ScopedFlushDenormals flush;
          return std::make_unique<Impl>(c, b, validate(c, b), options);
      }()) {}
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
    return impl_ ? impl_->latency : 0;
}
std::size_t Engine::tail_frames() const noexcept {
    return impl_ ? impl_->tail : 0;
}
void Engine::reset() noexcept {
    if (impl_)
        impl_->reset();
}
void Engine::process(std::span<const float> input, std::span<float> output, std::size_t frames) {
    // PERF-15: subnormals flushed to zero while processing, the caller's mode
    // restored on return or exception.
    const detail::ScopedFlushDenormals flush;
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
    for (std::size_t done = 0; done < frames;) {
        const auto count =
            std::min(frames - done, s.first - static_cast<std::size_t>(s.time % s.first));
        s.chunk(input, output, done, count);
        done += count;
    }
}
} // namespace convolver
