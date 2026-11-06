// SPDX-License-Identifier: GPL-2.0-or-later
// PERF-15: subnormal numbers are flushed to zero while Engine::process and
// HostProcessor::process run, and the caller's floating-point mode comes back
// unchanged on every way out: normal return, early return and exception,
// nested scopes, and callers (hosts) that already flush. Flushing is checked
// by its effect on arithmetic, not only by reading the control register:
//   - subnormal input: a signal entirely below the smallest normal float is
//     read as zero, so the output is exactly silent;
//   - a decaying tail: a filter that fades through the subnormal range (as a
//     long room-correction filter or a reverb does) still matches the direct
//     double-precision oracle, and no output sample is subnormal.
// On a processor without a flush control in denormals.hpp the restoration
// checks still run and the arithmetic checks expect IEEE gradual underflow.
#include "convolver/engine.hpp"
#include "convolver/host.hpp"
#include "denormals.hpp"
#include "oracle.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace convolver;
using detail::denormals_flushed;
using detail::flush_denormals_supported;
using detail::ScopedFlushDenormals;
using detail::set_denormals_flushed;
using test::oracle;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
constexpr float smallest_normal = std::numeric_limits<float>::min();

// Arithmetic on the current thread: does a result below the normal range
// survive (gradual underflow) or become zero (flush to zero), and is a
// subnormal operand read as zero (denormals are zero)? volatile keeps the
// compiler from folding these at build time.
bool results_flushed() {
    volatile float x = smallest_normal;
    volatile float y = x * 0.25f;
    return y == 0;
}
bool inputs_flushed() {
    volatile float x = std::numeric_limits<float>::denorm_min() * 64;
    volatile float y = x * 1.0f;
    return y == 0;
}
bool flushing() {
    return results_flushed() && inputs_flushed();
}
bool gradual() {
    return !results_flushed() && !inputs_flushed();
}

struct Unwound {};
bool early_return(bool leave) {
    const ScopedFlushDenormals flush;
    if (leave)
        return flushing();
    return false;
}
void scope_throws() {
    const ScopedFlushDenormals flush;
    require(!flush_denormals_supported || flushing(), "no flushing inside a throwing scope");
    throw Unwound{};
}

// The helper on its own, from a caller that does not flush and from one that does.
void helper() {
    set_denormals_flushed(false);
    require(gradual(), "the test thread should start with gradual underflow");
    require(!denormals_flushed(), "flushing reported while off");
    {
        const ScopedFlushDenormals flush;
        require(denormals_flushed() == flush_denormals_supported, "flush bits not set");
        require(flushing() == flush_denormals_supported, "arithmetic not flushed in the scope");
        {
            const ScopedFlushDenormals inner;
            require(flushing() == flush_denormals_supported, "nested scope changed the mode");
        }
        require(flushing() == flush_denormals_supported, "nested scope cleared flushing early");
    }
    require(gradual() && !denormals_flushed(), "normal return: mode not restored");

    require(early_return(true) == flush_denormals_supported, "no flushing before early return");
    require(gradual() && !denormals_flushed(), "early return: mode not restored");
    require(!early_return(false) && gradual(), "fallthrough: mode not restored");

    bool caught = false;
    try {
        scope_throws();
    } catch (const Unwound&) {
        caught = true;
    }
    require(caught && gradual() && !denormals_flushed(), "exception: mode not restored");

    // A host that already flushes keeps flushing after the scope.
    set_denormals_flushed(true);
    require(denormals_flushed() == flush_denormals_supported, "could not set flushing");
    {
        const ScopedFlushDenormals flush;
        require(denormals_flushed() == flush_denormals_supported, "flushing lost in scope");
    }
    require(denormals_flushed() == flush_denormals_supported &&
                flushing() == flush_denormals_supported,
            "a flushing caller's mode was cleared");
    set_denormals_flushed(false);

    // A host with only part of the flush control set (x86-64: FTZ without
    // DAZ, or DAZ without FTZ) gets exactly that back.
    if constexpr (std::popcount(detail::flush_denormal_bits) > 1) {
        const auto clear = detail::floating_point_mode() & ~detail::flush_denormal_bits;
        for (auto part = detail::flush_denormal_bits; part; part &= part - 1) {
            const auto lowest = part & (~part + 1);
            detail::set_floating_point_mode(clear | lowest);
            {
                const ScopedFlushDenormals flush;
                require(denormals_flushed(), "partial caller: not flushing in the scope");
            }
            require(detail::floating_point_mode() == (clear | lowest),
                    "partial caller: mode not restored exactly");
        }
        detail::set_floating_point_mode(clear);
    }

    // The rest of the mode (here the rounding direction) is the caller's.
    const auto before = detail::floating_point_mode();
    {
        const ScopedFlushDenormals flush;
    }
    require(detail::floating_point_mode() == before, "scope changed other mode bits");
    require(std::fesetround(FE_TOWARDZERO) == 0, "could not set rounding");
    {
        const ScopedFlushDenormals flush;
        require(std::fegetround() == FE_TOWARDZERO, "scope changed the rounding direction");
    }
    require(std::fegetround() == FE_TOWARDZERO, "rounding direction not restored");
    std::fesetround(FE_TONEAREST);
    require(gradual(), "mode not back to gradual underflow");
}

