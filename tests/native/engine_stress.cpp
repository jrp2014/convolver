// SPDX-License-Identifier: GPL-2.0-or-later
// PERF-2 stress and soak tests of the non-uniform engine at its production
// partition layout (engine_tests.cpp also forces small layouts, so short
// filters reach every stage; this program uses the real one with real filter
// lengths).
//
// Each filter is sparse apart from a dense start: a tap at and beside every
// block boundary of the partitioning the engine plans for it, the first few
// hundred taps dense, the last tap and some random ones. A direct
// double-precision sum over the nonzero taps is then cheap enough to check
// every output frame of long filters, and a tap placed in the wrong partition,
// stage or time shows at once.
//
//   convolver_engine_stress                     the CTest run (fixed seed):
//                                               spreading, then stress trials
//   convolver_engine_stress --trials 500 --seed 7 --longest 1500000
//   convolver_engine_stress --soak 3600         an hour of audio, one engine
//
// The spreading check (see spreading()) counts each callback's share of the
// large blocks' work through the engine's test probe. Stress trials check,
// against the reference: every output frame (within
// 1e-5 of the output peak), that two host callback patterns give identical
// samples, that a reset part-way through gives the same samples as a fresh
// engine, and that a reset engine is silent. The soak runs one engine for the
// given seconds of audio at 48 kHz with loud, quiet, tiny and silent passages
// and a filter whose dense start decays into subnormal coefficients; it
// checks sampled callbacks against the reference, requires exact silence
// once a silent passage outlasts the filter, and reports callback times
// (reported, not judged: a shared machine's timing is not evidence).
#include "convolver/engine.hpp"
#include "partitions.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace convolver;

