// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
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
        require(a[i] == 0 && b[i] == 0 && d[i] == 0, "unequal bypass must play silence");
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
    // Restoring it over a working filter restores what was saved: silence.
    HostFilter working(8);
    working.load(HostState{a}, stereo);
    working.load(failed_state, stereo, [&](const Configuration& c) { installed = c; });
    require(working.failed() && !working.filter() &&
                installed.paths[0].impulse == std::vector<float>{0},
            "saved failed load over a working filter does not restore as silence");
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
// VST3-1: a filter's output channel mask is compared with the host's speakers,
// only ever as advice; channels stay routed by bus index.
void speaker_notices() {
    const auto contains = [](const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    };
    Configuration c{48000, 2, 6, 0x3F, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    const HostLayout back{48000, 2, 6, 0x3, 0x3F}, side{48000, 2, 6, 0x3, 0x60F},
        music{48000, 2, 6, 0x3, 0x633}, unnamed{48000, 2, 6};
    require(speaker_notice(c, back).empty() && speaker_notice(c, side).empty(),
            "5.1 with back or side speakers must count as the same layout");
    const auto notice = speaker_notice(c, music);
    require(contains(notice, "FL FR FC LFE BL BR (channel mask 0x3F in its .cfg)") &&
                contains(notice, "carries FL FR BL BR SL SR") &&
                contains(notice, "output 3 is filtered for the front centre (FC) speaker but plays "
                                 "on the back left (BL)") &&
                contains(notice, "output 6 is filtered for the back right (BR) speaker but plays "
                                 "on the side right (SR)") &&
                !contains(notice, "output 1 ") && contains(notice, "plays as loaded"),
            "speaker mismatch not explained");
    require(speaker_notice(c, unnamed).empty(), "host without speakers compared");
    for (const std::uint32_t mask : {0x0U, 0x3U, 0x7FU, 0x4001FU}) {
        auto loose = c;
        loose.channel_mask = mask;
        require(speaker_notice(loose, music).empty(),
                "a mask that does not name one speaker per output was compared");
    }
    // Both pairs present (7.1): back and side are distinct speakers.
    Configuration seven{48000, 8, 8, 0x63F, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    require(speaker_notice(seven, {48000, 8, 8, 0, 0x63F}).empty() &&
                contains(speaker_notice(seven, {48000, 8, 8, 0, 0xFF}),
                         "output 7 is filtered for the side left (SL) speaker but plays on the "
                         "front left-centre (FLC)"),
            "7.1 layouts not compared exactly");
    // Bus order counts: 6.1 with side speakers is not 6.1 with back speakers,
    // because BC comes before SL SR but after BL BR.
    Configuration six{48000, 7, 7, 0x70F, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    const auto six_notice = speaker_notice(six, {48000, 7, 7, 0, 0x13F});
    require(contains(six_notice, "output 5 is filtered for the back centre (BC) speaker") &&
                contains(six_notice, "output 7 is filtered for the side right (SR) speaker but "
                                     "plays on the back centre (BC)"),
            "6.1 side layout matched 6.1 back layout");
    six.channel_mask = 0x13F;
    require(!speaker_notice(six, {48000, 7, 7, 0, 0x70F}).empty(),
            "6.1 back layout matched 6.1 side layout");
    Configuration wide{48000, 8, 8, 0x6CF, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    require(contains(speaker_notice(wide, {48000, 8, 8, 0, 0xFF}),
                     "output 5 is filtered for the front left-centre (FLC) speaker"),
            "7.1 with side speakers matched 7.1 Cine");
    // A notice for another channel does not list side-for-back channels.
    Configuration five{48000, 5, 5, 0x60B, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    const auto five_notice = speaker_notice(five, {48000, 5, 5, 0, 0x37});
    require(contains(five_notice, "output 3 is filtered for the subwoofer (LFE) speaker") &&
                !contains(five_notice, "output 4") && !contains(five_notice, "output 5"),
            "side speakers listed as different from back speakers");
    require(contains(five_notice, "set the host's track or bus to FL FR LFE SL SR") &&
                contains(five_notice, "load the .cfg again"),
            "notice does not say what to change");
    Configuration mono{48000, 1, 1, 0x4, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    require(speaker_notice(mono, {48000, 1, 1, 0x1, 0x1}).empty(), "mono speakers compared");

    // HostFilter keeps the notice for the accepted filter and the current host.
    HostFilter filter(8);
    filter.load(HostState{c}, music);
    require(filter.filter() && filter.error().empty() && contains(filter.notice(), "BL BR SL SR"),
            "notice not recorded on load");
    require(filter.activate(back).paths.size() == 1 && filter.notice().empty(),
            "matching speakers left a notice");
    filter.load(HostState{c}, std::nullopt);
    require(filter.notice().empty(), "notice without the host's configuration");
    require(filter.activate(music).paths.size() == 1 && !filter.notice().empty(),
            "notice not recorded at activation");
    require(filter.activate({44100, 2, 6, 0, 0x633}).paths[0].impulse == std::vector<float>{0} &&
                filter.notice().empty(),
            "notice kept while the filter does not play");
    filter.load(HostState{}, music);
    require(filter.notice().empty(), "empty instance kept a notice");
}
// Bypass with more inputs than outputs (2->1 downmix) or fewer (a 1->2 fork)
// fades every output to silence; until then the filter plays as configured.
void unequal_bypass() {
    for (const std::size_t inputs : {2U, 1U}) {
        const std::size_t outputs = 3 - inputs;
        Configuration c{48000, inputs, outputs, 0, {}, {}, {}};
        if (inputs == 2)
            c.paths.push_back({{1}, {{0, 1}, {1, 1}}, {{0, 1}}});
        else
            c.paths.push_back({{1}, {{0, 1}}, {{0, 1}, {1, 1}}});
        HostProcessor host(c, 32, 8);
        std::array<float, 32> left{}, right{}, a{}, b{};
        left.fill(1);
        right.fill(2);
        const float* both[] = {left.data(), right.data()};
        float* out[] = {a.data(), b.data()};
        std::span<const float* const> in(both, inputs);
        std::span<float* const> to(out, outputs);
        audio_thread = true;
        host.process(in, to, 32);
        audio_thread = false;
        for (std::size_t o = 0; o < outputs; ++o)
            require(std::abs(out[o][31] - (inputs == 2 ? 3.f : 1.f)) < 1e-5,
                    "unequal counts play the filter");
        host.set_bypass(true);
        audio_thread = true;
        for (int k = 0; k < 4; ++k)
            host.process(in, to, 32);
        audio_thread = false;
        for (std::size_t o = 0; o < outputs; ++o)
            for (int i = 0; i < 32; ++i)
                require(out[o][i] == 0, "unequal bypass must play silence");
    }
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
        speaker_notices();
        unequal_bypass();
        std::cout << "host lifecycle passed\n";
    } catch (const std::exception& e) {
        audio_thread = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
