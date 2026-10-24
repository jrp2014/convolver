// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include "host_log.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

thread_local bool audio_thread = false;
// Allocations allowed before the next one fails; negative means no limit.
thread_local long allocations_left = -1;
void* operator new(std::size_t n) {
    if (audio_thread)
        std::abort();
    if (allocations_left == 0)
        throw std::bad_alloc();
    if (allocations_left > 0)
        --allocations_left;
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
// Sized forms too, so library code (std::filesystem) never frees with the
// runtime's own delete what this operator new allocated.
void operator delete(void* p, std::size_t) noexcept {
    ::operator delete(p);
}
void operator delete[](void* p, std::size_t) noexcept {
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
    require(host.faults().rejected_input == 1 && host.faults().total() == 1,
            "rejected input not counted once");
    require(!host.process(in, out, 68), "overlarge callback accepted");
    require(host.faults().invalid_call == 1 && host.faults().total() == 2,
            "invalid call not counted");
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
// prepare() then publish() behaves as replace(); an unpublished replacement
// changes nothing, and one from another processor is ignored.
void prepared_replacements() {
    HostProcessor host(mono(), 64, 8), other(mono(), 64, 8);
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    { auto discarded = host.prepare(mono(7)); }
    auto foreign = other.prepare(mono(6));
    host.publish(std::move(foreign));
    auto r = host.prepare(mono(2));
    auto moved = std::move(r);
    host.publish(std::move(moved));
    audio_thread = true;
    host.process(in, out, 64);
    audio_thread = false;
    for (std::size_t i = 8; i < 64; ++i)
        require(std::abs(output[i] - (i == 8 ? 2.f : 2.25f)) < 1e-5,
                "published filter not installed directly");
    bool refused = false;
    try {
        auto wrong = mono();
        wrong.sample_rate = 44100;
        (void)host.prepare(wrong);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "prepare accepted another sample rate");
    host.collect();
}
// A reset ends any transition in progress: the newer filter plays at once, and
// a filter published after the reset is installed directly. Checked for a reset
// during the warm-up and during the crossfade, with and without a new filter.
void reset_during_transition() {
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    const auto run = [&](HostProcessor& host, std::size_t frames) {
        audio_thread = true;
        const bool ok = host.process(in, out, frames);
        audio_thread = false;
        require(ok, "process rejected");
    };
    const auto resumed = [&](float gain, const char* what) {
        for (std::size_t i = 0; i < 64; ++i)
            require(std::abs(output[i] - (i < 8 ? 0.f : i == 8 ? gain : gain + .25f)) < 1e-5, what);
    };
    for (const std::size_t into : {std::size_t{4}, std::size_t{64}}) { // warm-up, crossfade
        for (const bool reload : {true, false}) {
            HostProcessor host(mono(1), 64, 8);
            run(host, 64);
            host.replace(mono(2));
            run(host, into);
            audio_thread = true;
            host.reset();
            audio_thread = false;
            if (reload)
                host.replace(mono(3));
            run(host, 64);
            resumed(reload ? 3.f : 2.f, into == 4 ? "filter after reset in warm-up not direct"
                                                  : "filter after reset in crossfade not direct");
            // Once audio has resumed, a replacement warms up and fades again.
            host.replace(mono(4));
            run(host, 64);
            require(std::abs(output[0] - (reload ? 3.25f : 2.25f)) < 1e-5,
                    "replacement after a reset transition skipped warm-up");
            for (int k = 0; k < 8; ++k)
                run(host, 64);
            require(std::abs(output[63] - 4.25f) < 1e-5, "replacement after reset never finished");
            host.collect();
        }
    }
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
// PLUG-8: every failure is kept as a value naming what failed, the request it
// belongs to and whether the instance plays silence, until a later request
// succeeds (or, for a failure found at activation, the next activation).
void failure_reasons() {
    using Kind = HostFailureKind;
    const auto contains = [](const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    };
    const auto refused = [](auto&& load) {
        try {
            load();
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("load not refused");
    };
    const HostLayout stereo{48000, 2, 2};
    Configuration a{48000, 2, 2, 0, {}, {}, {{{1, .5f}, {{0, 1}}, {{1, 1}}}}};
    auto other_rate = a;
    other_rate.sample_rate = 44100;
    Configuration crossover{48000, 2, 6, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    Configuration wide{48000, 9, 9, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};

    HostFilter f(8);
    require(!f.failure() && f.error().empty(), "fresh instance has a failure");
    // Request 1: a failed first load, so silence.
    refused([&] { f.load(HostState{other_rate}, stereo); });
    {
        const auto& r = *f.failure();
        require(r.kind == Kind::sample_rate && r.request.number == 1 &&
                    r.request.source == HostFilterSource::state && r.sample_rate == 44100 &&
                    r.inputs == 2 && r.outputs == 2 && r.silent &&
                    contains(r.message, "44100 Hz") && contains(r.message, "plays silence") &&
                    f.error() == r.message,
                "failed first load not recorded");
    }
    // Request 2 succeeds and clears it.
    f.load(HostState{a}, stereo);
    require(!f.failure() && f.error().empty(), "success did not clear the failure");
    // Request 3: a failed replacement keeps the working filter, not silent,
    // and stays recorded across activations until a later request succeeds.
    refused([&] { f.load(HostState{crossover}, stereo); });
    require(f.failure()->kind == Kind::channels && f.failure()->request.number == 3 &&
                !f.failure()->silent && f.failure()->outputs == 6 &&
                contains(f.error(), "previous filter is kept"),
            "failed replacement not recorded");
    require(f.activate(stereo).paths.size() == 1 && f.failure() && f.failure()->request.number == 3,
            "activation cleared a rejected request's failure");
    // Request 4: preparation fails.
    refused([&] {
        f.load(HostState{a}, stereo, [](const Configuration&) { throw std::bad_alloc(); });
    });
    require(f.failure()->kind == Kind::preparation && f.failure()->request.number == 4 &&
                !f.failure()->silent && contains(f.error(), "could not be prepared"),
            "failed preparation not recorded");
    // Request 5: beyond the format's channel bound.
    refused([&] { f.load(HostState{wide}, std::nullopt); });
    require(f.failure()->kind == Kind::channels && f.failure()->request.number == 5 &&
                f.failure()->inputs == 9,
            "channel bound not recorded");
    // Requests that fail before decoding, numbered by the adapter or not.
    f.reject(Kind::state_newer, "it was saved by a newer version of Convolver.");
    require(f.failure()->kind == Kind::state_newer && f.failure()->request.number == 6 &&
                f.failure()->sample_rate == 0 && !f.failure()->silent,
            "newer state not recorded");
    f.reject(Kind::filter_file, "room.cfg is missing.",
             {HostFilterSource::file, "/music/room.cfg", 10});
    require(f.failure()->kind == Kind::filter_file && f.failure()->request.number == 10 &&
                f.failure()->request.source == HostFilterSource::file &&
                f.failure()->request.path == "/music/room.cfg",
            "adapter-numbered file request not recorded");
    f.reject("damaged");
    require(f.failure()->kind == Kind::state_unreadable && f.failure()->request.number == 11,
            "numbering does not continue after an adapter's number");

    // A failure found at activation belongs to the accepted filter's request,
    // comes before a rejected request's failure and clears when the host suits
    // the filter again; the rejected request's failure then shows again.
    HostFilter early(8);
    early.load(HostState{other_rate}, std::nullopt); // Request 1, accepted unchecked.
    refused([&] { early.load(HostState{crossover}, {{44100, 2, 2}}); }); // Request 2.
    require(early.activate(stereo).paths[0].impulse == std::vector<float>{0}, "mismatch played");
    require(early.failure()->kind == Kind::sample_rate && early.failure()->request.number == 1 &&
                early.failure()->silent && early.failure()->sample_rate == 44100 &&
                contains(early.error(), "does not suit the host"),
            "mismatch at activation not recorded");
    // A request rejected while the activation failure lasts: failure() still
    // explains the silence, rejection() explains the new request (to log it).
    refused([&] { early.load(HostState{crossover}, stereo); }); // Request 3.
    require(early.failure()->request.number == 1 && early.rejection() &&
                early.rejection()->request.number == 3 &&
                contains(early.rejection()->message, "6 outputs"),
            "a request rejected during an activation failure is not its own rejection");
    refused([&] { early.load(HostState{crossover}, {{44100, 2, 2}}); }); // Request 4.
    require(early.activate({44100, 2, 2}).sample_rate == 44100 && early.failure() &&
                early.failure()->request.number == 4 && !early.failure()->silent,
            "rejected request's failure lost behind an activation failure");
    early.fail_activation("out of memory");
    require(early.failure()->kind == Kind::activation && early.failure()->request.number == 1 &&
                early.failure()->silent && contains(early.error(), "out of memory"),
            "activation failure not recorded");
    early.activate({44100, 2, 2});
    require(early.failure()->kind != Kind::activation && early.failure()->request.number == 4,
            "activation failure outlived activation");
    // A number the adapter supplies (an LV2 generation) is kept, and later
    // numbering continues after it.
    early.load(HostState{}, std::nullopt, {}, {HostFilterSource::file, "/x.cfg", 9});
    require(!early.failure() && !early.rejection(), "empty state did not clear the failure");
    early.reject(Kind::filter_file, "missing", {HostFilterSource::file, "/y.cfg", 12});
    require(early.failure()->request.number == 12 && early.failure()->request.path == "/y.cfg",
            "a supplied request number was not kept");
    early.reject("damaged");
    require(early.failure()->request.number == 13, "numbering does not continue after 12");
    // A number reserved before load() is never reused.
    const auto reserved = early.next_request();
    require(reserved == 14, "next_request does not continue the numbering");
    early.reject(Kind::filter_file, "missing", {HostFilterSource::file, "/r.cfg", reserved});
    require(early.failure()->request.number == 14, "a reserved number was not kept");
    early.reject("damaged");
    require(early.failure()->request.number == 15, "a reserved number was reused");
    // A request abandoned by its install callback changes neither the filter
    // nor the failure (its number is used up all the same).
    const auto before = *early.failure();
    bool abandoned = false;
    try {
        early.load(HostState{crossover}, {{48000, 2, 6}},
                   [](const Configuration&) { throw SupersededRequest(); },
                   {HostFilterSource::file, "/z.cfg", 20});
    } catch (const SupersededRequest&) {
        abandoned = true;
    }
    require(abandoned && early.failure()->request.number == 15 &&
                early.failure()->message == before.message && !early.filter(),
            "a superseded request changed the filter or its failure");
    early.load(HostState{}, std::nullopt); // An empty instance succeeds.
    require(!early.failure(), "empty state did not clear the failure");
    HostFilter empty(8);
    empty.activate(stereo);
    empty.fail_activation("out of memory");
    require(empty.failure()->request.number == 0 && empty.failure()->inputs == 0,
            "an instance that never loaded a filter has a request");

    // Restoring a saved failure is a successful request whose instance plays
    // silence, and the reason says so.
    HostFilter reopened(8);
    HostState failed_state;
    failed_state.failed = true;
    reopened.load(failed_state, stereo);
    require(reopened.failure()->kind == Kind::saved_failure &&
                reopened.failure()->request.number == 1 && reopened.failure()->silent &&
                contains(reopened.error(), "failed to load before this state was saved"),
            "saved failure not recorded");
    require(reopened.activate(stereo).paths[0].impulse == std::vector<float>{0} &&
                reopened.failure()->kind == Kind::saved_failure,
            "activation cleared a saved failure");
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
// mute(), the audio thread's own silence: the current filter ramps to 0 over
// 64 frames and stays silent until a published filter takes over and fades in
// from silence. Bypass still passes the dry signal; a reset keeps the silence.
void muting() {
    std::array<float, 512> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    const auto run = [&](HostProcessor& host, std::size_t frames) {
        audio_thread = true;
        const bool ok = host.process(in, out, frames);
        audio_thread = false;
        require(ok, "process rejected");
    };
    const auto silent_from = [&](std::size_t from, std::size_t frames) {
        for (std::size_t i = from; i < frames; ++i)
            if (output[i] != 0)
                return false;
        return true;
    };
    HostProcessor host(mono(1), 512, 8);
    run(host, 64);
    require(std::abs(output[63] - 1.25f) < 1e-5, "steady output before muting");
    audio_thread = true;
    host.mute();
    audio_thread = false;
    run(host, 128);
    for (std::size_t i = 0; i < 64; ++i)
        require(std::abs(output[i] - 1.25f * (1 - static_cast<float>(i + 1) / 64)) < 1e-5,
                "mute does not ramp over 64 frames");
    require(silent_from(63, 128), "muted output not exactly silent");
    host.set_bypass(true);
    run(host, 256);
    require(std::abs(output[255] - 1) < 1e-6, "bypass while muted does not pass the input");
    host.set_bypass(false);
    run(host, 256);
    require(silent_from(64, 256), "muted again after bypass");
    audio_thread = true;
    host.reset();
    audio_thread = false;
    run(host, 64);
    require(silent_from(0, 64), "a reset ended the silence");
    run(host, 64);
    // A replacement warms up while the muted filter plays (silence), then fades in.
    host.replace(mono(2));
    run(host, 512);
    require(silent_from(0, 8), "replacement after mute: warm-up not silent");
    bool rising = true;
    for (std::size_t i = 9; i < 264; ++i)
        rising = rising && output[i] >= output[i - 1] - 1e-6f && output[i] <= 2.25f + 1e-5f;
    require(rising && std::abs(output[511] - 2.25f) < 1e-5,
            "replacement after mute does not fade in from silence");
    host.collect();
    // Muted during a crossfade: the old filter fades out, the new is silent.
    HostProcessor fading(mono(1), 512, 8);
    run(fading, 64);
    fading.replace(mono(2));
    run(fading, 16);
    audio_thread = true;
    fading.mute();
    audio_thread = false;
    run(fading, 512);
    require(silent_from(300, 512), "mute during a crossfade not silent at its end");
    fading.collect();
    // After a reset, a published filter is installed directly and plays.
    audio_thread = true;
    fading.reset();
    audio_thread = false;
    fading.replace(mono(3));
    run(fading, 64);
    for (std::size_t i = 0; i < 64; ++i)
        require(std::abs(output[i] - (i < 8    ? 0.f
                                      : i == 8 ? 3.f
                                               : 3.25f)) < 1e-5,
                "filter after a muted reset not installed directly");
    fading.collect();
    // PLUG-7: a filter published before mute() but not yet picked up by the
    // audio thread is silenced too; only one published after it plays. Found
    // in LV2: restoring an empty state while active, then run() refusing a
    // file, played the empty instance's unfiltered audio.
    for (const bool started : {true, false}) {
        HostProcessor early(mono(1), 512, 8);
        if (started)
            run(early, 64);
        early.replace(mono(2));
        audio_thread = true;
        early.mute();
        audio_thread = false;
        run(early, 512);
        require(silent_from(started ? 300 : 0, 512),
                "a filter published before mute() was heard when picked up");
        early.replace(mono(3));
        run(early, 512);
        require(std::abs(output[511] - 3.25f) < 1e-5, "a filter published after mute() is silent");
        early.collect();
    }
    // mute(through): what was published after the count stays audible.
    HostProcessor counted(mono(1), 512, 8);
    run(counted, 64);
    audio_thread = true;
    const auto seen = counted.publications();
    audio_thread = false;
    counted.replace(mono(2));
    audio_thread = true;
    counted.mute(seen);
    audio_thread = false;
    run(counted, 512);
    require(seen == 0 && counted.publications() == 1 && std::abs(output[511] - 2.25f) < 1e-5,
            "mute(through) silenced a later publication");
    // ... including one the audio thread has already picked up.
    audio_thread = true;
    counted.mute(seen);
    audio_thread = false;
    run(counted, 512);
    require(std::abs(output[511] - 2.25f) < 1e-5,
            "mute(through) silenced a playing filter published after the count");
    counted.collect();
}
// A load is a transaction: whichever allocation runs out of memory, what
// plays (the last configuration `install` published) is what the filter
// saves, and a rejected load leaves the previous state whole. Found in jrp's
// review of 0.4.30: a restored failed state published silence, then building
// its message threw, so the host kept saving the old filter.
void allocation_failures() {
    const HostLayout host{48000, 2, 6, 0x3, 0x633}; // Speakers that differ from the mask.
    const auto filter = [](float gain) {
        return Configuration{48000, 2, 6, 0x3F, {}, {}, {{{gain}, {{0, 1}}, {{0, 1}}}}};
    };
    // What a configuration plays, told apart by its first coefficient.
    const auto sound = [](const Configuration& c) { return c.paths.at(0).impulse.at(0); };
    const auto saved_sound = [&](const HostFilter& f) {
        return f.filter() ? sound(*f.filter()) : sound(unfiltered_configuration(host, f.failed()));
    };
    HostState failed;
    failed.failed = true;
    struct Case {
        const char* name;
        std::optional<HostState> before; // Loaded first, without limits.
        HostState next;
    };
    const Case cases[] = {
        {"first filter", std::nullopt, HostState{filter(2)}},
        {"replacement with a speaker notice", HostState{filter(.5f)}, HostState{filter(2)}},
        {"saved failed load over a filter", HostState{filter(.5f)}, failed},
        {"empty state over a filter", HostState{filter(.5f)}, HostState{}},
    };
    for (const auto& c : cases) {
        bool done = false;
        for (long limit = 0; !done; ++limit) {
            require(limit < 10000, "load never succeeded under the allocation limit");
            HostFilter f(8);
            float audible = sound(unfiltered_configuration(host, false));
            const std::function<void(const Configuration&)> install =
                [&](const Configuration& next) { audible = sound(next); };
            if (c.before)
                f.load(*c.before, host, install);
            const float before = audible;
            auto next = c.next;
            const std::optional<HostLayout> layout = host;
            allocations_left = limit;
            try {
                f.load(std::move(next), layout, install);
                done = true;
            } catch (const std::exception&) {
            }
            allocations_left = -1;
            const auto what =
                std::string(c.name) + " with " + std::to_string(limit) + " allocations: ";
            require(audible == saved_sound(f), (what + "what plays is not what is saved").c_str());
            if (!done)
                require(audible == before && f.filter().has_value() == c.before.has_value(),
                        (what + "a rejected load changed the previous state").c_str());
        }
    }
    // The notice is kept with the filter it describes.
    HostFilter f(8);
    f.load(HostState{filter(2)}, host, [](const Configuration&) {});
    require(!f.notice().empty(), "speaker notice not kept");
}
// CONVOLVER_LOG, read and set wide on Windows, as host_log_path() reads it,
// since temporary folders may hold characters outside the ANSI code page.
std::optional<std::filesystem::path> log_variable() {
#ifdef _WIN32
    if (const wchar_t* value = _wgetenv(L"CONVOLVER_LOG"))
        return std::filesystem::path(value);
#else
    if (const char* value = std::getenv("CONVOLVER_LOG"))
        return std::filesystem::path(value);
#endif
    return std::nullopt;
}
void set_log_variable(const std::optional<std::filesystem::path>& value) {
#ifdef _WIN32
    _wputenv_s(L"CONVOLVER_LOG", value ? value->wstring().c_str() : L""); // Empty removes it.
#else
    if (value)
        setenv("CONVOLVER_LOG", value->c_str(), 1);
    else
        unsetenv("CONVOLVER_LOG");
#endif
}
// DIAG-1: jrp's reproduction (review of 24 October 2026). Extreme but finite
// input and filter values overflow the output: it is silenced and process()
// succeeds, as before, but the fault is now counted, so the status no longer
// says "Filter loaded" and the log explains.
void processing_faults() {
    const auto huge = std::numeric_limits<float>::max() / 4;
    const Configuration loud = mono(huge);
    const HostLayout layout{48000, 1, 1};
    HostProcessor host(loud, 64, 8);
    std::array<float, 64> data{};
    data[0] = huge;
    const float* in[] = {data.data()};
    float* out[] = {data.data()};
    audio_thread = true;
    bool ok = host.process(in, out, 64);
    audio_thread = false;
    require(ok, "an overflowing call must still succeed");
    for (auto v : data)
        require(std::isfinite(v) && std::abs(v) <= std::numeric_limits<float>::max(),
                "overflowing output not silenced");
    const auto counted = host.faults();
    require(counted.output_overflow == 1 && counted.total() == 1,
            "output overflow not counted once");

    HostFilter f(8);
    f.load(HostState{loud}, layout);
    require(host_status(f, layout).status == HostStatus::playing, "playing before observing");
    const auto fresh = f.observe(host.faults());
    require(fresh.output_overflow == 1 && f.faults().output_overflow == 1, "observe");
    auto line = host_status(f, layout);
    require(line.status == HostStatus::fault &&
                line.text == "Fault: output too large, silenced and the filter restarted. Details "
                             "in the Convolver log.",
            "overflow status line");
    require(host_status(f, layout, false).text.find("could not be written") != std::string::npos,
            "unwritable log not mentioned");
    const auto message = fault_message(fresh);
    require(message.find("too large to represent (once)") != std::string::npos &&
                message.find("Lower the level") != std::string::npos &&
                message.find("invalid samples") == std::string::npos,
            "overflow message");
    require(fault_message({}).empty(), "message without faults");
    // Latched: nothing new to log, and the status stays until a load or start.
    require(!f.observe(host.faults()).any() && host_status(f, layout).status == HostStatus::fault,
            "fault not latched");
    // A failed load comes first; it does not clear the fault.
    try {
        f.load(HostState{mono()}, HostLayout{44100, 1, 1});
    } catch (const std::invalid_argument&) {
    }
    require(host_status(f, layout).status == HostStatus::load_failed, "load failure first");
    f.load(HostState{mono()}, layout);
    require(!f.faults().any() && host_status(f, layout).status == HostStatus::playing,
            "an accepted load clears the fault");
    // A new processor counts from 0 after activate().
    static_cast<void>(f.activate(layout));
    HostFaults restarted;
    restarted.rejected_input = 1;
    require(f.observe(restarted).rejected_input == 1 &&
                host_status(f, layout).text.find("invalid input samples") != std::string::npos,
            "counts after activation");
    HostFaults every{3, 2, 1, 1};
    const auto all = fault_message(every);
    require(all.find("(2 times)") != std::string::npos &&
                all.find("invalid samples (not a number, infinite, or beyond the 32-bit float "
                         "range) 3 times") != std::string::npos &&
                all.find("Convolver bug") != std::string::npos &&
                all.find("host's developers") != std::string::npos,
            "every fault explained");
    HostFilter g(8);
    g.load(HostState{mono()}, layout);
    static_cast<void>(g.observe(every));
    require(host_status(g, layout).text.find("the filter failed while playing") !=
                std::string::npos,
            "most serious fault first");
}
// A file name of this process's own, so that concurrent runs never share it.
std::filesystem::path private_temp(const char* name) {
    return std::filesystem::temp_directory_path() /
           (std::string(name) + "-" + std::to_string(std::random_device{}()));
}
// DIAG-1: the status line a host shows, and the Convolver log.
void status_lines() {
    using Status = HostStatus;
    const auto contains = [](const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    };
    const auto check = [&](const HostStatusLine& line, Status status, const char* part,
                           const char* what) {
        bool ascii = true;
        for (const char ch : line.text)
            ascii = ascii && static_cast<unsigned char>(ch) >= 32 &&
                    static_cast<unsigned char>(ch) < 127;
        if (line.status != status || !contains(line.text, part) ||
            line.text.size() > max_status_text || !ascii || line.text.back() != '.') {
            std::cerr << "status line: " << line.text << '\n';
            throw std::runtime_error(what);
        }
    };
    const auto refused = [](auto&& load) {
        try {
            load();
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("load not refused");
    };
    const HostLayout stereo{48000, 2, 2}, crossover_bus{48000, 2, 6};
    const Configuration a{48000, 2, 2, 0, {}, {}, {{{1, .5f}, {{0, 1}}, {{1, 1}}}}};
    auto other_rate = a;
    other_rate.sample_rate = 44100;
    Configuration crossover{48000, 2, 6, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};

    HostFilter f(8);
    check(host_status(f, std::nullopt), Status::no_filter, "No filter loaded.", "empty, unknown");
    check(host_status(f, stereo), Status::no_filter, "audio passes unchanged", "empty, stereo");
    check(host_status(f, crossover_bus), Status::no_filter, "silent, as inputs and outputs differ",
          "empty, 2->6");
    // A wrong rate as the first load: silence, with both rates.
    refused([&] { f.load(HostState{other_rate}, stereo); });
    check(host_status(f, stereo), Status::load_failed,
          "Silent: the filter is for 44100 Hz, the host runs at 48000 Hz. Details in the "
          "Convolver log.",
          "failed first load");
    // Accepted before the host is known (VST3 before activation, Audacity's
    // window): what was loaded, not yet checked.
    f.load(HostState{other_rate}, std::nullopt);
    check(host_status(f, std::nullopt), Status::unchecked,
          "Filter loaded: 44100 Hz, 2 in, 2 out; checked when the host starts it.", "unchecked");
    // Checked at activation: silence, and why.
    static_cast<void>(f.activate(stereo));
    check(host_status(f, stereo), Status::load_failed, "Silent: the filter is for 44100 Hz",
          "mismatch at activation");
    f.load(HostState{a}, stereo);
    check(host_status(f, stereo), Status::playing, "Filter loaded: 48000 Hz, 2 in, 2 out.",
          "playing");
    // A refused replacement: the previous filter plays, and the line says so.
    refused([&] { f.load(HostState{crossover}, stereo); });
    check(host_status(f, stereo), Status::load_failed,
          "Load failed, previous filter kept: the filter has 2 in, 6 out, the plug-in 2 in, 2 out.",
          "refused replacement");
    // The longest common line still fits when the log cannot be written.
    refused([&] { f.load(HostState{other_rate}, stereo); });
    check(host_status(f, stereo, false), Status::load_failed,
          "Load failed, previous filter kept: the filter is for 44100 Hz, the host runs at 48000 "
          "Hz. The log could not be written.",
          "unwritable log, refused rate");
    const std::array<std::pair<HostFailureKind, const char*>, 6> kinds{{
        {HostFailureKind::filter_file, "filter file could not be read"},
        {HostFailureKind::state_unreadable, "preset or saved state could not be read"},
        {HostFailureKind::state_newer, "saved by a newer Convolver"},
        {HostFailureKind::preparation, "could not be prepared"},
        {HostFailureKind::sample_rate, "the filter is for 0 Hz"},
        {HostFailureKind::channels, "the filter has 0 in, 0 out"},
    }};
    for (const auto& [kind, part] : kinds) {
        f.reject(kind, "why");
        check(host_status(f, std::nullopt), Status::load_failed, part, "failure kind");
        check(host_status(f, std::nullopt), Status::load_failed, "previous filter kept",
              "failure kind keeps the filter");
        check(host_status(f, std::nullopt, false), Status::load_failed,
              "The log could not be written.", "failure kind, unwritable log");
    }
    // A saved failed load plays silence and says so.
    HostState saved_failure;
    saved_failure.failed = true;
    f.load(saved_failure, stereo);
    check(host_status(f, stereo), Status::load_failed,
          "Silent: a load had failed when this was saved.", "saved failure");
    f.fail_activation("no memory");
    check(host_status(f, stereo), Status::load_failed,
          "Silent: the plug-in could not start the filter.", "activation failure");
    // A speaker notice is mentioned, briefly.
    HostFilter labelled(8);
    Configuration surround{48000, 2, 6, 0x3F, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    labelled.load(HostState{surround}, HostLayout{48000, 2, 6, 0x3, 0x633});
    check(host_status(labelled, HostLayout{48000, 2, 6, 0x3, 0x633}), Status::playing,
          "Speaker labels differ", "speaker notice");

    // The log lines adapters write.
    HostFilter g(8);
    require(loaded_line(g, true) == "Request 0: Loaded state with no filter." &&
                contains(started_line(g, stereo, 512),
                         "Started at 48000 Hz, 2 in, 2 out, blocks of up to 512 frames. No "
                         "filter, so audio passes unchanged.") &&
                contains(started_line(g, crossover_bus, 512), "plays silence"),
            "empty instance log lines");
    g.load(HostState{other_rate}, std::nullopt);
    require(loaded_line(g, false) == "Request 1: Loaded a filter for 44100 Hz, 2 in, 2 out; it is "
                                     "checked when the host starts the plug-in.",
            "unchecked load line");
    require(rejected_line(g).empty(), "rejected line without a rejection");
    try {
        g.load(HostState{crossover}, stereo);
    } catch (const std::invalid_argument&) {
    }
    require(rejected_line(g).rfind("Request 2: Filter not loaded; the previous filter is kept.",
                                   0) == 0,
            "rejected line");
    static_cast<void>(g.activate(stereo));
    require(contains(started_line(g, stereo, 64), "does not suit the host") &&
                contains(started_line(g, stereo, 64), "Convolver does not resample"),
            "silent start line does not explain");
    g.load(saved_failure, stereo);
    require(contains(loaded_line(g, true), "records a failed load") &&
                contains(loaded_line(g, true), "plays silence"),
            "saved failure load line");

    // The file: one dated line per message, with the version and format, and
    // rotation once it has grown past 1 MB.
    const auto folder = private_temp("convolver-host-log-test");
    std::filesystem::remove_all(folder);
    const auto path = folder / "nested" / "Convolver.log";
    const auto earlier = log_variable(); // Restored below.
    set_log_variable(path);
    require(host_log_path() == path, "CONVOLVER_LOG not used");
    host_log("Test #1", "first line");
    host_log("Test #1", "second line", false);
    host_log("Test #1", "");
    const auto read = [](const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    auto text = read(path);
    // The process's first line is preceded by what ran (once per process; an
    // earlier test in this process may have written it elsewhere).
    require(contains(text, " Convolver ") && contains(text, " Test #1") &&
                contains(text, " (process ") && contains(text, ": first line\n") &&
                contains(text, ": second line\n") &&
                std::count(text.begin(), text.end(), '\n') <= 3 && host_log_written(),
            "log lines not written");
    const auto session = execution_line();
    require(contains(session, "Session: Convolver ") && contains(session, "FFT backend ") &&
                contains(session, fft_backend_name()) && contains(session, "CPU kernels ") &&
                contains(session, cpu_kernels_name()) && contains(session, ", process "),
            "execution line");
    require(contains(text, session.c_str()), "execution line not logged first");
    {
        std::ofstream grow(path, std::ios::app | std::ios::binary);
        grow << std::string(1024 * 1024 + 1, 'x');
    }
    host_log("Test #1", "after rotation");
    auto old = path;
    old += ".old";
    require(contains(read(old), "first line") && read(path).size() < 1024 &&
                contains(read(path), "after rotation"),
            "log not rotated");
    // Empty turns the file off; an unwritable place is not an error, but it
    // is visible: host_log_written() is false (the status line says so) and
    // the line goes to standard error.
    set_log_variable(folder / "nested"); // A folder: cannot be opened.
    host_log("Test #1", "dropped");
    require(!host_log_written(), "unwritable log not noticed");
    {
        HostFilter h(8);
        h.reject("why");
        require(contains(host_status(h, std::nullopt, host_log_written()).text,
                         "The log could not be written."),
                "unwritable log not in the status line");
    }
    set_log_variable(path);
    host_log("Test #1", "written again");
    require(host_log_written(), "log not written again");
#ifndef _WIN32
    setenv("CONVOLVER_LOG", "", 1);
    require(host_log_path().empty(), "empty CONVOLVER_LOG does not turn the log off");
    set_log_variable(folder / "nested");
    host_log("Test #1", "dropped");
    require(!host_log_written(), "unwritable log not noticed again");
    setenv("CONVOLVER_LOG", "", 1);
    host_log("Test #1", "off");
    require(host_log_written(), "a log turned off on purpose reads as unwritable");
#endif
    set_log_variable(earlier);
    std::filesystem::remove_all(folder);
}

int main() {
    try {
        numeric<float>();
        numeric<double>();
        lifecycle();
        queued_replacements();
        unequal_channels();
        direct_installation();
        prepared_replacements();
        reset_during_transition();
        filter_contract();
        failure_reasons();
        speaker_notices();
        unequal_bypass();
        muting();
        allocation_failures();
        status_lines();
        processing_faults();
        std::cout << "host lifecycle passed\n";
    } catch (const std::exception& e) {
        audio_thread = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