namespace {
struct Tap {
    std::size_t index;
    float value;
};
// The nonzero taps of each path, in the order of Configuration::paths.
using Taps = std::vector<std::vector<Tap>>;

Taps nonzero(const Configuration& c) {
    Taps taps;
    for (const auto& p : c.paths) {
        auto& list = taps.emplace_back();
        for (std::size_t k = 0; k < p.impulse.size(); ++k)
            if (p.impulse[k] != 0)
                list.push_back({k, p.impulse[k]});
    }
    return taps;
}

// Every tap index at or beside a block boundary of the partitioning planned
// for `length` taps at `latency`.
std::vector<std::size_t> boundaries(std::size_t length, std::size_t latency) {
    const auto plan = detail::partition(length, latency, detail::partition_layout());
    std::vector<std::size_t> result{plan.head_taps(length)};
    for (const auto& stage : plan.stages)
        for (std::size_t j = 0; j <= stage.partitions; ++j) {
            const auto time = stage.offset + j * stage.block;
            if (time >= plan.planned)
                result.push_back(time - plan.planned);
        }
    return result;
}

// A filter of `length` taps with taps at and beside the block boundaries the
// engine uses: those planned for the longest path, `planned_for` taps, since
// the engine plans every path by the longest.
std::vector<float> filter(std::mt19937& random, std::size_t length, std::size_t planned_for,
                          std::size_t latency, std::size_t dense) {
    std::uniform_real_distribution<float> sample(-.5f, .5f);
    std::vector<float> h(length, 0.f);
    for (std::size_t k = 0; k < std::min(dense, length); ++k)
        h[k] = sample(random);
    for (const auto b : boundaries(planned_for, latency))
        for (const auto k : {b - 1, b, b + 1})
            if (k < length) // b - 1 wraps to a huge value when b is 0.
                h[k] = sample(random);
    for (int i = 0; i < 40; ++i)
        h[random() % length] = sample(random);
    h.back() = sample(random);
    return h;
}

// Output frame t's reference value for output channel o, from interleaved
// input history `x` (frame f at x[f * inputs + channel]; frames before 0 or at
// or after `frames` are silence).
double reference(const Configuration& c, const Taps& taps, std::size_t latency,
                 const std::vector<float>& x, std::size_t frames, std::size_t t, std::size_t o) {
    double sum = 0;
    for (std::size_t p = 0; p < c.paths.size(); ++p) {
        const auto& path = c.paths[p];
        for (const auto& out : path.outputs) {
            if (out.channel != o)
                continue;
            for (const auto& in : path.inputs) {
                const auto shift = latency +
                                   (c.input_delays.empty() ? 0 : c.input_delays[in.channel]) +
                                   (c.output_delays.empty() ? 0 : c.output_delays[o]);
                if (t < shift)
                    continue;
                const auto at = t - shift;
                double route = 0;
                for (const auto& tap : taps[p]) {
                    if (tap.index > at)
                        break;
                    const auto f = at - tap.index;
                    if (f < frames)
                        route +=
                            static_cast<double>(x[f * c.input_channels + in.channel]) * tap.value;
                }
                sum += route * in.gain * out.gain;
            }
        }
    }
    return sum;
}

std::vector<float> run(Engine& engine, const std::vector<float>& input, std::size_t frames,
                       const std::vector<std::size_t>& chunks) {
    const auto ins = engine.input_channels(), outs = engine.output_channels();
    std::vector<float> out(frames * outs);
    const auto in_frames = input.size() / ins;
    std::size_t done = 0, tick = 0;
    while (done < frames) {
        auto n = std::min(chunks[tick++ % chunks.size()], frames - done);
        if (done < in_frames)
            n = std::min(n, in_frames - done);
        const auto in = done < in_frames
                            ? std::span<const float>(input).subspan(done * ins, n * ins)
                            : std::span<const float>{};
        engine.process(in, std::span<float>(out).subspan(done * outs, n * outs), n);
        done += n;
    }
    return out;
}

struct Worst {
    double relative = 0;
    std::string where;
};

std::vector<std::size_t> chunk_pattern(std::mt19937& random) {
    static const std::size_t common[] = {1,   2,    3,    7,    31,   32,   63,
                                         64,  65,   127,  128,  256,  441,  480,
                                         512, 1000, 1024, 2048, 4096, 8192, 16384};
    std::vector<std::size_t> chunks;
    const auto count = random() % 3 == 0 ? 1 : 1 + random() % 8;
    for (std::size_t i = 0; i < count; ++i)
        chunks.push_back(random() % 4 == 0 ? 1 + random() % 5000
                                           : common[random() % std::size(common)]);
    return chunks;
}

std::string describe(const std::vector<std::size_t>& v) {
    std::string s;
    for (auto x : v)
        s += (s.empty() ? "" : ",") + std::to_string(x);
    return s;
}

void stress(std::uint32_t seed, std::size_t trials, std::size_t longest) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> sample(-.6f, .6f);
    static const std::size_t latencies[] = {0,    1,    17,    63,    64,    65,
                                            100,  128,  256,   512,   1024,  2048,
                                            4096, 8191, 16383, 16384, 16385, 20000};
    Worst worst;
    for (std::size_t trial = 0; trial < trials; ++trial) {
        const auto latency = latencies[random() % std::size(latencies)];
        const std::size_t inputs = 1 + random() % 2, outputs = 1 + random() % 3;
        Configuration c{48000, inputs, outputs, 0, {}, {}, {}};
        if (random() % 2) {
            c.input_delays.resize(inputs);
            c.output_delays.resize(outputs);
            for (auto& d : c.input_delays)
                d = random() % 3 ? random() % 50 : random() % 5000;
            for (auto& d : c.output_delays)
                d = random() % 3 ? random() % 50 : random() % 5000;
        }
        const auto paths = 1 + random() % 4;
        std::vector<std::size_t> lengths;
        for (std::size_t p = 0; p < paths; ++p) {
            // Lengths at, just before and just after a stage or block boundary
            // of a plan, as well as random ones from 1 tap up.
            std::size_t length = 1 + random() % longest;
            if (random() % 2) {
                const auto b = boundaries(1 + random() % longest, latency);
                length = std::max<std::size_t>(1, b[random() % b.size()] + random() % 3);
                length = std::min(length + 1 - random() % 2, longest);
                length = std::max<std::size_t>(length, 1);
            } else if (random() % 4 == 0) {
                length = 1 + random() % 300;
            }
            lengths.push_back(length);
        }
        const auto length_of_longest = *std::max_element(lengths.begin(), lengths.end());
        for (const auto length : lengths) {
            FilterPath path;
            path.impulse = filter(random, length, length_of_longest, latency, 64 + random() % 600);
            path.inputs = {{random() % inputs, 1}};
            if (random() % 3 == 0)
                path.inputs.push_back({random() % inputs, sample(random)});
            path.outputs = {{random() % outputs, sample(random)}};
            if (random() % 3 == 0)
                path.outputs.push_back({random() % outputs, sample(random)});
            c.paths.push_back(std::move(path));
        }
        const auto taps = nonzero(c);
        const auto in_frames = 1 + random() % (length_of_longest + 20000);
        std::vector<float> in(in_frames * inputs);
        for (auto& x : in)
            x = sample(random);
        Engine engine(c, latency);
        if (engine.latency_frames() != latency)
            throw std::runtime_error("latency_frames differs from the latency asked for");
        const auto frames = in_frames + engine.tail_frames() + latency + 300;
        const auto first = chunk_pattern(random), second = chunk_pattern(random);
        const auto a = run(engine, in, frames, first);
        const auto context =
            "trial " + std::to_string(trial) + " (seed " + std::to_string(seed) + "): latency " +
            std::to_string(latency) + ", " + std::to_string(paths) + " paths, longest " +
            std::to_string(length_of_longest) + " taps, " + std::to_string(in_frames) +
            " input frames, callbacks " + describe(first);
        // Every frame against the reference.
        std::vector<double> expected(a.size());
        double peak = 0;
        for (std::size_t t = 0; t < frames; ++t)
            for (std::size_t o = 0; o < outputs; ++o) {
                expected[t * outputs + o] = reference(c, taps, latency, in, in_frames, t, o);
                peak = std::max(peak, std::abs(expected[t * outputs + o]));
            }
        double error = 0;
        std::size_t at = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!std::isfinite(a[i]))
                throw std::runtime_error(context + ": non-finite output at sample " +
                                         std::to_string(i));
            if (const auto e = std::abs(a[i] - expected[i]); e > error) {
                error = e;
                at = i;
            }
        }
        const auto relative = peak > 0 ? error / peak : error;
        if (relative > worst.relative)
            worst = {relative, context};
        if (relative > 1e-5)
            throw std::runtime_error(
                context + ": error " + std::to_string(relative) + " of the peak at frame " +
                std::to_string(at / outputs) + ", output " + std::to_string(at % outputs) + " (" +
                std::to_string(a[at]) + " against " + std::to_string(expected[at]) + ")");
        // Another callback pattern after a reset part-way through noise: the
        // same samples as the first run, exactly.
        std::vector<float> noise((1 + random() % (2 * length_of_longest + 100)) * inputs);
        for (auto& x : noise)
            x = sample(random);
        run(engine, noise, noise.size() / inputs, chunk_pattern(random));
        engine.reset();
        const auto b = run(engine, in, frames, second);
        if (a != b)
            throw std::runtime_error(context + ": callbacks " + describe(second) +
                                     " after a reset give different samples");
        // A reset engine is silent.
        engine.reset();
        std::vector<float> silence(std::min<std::size_t>(frames, 200000) * outputs, 1.f);
        engine.process({}, silence, silence.size() / outputs);
        if (!std::all_of(silence.begin(), silence.end(), [](float x) { return x == 0; }))
            throw std::runtime_error(context + ": not silent after a reset");
    }
    std::cout << "engine stress: " << trials << " trials passed (seed " << seed << ", up to "
              << longest << " taps); worst error " << worst.relative << " of the peak, in "
              << worst.where << '\n';
}