Configuration mono(std::vector<float> impulse) {
    Configuration c{48000, 1, 1, 0, {}, {}, {}};
    FilterPath p;
    p.impulse = std::move(impulse);
    p.inputs = {{0, 1}};
    p.outputs = {{0, 1}};
    c.paths.push_back(std::move(p));
    return c;
}
// A filter fading from 0.5 to about 1e-60, through the whole subnormal range
// (float taps below about 1.4e-45 are zero).
std::vector<float> decaying(std::size_t taps) {
    std::mt19937 random(15);
    std::normal_distribution<double> noise(0, 1);
    std::vector<float> h(taps);
    const double rate = std::log(1e60) / double(taps);
    for (std::size_t k = 0; k < taps; ++k)
        h[k] = static_cast<float>(0.5 * noise(random) * std::exp(-rate * double(k)));
    return h;
}

// Engine construction and Engine::process from both kinds of caller:
// restored after a normal return and after their exceptions (an invalid
// configuration; nonfinite input).
void engine_restores() {
    for (const bool caller : {false, true}) {
        set_denormals_flushed(caller);
        const bool expected = caller && flush_denormals_supported;
        Engine engine(mono(decaying(512)), 16);
        require(denormals_flushed() == expected, "Engine construction changed the mode");
        bool refused = false;
        try {
            Engine invalid(mono({}), 16);
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        require(refused, "an empty filter was accepted");
        require(denormals_flushed() == expected,
                "Engine construction exception: mode not restored");
        std::vector<float> in(32, .1f), out(32);
        engine.process(in, out, 32);
        require(denormals_flushed() == expected, "Engine::process changed the caller's mode");
        auto bad = in;
        bad[3] = std::numeric_limits<float>::quiet_NaN();
        bool threw = false;
        try {
            engine.process(bad, out, 32);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw, "nonfinite input accepted");
        require(denormals_flushed() == expected, "Engine::process exception: mode not restored");
    }
    set_denormals_flushed(false);
}

// HostProcessor::process (float and double), returning normally and refusing
// a call early (more frames than its maximum), and preparing a replacement.
template <class T> void host_restores() {
    HostProcessor host(mono({1.f, .5f}), 64, 32);
    std::vector<T> in(64, T(.1)), out(64);
    const T* ins[] = {in.data()};
    T* outs[] = {out.data()};
    for (const bool caller : {false, true}) {
        set_denormals_flushed(caller);
        const bool expected = caller && flush_denormals_supported;
        require(host.process(ins, outs, 64), "HostProcessor refused a valid call");
        require(denormals_flushed() == expected, "HostProcessor::process changed the mode");
        require(!host.process(ins, outs, 65), "HostProcessor accepted too many frames");
        require(denormals_flushed() == expected, "HostProcessor early return: mode not restored");
        auto replacement = host.prepare(mono(decaying(256)));
        require(denormals_flushed() == expected, "HostProcessor::prepare changed the mode");
        host.publish(std::move(replacement));
        require(host.process(ins, outs, 64), "HostProcessor refused a call after publish");
        host.collect();
        require(denormals_flushed() == expected, "replacement changed the mode");
    }
    set_denormals_flushed(false);
}

// A signal entirely below the normal range.
std::vector<float> subnormal_signal(std::size_t frames) {
    std::mt19937 random(3);
    std::uniform_real_distribution<float> fraction(-1, 1);
    std::vector<float> x(frames);
    for (auto& v : x)
        v = fraction(random) * smallest_normal * .99f;
    return x;
}
std::size_t count_subnormal(const std::vector<float>& y) {
    return static_cast<std::size_t>(std::count_if(
        y.begin(), y.end(), [](float v) { return std::fpclassify(v) == FP_SUBNORMAL; }));
}

void subnormal_input() {
    const auto c = mono({1.f, -.75f, .5f, .25f, .125f});
    constexpr std::size_t block = 16, frames = 256;
    const auto in = subnormal_signal(frames);
    const auto expected = oracle(c, in, block);
    const auto total = expected.size();
    // Engine.
    Engine engine(c, block);
    std::vector<float> y(total);
    std::vector<float> padded(in);
    padded.resize(total, 0.f);
    engine.process(padded, y, total);
    // HostProcessor, with gain and a dry mix so its own arithmetic sees the
    // input too.
    HostProcessor host(c, total, block);
    host.set_gain_db(-6);
    host.set_mix(.5);
    std::vector<float> hy(total);
    const float* ins[] = {padded.data()};
    float* outs[] = {hy.data()};
    require(host.process(ins, outs, total), "HostProcessor refused subnormal input");
    for (std::size_t t = 0; t < total; ++t) {
        require(std::isfinite(y[t]) && std::isfinite(hy[t]), "nonfinite output");
        // Either way the result is within the oracle's tolerance.
        require(std::abs(y[t] - expected[t]) <= 2e-5 * (1 + std::abs(expected[t])),
                "subnormal input outside the oracle tolerance");
        if (flush_denormals_supported)
            require(y[t] == 0 && hy[t] == 0,
                    "subnormal input not read as zero (frame " + std::to_string(t) + ")");
    }
    // HostProcessor's double path, with input that is a normal double but
    // below FLT_MIN: flushing makes its conversion to float zero, so the dry
    // share is silent too; without flushing it would pass through.
    HostProcessor host_double(c, total, block);
    host_double.set_mix(.5);
    std::vector<double> din(total), dy(total);
    for (std::size_t t = 0; t < frames; ++t)
        din[t] = (t % 2 ? -1e-39 : 1e-39) * double(1 + t % 7);
    const double* dins[] = {din.data()};
    double* douts[] = {dy.data()};
    require(host_double.process(dins, douts, total), "HostProcessor refused (double)");
    for (std::size_t t = 0; t < total; ++t) {
        require(std::isfinite(dy[t]) && std::abs(dy[t]) < 1e-37, "double output out of range");
        if (flush_denormals_supported)
            require(dy[t] == 0, "double input below FLT_MIN not read as zero (frame " +
                                    std::to_string(t) + ")");
    }
    std::cout << "subnormal input: "
              << (flush_denormals_supported ? "silent output, as flushed"
                                            : "gradual underflow (no flush control here)")
              << '\n';
}

void decaying_tail() {
    constexpr std::size_t taps = 4096, block = 64, burst = 256;
    const auto c = mono(decaying(taps));
    std::mt19937 random(9);
    std::uniform_real_distribution<float> sample(-.5f, .5f);
    std::vector<float> in(burst);
    for (auto& v : in)
        v = sample(random);
    const auto expected = oracle(c, in, block);
    const auto total = expected.size();
    std::vector<float> padded(in);
    padded.resize(total, 0.f);

    Engine engine(c, block);
    std::vector<float> y(total);
    for (std::size_t done = 0; done < total; done += 61) { // Chunks unlike the block.
        const auto n = std::min<std::size_t>(61, total - done);
        engine.process(std::span<const float>(padded).subspan(done, n),
                       std::span<float>(y).subspan(done, n), n);
    }
    double peak = 0, worst = 0;
    std::size_t deep = 0; // Oracle samples in the subnormal range: the case is not vacuous.
    for (std::size_t t = 0; t < total; ++t) {
        require(std::isfinite(y[t]), "nonfinite output");
        const double e = std::abs(y[t] - expected[t]);
        require(e <= 2e-5 * (1 + std::abs(expected[t])), "decaying tail outside the oracle "
                                                         "tolerance at frame " +
                                                             std::to_string(t));
        peak = std::max(peak, std::abs(expected[t]));
        worst = std::max(worst, e);
        deep += expected[t] != 0 && std::abs(expected[t]) < smallest_normal ? 1 : 0;
    }
    require(deep > 0, "the decaying tail never reaches the subnormal range");
    const auto engine_subnormal = count_subnormal(y);

    // The same through HostProcessor, float and double, with gain and mix.
    HostProcessor host(c, block, block);
    host.set_gain_db(-3);
    host.set_mix(.75);
    std::vector<float> hy(total);
    std::vector<double> dy(total), din(padded.begin(), padded.end());
    HostProcessor host_double(c, block, block);
    host_double.set_gain_db(-3);
    host_double.set_mix(.75);
    for (std::size_t done = 0; done < total; done += block) {
        const auto n = std::min(block, total - done);
        const float* ins[] = {padded.data() + done};
        float* outs[] = {hy.data() + done};
        require(host.process(ins, outs, n), "HostProcessor refused the decaying tail");
        const double* dins[] = {din.data() + done};
        double* douts[] = {dy.data() + done};
        require(host_double.process(dins, douts, n), "HostProcessor refused (double)");
    }
    const auto host_subnormal = count_subnormal(hy);
    std::size_t double_subnormal = 0;
    for (double v : dy) {
        require(std::isfinite(v), "nonfinite double output");
        double_subnormal += std::fpclassify(v) == FP_SUBNORMAL ? 1 : 0;
    }
    char line[256];
    std::snprintf(line, sizeof line,
                  "decaying tail: worst error %.3g of peak (%.3g absolute, peak %.3g); %zu oracle "
                  "samples below FLT_MIN; subnormal outputs: engine %zu, host %zu, host double "
                  "%zu\n",
                  peak > 0 ? worst / peak : 0, worst, peak, deep, engine_subnormal, host_subnormal,
                  double_subnormal);
    std::cout << line;
    if (flush_denormals_supported)
        require(engine_subnormal == 0 && host_subnormal == 0 && double_subnormal == 0,
                "subnormal output while flushing");
}
} // namespace

int main() {
    try {
        std::cout << "flush control: "
                  << (flush_denormals_supported ? "yes" : "none on this processor") << '\n';
        helper();
        engine_restores();
        host_restores<float>();
        host_restores<double>();
        subnormal_input();
        decaying_tail();
        require(gradual(), "the test left flushing on");
        std::cout << "denormal tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "unexpected exception\n";
        return 1;
    }
    return 0;
}
