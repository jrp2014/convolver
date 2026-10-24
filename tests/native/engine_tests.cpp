// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include "kernels.hpp"
#include "oracle.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The processing and reset contract forbids C++ heap allocation. FFTW plans
// and scratch storage have already been prepared before this guard is enabled.
static bool forbid_allocation = false;
void* operator new(std::size_t bytes) {
    if (forbid_allocation)
        throw std::bad_alloc();
    if (auto* p = std::malloc(bytes ? bytes : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) {
    return ::operator new(bytes);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}

using namespace convolver;
namespace {
void require(bool b, const char* message) {
    if (!b)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return;
    } catch (const std::length_error&) {
        return;
    }
    throw std::runtime_error("expected invalid_argument/length_error");
}
Configuration mono(std::vector<float> impulse = {1}) {
    return {48000, 1, 1, 0, {0}, {0}, {{std::move(impulse), {{0, 1}}, {{0, 1}}}}};
}
using test::oracle;
using test::tail;
std::vector<float> run(Engine& engine, const std::vector<float>& input, std::size_t output_frames,
                       const std::vector<std::size_t>& chunks) {
    std::vector<float> out(output_frames * engine.output_channels());
    const auto in_frames = input.size() / engine.input_channels();
    std::size_t done = 0, tick = 0;
    while (done < output_frames) {
        auto n = std::min(chunks[tick++ % chunks.size()], output_frames - done);
        if (done < in_frames)
            n = std::min(n, in_frames - done);
        const auto in = done < in_frames
                            ? std::span<const float>(input).subspan(done * engine.input_channels(),
                                                                    n * engine.input_channels())
                            : std::span<const float>{};
        engine.process(in,
                       std::span<float>(out).subspan(done * engine.output_channels(),
                                                     n * engine.output_channels()),
                       n);
        done += n;
    }
    return out;
}
// The worst oracle error is reported relative to the peak of the case's
// expected output, because float rounding scales with the signal: a louder
// case has a larger absolute error at the same accuracy. The report names the
// case, so a new loud test is not mistaken for a loss of accuracy.
struct OracleError {
    double relative = 0, absolute = 0, peak = 0; // relative = absolute / peak.
    const char* group = "";
    const char* kernels = "";
    std::size_t block = 0;
};
const char* oracle_group = "";        // The test function running, set by main.
std::vector<OracleError> group_worst; // One entry per group and kernel set, in run order.
double worst_absolute = 0;            // Largest |actual - oracle| of any case.
void record(const std::vector<float>& actual, const std::vector<double>& expected,
            std::size_t block) {
    OracleError e{0, 0, 0, oracle_group, detail::kernels().name, block};
    for (std::size_t i = 0; i < actual.size(); ++i) {
        e.absolute = std::max(e.absolute, std::abs(actual[i] - expected[i]));
        e.peak = std::max(e.peak, std::abs(expected[i]));
    }
    worst_absolute = std::max(worst_absolute, e.absolute);
    if (e.peak == 0) // Silent cases: any error is caught by the tolerance below.
        return;
    e.relative = e.absolute / e.peak;
    if (group_worst.empty() || std::string_view(group_worst.back().group) != e.group ||
        std::string_view(group_worst.back().kernels) != e.kernels)
        group_worst.push_back(e);
    else if (e.relative > group_worst.back().relative)
        group_worst.back() = e;
}
void report_oracle_errors() {
    const auto line = [](const std::string& label, const OracleError& e) {
        std::cout << "  " << std::left << std::setw(29) << label << std::right
                  << std::setprecision(3) << e.relative << " of peak ("
                  << e.relative / std::numeric_limits<float>::epsilon() << " float epsilons); "
                  << e.absolute << " absolute at peak " << e.peak << ", B = " << e.block << ", "
                  << e.kernels << " kernels\n";
    };
    std::cout << "Worst error against the double oracle, relative to the case's output peak:\n";
    const OracleError* worst = nullptr;
    for (const auto& e : group_worst) {
        line(e.group, e);
        if (!worst || e.relative > worst->relative)
            worst = &e;
    }
    if (worst)
        line("overall (" + std::string(worst->group) + ")", *worst);
    std::cout << "  Largest absolute error of any case: " << worst_absolute
              << " (it grows with the signal level)\n";
}
void close(const std::vector<float>& actual, const std::vector<double>& expected,
           std::size_t block) {
    require(actual.size() == expected.size(), "output size");
    record(actual, expected, block);
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (!std::isfinite(actual[i]) ||
            std::abs(actual[i] - expected[i]) > 2e-5 * (1 + std::abs(expected[i])))
            throw std::runtime_error("oracle mismatch at " + std::to_string(i) + ": " +
                                     std::to_string(actual[i]) + " versus " +
                                     std::to_string(expected[i]));
}
void scenario(const Configuration& c, std::size_t b, const std::vector<float>& in,
              FftOptions options = {}) {
    Engine engine(c, b, options);
    require(engine.latency_frames() == b, "latency");
    require(engine.tail_frames() == tail(c), "tail");
    auto expected = oracle(c, in, b);
    auto a = run(engine, in, expected.size() / c.output_channels, {1, 7, 2, 31, 3});
    close(a, expected, b);
    engine.reset();
    engine.process({}, {}, 0);
    auto whole = run(engine, in, expected.size() / c.output_channels, {expected.size()});
    require(a == whole, "chunk invariance");
    engine.reset();
    std::vector<float> silence((b * 4 + tail(c)) * c.output_channels, 1);
    engine.process({}, silence, silence.size() / c.output_channels);
    require(std::all_of(silence.begin(), silence.end(), [](float x) { return x == 0; }),
            "reset/silence");
}
void cases() {
    for (auto b :
         {std::size_t(1), std::size_t(3), std::size_t(8), std::size_t(17), std::size_t(64)}) {
        scenario(mono(), b, {1, 0, -0.25f, 0.75f, 0, 0});
        scenario(mono({0, 0, 0}), b, {1, 2, -1});
        for (auto length : {b, b + 1, 2 * b, 3 * b + 2}) {
            std::vector<float> h(length);
            h.front() = .5f;
            h.back() = -.25f;
            scenario(mono(h), b, {1, -1, .125f, .75f});
        }
    }
    std::mt19937 random(0x5eed1234);
    std::uniform_real_distribution<float> sample(-.6f, .6f);
    for (std::size_t trial = 0; trial < 30; ++trial) {
        std::size_t b = 1 + random() % 31;
        Configuration c{44100,
                        3,
                        4,
                        0,
                        {random() % 41, random() % 41, random() % 41},
                        {random() % 41, random() % 41, random() % 41, random() % 41},
                        {}};
        for (std::size_t path = 0; path < 5; ++path) {
            FilterPath p;
            p.impulse.resize(1 + random() % (b * 4 + 3));
            for (auto& x : p.impulse)
                x = sample(random);
            p.inputs = {{path % 3, sample(random)}, {(path + 1) % 3, sample(random)}};
            p.outputs = {{path % 4, sample(random)}, {(path + 2) % 4, sample(random)}};
            c.paths.push_back(std::move(p));
        }
        std::vector<float> in((1 + random() % 99) * c.input_channels);
        for (auto& x : in)
            x = sample(random);
        scenario(c, b, in);
        if (trial < 4 && fft_supports_wisdom()) {
            scenario(c, b, in, {FftPlanning::measure, false});
            scenario(c, b, in, {FftPlanning::patient, false});
            scenario(c, b, in, {FftPlanning::patient, true});
            scenario(c, b, in, {FftPlanning::exhaustive, false});
            scenario(c, b, in, {FftPlanning::exhaustive, true});
            scenario(c, b, in, {FftPlanning::patient, false, 0.5});
        }
    }
    // The largest input and output delays are on disconnected channels; they must not inflate tail.
    Configuration sparse{48000, 2, 2, 0, {2, 99}, {88, 3}, {{{1, 0, 0}, {{0, 1}}, {{1, 1}}}}};
    scenario(sparse, 7, {1, 0, 0, 1, .5f, 0});
    auto zero = mono({1, 2});
    zero.paths[0].inputs[0].gain = 0;
    scenario(zero, 3, {1});
    // Independent differently sized instances remain valid when their processing is interleaved.
    Engine one(mono({.5f, -.25f}), 3), two(mono({1, 0, 0, 1}), 8);
    std::vector<float> in = {1, .25f, -.5f};
    auto expected_one = oracle(mono({.5f, -.25f}), in, 3),
         expected_two = oracle(mono({1, 0, 0, 1}), in, 8);
    std::vector<float> first(1);
    one.process(std::span<const float>(in).first(1), first, 1);
    close(run(two, in, expected_two.size(), {1, 9}), expected_two, 8);
    std::vector<float> remaining(in.begin() + 1, in.end());
    auto rest = run(one, remaining, expected_one.size() - 1, {2, 1});
    first.insert(first.end(), rest.begin(), rest.end());
    close(first, expected_one, 3);
    Engine moved(std::move(one));
    moved.reset();
    close(run(moved, in, expected_one.size(), {7}), expected_one, 3);
}
// Paths with identical ordered input routes share one forward transform and history.
// Check sharing against the oracle and against separately prepared single-path engines.
void shared_input_mixes() {
    std::mt19937 random(0x5aa7ed);
    std::uniform_real_distribution<float> sample(-.6f, .6f);
    for (std::size_t trial = 0; trial < 24; ++trial) {
        const std::size_t b = 1 + random() % 19;
        Configuration c{48000, 2, 3, 0, {random() % 9, random() % 9}, {}, {}};
        if (trial % 2)
            c.output_delays = {random() % 9, random() % 9, random() % 9};
        const float g = sample(random);
        // Same channels in a different order, and signed zero gains, are distinct mixes.
        const std::vector<std::vector<Route>> pool{{{0, 1}},           {{1, g}},
                                                   {{0, g}, {1, .5f}}, {{1, .5f}, {0, g}},
                                                   {{0, 0.f}, {1, 1}}, {{0, -0.f}, {1, 1}}};
        const auto count = 2 + random() % 7;
        for (std::size_t path = 0; path < count; ++path) {
            FilterPath p;
            // Members of one mix get different lengths, so their partition counts differ.
            p.impulse.resize(1 + random() % (b * 5 + 4));
            for (auto& x : p.impulse)
                x = sample(random);
            p.inputs = pool[random() % pool.size()];
            p.outputs = {{random() % 3, sample(random)}};
            if (random() % 2)
                p.outputs.push_back({random() % 3, sample(random)});
            c.paths.push_back(std::move(p));
        }
        std::vector<float> in((1 + random() % 120) * c.input_channels);
        for (auto& x : in)
            x = sample(random);
        scenario(c, b, in);
        Engine shared(c, b);
        const auto frames = oracle(c, in, b).size() / c.output_channels;
        const auto together = run(shared, in, frames, {5, 1, 13});
        std::vector<double> separate(together.size());
        for (const auto& path : c.paths) {
            Configuration single = c;
            single.paths = {path};
            Engine alone(single, b);
            const auto part = run(alone, in, frames, {frames});
            for (std::size_t i = 0; i < part.size(); ++i)
                separate[i] += part[i];
        }
        for (std::size_t i = 0; i < together.size(); ++i)
            if (std::abs(together[i] - separate[i]) > 1e-6 * (1 + std::abs(separate[i])))
                throw std::runtime_error("shared mix differs from separate paths at " +
                                         std::to_string(i));
    }
}
// PERF-5: a path multiplies only the input history written since construction
// or reset. Reset invalidates the history without clearing it, so stale spectra
// stay in the ring. Check, for awkward partition sizes and host callback sizes
// unrelated to them, that output during the first blocks, after a reset at
// several points of the ring's cycle, and with members of one input mix of
// different lengths (a ring longer than some paths), matches the oracle and is
// bit-identical to a fresh engine given the same input.
void history_validity() {
    std::mt19937 random(0x417e5);
    std::uniform_real_distribution<float> sample(-.6f, .6f);
    for (const std::size_t b :
         {std::size_t(1), std::size_t(5), std::size_t(64), std::size_t(255), std::size_t(257)}) {
        Configuration c{48000, 2, 3, 0, {0, 3}, {2, 0, 1}, {}};
        // Two paths share input 0's mix with 5 and 2 partitions; one has its own.
        for (const auto [channel, partitions] :
             {std::pair{std::size_t(0), std::size_t(5)}, std::pair{std::size_t(1), std::size_t(3)},
              std::pair{std::size_t(0), std::size_t(2)}}) {
            FilterPath p;
            p.impulse.resize((partitions - 1) * b + 1 + random() % b);
            for (auto& x : p.impulse)
                x = sample(random);
            p.inputs = {{channel, 1}};
            p.outputs = {{random() % 3, sample(random)}, {random() % 3, sample(random)}};
            c.paths.push_back(std::move(p));
        }
        std::vector<float> in((7 * b + 11) * c.input_channels);
        for (auto& x : in)
            x = sample(random);
        const auto expected = oracle(c, in, b);
        const auto frames = expected.size() / c.output_channels;
        Engine fresh(c, b);
        const auto reference = run(fresh, in, frames, {frames});
        close(reference, expected, b);
        // Reset after 1 to 9 blocks: before, at and after the 5-slot ring fills
        // and wraps, and part-way through a block.
        for (const std::size_t before : {b, 2 * b + 1, 5 * b, 6 * b + b / 2, 9 * b}) {
            for (const auto& chunks :
                 {std::vector<std::size_t>{64}, std::vector<std::size_t>{1, 63, 65, 100},
                  std::vector<std::size_t>{1000}}) {
                Engine engine(c, b);
                std::vector<float> noise(before * c.input_channels);
                for (auto& x : noise)
                    x = sample(random);
                run(engine, noise, before, chunks);
                engine.reset();
                const auto after = run(engine, in, frames, chunks);
                require(after == reference, "reset output differs from a fresh engine");
            }
        }
    }
}
void no_processing_allocation() {
    Engine engine(mono(std::vector<float>(71, .01f)), 7);
    std::vector<float> input(100, 1), output(100);
    forbid_allocation = true;
    try {
        engine.process(input, output, 100);
        engine.process({}, output, 100);
        engine.reset();
    } catch (...) {
        forbid_allocation = false;
        throw;
    }
    forbid_allocation = false;
    Configuration c{48000, 2, 2, 0, {}, {}, {}};
    for (const std::size_t length :
         {std::size_t{5}, std::size_t{40}, std::size_t{17}, std::size_t{3}})
        c.paths.push_back(
            {std::vector<float>(length, .01f), {{length % 2, 1}}, {{0, 1}, {1, .5f}}});
    Engine shared(c, 4);
    std::vector<float> stereo(200, 1), stereo_out(200);
    forbid_allocation = true;
    try {
        shared.process(stereo, stereo_out, 100);
        shared.process({}, stereo_out, 100);
        shared.reset();
    } catch (...) {
        forbid_allocation = false;
        throw;
    }
    forbid_allocation = false;
}
void invalid() {
    rejects([] { Engine e(mono(), 0); });
    rejects([] { Engine e(mono(), std::numeric_limits<std::size_t>::max()); });
    for (auto rate : {0, -1, 768001}) {
        auto c = mono();
        c.sample_rate = rate;
        rejects([&] { Engine e(c); });
    }
    auto c = mono();
    c.input_channels = 0;
    rejects([&] { Engine e(c); });
    c = mono();
    c.output_channels = 257;
    rejects([&] { Engine e(c); });
    c = mono();
    c.paths.clear();
    rejects([&] { Engine e(c); });
    c = mono();
    c.paths[0].impulse.clear();
    rejects([&] { Engine e(c); });
    c = mono();
    c.paths[0].inputs.clear();
    rejects([&] { Engine e(c); });
    c = mono();
    c.paths[0].outputs[0].channel = 1;
    rejects([&] { Engine e(c); });
    c = mono();
    c.paths[0].inputs[0].gain = std::numeric_limits<float>::infinity();
    rejects([&] { Engine e(c); });
    c = mono();
    c.paths[0].impulse[0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { Engine e(c); });
    c = mono();
    c.input_delays = {0, 0};
    rejects([&] { Engine e(c); });
    c = mono();
    c.output_delays = {std::numeric_limits<std::size_t>::max()};
    rejects([&] { Engine e(c); });
    c = mono();
    c.input_channels = 256;
    c.output_channels = 256;
    c.input_delays.clear();
    c.output_delays.clear();
    rejects([&] { Engine e(c, 1U << 20); }); // Aggregate resource limit, before allocation.
    Engine engine(mono(), 3);
    std::vector<float> input(20, 1), output(20);
    rejects([&] { engine.process(input, output, 21); });
    rejects([&] { engine.process(std::span<const float>(input).first(1), output, 2); });
    rejects([&] { engine.process(input, input, 1); });
    rejects([&] { engine.process(input, std::span<float>(input).subspan(1), 3); });
    rejects([&] { engine.process({}, output, std::numeric_limits<std::size_t>::max()); });
    engine.process(std::span<const float>(input).first(2), std::span<float>(input).subspan(2, 2),
                   2); // Adjacent used ranges are legal.
    engine.reset();
    input[0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { engine.process(input, output, 1); });
    // Rejections must leave the processing state untouched.
    close(run(engine, {1}, oracle(mono(), {1}, 3).size(), {1}), oracle(mono(), {1}, 3), 3);
}
void attenuation() {
    // Two inputs, two outputs. Output 0 combines a cancelling pair on input 0
    // (kernel {0, 0.5, 0.25}) and a scaled path from input 1; output 1 mirrors.
    Configuration c;
    c.sample_rate = 48000;
    c.input_channels = c.output_channels = 2;
    c.paths.push_back({{1, 0.5f}, {{0, 1}}, {{0, 1}}});
    c.paths.push_back({{-1, 0, 0.25f}, {{0, 1}}, {{0, 1}}});
    c.paths.push_back({{0.25f, -0.5f}, {{1, -2}}, {{0, 1}, {1, 0.5f}}});
    const auto e = estimate_attenuation(c, 3);
    // Output 0: |0|+|0.5|+|0.25| + 2*(0.25+0.5) = 2.25; output 1: 0.5*0.75*2 = 0.75.
    require(std::abs(e.worst_case_peak - 2.25) < 1e-12, "worst-case peak");
    require(std::abs(e.worst_case_attenuation_db + 20 * std::log10(2.25)) < 1e-9,
            "worst-case attenuation");
    // The bound is reached: inputs equal to the sign of the time-reversed kernels.
    const std::vector<double> k0{0, 0.5, 0.25}, k1{-0.5, 1, 0};
    std::vector<float> in;
    for (std::size_t t = 0; t < 3; ++t)
        for (const auto* k : {&k0, &k1}) {
            const double v = (*k)[2 - t];
            in.push_back(v > 0 ? 1.f : v < 0 ? -1.f : 0.f);
        }
    const auto out = oracle(c, in, 1);
    double reached = 0;
    for (std::size_t i = 0; i < out.size(); i += 2)
        reached = std::max(reached, std::abs(out[i]));
    require(std::abs(reached - e.worst_case_peak) < 1e-12, "worst-case peak is attainable");
    // The historical noise estimate is a typical peak: below the bound, and
    // seeded, so repeatable.
    require(e.noise_peak > 0.5 && e.noise_peak <= e.worst_case_peak, "noise peak range");
    require(std::abs(e.noise_attenuation_db + 20 * std::log10(e.noise_peak)) < 1e-9,
            "noise attenuation");
    const auto again = estimate_attenuation(c, 3);
    require(again.noise_peak == e.noise_peak, "noise estimate is reproducible");
    require(estimate_attenuation(c, 4).noise_peak != e.noise_peak, "noise estimate uses seed");
    // A unit impulse passes noise unchanged: the estimate is just under 0 dB.
    Configuration dirac;
    dirac.sample_rate = 48000;
    dirac.input_channels = dirac.output_channels = 1;
    dirac.paths.push_back({std::vector<float>(1000, 0.f), {{0, 1}}, {{0, 1}}});
    dirac.paths[0].impulse[0] = 1;
    const auto d = estimate_attenuation(dirac);
    require(d.worst_case_peak == 1 && d.noise_peak > 0.999 && d.noise_peak <= 1,
            "unit impulse estimate");
    // Silent routing: no peak, no gain change.
    auto silent = dirac;
    silent.paths[0].outputs[0].gain = 0;
    const auto z = estimate_attenuation(silent);
    require(z.noise_peak == 0 && z.worst_case_peak == 0 && z.noise_attenuation_db == 0 &&
                z.worst_case_attenuation_db == 0,
            "silent estimate");
    rejects([&] { estimate_attenuation(dirac, 0, 0); });
    rejects([&] { estimate_attenuation(Configuration{}); });
}
// PERF-4: the AVX2 set uses fused multiply-add (owner's decision, 24 October
// 2026), so it may round differently from the baseline set, but each set must
// stay within the standard floating-point error bound of the exact result:
// |computed - exact| <= (terms + 2) * eps * (sum of |terms|), checked in double
// over odd lengths (vector remainders) and values with a wide range of exponents.
// The layout functions do no arithmetic beyond one scaling, so they must still
// agree bit for bit (signed zeros and NaN payloads included).
bool same(const std::vector<float>& a, const std::vector<float>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](float p, float q) {
        return std::bit_cast<std::uint32_t>(p) == std::bit_cast<std::uint32_t>(q);
    });
}
// Exact (double) real and imaginary sums and the sums of absolute terms, per bin.
struct Exact {
    std::vector<double> re, im, re_size, im_size;
    std::size_t terms = 0;
    explicit Exact(std::size_t bins) : re(bins), im(bins), re_size(bins), im_size(bins) {}
    void add(const float* x, const float* h, std::size_t bins) {
        for (std::size_t j = 0; j < bins; ++j) {
            const double xr = x[j], xi = x[bins + j], hr = h[j], hi = h[bins + j];
            re[j] += xr * hr - xi * hi;
            im[j] += xr * hi + xi * hr;
            re_size[j] += std::abs(xr * hr) + std::abs(xi * hi);
            im_size[j] += std::abs(xr * hi) + std::abs(xi * hr);
        }
        terms += 2;
    }
};
void within_bound(const std::vector<float>& actual, const Exact& e, const char* message) {
    const auto bins = e.re.size();
    const double eps = std::numeric_limits<float>::epsilon() / 2, k = double(e.terms + 2) * eps;
    for (std::size_t j = 0; j < bins; ++j) {
        // The absolute part covers products that are subnormal in float.
        const double slack = double(e.terms) * std::numeric_limits<float>::denorm_min();
        require(std::abs(actual[j] - e.re[j]) <= k * e.re_size[j] + slack &&
                    std::abs(actual[bins + j] - e.im[j]) <= k * e.im_size[j] + slack,
                message);
    }
}
void kernels_agree() {
    const auto& fast = detail::kernels();
    const auto& base = detail::baseline_kernels();
    std::mt19937 random(7);
    std::uniform_real_distribution<float> value(-1, 1);
    std::uniform_int_distribution<int> exponent(-40, 40);
    std::size_t fused_differs = 0;
    for (std::size_t bins : {std::size_t(1), std::size_t(3), std::size_t(7), std::size_t(8),
                             std::size_t(9), std::size_t(17), std::size_t(33), std::size_t(257),
                             std::size_t(513), std::size_t(1025)}) {
        std::vector<float> x(2 * bins), h(2 * bins), interleaved(2 * bins);
        for (auto* v : {&x, &h, &interleaved})
            for (auto& f : *v)
                f = std::ldexp(value(random), exponent(random));
        std::vector<float> a(2 * bins), b(2 * bins);
        fast.deinterleave(interleaved.data(), a.data(), bins, 0.37f);
        base.deinterleave(interleaved.data(), b.data(), bins, 0.37f);
        require(same(a, b), "deinterleave kernels differ");
        fast.interleave(x.data(), x.data() + bins, a.data(), bins);
        base.interleave(x.data(), x.data() + bins, b.data(), bins);
        require(same(a, b), "interleave kernels differ");
        Exact exact(bins);
        exact.add(x.data(), h.data(), bins);
        fast.multiply(a.data(), a.data() + bins, x.data(), h.data(), bins);
        base.multiply(b.data(), b.data() + bins, x.data(), h.data(), bins);
        within_bound(a, exact, "multiply kernel (selected set) outside the error bound");
        within_bound(b, exact, "multiply kernel (baseline) outside the error bound");
        for (int repeat = 0; repeat < 8; ++repeat) {
            fast.multiply_accumulate(a.data(), a.data() + bins, x.data(), h.data(), bins);
            base.multiply_accumulate(b.data(), b.data() + bins, x.data(), h.data(), bins);
            exact.add(x.data(), h.data(), bins);
        }
        within_bound(a, exact, "multiply-accumulate (selected set) outside the error bound");
        within_bound(b, exact, "multiply-accumulate (baseline) outside the error bound");
        fused_differs += same(a, b) ? 0 : 1;
        // A ring of five spectra, starting at each slot, summed over three partitions.
        std::vector<float> ring(5 * 2 * bins), filters(3 * 2 * bins);
        for (auto* v : {&ring, &filters})
            for (auto& f : *v)
                f = std::ldexp(value(random), exponent(random));
        for (std::size_t head = 0; head < 5; ++head) {
            fast.accumulate(a.data(), a.data() + bins, ring.data(), head, 5, filters.data(), 3,
                            bins);
            base.accumulate(b.data(), b.data() + bins, ring.data(), head, 5, filters.data(), 3,
                            bins);
            Exact sum(bins);
            for (std::size_t p = 0; p < 3; ++p)
                sum.add(ring.data() + ((head + 5 - p) % 5) * 2 * bins,
                        filters.data() + p * 2 * bins, bins);
            within_bound(a, sum, "partition accumulation (selected set) outside the error bound");
            within_bound(b, sum, "partition accumulation (baseline) outside the error bound");
        }
    }
    // The bound must not be vacuous: with FMA some results do differ.
    if (std::string(fast.name) != base.name)
        require(fused_differs > 0, "FMA kernels gave the baseline's results bit for bit");
    // A whole engine with long, shared and cross-routed paths renders within
    // float rounding of the baseline set (native.kernel_divergence measures
    // room-correction workloads; the oracle cases above check both sets).
    Configuration c{48000, 2, 3, 0, {0, 5}, {0, 0, 3}, {}};
    std::mt19937 fill(11);
    for (std::size_t p = 0; p < 4; ++p) {
        FilterPath path;
        path.impulse.resize(3000 + 517 * p);
        for (auto& f : path.impulse)
            f = value(fill) * 0.1f;
        path.inputs = {{p % 2, 1}, {(p + 1) % 2, 0.25f}};
        path.outputs = {{p % 3, 0.5f + 0.25f * float(p)}};
        c.paths.push_back(std::move(path));
    }
    std::vector<float> in(2 * 9000);
    for (auto& f : in)
        f = value(fill);
    std::vector<float> outputs[2];
    for (int pass = 0; pass < 2; ++pass) {
        detail::force_baseline_kernels(pass == 1);
        Engine engine(c, 128);
        outputs[pass] = run(engine, in, 9000 + 128 + tail(c), {128, 77, 300});
    }
    detail::force_baseline_kernels(false);
    float peak = 0, difference = 0;
    for (std::size_t i = 0; i < outputs[0].size(); ++i) {
        peak = std::max(peak, std::abs(outputs[1][i]));
        difference = std::max(difference, std::abs(outputs[0][i] - outputs[1][i]));
    }
    require(difference <= 1e-6f * peak, "engine output differs between kernel sets by more "
                                        "than float rounding");
}
} // namespace
int main() {
    try {
        // Oracle checks with the kernels this CPU selects, then with the baseline set.
        const auto* selected = detail::kernels().name;
#if defined(CONVOLVER_EXPERIMENT_VDSP_MAC)
        require(std::string(selected) == "vDSP", "vDSP experiment did not select vDSP kernels");
#endif
        for (bool baseline : {false, true}) {
            detail::force_baseline_kernels(baseline);
            oracle_group = "cases";
            cases();
            oracle_group = "shared_input_mixes";
            shared_input_mixes();
            oracle_group = "history_validity";
            history_validity();
        }
        detail::force_baseline_kernels(false);
        kernels_agree();
        attenuation();
        oracle_group = "invalid";
        invalid();
        no_processing_allocation();
        std::cout << "native engine tests passed (" << fft_backend_name() << ", " << selected
                  << " kernels)\n";
        report_oracle_errors();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