// The PERF-2 independent review: spreading a large block's work over the
// following callbacks is what keeps the worst callback short, and nothing
// else would notice if it stopped (the output stays right). Through the
// engine's test probe, each boundary's work per stage, in its cost weights,
// must stay within the block's even share plus one unit of work (the unit
// that may lag behind), and each block's shares must add up to its total.
// Only blocks no larger than the latency (or the first size) may be computed
// whole. Deterministic: no timing.
struct Spread {
    std::size_t first = 0, latency = 0, reports = 0, spread_jobs = 0;
    double worst = 0; // Largest cost / (share + unit) seen.
    std::string failure;
    std::map<std::pair<std::size_t, std::size_t>, double> sums; // (stage, job) -> cost so far.
};
Spread* spread_now = nullptr;

void observe(const detail::StageWork& w) noexcept {
    auto& s = *spread_now;
    if (!s.failure.empty())
        return;
    try {
        ++s.reports;
        const auto where = "stage " + std::to_string(w.stage) + " (" + std::to_string(w.block) +
                           " frames) block " + std::to_string(w.job) + " boundary " +
                           std::to_string(w.boundary) + " of " + std::to_string(w.boundaries);
        if (w.whole) {
            if (w.block > std::max(s.first, s.latency))
                s.failure = where + ": computed whole, though larger than the latency";
            return;
        }
        const auto bound = w.total / static_cast<double>(w.boundaries) + w.unit;
        s.worst = std::max(s.worst, w.cost / bound);
        if (w.cost > bound * (1 + 1e-9))
            s.failure = where + ": did " + std::to_string(w.cost) + " of " +
                        std::to_string(w.total) + ", more than its share plus one unit (" +
                        std::to_string(bound) + ")";
        auto& sum = s.sums[{w.stage, w.job}];
        sum += w.cost;
        if (w.boundary == w.boundaries) {
            if (std::abs(sum - w.total) > 1e-9 * w.total)
                s.failure = where + ": the block's shares add up to " + std::to_string(sum) +
                            ", not its total " + std::to_string(w.total);
            if (w.total > 2 * bound)
                ++s.spread_jobs;
            s.sums.erase({w.stage, w.job});
        }
    } catch (...) {
        s.failure = "out of memory in the spreading check";
    }
}

