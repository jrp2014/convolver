// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
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
std::size_t tail(const Configuration& c) {
    std::size_t result = 0;
    for (const auto& p : c.paths)
        for (const auto& i : p.inputs)
            for (const auto& o : p.outputs)
                if (i.gain != 0 && o.gain != 0)
                    result = std::max(
                        result, p.impulse.size() - 1 +
                                    (c.input_delays.empty() ? 0 : c.input_delays[i.channel]) +
                                    (c.output_delays.empty() ? 0 : c.output_delays[o.channel]));
    return result;
}
std::vector<double> oracle(const Configuration& c, const std::vector<float>& input, std::size_t b) {
    const auto count = input.size() / c.input_channels;
    std::vector<double> output((count + tail(c) + b + 2 * b) * c.output_channels);
    // Deliberately direct double-precision route-by-route convolution, independent of FFT
    // partitioning.
    for (const auto& p : c.paths)
        for (const auto& i : p.inputs)
            for (const auto& o : p.outputs)
                for (std::size_t t = 0; t < count; ++t)
                    for (std::size_t k = 0; k < p.impulse.size(); ++k) {
                        const auto when =
                            b + t + k + (c.input_delays.empty() ? 0 : c.input_delays[i.channel]) +
                            (c.output_delays.empty() ? 0 : c.output_delays[o.channel]);
                        output[when * c.output_channels + o.channel] +=
                            static_cast<double>(input[t * c.input_channels + i.channel]) * i.gain *
                            p.impulse[k] * o.gain;
                    }
    return output;
}
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
void close(const std::vector<float>& actual, const std::vector<double>& expected) {
    require(actual.size() == expected.size(), "output size");
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
    close(a, expected);
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
    close(run(two, in, expected_two.size(), {1, 9}), expected_two);
    std::vector<float> remaining(in.begin() + 1, in.end());
    auto rest = run(one, remaining, expected_one.size() - 1, {2, 1});
    first.insert(first.end(), rest.begin(), rest.end());
    close(first, expected_one);
    Engine moved(std::move(one));
    moved.reset();
    close(run(moved, in, expected_one.size(), {7}), expected_one);
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
    close(run(engine, {1}, oracle(mono(), {1}, 3).size(), {1}), oracle(mono(), {1}, 3));
}
} // namespace
int main() {
    try {
        cases();
        shared_input_mixes();
        invalid();
        no_processing_allocation();
        std::cout << "native engine tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
