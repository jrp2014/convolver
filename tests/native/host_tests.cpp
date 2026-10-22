// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

thread_local bool audio_thread = false;
void* operator new(std::size_t n) {
    if (audio_thread)
        std::abort();
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void* p) noexcept {
    if (audio_thread && p)
        std::abort();
    std::free(p);
}
void operator delete[](void* p) noexcept {
    ::operator delete(p);
}
using namespace convolver;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Configuration mono(float gain = 1) {
    return {48000, 1, 1, 0, {}, {}, {{{gain, .25f}, {{0, 1}}, {{0, 1}}}}};
}
template <class T> void numeric() {
    HostProcessor host(mono(), 67, 8);
    std::array<T, 67> data{};
    std::vector<T> result;
    std::size_t position = 0;
    for (auto count : {3, 0, 17, 1, 67, 29}) {
        for (int j = 0; j < count; ++j)
            data[j] = position + j == 0 ? T(1) : T(0);
        const T* in[] = {data.data()};
        T* out[] = {data.data()};
        audio_thread = true;
        bool ok = host.process(in, out, count);
        audio_thread = false;
        require(ok, "host rejected valid block");
        result.insert(result.end(), data.begin(), data.begin() + count);
        position += count;
    }
    for (std::size_t i = 0; i < result.size(); ++i)
        require(std::abs(result[i] - (i == 8   ? 1
                                      : i == 9 ? .25
                                               : 0)) < 1e-5,
                "planar/in-place convolution or latency");
    host.reset();
    data.fill(0);
    data[0] = std::numeric_limits<T>::quiet_NaN();
    data[1] = std::numeric_limits<T>::infinity();
    const T* in[] = {data.data()};
    T* out[] = {data.data()};
    audio_thread = true;
    bool ok = host.process(in, out, 67);
    audio_thread = false;
    require(ok, "nonfinite input must be contained");
    for (auto v : data)
        require(v == 0, "invalid samples poisoned state");
    require(!host.process(in, out, 68), "overlarge callback accepted");
}
void lifecycle() {
    auto c = mono();
    c.paths[0].impulse = {0};
    HostProcessor host(c, 64, 8);
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    host.set_bypass(true);
    for (int k = 0; k < 3; ++k) {
        audio_thread = true;
        host.process(in, out, 64);
        audio_thread = false;
    }
    for (auto v : output)
        require(v == 1, "bypass must pass latency-matched dry signal");
    host.set_bypass(false);
    for (int k = 0; k < 3; ++k)
        host.process(in, out, 64);
    for (auto v : output)
        require(v == 0, "bypass does not return to wet");
    c.paths[0].impulse = {1};
    host.replace(c);
    for (int k = 0; k < 8; ++k) {
        audio_thread = true;
        host.process(in, out, 64);
        audio_thread = false;
        for (auto v : output)
            require(v >= 0 && v <= 1.00001f, "filter transition overshoot");
    }
    for (auto v : output)
        require(std::abs(v - 1) < 1e-5, "filter replacement not applied");
    host.collect();
    audio_thread = true;
    host.reset();
    host.process({}, out, 64);
    audio_thread = false;
    for (auto v : output)
        require(v == 0, "restart retains old history");
    std::atomic<bool> done = false;
    std::thread producer([&] {
        for (int k = 0; k < 100; ++k) {
            auto next = mono(k % 2 ? .5f : 1.f);
            host.replace(next);
        }
        done = true;
    });
    while (!done.load()) {
        audio_thread = true;
        host.process(in, out, 64);
        audio_thread = false;
        for (auto v : output)
            require(std::isfinite(v), "concurrent replacement nonfinite");
    }
    producer.join();
    host.collect();
}
void queued_replacements() {
    auto c = mono();
    c.paths[0].impulse = {1};
    HostProcessor host(c, 64, 8);
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    c.paths[0].impulse = {2};
    host.replace(c);
    audio_thread = true;
    host.process(in, out, 64);
    audio_thread = false;
    c.paths[0].impulse = {3};
    host.replace(c); // Queued during the first transition; no later control calls.
    for (int k = 0; k < 32; ++k) {
        audio_thread = true;
        host.process(in, out, 64);
        audio_thread = false;
    }
    for (auto v : output)
        require(std::abs(v - 3) < 1e-5, "queued replacement waits for control collection");
}
void unequal_channels() {
    // Two inputs, three outputs: input 0 feeds output 2 at half gain, input 1
    // feeds outputs 0 and 1 through one delayed path.
    Configuration c{48000, 2, 3, 0, {}, {}, {}};
    c.paths.push_back({{.5f}, {{0, 1}}, {{2, 1}}});
    c.paths.push_back({{0, 1}, {{1, 1}}, {{0, 1}, {1, -1}}});
    HostProcessor host(c, 32, 8);
    std::array<float, 32> left{}, right{}, a{}, b{}, d{};
    left[0] = 1;
    right[0] = 2;
    const float* in[] = {left.data(), right.data()};
    float* out[] = {a.data(), b.data(), d.data()};
    audio_thread = true;
    bool ok = host.process(in, out, 32);
    audio_thread = false;
    require(ok, "unequal layout rejected");
    for (int i = 0; i < 32; ++i) {
        require(std::abs(a[i] - (i == 9 ? 2.f : 0.f)) < 1e-6, "unequal route 1->0");
        require(std::abs(b[i] - (i == 9 ? -2.f : 0.f)) < 1e-6, "unequal route 1->1");
        require(std::abs(d[i] - (i == 8 ? .5f : 0.f)) < 1e-6, "unequal route 0->2");
    }
    float* too_many[] = {a.data(), b.data(), d.data(), d.data()};
    const float* too_many_in[] = {left.data(), right.data(), left.data()};
    require(!host.process(in, too_many, 32) && !host.process(too_many_in, out, 32),
            "extra channels accepted");
    host.reset();
    host.set_bypass(true);
    left.fill(1);
    right.fill(3);
    for (int k = 0; k < 4; ++k)
        host.process(in, out, 32);
    for (int i = 0; i < 32; ++i)
        require(a[i] == 1 && b[i] == 3 && d[i] == 0, "unequal bypass mapping");
    auto mismatch = c;
    mismatch.output_channels = 2;
    mismatch.paths.resize(1);
    mismatch.paths[0].outputs = {{1, 1}};
    bool rejected = false;
    try {
        host.replace(mismatch);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "replacement changed output channels");
}
// Initial restoration: a filter published before any audio since construction
// or reset is installed directly; afterwards a replacement warms up and fades.
void direct_installation() {
    HostProcessor host(mono(), 64, 8);
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    host.replace(mono(2));
    audio_thread = true;
    host.process(in, out, 64);
    audio_thread = false;
    for (std::size_t i = 8; i < 64; ++i)
        require(std::abs(output[i] - (i == 8 ? 2.f : 2.25f)) < 1e-5,
                "initial filter not installed directly");
    host.replace(mono(4));
    audio_thread = true;
    host.process(in, out, 64);
    audio_thread = false;
    require(std::abs(output[0] - 2.25f) < 1e-5, "live replacement skipped warm-up");
    host.collect();
    host.reset();
    host.replace(mono(3));
    audio_thread = true;
    for (int k = 0; k < 16; ++k) // Let both transitions finish.
        host.process(in, out, 64);
    host.reset();
    audio_thread = false;
    host.replace(mono(5));
    audio_thread = true;
    host.process(in, out, 64);
    audio_thread = false;
    for (std::size_t i = 8; i < 64; ++i)
        require(std::abs(output[i] - (i == 8 ? 5.f : 5.25f)) < 1e-5,
                "filter after reset not installed directly");
    host.collect();
}
// The control side of the shared adapter contract, which every format uses.
void filter_contract() {
    const HostLayout stereo{48000, 2, 2}, crossover{48000, 2, 6};
    auto identity = unfiltered_configuration(stereo, false);
    require(identity.paths.size() == 2 && identity.paths[1].impulse == std::vector<float>{1} &&
                identity.paths[1].inputs[0].channel == 1 &&
                identity.paths[1].outputs[0].channel == 1,
            "empty instance with equal counts is not identity");
    for (const auto& silent :
         {unfiltered_configuration(crossover, false), unfiltered_configuration(stereo, true)})
        require(silent.paths.size() == 1 && silent.paths[0].impulse == std::vector<float>{0} &&
                    configuration_tail_frames(silent) == 0,
                "unequal counts or a failed load are not silent");
    const auto expect_rejection = [](auto&& load, const char* what) {
        try {
            load();
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error(what);
    };
    Configuration a{48000, 2, 2, 0, {}, {}, {{{1, .5f}, {{0, 1}}, {{1, 1}}}}};
    auto other_rate = a;
    other_rate.sample_rate = 44100;
    auto wide = a;
    wide.input_channels = wide.output_channels = 9;

    // Failed initial load: nothing accepted, so silence with an explanation.
    HostFilter first(8);
    require(!first.filter() && !first.failed() && first.error().empty(), "fresh filter state");
    expect_rejection([&] { first.load(HostState{other_rate}, stereo); }, "wrong rate accepted");
    require(first.failed() && !first.filter(), "failed initial load not recorded");
    require(first.error().find("44100 Hz") != std::string::npos &&
                first.error().find("plays silence") != std::string::npos,
            "failed initial load not explained");
    require(first.activate(stereo).paths[0].impulse == std::vector<float>{0},
            "failed initial load does not play silence");
    expect_rejection([&] { first.load(HostState{wide}, std::nullopt); }, "channel bound unchecked");
    first.load(HostState{}, stereo); // Back to an empty instance.
    require(!first.failed() && first.error().empty() && first.activate(stereo).paths.size() == 2,
            "empty state does not restore identity");

    // A saved failed load (state version 2) restores as silence, not identity.
    HostFilter reopened(8);
    HostState failed_state;
    failed_state.failed = true;
    Configuration installed;
    reopened.load(failed_state, stereo, [&](const Configuration& c) { installed = c; });
    require(reopened.failed() && !reopened.filter() && !reopened.error().empty() &&
                installed.paths.size() == 1 && installed.paths[0].impulse == std::vector<float>{0},
            "saved failed load does not restore as silence");
    require(reopened.activate(stereo).paths[0].impulse == std::vector<float>{0},
            "saved failed load activates as identity");
    auto contradictory = failed_state;
    contradictory.configuration = a;
    expect_rejection([&] { HostFilter(8).load(contradictory, stereo); },
                     "failed state with a filter accepted");

    // Failed replacement: the accepted filter stays, and install is not called.
    HostFilter running(8);
    int installs = 0;
    running.load(HostState{a}, stereo, [&](const Configuration&) { ++installs; });
    require(installs == 1 && running.filter() && running.filter()->paths[0].impulse.size() == 2,
            "valid filter not installed");
    expect_rejection(
        [&] { running.load(HostState{other_rate}, stereo, [&](auto&) { ++installs; }); },
        "replacement at the wrong rate accepted");
    expect_rejection(
        [&] {
            running.load(HostState{a}, stereo,
                         [](const Configuration&) { throw std::bad_alloc(); });
        },
        "failed preparation accepted");
    require(installs == 1 && !running.failed() && running.filter()->sample_rate == 48000,
            "failed replacement lost the working filter");
    require(running.error().find("previous filter is kept") != std::string::npos,
            "failed replacement not explained");

    // Restored before the host is configured: kept, checked at activation.
    HostFilter early(8);
    early.load(HostState{other_rate}, std::nullopt);
    require(early.filter() && early.filter()->sample_rate == 44100, "early restore not kept");
    require(early.activate(stereo).paths[0].impulse == std::vector<float>{0} &&
                early.error().find("44100 Hz") != std::string::npos,
            "mismatch at activation does not play silence with an error");
    require(early.filter() && early.activate({44100, 2, 2}).sample_rate == 44100 &&
                early.error().empty(),
            "correcting the rate does not bring the filter back");
    require(early.activate({44100, 2, 6}).paths[0].impulse == std::vector<float>{0} &&
                early.error().find("2 inputs and 6 outputs") != std::string::npos,
            "channel mismatch at activation not explained");
}
int main() {
    try {
        numeric<float>();
        numeric<double>();
        lifecycle();
        queued_replacements();
        unequal_channels();
        direct_installation();
        filter_contract();
        std::cout << "host lifecycle passed\n";
    } catch (const std::exception& e) {
        audio_thread = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