void spreading(std::uint32_t seed) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> sample(-.5f, .5f);
    struct Case {
        detail::PartitionLayout layout;
        std::size_t latency, length, inputs, outputs;
        bool true_stereo;
    };
    const detail::PartitionLayout production = detail::partition_layout();
    const Case cases[] = {
        {production, 0, 300000, 2, 2, false},  {production, 256, 300000, 2, 2, false},
        {production, 0, 200000, 2, 2, true},   {production, 4096, 200000, 2, 2, false},
        {{16, 512, 2}, 0, 6000, 2, 2, false},  {{16, 512, 2}, 32, 6000, 2, 2, true},
        {{16, 1024, 4}, 0, 9000, 1, 2, false},
    };
    double worst = 0;
    for (const auto& c : cases) {
        detail::force_partition_layout(&c.layout);
        Configuration config{48000, c.inputs, c.outputs, 0, {}, {}, {}};
        for (std::size_t i = 0; i < c.inputs; ++i)
            for (std::size_t o = 0; o < c.outputs; ++o) {
                if (!c.true_stereo && i != o)
                    continue;
                FilterPath path;
                path.impulse.assign(c.length, 0.f);
                for (std::size_t k = 0; k < c.length; k += 97)
                    path.impulse[k] = sample(random);
                path.impulse.back() = sample(random);
                path.inputs = {{i, 1}};
                path.outputs = {{o, 1}};
                config.paths.push_back(std::move(path));
            }
        Spread s;
        s.first = c.layout.first;
        s.latency = c.latency;
        spread_now = &s;
        detail::probe_stage_work(&observe);
        {
            Engine engine(config, c.latency);
            std::vector<float> in((2 * c.length + 4 * c.layout.largest) * c.inputs);
            for (auto& x : in)
                x = sample(random);
            run(engine, in, in.size() / c.inputs, chunk_pattern(random));
        }
        detail::probe_stage_work(nullptr);
        detail::force_partition_layout(nullptr);
        spread_now = nullptr;
        const auto context = "spreading at latency " + std::to_string(c.latency) + ", blocks " +
                             std::to_string(c.layout.first) + " to " +
                             std::to_string(c.layout.largest) + ", " + std::to_string(c.length) +
                             " taps: ";
        if (!s.failure.empty())
            throw std::runtime_error(context + s.failure);
        if (s.spread_jobs < 4)
            throw std::runtime_error(context + "too few blocks large enough to show spreading (" +
                                     std::to_string(s.spread_jobs) + ")");
        worst = std::max(worst, s.worst);
    }
    std::cout << "engine spreading: " << std::size(cases)
              << " cases passed; the largest boundary did " << worst
              << " of its share plus one unit\n";
}

void soak(double seconds, std::uint32_t seed) {
    constexpr std::size_t rate = 48000, length = 480000, dense = 16384;
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> sample(-1.f, 1.f);
    for (const std::size_t latency : {std::size_t(0), std::size_t(256)}) {
        Configuration c{static_cast<int>(rate), 2, 2, 0, {0, 3}, {0, 7}, {}};
        // True stereo: four paths. Each dense start decays from 0.5 to below
        // 1e-40, into subnormal coefficients.
        for (std::size_t p = 0; p < 4; ++p) {
            FilterPath path;
            path.impulse = filter(random, length, length, latency, 0);
            for (std::size_t k = 0; k < dense; ++k)
                path.impulse[k] =
                    .5f * sample(random) * std::exp(-static_cast<float>(k) * 92.f / dense);
            path.inputs = {{p / 2, 1}};
            path.outputs = {{p % 2, .5f}};
            c.paths.push_back(std::move(path));
        }
        const auto taps = nonzero(c);
        Engine engine(c, latency);
        // The input history the reference reads: all of it would be 1.3 GB for
        // an hour, so keep a power-of-two ring longer than the filter, delays
        // and latency together, and read it through a window copy.
        const std::size_t span = length + latency + 16 + 4096;
        std::size_t ring = 1;
        while (ring < span + 8192)
            ring <<= 1;
        std::vector<float> history(ring * 2, 0.f);
        const auto total = static_cast<std::size_t>(seconds * rate);
        std::size_t position = 0, checked = 0, silent_since = 0, exact_silence = 0;
        double residue = 0;
        std::vector<double> times;
        double worst = 0, worst_time = 0;
        std::vector<float> in, out, window;
        // Passages of 4 s cycle: loud noise, quiet noise, tiny noise (products
        // fall into the subnormal range), single clicks, and 16 s of silence,
        // longer than the filter.
        while (position < total) {
            const auto passage = (position / (rate * 4)) % 8;
            const float level = passage == 0   ? 1.f
                                : passage == 1 ? 1e-3f
                                : passage == 2 ? 1e-30f
                                : passage == 3 ? 0.f // Clicks only.
                                               : 0.f;
            const bool silent = passage >= 4;
            const std::size_t n = std::min<std::size_t>(
                random() % 4 == 0 ? 1 + random() % 1024 : 64 << (random() % 4), total - position);
            in.assign(n * 2, 0.f);
            if (!silent)
                for (std::size_t i = 0; i < n; ++i) {
                    in[i * 2] = level * sample(random);
                    in[i * 2 + 1] = level * sample(random);
                    if (passage == 3 && random() % 9000 == 0)
                        in[i * 2 + random() % 2] = sample(random);
                }
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t ch = 0; ch < 2; ++ch)
                    history[((position + i) % ring) * 2 + ch] = in[i * 2 + ch];
            out.assign(n * 2, 0.f);
            const auto start = std::chrono::steady_clock::now();
            engine.process(in, out, n);
            const auto elapsed =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
                    .count();
            times.push_back(elapsed);
            for (const auto x : out)
                if (!std::isfinite(x))
                    throw std::runtime_error("soak: non-finite output");
            if (silent) {
                if (!silent_since)
                    silent_since = position;
                // Past the filter's end, delays and latency, the largest
                // blocks' transforms may still leave rounding residue for up
                // to two of the largest blocks (their last partition is padded
                // with zeros, which rounding does not keep exactly zero); after
                // that nothing may remain in any buffer.
                const auto quiet = silent_since + span;
                const auto exact = quiet + 2 * detail::partition_layout().largest;
                for (std::size_t i = 0; i < n; ++i)
                    for (std::size_t o = 0; o < 2; ++o) {
                        const auto x = out[i * 2 + o];
                        if (position + i >= exact && x != 0)
                            throw std::runtime_error("soak: output not exactly silent " +
                                                     std::to_string(position + i - silent_since) +
                                                     " frames into silence, latency " +
                                                     std::to_string(latency));
                        if (position + i >= quiet)
                            residue = std::max(residue, std::abs(static_cast<double>(x)));
                    }
                if (position >= exact)
                    exact_silence += n;
            } else {
                silent_since = 0;
            }
            // Check about one callback in 40 against the reference, from a
            // linear copy of the ring's last `span` frames.
            if (random() % 40 == 0 && position + n > span) {
                const auto first = position + n - span;
                window.resize(span * 2);
                for (std::size_t f = 0; f < span; ++f)
                    for (std::size_t ch = 0; ch < 2; ++ch)
                        window[f * 2 + ch] = history[((first + f) % ring) * 2 + ch];
                double peak = 0, error = 0;
                for (std::size_t i = 0; i < n; ++i)
                    for (std::size_t o = 0; o < 2; ++o) {
                        // Frame position + i, relative to the window.
                        const auto e =
                            reference(c, taps, latency, window, span, position + i - first, o);
                        peak = std::max(peak, std::abs(e));
                        error = std::max(error, std::abs(out[i * 2 + o] - e));
                    }
                // Relative to this callback's peak, or to -20 dBFS when it is
                // quieter: transform rounding follows the signal in each block
                // (up to 8192 frames), so a full-scale click or passage leaves
                // errors near one float step of its own level in frames whose
                // true value is far smaller. A misplaced tap still shows: even
                // in the -60 dBFS passage its error is about 2e-4.
                const auto relative = error / std::max(peak, 0.1);
                if (relative > worst) {
                    worst = relative;
                    worst_time = static_cast<double>(position) / rate;
                }
                if (relative > 1e-5)
                    throw std::runtime_error("soak: error " + std::to_string(relative) + " at " +
                                             std::to_string(static_cast<double>(position) / rate) +
                                             " s, latency " + std::to_string(latency));
                ++checked;
            }
            position += n;
        }
        std::sort(times.begin(), times.end());
        const auto at = [&](double q) {
            return times[std::min(times.size() - 1, static_cast<std::size_t>(q * times.size()))];
        };
        std::cout << "engine soak, latency " << latency << ": " << seconds
                  << " s of audio, 4 paths of " << length << " taps; " << checked
                  << " callbacks checked, worst error " << worst << " (at " << worst_time << " s); "
                  << exact_silence << " frames of exact silence, largest residue " << residue
                  << " before it; callback us p50 " << at(.5) << ", p99 " << at(.99) << ", p99.9 "
                  << at(.999) << ", max " << times.back() << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    std::uint32_t seed = 0x5713e55;
    std::size_t trials = 12, longest = 40000;
    double soak_seconds = 0;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto value = [&]() -> std::string {
                if (i + 1 >= argc)
                    throw std::invalid_argument(arg + " needs a value");
                return argv[++i];
            };
            if (arg == "--seed")
                seed = static_cast<std::uint32_t>(std::stoul(value()));
            else if (arg == "--trials")
                trials = std::stoul(value());
            else if (arg == "--longest")
                longest = std::stoul(value());
            else if (arg == "--soak")
                soak_seconds = std::stod(value());
            else
                throw std::invalid_argument("unknown option " + arg);
        }
        if (soak_seconds > 0)
            soak(soak_seconds, seed);
        else {
            spreading(seed);
            stress(seed, trials, std::max<std::size_t>(longest, 2));
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
