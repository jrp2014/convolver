// SPDX-License-Identifier: GPL-2.0-or-later
#include "advice.hpp"
#include "cases.hpp"
#include "convolver/host.hpp"
#include "host_activation.hpp"
#include "host_log.hpp"
#include "oracle.hpp"
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
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
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
// GCC warns that free() here mismatches the operator new above once this
// delete is inlined; both use malloc's heap, so the pair matches.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* p) noexcept {
    if (audio_thread && p)
        std::abort();
    std::free(p);
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include "replaced_allocation.hpp"
using namespace convolver;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void require(bool value, const std::string& message) {
    require(value, message.c_str());
}
// A message's text, for searching: DIAG-4 records keep their parts.
const std::string& text_of(const std::string& text) {
    return text;
}
std::string text_of(const Advice& advice) {
    return detail::advice_text(advice);
}
// What a host saves for an instance (PLUG-39 step 4a: a copy of the shared
// filter, as an adapter's state encoding makes).
HostState saved_of(const HostFilter& f) {
    HostState s;
    if (f.filter())
        s.configuration = *f.filter();
    return s;
}
Configuration mono(float gain = 1) {
    return {48000, 1, 1, 0, {}, {}, {{{gain, .25f}, {{0, 1}}, {{0, 1}}}}};
}
template <class T> void numeric() {
    HostProcessor host(filter_playback(mono()), 67, 8);
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
    HostProcessor host(filter_playback(c), 64, 8);
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
    host.replace(filter_playback(c));
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
            host.replace(filter_playback(next));
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
    HostProcessor host(filter_playback(c), 64, 8);
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    c.paths[0].impulse = {2};
    host.replace(filter_playback(c));
    audio_thread = true;
    host.process(in, out, 64);
    audio_thread = false;
    c.paths[0].impulse = {3};
    host.replace(filter_playback(c)); // Queued during the first transition; no later control calls.
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
    HostProcessor host(filter_playback(c), 32, 8);
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
        host.replace(filter_playback(mismatch));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "replacement changed output channels");
}
// Initial restoration: a filter published before any audio since construction
// or reset is installed directly; afterwards a replacement warms up and fades.
void direct_installation() {
    HostProcessor host(filter_playback(mono()), 64, 8);
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    host.replace(filter_playback(mono(2)));
    audio_thread = true;
    host.process(in, out, 64);
    audio_thread = false;
    for (std::size_t i = 8; i < 64; ++i)
        require(std::abs(output[i] - (i == 8 ? 2.f : 2.25f)) < 1e-5,
                "initial filter not installed directly");
    host.replace(filter_playback(mono(4)));
    audio_thread = true;
    host.process(in, out, 64);
    audio_thread = false;
    require(std::abs(output[0] - 2.25f) < 1e-5, "live replacement skipped warm-up");
    host.collect();
    host.reset();
    host.replace(filter_playback(mono(3)));
    audio_thread = true;
    for (int k = 0; k < 16; ++k) // Let both transitions finish.
        host.process(in, out, 64);
    host.reset();
    audio_thread = false;
    host.replace(filter_playback(mono(5)));
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
    HostProcessor host(filter_playback(mono()), 64, 8), other(filter_playback(mono()), 64, 8);
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    {
        auto discarded = host.prepare(filter_playback(mono(7)));
    }
    auto foreign = other.prepare(filter_playback(mono(6)));
    host.publish(std::move(foreign));
    auto r = host.prepare(filter_playback(mono(2)));
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
        (void)host.prepare(filter_playback(wrong));
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
            HostProcessor host(filter_playback(mono(1)), 64, 8);
            run(host, 64);
            host.replace(filter_playback(mono(2)));
            run(host, into);
            audio_thread = true;
            host.reset();
            audio_thread = false;
            if (reload)
                host.replace(filter_playback(mono(3)));
            run(host, 64);
            resumed(reload ? 3.f : 2.f, into == 4 ? "filter after reset in warm-up not direct"
                                                  : "filter after reset in crossfade not direct");
            // Once audio has resumed, a replacement warms up and fades again.
            host.replace(filter_playback(mono(4)));
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
    // DIAG-2: the problem, cause and fix; what plays instead follows from
    // `silent` (failure_consequence()).
    require(first.error().find("the filter is for 44100 Hz") == 0 &&
                first.error().find("\nFix: Load a filter made for 48000 Hz") != std::string::npos &&
                first.failure()->silent,
            "failed initial load not explained");
    // PLUG-25: a rate refusal's fix says the playing rate can differ from
    // the export's (decisions log, 8 November 2026); a channel refusal's does not.
    const std::string playback_rate =
        "or set the host's project or audio device to 44100 Hz. The host may play at a "
        "different rate from the one it exports at.";
    require(first.error().find(playback_rate) != std::string::npos,
            "rate refusal without the playback-rate line: " + first.error());
    require(test::started(first, stereo).filter->paths[0].impulse == std::vector<float>{0},
            "failed initial load does not play silence");
    expect_rejection([&] { first.load(HostState{wide}, std::nullopt); }, "channel bound unchecked");
    require(first.error().find("exports at") == std::string::npos,
            "channel refusal names the playback rate: " + first.error());
    first.load(HostState{}, stereo); // Back to an empty instance.
    require(!first.failed() && first.error().empty() &&
                test::started(first, stereo).filter->paths.size() == 2,
            "empty state does not restore identity");

    // A saved failed load (state version 2) restores as silence, not identity.
    HostFilter reopened(8);
    HostState failed_state;
    failed_state.failed = true;
    Configuration installed;
    reopened.load(failed_state, stereo, [&](const HostPlayback& c) { installed = *c.filter; });
    require(reopened.failed() && !reopened.filter() && !reopened.error().empty() &&
                installed.paths.size() == 1 && installed.paths[0].impulse == std::vector<float>{0},
            "saved failed load does not restore as silence");
    require(test::started(reopened, stereo).filter->paths[0].impulse == std::vector<float>{0},
            "saved failed load activates as identity");
    // Restoring it over a working filter restores what was saved: silence.
    HostFilter working(8);
    working.load(HostState{a}, stereo);
    working.load(failed_state, stereo, [&](const HostPlayback& c) { installed = *c.filter; });
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
    running.load(HostState{a}, stereo, [&](const HostPlayback&) { ++installs; });
    require(installs == 1 && running.filter() && running.filter()->paths[0].impulse.size() == 2,
            "valid filter not installed");
    expect_rejection(
        [&] { running.load(HostState{other_rate}, stereo, [&](auto&) { ++installs; }); },
        "replacement at the wrong rate accepted");
    expect_rejection(
        [&] {
            running.load(HostState{a}, stereo, [](const HostPlayback&) { throw std::bad_alloc(); });
        },
        "failed preparation accepted");
    require(installs == 1 && !running.failed() && running.filter()->sample_rate == 48000,
            "failed replacement lost the working filter");
    require(running.error().find("the filter could not be prepared") == 0 &&
                !running.failure()->silent &&
                failure_consequence(*running.failure(), &stereo, {}, true, false) ==
                    "The previous filter keeps playing." &&
                failure_consequence(*running.failure(), nullptr, {}, true, false) ==
                    "The previous filter is kept.",
            "failed replacement not explained");

    // Restored before the host is configured: kept, checked at activation.
    HostFilter early(8);
    early.load(HostState{other_rate}, std::nullopt);
    require(early.filter() && early.filter()->sample_rate == 44100, "early restore not kept");
    require(test::started(early, stereo).filter->paths[0].impulse == std::vector<float>{0} &&
                early.error().find("44100 Hz") != std::string::npos,
            "mismatch at activation does not play silence with an error");
    require(early.filter() && test::started(early, {44100, 2, 2}).filter->sample_rate == 44100 &&
                early.error().empty(),
            "correcting the rate does not bring the filter back");
    require(test::started(early, {44100, 2, 6}).filter->paths[0].impulse == std::vector<float>{0} &&
                early.error().find("2 inputs and 6 outputs") != std::string::npos,
            "channel mismatch at activation not explained");
}
// PLUG-8: every failure is kept as a value naming what failed, the request it
// belongs to and whether the instance plays silence, until a later request
// succeeds (or, for a failure found at activation, the next activation).
void failure_reasons() {
    using Kind = HostFailureKind;
    const auto contains = [](const auto& text, const char* part) {
        return text_of(text).find(part) != std::string::npos;
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
                    contains(r.message, "44100 Hz") && contains(r.message, "\nFix: ") &&
                    f.error() == detail::advice_text(r.message),
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
                contains(f.error(), "the filter has 2 inputs and 6 outputs"),
            "failed replacement not recorded");
    require(test::started(f, stereo).filter->paths.size() == 1 && f.failure() &&
                f.failure()->request.number == 3,
            "activation cleared a rejected request's failure");
    // Request 4: preparation fails.
    refused(
        [&] { f.load(HostState{a}, stereo, [](const HostPlayback&) { throw std::bad_alloc(); }); });
    require(f.failure()->kind == Kind::preparation && f.failure()->request.number == 4 &&
                !f.failure()->silent && contains(f.error(), "could not be prepared"),
            "failed preparation not recorded");
    // Request 5: beyond the format's channel bound.
    refused([&] { f.load(HostState{wide}, std::nullopt); });
    require(f.failure()->kind == Kind::channels && f.failure()->request.number == 5 &&
                f.failure()->inputs == 9,
            "channel bound not recorded");
    // Requests that fail before decoding, numbered by the adapter or not.
    f.reject(Kind::state_newer, {"it was saved by a newer version of Convolver."});
    require(f.failure()->kind == Kind::state_newer && f.failure()->request.number == 6 &&
                f.failure()->sample_rate == 0 && !f.failure()->silent,
            "newer state not recorded");
    f.reject(Kind::filter_file, {"room.cfg is missing."},
             {HostFilterSource::file, "/music/room.cfg", 10});
    require(f.failure()->kind == Kind::filter_file && f.failure()->request.number == 10 &&
                f.failure()->request.source == HostFilterSource::file &&
                f.failure()->request.path == "/music/room.cfg",
            "adapter-numbered file request not recorded");
    f.reject({"damaged"});
    require(f.failure()->kind == Kind::state_unreadable && f.failure()->request.number == 11,
            "numbering does not continue after an adapter's number");

    // A failure found at activation belongs to the accepted filter's request,
    // comes before a rejected request's failure and clears when the host suits
    // the filter again; the rejected request's failure then shows again.
    HostFilter early(8);
    early.load(HostState{other_rate}, std::nullopt); // Request 1, accepted unchecked.
    refused([&] { early.load(HostState{crossover}, {{44100, 2, 2}}); }); // Request 2.
    require(test::started(early, stereo).filter->paths[0].impulse == std::vector<float>{0},
            "mismatch played");
    require(early.failure()->kind == Kind::sample_rate && early.failure()->request.number == 1 &&
                early.failure()->silent && early.failure()->sample_rate == 44100 &&
                contains(early.error(), "the saved filter is for 44100 Hz"),
            "mismatch at activation not recorded");
    // A request rejected while the activation failure lasts: failure() still
    // explains the silence, rejection() explains the new request (to log it).
    refused([&] { early.load(HostState{crossover}, stereo); }); // Request 3.
    require(early.failure()->request.number == 1 && early.rejection() &&
                early.rejection()->request.number == 3 &&
                contains(early.rejection()->message, "6 outputs"),
            "a request rejected during an activation failure is not its own rejection");
    refused([&] { early.load(HostState{crossover}, {{44100, 2, 2}}); }); // Request 4.
    require(test::started(early, {44100, 2, 2}).filter->sample_rate == 44100 && early.failure() &&
                early.failure()->request.number == 4 && !early.failure()->silent,
            "rejected request's failure lost behind an activation failure");
    early.fail_activation("out of memory");
    require(early.failure()->kind == Kind::activation && early.failure()->request.number == 1 &&
                early.failure()->silent && contains(early.error(), "out of memory"),
            "activation failure not recorded");
    test::started(early, {44100, 2, 2});
    require(early.failure()->kind != Kind::activation && early.failure()->request.number == 4,
            "activation failure outlived activation");
    // PLUG-39 step 5b: from the exception, the reason is its problem alone,
    // so its cause and fix never appear inside the record's own; and one
    // that is not a std::exception still gives a reason.
    early.fail_activation(&static_cast<const std::exception&>(
        convolver::detail::AdvisedError<std::runtime_error>({"no room", "Cause.", "Fix.", {}})));
    require(early.failure()->kind == Kind::activation &&
                contains(early.failure()->message.problem, ": no room") &&
                !contains(early.error(), "Cause.") && !contains(early.error(), "Fix."),
            "an advised exception's cause or fix reached the activation failure");
    early.fail_activation(nullptr);
    require(contains(early.failure()->message.problem, "the processor could not be built"),
            "an activation failure without an exception gives no reason");
    test::started(early, {44100, 2, 2});
    // A number the adapter supplies (an LV2 generation) is kept, and later
    // numbering continues after it.
    early.load(HostState{}, std::nullopt, {}, {HostFilterSource::file, "/x.cfg", 9});
    require(!early.failure() && !early.rejection(), "empty state did not clear the failure");
    early.reject(Kind::filter_file, {"missing"}, {HostFilterSource::file, "/y.cfg", 12});
    require(early.failure()->request.number == 12 && early.failure()->request.path == "/y.cfg",
            "a supplied request number was not kept");
    early.reject({"damaged"});
    require(early.failure()->request.number == 13, "numbering does not continue after 12");
    // A number reserved before load() is never reused.
    const auto reserved = early.next_request();
    require(reserved == 14, "next_request does not continue the numbering");
    early.reject(Kind::filter_file, {"missing"}, {HostFilterSource::file, "/r.cfg", reserved});
    require(early.failure()->request.number == 14, "a reserved number was not kept");
    early.reject({"damaged"});
    require(early.failure()->request.number == 15, "a reserved number was reused");
    // A request abandoned by its install callback changes neither the filter
    // nor the failure (its number is used up all the same).
    const auto before = *early.failure();
    bool abandoned = false;
    try {
        early.load(HostState{crossover}, {{48000, 2, 6}},
                   [](const HostPlayback&) { throw SupersededRequest(); },
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
    test::started(empty, stereo);
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
                contains(reopened.error(), "had failed to load when this project or preset was "
                                           "saved"),
            "saved failure not recorded");
    require(test::started(reopened, stereo).filter->paths[0].impulse == std::vector<float>{0} &&
                reopened.failure()->kind == Kind::saved_failure,
            "activation cleared a saved failure");
}
// VST3-1: a filter's output channel mask is compared with the host's speakers,
// only ever as advice; channels stay routed by bus index.
void speaker_notices() {
    const auto contains = [](const auto& text, const char* part) {
        return text_of(text).find(part) != std::string::npos;
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
    require(test::started(filter, back).filter->paths.size() == 1 && filter.notice().empty(),
            "matching speakers left a notice");
    filter.load(HostState{c}, std::nullopt);
    require(filter.notice().empty(), "notice without the host's configuration");
    require(test::started(filter, music).filter->paths.size() == 1 && !filter.notice().empty(),
            "notice not recorded at activation");
    // ROBUST-8: an activation whose processor was not built keeps it.
    require(filter.activation(back).notice.empty() && !filter.notice().empty(),
            "an activation that never played changed the notice");
    require(test::started(filter, {44100, 2, 6, 0, 0x633}).filter->paths[0].impulse ==
                    std::vector<float>{0} &&
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
        HostProcessor host(filter_playback(c), 32, 8);
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
// PLUG-23: REAPER gives a VST3 plug-in as many inputs as outputs, so a 2-in,
// 6-out crossover on a six-channel track gets 6 of each. The filter reads
// inputs 1-2 and ignores the rest, and with no dry signal of its own, bypass
// and Mix never pass input c to output c, though the host's counts are equal.
void wider_inputs() {
    const HostLayout six{48000, 6, 6};
    Configuration x{48000, 2, 6, 0, {}, {}, {}};
    x.paths.push_back({{1, .5f}, {{0, 1}}, {{0, 1}}});
    x.paths.push_back({{.25f}, {{1, 1}}, {{5, 1}}});
    const auto expect_rejection = [](auto&& load, const char* what) {
        try {
            load();
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error(what);
    };

    // The filter: accepted on the wider bus, refused with too few inputs or
    // other outputs.
    HostFilter f(8);
    Configuration installed;
    f.load(HostState{x}, six, [&](const HostPlayback& c) { installed = *c.filter; });
    require(installed.input_channels == 2 && test::started(f, six).filter->input_channels == 2 &&
                !f.failure(),
            "a filter with fewer inputs than the host's is refused");
    require(ignored_inputs(x, six) == "inputs 3–6" && ignored_inputs(x, six, "-") == "inputs 3-6" &&
                ignored_inputs(x, {48000, 3, 6}) == "input 3" &&
                ignored_inputs(x, {48000, 2, 6}).empty(),
            "ignored inputs in words");
    auto three = x;
    three.input_channels = 3;
    expect_rejection([&] { HostFilter(8).load(HostState{three}, HostLayout{48000, 2, 6}); },
                     "a filter with more inputs than the host's accepted");
    expect_rejection([&] { HostFilter(8).load(HostState{x}, HostLayout{48000, 6, 2}); },
                     "a filter with other outputs accepted");
    HostFilter stereo_filter(8);
    expect_rejection(
        [&] { stereo_filter.load(HostState{unfiltered_configuration({48000, 2, 2}, false)}, six); },
        "a 2-out filter accepted on 6 outputs");
    require(stereo_filter.error().find("at least its inputs") != std::string::npos,
            "the channel refusal does not explain the rule");

    // What the status, the log and the settings say.
    require(host_status(f, six).text ==
                "Filter loaded: 48000 Hz, 2 in, 6 out; plug-in inputs 3-6 ignored.",
            "status: " + host_status(f, six).text);
    require(host_status(f, six, true, {0, 0}).text.find("dry") == std::string::npos,
            "Mix 0% is said to play the dry signal of a filter that has none");
    require(!dry_signal(f, six) && dry_signal(HostFilter(8), six),
            "the dry signal follows the host's counts, not the filter's");
    const auto playing = playing_words(f, six, {}, false);
    require(playing.find("Inputs 3–6 are ignored: the filter reads inputs 1–2.") !=
                std::string::npos,
            "playing words: " + playing);
    require(loaded_line(f, std::optional(six)).find("Inputs 3–6 are ignored") !=
                    std::string::npos &&
                loaded_line(f, std::optional<HostLayout>()).find("ignored") == std::string::npos,
            "loaded line: " + loaded_line(f, std::optional(six)));
    require(started_line(f, six, 512, nullptr, false).find("Inputs 3–6 are ignored") !=
                std::string::npos,
            "started line");

    // The processor: the extra inputs are read and ignored, so it plays as a
    // 2-input processor does. The filter's counts differ, so there is no dry
    // signal: Mix 0% still plays the filtered sound (fully wet), and bypass
    // plays silence.
    const HostSettings fast{0, 1, 64};
    HostProcessor wide(filter_playback(x), 32, fast, 6), narrow(filter_playback(x), 32, fast);
    std::vector<std::vector<float>> in(6, std::vector<float>(32)), a(6, std::vector<float>(32)),
        b(6, std::vector<float>(32));
    std::mt19937 noise(22);
    std::uniform_real_distribution<float> sample(-.5f, .5f);
    const auto play = [&](HostProcessor& p, std::vector<std::vector<float>>& out,
                          std::size_t inputs) {
        std::vector<const float*> from;
        std::vector<float*> to;
        for (std::size_t c = 0; c < inputs; ++c)
            from.push_back(in[c].data());
        for (auto& o : out)
            to.push_back(o.data());
        audio_thread = true;
        const bool ok = p.process(from, to, 32);
        audio_thread = false;
        require(ok, "a processor refused its inputs");
    };
    float loudest = 0;
    for (int k = 0; k < 12; ++k) {
        for (auto& channel : in)
            for (auto& v : channel)
                v = sample(noise);
        play(wide, a, 6);
        play(narrow, b, 2);
        for (std::size_t o = 0; o < 6; ++o)
            for (std::size_t i = 0; i < 32; ++i) {
                require(a[o][i] == b[o][i], "the ignored inputs changed the output");
                loudest = std::max(loudest, std::abs(a[o][i]));
            }
    }
    require(loudest > .1f, "the filter played nothing");
    wide.set_mix(0);
    narrow.set_mix(0);
    loudest = 0;
    for (int k = 0; k < 6; ++k) {
        for (auto& channel : in)
            for (auto& v : channel)
                v = sample(noise);
        play(wide, a, 6);
        play(narrow, b, 2);
        for (std::size_t o = 0; o < 6; ++o)
            for (std::size_t i = 0; i < 32; ++i) {
                require(a[o][i] == b[o][i], "Mix 0% on a wider bus is not the filtered sound");
                loudest = std::max(loudest, std::abs(a[o][i]));
            }
    }
    require(loudest > .1f, "Mix 0% silenced a filter with no dry signal");
    wide.set_mix(1);
    wide.set_bypass(true);
    for (int k = 0; k < 6; ++k)
        play(wide, a, 6); // Past the 64-frame bypass ramp.
    for (std::size_t o = 0; o < 6; ++o)
        for (std::size_t i = 0; i < 32; ++i)
            require(a[o][i] == 0, "bypass passes input to output on a wider bus");
    wide.set_bypass(false);

    // Replacements may have any input count up to the host's, and must keep
    // the outputs.
    auto one = x;
    one.input_channels = 1;
    one.paths.resize(1);
    static_cast<void>(wide.prepare(filter_playback(one)));
    auto seven = x;
    seven.input_channels = 7;
    expect_rejection([&] { static_cast<void>(wide.prepare(filter_playback(seven))); },
                     "a replacement with more inputs than the host's accepted");
    expect_rejection(
        [&] {
            static_cast<void>(
                wide.prepare(filter_playback(unfiltered_configuration({48000, 6, 2}, false))));
        },
        "a replacement with other outputs accepted");

    // Replacements between a filter with a dry signal and one without, while
    // bypassed, on the six-channel track: an empty instance (identity, so
    // bypass passes input c to output c), then the crossover, then a 6-in,
    // 6-out filter, then the crossover again. The dry signal fades with the
    // filter that has it, so the output never jumps, ends silent under the
    // crossover and plays the input again under the 6x6 filter. Constant
    // input 1: the crossfade (256 frames at 48 kHz) moves at most 1/256 a frame.
    HostProcessor bypassed(filter_playback(unfiltered_configuration(six, false)), 32, fast, 6);
    bypassed.set_bypass(true);
    for (auto& channel : in)
        std::fill(channel.begin(), channel.end(), 1.f);
    std::vector<float> last(6, 0.f);
    const auto settle = [&](float want, const std::string& what) {
        float step = 0;
        for (int k = 0; k < 16; ++k) { // 512 frames: past the 64-frame warm-up and the fade.
            play(bypassed, a, 6);
            for (std::size_t o = 0; o < 6; ++o)
                for (std::size_t i = 0; i < 32; ++i) {
                    step = std::max(step, std::abs(a[o][i] - last[o]));
                    last[o] = a[o][i];
                }
        }
        require(step <= 1.f / 200,
                what + ": the bypassed output jumped by " + std::to_string(step) + " in one frame");
        for (std::size_t o = 0; o < 6; ++o)
            require(a[o][31] == want, what + ": output " + std::to_string(o + 1) + " ends at " +
                                          std::to_string(a[o][31]));
    };
    // The bypass ramp itself (64 frames) starts from silence: let it pass.
    for (int k = 0; k < 4; ++k)
        play(bypassed, a, 6);
    for (std::size_t o = 0; o < 6; ++o)
        last[o] = a[o][31];
    require(a[2][31] == 1, "bypass on an empty instance does not pass the input");
    auto equal = unfiltered_configuration(six, false); // A 6x6 filter with a dry signal.
    for (auto& path : equal.paths)
        for (auto& v : path.impulse)
            v *= .5f;
    bypassed.publish(bypassed.prepare(filter_playback(x)));
    settle(0, "identity to the crossover");
    bypassed.publish(bypassed.prepare(filter_playback(equal)));
    settle(1, "the crossover to a 6x6 filter");
    bypassed.publish(bypassed.prepare(filter_playback(x)));
    settle(0, "a 6x6 filter to the crossover");
    // The same replacements playing (not bypassed) at Mix 0% and 50%: each
    // filter applies Mix by its own rule (the crossover is fully wet, the 6x6
    // filter mixes in the dry signal), and the output crossfades between the
    // two without a gap or dip. Constant input 1: the 6x6 filter plays
    // 0.5 wet, so m * 0.5 + (1 - m); the crossover 1.5 on output 1 and 0.25
    // on output 6.
    for (const float m : {0.f, .5f}) {
        const auto label = "Mix " + std::to_string(static_cast<int>(m * 100)) + "%, ";
        HostProcessor mixing(filter_playback(equal), 32, HostSettings{0, m, 64}, 6);
        for (int k = 0; k < 8; ++k)
            play(mixing, a, 6);
        const float mixed = m * .5f + (1 - m);
        for (std::size_t o = 0; o < 6; ++o) {
            require(std::abs(a[o][31] - mixed) < 1e-6f,
                    label + "the 6x6 filter plays " + std::to_string(a[o][31]));
            last[o] = a[o][31];
        }
        const std::vector<float> crossover{1.5f, 0, 0, 0, 0, .25f}, filtered(6, mixed);
        for (const auto& [next, want, what] :
             {std::tuple{&x, &crossover, "the 6x6 filter to the crossover"},
              std::tuple{&equal, &filtered, "the crossover to the 6x6 filter"}}) {
            mixing.publish(mixing.prepare(filter_playback(*next)));
            float step = 0;
            for (int k = 0; k < 16; ++k) {
                play(mixing, a, 6);
                for (std::size_t o = 0; o < 6; ++o)
                    for (std::size_t i = 0; i < 32; ++i) {
                        step = std::max(step, std::abs(a[o][i] - last[o]));
                        last[o] = a[o][i];
                    }
            }
            require(step <= 1.f / 200, label + what + ": the output jumped by " +
                                           std::to_string(step) + " in one frame");
            for (std::size_t o = 0; o < 6; ++o)
                require(std::abs(a[o][31] - (*want)[o]) < 1e-6f,
                        label + what + ": output " + std::to_string(o + 1) + " ends at " +
                            std::to_string(a[o][31]));
        }
    }
    // Bypass off and on again under the crossover: still silent.
    bypassed.set_bypass(false);
    play(bypassed, a, 6);
    bypassed.set_bypass(true);
    for (int k = 0; k < 4; ++k)
        play(bypassed, a, 6);
    for (const auto& channel : a)
        for (auto v : channel)
            require(v == 0, "bypass passes input under the crossover after a toggle");

    // jrp, 7 November 2026: with no filter playing, a kept filter's counts
    // decide too, and a failed load with no filter kept has no dry signal.
    // The crossover kept at the wrong rate on the six-channel track: silent,
    // bypassed or not, at any Mix.
    const HostLayout slower{44100, 6, 6};
    HostFilter kept(8);
    kept.load(HostState{x}, std::nullopt);
    const auto silent_crossover = test::started(kept, slower);
    require(kept.failure() && kept.failure()->silent &&
                dry_signal_rule(kept, slower) == HostDrySignal::filter_counts &&
                !silent_crossover.dry && silent_crossover.filter->output_channels == 6,
            "a kept crossover that does not suit the host has a dry signal");
    require(host_status(kept, slower, true, {0, .5}).text.rfind("Silent, waiting for ", 0) == 0,
            "status: " + host_status(kept, slower, true, {0, .5}).text);
    require(failure_consequence(*kept.failure(), &slower, {0, .5}, dry_signal(kept, slower),
                                false) == "Convolver plays silence.",
            "a kept crossover is said to play the dry signal");
    for (const float m : {0.f, 1.f}) {
        HostProcessor q(silent_crossover, 32, HostSettings{0, m, 64}, 6);
        q.set_bypass(m == 1);
        for (int k = 0; k < 4; ++k) {
            play(q, a, 6);
            for (const auto& channel : a)
                for (auto v : channel)
                    require(v == 0, "a kept crossover that does not suit the host passes input");
        }
    }
    // A kept 6x6 filter at the wrong rate keeps its dry signal.
    HostFilter kept_equal(8);
    kept_equal.load(HostState{equal}, std::nullopt);
    const auto silent_equal = test::started(kept_equal, slower);
    require(dry_signal(kept_equal, slower) && silent_equal.dry,
            "a kept 6x6 filter that does not suit the host has no dry signal");
    // A failed load with no filter kept (here, an unreadable state; a missing
    // file is the same): no dry signal, whatever the counts.
    HostFilter failed(8);
    failed.reject(HostFailureKind::filter_file, {"The filter file could not be read."});
    require(dry_signal_rule(failed, six) == HostDrySignal::failed_load &&
                !test::started(failed, six).dry && !unfiltered_playback(six, true, false).dry &&
                unfiltered_playback(six, true, true).dry &&
                !unfiltered_playback({48000, 2, 6}, false, true).dry,
            "a failed load with no filter kept has a dry signal");
    // jrp, 7 November 2026 (19:42): the same with one input and one output.
    HostFilter failed_mono(8);
    failed_mono.reject(HostFailureKind::filter_file, {"The filter file could not be read."});
    const HostLayout mono_host{48000, 1, 1};
    require(dry_signal_rule(failed_mono, mono_host) == HostDrySignal::failed_load &&
                !test::started(failed_mono, mono_host).dry,
            "a failed mono load has a dry signal");
    {
        HostProcessor q(test::started(failed_mono, mono_host), 32, HostSettings{0, .5, 64}, 1);
        q.set_bypass(true);
        std::vector<std::vector<float>> ones(1, std::vector<float>(32, 1.f));
        for (int k = 0; k < 4; ++k) {
            for (auto& v : in[0])
                v = 1.f;
            play(q, ones, 1);
            for (auto v : ones[0])
                require(v == 0, "a failed mono load passes its input bypassed");
        }
    }
    // An untouched mono instance still passes its input.
    HostFilter untouched_mono(8);
    require(dry_signal(untouched_mono, mono_host) && test::started(untouched_mono, mono_host).dry,
            "an untouched mono instance has no dry signal");
    require(host_status(failed, six, true, {0, .5}).text.rfind("Silent: ", 0) == 0,
            "status: " + host_status(failed, six, true, {0, .5}).text);
    // An empty instance that fails its first load while bypassed (the
    // adapters' silence()): the dry signal ramps out with the filter, and a
    // filter loaded afterwards brings it back by its own rule, never jumping.
    for (const float m : {1.f, 0.f}) {
        const auto label = std::string(m == 1 ? "bypassed" : "Mix 0%") + ", ";
        HostProcessor empty(filter_playback(unfiltered_configuration(six, false)), 32,
                            HostSettings{0, m, 64}, 6);
        empty.set_bypass(m == 1);
        for (int k = 0; k < 4; ++k)
            play(empty, a, 6);
        for (std::size_t o = 0; o < 6; ++o) {
            require(a[o][31] == 1, label + "an empty instance does not pass the input");
            last[o] = 1;
        }
        const auto settle_on = [&](float want, const std::string& what) {
            float step = 0;
            for (int k = 0; k < 16; ++k) {
                play(empty, a, 6);
                for (std::size_t o = 0; o < 6; ++o)
                    for (std::size_t i = 0; i < 32; ++i) {
                        step = std::max(step, std::abs(a[o][i] - last[o]));
                        last[o] = a[o][i];
                    }
            }
            require(step <= 1.f / 32, label + what + ": the output jumped by " +
                                          std::to_string(step) + " in one frame");
            for (std::size_t o = 0; o < 6; ++o)
                require(a[o][31] == want, label + what + ": output " + std::to_string(o + 1) +
                                              " ends at " + std::to_string(a[o][31]));
        };
        empty.silence();
        settle_on(0, "a failed first load");
        empty.publish(empty.prepare(filter_playback(equal)));
        settle_on(m == 1 ? 1.f : .5f * m + (1 - m), "a 6x6 filter after the failure");
    }
}
// mute(), the audio thread's own silence: the current filter ramps to 0 over
// 64 frames and stays silent until a published filter takes over and fades in
// from silence. Its dry signal goes with it, with one output too (PLUG-23,
// jrp, 7 November 2026); a reset keeps the silence.
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
    HostProcessor host(filter_playback(mono(1)), 512, 8);
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
    // A failed first load passes nothing, not even the dry signal of a
    // filter with one output (PLUG-23, jrp, 7 November 2026).
    host.set_bypass(true);
    run(host, 256);
    require(silent_from(0, 256), "bypass while muted passes the input");
    host.set_bypass(false);
    run(host, 256);
    require(silent_from(0, 256), "muted output after bypass not silent");
    audio_thread = true;
    host.reset();
    audio_thread = false;
    run(host, 64);
    require(silent_from(0, 64), "a reset ended the silence");
    run(host, 64);
    // A replacement warms up while the muted filter plays (silence), then fades in.
    host.replace(filter_playback(mono(2)));
    run(host, 512);
    require(silent_from(0, 8), "replacement after mute: warm-up not silent");
    bool rising = true;
    for (std::size_t i = 9; i < 264; ++i)
        rising = rising && output[i] >= output[i - 1] - 1e-6f && output[i] <= 2.25f + 1e-5f;
    require(rising && std::abs(output[511] - 2.25f) < 1e-5,
            "replacement after mute does not fade in from silence");
    host.collect();
    // Muted during a crossfade: the old filter fades out, the new is silent.
    HostProcessor fading(filter_playback(mono(1)), 512, 8);
    run(fading, 64);
    fading.replace(filter_playback(mono(2)));
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
    fading.replace(filter_playback(mono(3)));
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
        HostProcessor early(filter_playback(mono(1)), 512, 8);
        if (started)
            run(early, 64);
        early.replace(filter_playback(mono(2)));
        audio_thread = true;
        early.mute();
        audio_thread = false;
        run(early, 512);
        require(silent_from(started ? 300 : 0, 512),
                "a filter published before mute() was heard when picked up");
        early.replace(filter_playback(mono(3)));
        run(early, 512);
        require(std::abs(output[511] - 3.25f) < 1e-5, "a filter published after mute() is silent");
        early.collect();
    }
    // mute(through): what was published after the count stays audible.
    HostProcessor counted(filter_playback(mono(1)), 512, 8);
    run(counted, 64);
    audio_thread = true;
    const auto seen = counted.publications();
    audio_thread = false;
    counted.replace(filter_playback(mono(2)));
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
// ROBUST-3: silence(), the control side's mute for a failed first load,
// allocates nothing, so it works when memory has run out. The audio thread
// applies it at its next call: silent from the first frame before any audio
// (no ramp lets the input through), a 64-frame ramp while audio plays. A
// filter published before it stays silent; one published after it plays.
// mute() before any audio is silent at once too.
void control_silence() {
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
    // Any allocation throws, which ends the test from a noexcept function.
    const auto starved_silence = [](HostProcessor& host) {
        allocations_left = 0;
        host.silence();
        allocations_left = -1;
    };
    HostProcessor fresh(filter_playback(mono(1)), 512, 8);
    starved_silence(fresh);
    run(fresh, 128);
    require(silent_from(0, 128), "silence() before any audio let the input through");
    HostProcessor muted(filter_playback(mono(1)), 512, 8);
    audio_thread = true;
    muted.mute();
    audio_thread = false;
    run(muted, 128);
    require(silent_from(0, 128), "mute() before any audio let the input through");
    HostProcessor playing(filter_playback(mono(1)), 512, 8);
    run(playing, 64);
    starved_silence(playing);
    run(playing, 128);
    for (std::size_t i = 0; i < 64; ++i)
        require(std::abs(output[i] - 1.25f * (1 - static_cast<float>(i + 1) / 64)) < 1e-5,
                "silence() does not ramp over 64 frames while audio plays");
    require(silent_from(63, 128), "silence() not exactly silent");
    for (const bool started : {false, true}) {
        HostProcessor host(filter_playback(mono(1)), 512, 8);
        if (started)
            run(host, 64);
        host.replace(filter_playback(mono(2)));
        starved_silence(host);
        run(host, 512);
        require(silent_from(started ? 300 : 0, 512),
                "a filter published before silence() was heard when picked up");
        host.replace(filter_playback(mono(3)));
        run(host, 512);
        require(std::abs(output[511] - 3.25f) < 1e-5,
                "a filter published after silence() is silent");
        host.collect();
    }
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
            const std::function<void(const HostPlayback&)> install = [&](const HostPlayback& next) {
                audible = sound(*next.filter);
            };
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
                require(audible == before && (f.filter() != nullptr) == c.before.has_value() &&
                            f.failed() == !c.before,
                        (what + "a rejected load changed the previous state, or a refused "
                                "first load did not fail the instance (ROBUST-7)")
                            .c_str());
        }
    }
    // The notice is kept with the filter it describes.
    HostFilter f(8);
    f.load(HostState{filter(2)}, host, [](const HostPlayback&) {});
    require(!f.notice().empty(), "speaker notice not kept");
}
// ROBUST-7: a failure is first recorded without allocating, so a failed first
// load fails the instance (silence, saved as failed) however early memory
// runs out. reject() then keeps its kind, request number and message without
// the path; load() refused before even its message could be built keeps a
// record without one, which failure_message() explains. An accepted filter
// is kept, and the instance does not fail.
void unrecorded_failures() {
    const HostLayout host{48000, 2, 2};
    const Configuration stereo{48000, 2, 2, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    const std::function<void(const HostPlayback&)> install = [](const HostPlayback&) {};
    for (const bool kept : {false, true}) {
        const std::string label = kept ? " over an accepted filter" : " as a first load";
        HostFilter f(8);
        if (kept)
            f.load(HostState{stereo}, host, install);
        Advice why{"the filter file could not be read, and this sentence is long"};
        HostRequest request{HostFilterSource::file, "/a/folder/with/a/long/name/filter.wav", 0};
        const auto expected = why;
        allocations_left = 0;
        f.reject(HostFailureKind::filter_file, std::move(why), std::move(request));
        allocations_left = -1;
        const auto& r = f.failure();
        require(f.failed() == !kept && (f.filter() != nullptr) == kept && r &&
                    r->kind == HostFailureKind::filter_file && r->silent == !kept &&
                    r->request.source == HostFilterSource::file && r->request.number > 0 &&
                    r->request.path.empty() && r->message == expected,
                ("reject() with no memory" + label).c_str());

        HostFilter g(8);
        if (kept)
            g.load(HostState{stereo}, host, install);
        Configuration nine{48000, 9, 9, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
        HostState refused{std::move(nine)};
        bool threw = false;
        allocations_left = 0;
        try {
            g.load(std::move(refused), host, install);
        } catch (...) {
            threw = true;
        }
        allocations_left = -1;
        const auto& q = g.failure();
        require(threw && g.failed() == !kept && (g.filter() != nullptr) == kept && q &&
                    q->kind == HostFailureKind::preparation && q->silent == !kept &&
                    q->message.empty() &&
                    failure_message(*q).problem.find("memory ran out") != std::string::npos,
                ("load() refused with no memory" + label).c_str());
        const auto status = host_status(g, host);
        require(status.status == HostStatus::load_failed,
                ("status after load() refused with no memory" + label).c_str());
    }
}

// PLUG-12: a filter is accepted only if its saved state fits. Loading a
// filter one sample over the limit is refused before anything is installed,
// keeping the previous filter and what it saves byte for byte (or, for an
// empty instance, a failed first load that saves); one exactly at the limit
// loads and saves. jrp's review of 6920a45: a 16,777,216-sample WAV loaded
// and then could not be saved. PLUG-13: the limit counts the settings
// whether or not the state holds them yet; jrp's review of 276c7dc loaded a
// filter that filled the limit at default settings, changed Gain, and then
// could not save.
void saved_state_limit() {
    using Kind = HostFailureKind;
    // encoded_host_state_size() counts what encode_host_state() writes.
    Configuration routed{48000, 2, 3, 0x7, {1, 2}, {0, 4, 9}, {}};
    routed.paths.push_back({{1, .5f, .25f}, {{0, 1}, {1, .5f}}, {{2, 1}}});
    routed.paths.push_back({{-1}, {{1, 1}}, {{0, .5f}, {1, .25f}, {2, 1}}});
    HostState failed;
    failed.failed = true;
    HostState bypassed{mono()};
    bypassed.bypass = true;
    for (const auto& state : {HostState{}, failed, bypassed, HostState{routed}})
        require(encoded_host_state_size(state) == encode_host_state(state).size(),
                "encoded_host_state_size differs from the encoding");
    // One mono path: 17 words besides its samples, and the room kept for
    // the settings (PLUG-13), so this many fill the limit.
    constexpr std::size_t at_limit = (max_host_state_bytes - host_settings_room_bytes) / 4 - 17;
    const auto filter = [](std::size_t samples) {
        Configuration c{48000, 1, 1, 0, {}, {}, {}};
        c.paths.push_back({std::vector<float>(samples, 0.f), {{0, 1}}, {{0, 1}}});
        c.paths[0].impulse[0] = 1;
        return c;
    };
    const HostSettings quieter{-6, 1, default_host_latency};
    const auto with_settings = [&](std::size_t samples) {
        HostState s{filter(samples)};
        s.settings = quieter;
        return s;
    };
    require(encoded_host_state_size(HostState{filter(at_limit)}) ==
                    max_host_state_bytes - host_settings_room_bytes &&
                encoded_host_state_size(with_settings(at_limit)) ==
                    max_host_state_bytes - host_settings_room_bytes + 20,
            "the boundary filter does not leave exactly the settings' room");
    const HostLayout host{48000, 1, 1};
    std::size_t installed = 0;
    const std::function<void(const HostPlayback&)> install = [&](const HostPlayback& p) {
        installed = p.filter->paths.at(0).impulse.size();
    };
    const auto refused_over = [&](HostFilter& f) {
        installed = 0;
        try {
            f.load(HostState{filter(at_limit + 1)}, host, install);
        } catch (const std::invalid_argument& e) {
            require(std::string(e.what()).find("too large to save") != std::string::npos,
                    "the refusal does not explain the size limit");
            require(!installed, "a filter too large to save was installed");
            return;
        }
        throw std::runtime_error("a filter too large to save was accepted");
    };
    // Over a working filter: refused, the filter and its saved bytes kept.
    HostFilter kept(8);
    kept.load(HostState{mono(.5f)}, host, install);
    const auto before = encode_host_state(saved_of(kept));
    refused_over(kept);
    require(kept.failure() && kept.failure()->kind == Kind::too_large && !kept.failure()->silent &&
                kept.failure()->inputs == 1 && kept.failure()->outputs == 1,
            "the refusal's failure record is wrong");
    require(!kept.failed() && encode_host_state(saved_of(kept)) == before,
            "a refused oversized filter changed the saved state");
    // Without the host's configuration, the check still runs at once.
    try {
        kept.load(HostState{filter(at_limit + 1)}, std::nullopt);
        throw std::runtime_error("an oversized filter was accepted before activation");
    } catch (const std::invalid_argument&) {
    }
    require(encode_host_state(saved_of(kept)) == before,
            "an oversized filter changed the saved state before activation");
    // An empty instance: a failed first load, which saves.
    HostFilter empty(8);
    refused_over(empty);
    require(empty.failed() && !empty.filter() && empty.failure()->silent,
            "an oversized first filter is not a failed first load");
    HostState saved_failure;
    saved_failure.failed = true;
    require(encode_host_state(saved_failure).size() == 12, "the failed first load does not save");
    // Exactly at the limit: accepted and installed at default settings; it
    // saves without them and, after a setting changes, with them, and both
    // read back.
    empty.load(HostState{filter(at_limit)}, host, install);
    require(installed == at_limit && !empty.failure(), "the filter at the limit was refused");
    require(encode_host_state(saved_of(empty)).size() ==
                max_host_state_bytes - host_settings_room_bytes,
            "the filter at the limit did not save at default settings");
    HostState changed = saved_of(empty);
    changed.settings = quieter;
    const auto bytes = encode_host_state(changed);
    require(bytes.size() == max_host_state_bytes - host_settings_room_bytes + 20,
            "the filter at the limit did not save after a setting changed");
    const auto restored = decode_host_state(bytes);
    require(restored.configuration->paths.at(0).impulse.size() == at_limit &&
                restored.settings == quieter,
            "the filter at the limit does not read back with its settings");
    // A state that already holds settings (a VST3 project) is checked the same
    // way: at the limit it is accepted, one sample more is refused.
    HostFilter restoring(8);
    restoring.load(with_settings(at_limit), host, install);
    require(!restoring.failure(), "a state with settings at the limit was refused");
    try {
        restoring.load(with_settings(at_limit + 1), host, install);
        throw std::runtime_error("a state with settings over the limit was accepted");
    } catch (const std::invalid_argument& e) {
        require(std::string(e.what()).find("too large to save") != std::string::npos,
                "the refusal with settings does not explain the size limit");
    }
}
// CONVOLVER_LOG, read and set wide on Windows, as host_log_path() reads it,
// since temporary folders may hold characters outside the ANSI code page.
std::optional<std::filesystem::path> log_variable() {
#ifdef _WIN32
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, L"CONVOLVER_LOG") == 0 && value) {
        std::filesystem::path copy(value);
        std::free(value);
        return copy;
    }
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
// PLUG-38: HostFilter::load() marks a filter as the one playing only when it
// is the accepted filter, playing, and equal in every stored value (a float's
// bits, so a signed-zero route gain differs); HostProcessor::keep() then
// keeps the processor, with the new gains, unless it met a fault.
void same_filter_kept() {
    const HostLayout layout{48000, 1, 1};
    const Configuration a = mono(.5f);
    std::optional<bool> playing;
    const auto install = [&](const HostPlayback& c) { playing = c.playing; };
    const auto loads = [&](HostFilter& f, const Configuration& c, const HostLayout& host,
                           HostLoadCheck check = HostLoadCheck::refuse_mismatch) {
        playing.reset();
        f.load(HostState{c}, host, install, {}, nullptr, check);
        return playing.value_or(false);
    };
    HostFilter f(8);
    require(!loads(f, a, layout), "a first load counted as the filter playing");
    require(loads(f, a, layout), "the same filter again not counted as the one playing");
    auto nudged = a;
    nudged.paths[0].impulse[0] = std::nextafter(.5f, 1.f);
    require(!loads(f, nudged, layout), "a filter one sample apart counted as the same");
    require(loads(f, nudged, layout), "the nudged filter again not counted as playing");
    auto zero = a, negative_zero = a;
    zero.paths[0].outputs[0].gain = 0.f;
    negative_zero.paths[0].outputs[0].gain = -0.f;
    loads(f, zero, layout);
    require(!loads(f, negative_zero, layout), "a signed-zero route gain counted as the same");
    auto delayed = negative_zero;
    delayed.output_delays = {1};
    require(!loads(f, delayed, layout), "a delayed filter counted as the same");
    // A refused request leaves the filter playing, so it still counts.
    bool refused = false;
    try {
        f.load(HostState{mono(.5f)}, HostLayout{44100, 1, 1}, install);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && loads(f, delayed, layout), "the filter after a refusal not counted");
    // A filter kept silent because it does not suit the host does not play.
    HostFilter kept(8);
    const HostLayout other{44100, 1, 1};
    loads(kept, a, other, HostLoadCheck::keep_until_suited);
    require(!loads(kept, a, other, HostLoadCheck::keep_until_suited),
            "a filter kept silent counted as playing");
    test::started(kept, layout);
    require(loads(kept, a, layout), "a filter playing after activation not counted");

    HostProcessor host(filter_playback(a), 64, 8);
    auto same = filter_playback(a);
    require(!host.keep(same), "kept without HostFilter saying it plays");
    same.playing = true;
    same.gains = FilterGains{-3.0, -1.0};
    require(host.keep(same), "the playing filter not kept");
    // A processor that met a fault starts afresh, as before.
    const auto huge = std::numeric_limits<float>::max() / 4;
    const Configuration loud = mono(huge);
    HostProcessor faulty(filter_playback(loud), 64, 8);
    std::array<float, 64> data{};
    data[0] = huge;
    const float* in[] = {data.data()};
    float* out[] = {data.data()};
    audio_thread = true;
    faulty.process(in, out, 64);
    audio_thread = false;
    auto loud_playing = filter_playback(loud);
    loud_playing.playing = true;
    require(!faulty.keep(loud_playing), "a processor that met a fault was kept");
    faulty.clear_faults();
    require(faulty.keep(loud_playing), "a processor cleared of its faults not kept");
}
// PLUG-42 (jrp, 21 November 2026): with a host whose configuration is
// settled now but may change at a later start (HostLoadCheck::
// keep_unless_one_plays: VST3 and the Audio Unit while the host plays), a
// filter that does not suit it is refused only while a suiting filter
// plays. jrp's two tests: a waiting filter starts when the settings fit
// (Ardour 8.4 restarting the plug-in with new outputs a little after a
// preset loaded), and a refused replacement leaves the working filter
// selected across a restart.
void kept_unless_one_plays() {
    const HostLayout stereo{48000, 2, 2}, six{48000, 2, 6};
    const Configuration two{48000, 2, 2, 0, {}, {}, {{{.5f}, {{0, 1}}, {{0, 1}}}}};
    const Configuration crossover{48000, 2, 6, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    const auto refused = [](auto&& load) {
        try {
            load();
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    const auto check = HostLoadCheck::keep_unless_one_plays;
    // 1. Nothing plays (an empty instance): the crossover is kept, silent
    // with the reason, saved as the filter, and plays at the next start
    // with the outputs it was made for.
    {
        HostFilter f(8);
        static_cast<void>(test::started(f, stereo));
        f.load(HostState{crossover}, stereo, {}, {}, nullptr, check);
        require(f.kept_unsuited() && !f.plays_suiting() && !f.rejection(),
                "a filter refused with nothing playing");
        require(f.filter() && f.filter()->output_channels == 6 && f.failure() &&
                    f.failure()->silent && f.failure()->kind == HostFailureKind::channels,
                "the kept filter is not silent with its reason");
        require(!f.failed() && saved_of(f).configuration &&
                    saved_of(f).configuration->output_channels == 6,
                "the kept filter not saved as the filter");
        require(!f.awaiting(), "a settled host's check counted as awaiting validation");
        const auto plays = test::started(f, six);
        require(!f.failure() && plays.filter && plays.filter->output_channels == 6,
                "the kept filter does not play at a start that suits it");
    }
    // A filter that does not suit the host after a restart plays nothing
    // either, so the latest choice replaces it.
    {
        HostFilter f(8);
        f.load(HostState{two}, stereo);
        static_cast<void>(test::started(f, six));
        require(f.failure() && f.failure()->silent, "a filter that does not suit plays");
        auto quieter = two;
        quieter.paths[0].impulse[0] = .25f;
        require(!refused([&] { f.load(HostState{quieter}, six, {}, {}, nullptr, check); }) &&
                    f.filter() && f.failure() && f.failure()->silent,
                "a choice refused while the earlier filter played silence");
        require(f.filter()->paths[0].impulse[0] == .25f, "the latest choice not kept");
        static_cast<void>(test::started(f, stereo));
        require(!f.failure(), "the latest choice does not play when the settings fit");
    }
    // 2. A suiting filter plays: the crossover is refused, and the playing
    // filter stays selected across a restart, also at the refused filter's
    // outputs, and is what is saved.
    {
        HostFilter f(8);
        f.load(HostState{two}, stereo);
        static_cast<void>(test::started(f, stereo));
        require(refused([&] { f.load(HostState{crossover}, stereo, {}, {}, nullptr, check); }),
                "a replacement that does not suit accepted while a filter plays");
        require(f.rejection() && !f.rejection()->silent, "the refusal not recorded");
        require(text_of(f.rejection()->message)
                        .find("The filter Convolver has now stays selected when the host restarts "
                              "the plug-in, even with settings that suit this one") !=
                    std::string::npos,
                "the refusal does not say the playing filter stays selected");
        const auto again = test::started(f, stereo);
        require(f.filter() && f.filter()->output_channels == 2 && again.filter &&
                    again.filter->output_channels == 2 && f.failure() && !f.failure()->silent,
                "a restart with the same settings switched filters");
        static_cast<void>(test::started(f, six));
        require(f.filter() && f.filter()->output_channels == 2,
                "a restart with the refused filter's outputs switched to it");
        require(saved_of(f).configuration && saved_of(f).configuration->output_channels == 2,
                "the refused filter saved");
    }
    // jrp's review of #258: on a running empty six-channel instance at
    // Latency 4096, a kept wrong-rate crossover left output 6 at full level
    // for the new playback's warm-up (90.6 ms) while Status said Silent; a
    // refusal on main muted it within 64 frames. What played mutes at once,
    // as for a failed first load; the silent playback then takes over.
    {
        const HostLayout wide{48000, 6, 6};
        HostSettings settings;
        settings.latency = 4096;
        HostProcessor p(unfiltered_playback(wide, false, true), 512, settings);
        std::vector<std::vector<float>> data(6, std::vector<float>(512, 1.f));
        std::vector<const float*> in;
        std::vector<float*> out;
        for (auto& c : data) {
            in.push_back(c.data());
            out.push_back(c.data());
        }
        const auto block = [&] {
            for (auto& c : data)
                std::fill(c.begin(), c.end(), 1.f);
            audio_thread = true;
            const bool ok =
                p.process(std::span<const float* const>(in), std::span<float* const>(out), 512);
            audio_thread = false;
            require(ok, "a block refused");
        };
        for (int k = 0; k < 10; ++k) // Past the latency: the input passes.
            block();
        require(std::abs(data[5][511] - 1.f) < 1e-5f, "the empty instance does not pass its input");
        HostFilter f(8);
        auto wrong_rate = Configuration{44100, 2, 6, 0, {}, {}, {{{1}, {{0, 1}}, {{5, 1}}}}};
        f.load(
            HostState{wrong_rate}, wide, [&](const HostPlayback& c) { p.play(c); }, {}, &p, check);
        require(f.failure() && f.failure()->silent, "the crossover not kept silent");
        block();
        for (std::size_t i = 64; i < 512; ++i)
            require(data[5][i] == 0.f, "output 6 not muted within 64 frames of the kept filter");
        for (int k = 0; k < 10; ++k) // Past the silent playback's warm-up.
            block();
        require(data[5][511] == 0.f && data[0][511] == 0.f, "the kept filter is not silent");
        // The audio thread may adopt the silent playback before silence()
        // reaches it: the input it is fading out mutes all the same.
        HostProcessor q(unfiltered_playback(wide, false, true), 512, settings);
        const auto q_block = [&] {
            for (auto& c : data)
                std::fill(c.begin(), c.end(), 1.f);
            audio_thread = true;
            const bool ok =
                q.process(std::span<const float* const>(in), std::span<float* const>(out), 512);
            audio_thread = false;
            require(ok, "a block refused");
        };
        for (int k = 0; k < 10; ++k)
            q_block();
        const auto before = q.publications();
        q.play(unfiltered_playback(wide, true, true));
        q_block(); // Adopted: the input still plays while the new one warms up.
        require(std::abs(data[5][511] - 1.f) < 1e-5f, "the warm-up did not play the input");
        q.silence(before);
        q_block();
        for (std::size_t i = 64; i < 512; ++i)
            require(data[5][i] == 0.f, "the faded-out input not muted by silence(through)");
    }
    // The independent review of #258: silence() during a replacement's
    // warm-up mutes the filter still playing as well as the replacement.
    {
        const HostLayout pair{48000, 2, 2};
        HostSettings settings;
        settings.latency = 4096;
        HostProcessor p(unfiltered_playback(pair, false, true), 512, settings);
        std::vector<std::vector<float>> data(2, std::vector<float>(512, 1.f));
        std::vector<const float*> in{data[0].data(), data[1].data()};
        std::vector<float*> out{data[0].data(), data[1].data()};
        const auto block = [&] {
            for (auto& c : data)
                std::fill(c.begin(), c.end(), 1.f);
            audio_thread = true;
            const bool ok =
                p.process(std::span<const float* const>(in), std::span<float* const>(out), 512);
            audio_thread = false;
            require(ok, "a block refused");
        };
        for (int k = 0; k < 10; ++k)
            block();
        p.play(unfiltered_playback(pair, false, true));
        block(); // Adopted: the first one plays while the replacement warms up.
        require(std::abs(data[0][511] - 1.f) < 1e-5f, "the warm-up did not play the input");
        p.silence();
        block();
        for (std::size_t i = 64; i < 512; ++i)
            require(data[0][i] == 0.f, "silence() during a warm-up left the playing filter");
    }
    // The independent review of #258: a kept filter replacing one kept
    // before (a host restoring the same preset again, as GarageBand does)
    // mutes nothing: bypass, and Mix's dry share, play on without a gap.
    for (const bool bypassed : {true, false}) {
        const HostLayout pair{48000, 2, 2};
        HostSettings settings;
        settings.latency = 4096;
        settings.mix = bypassed ? 1 : 0.5;
        HostProcessor p(unfiltered_playback(pair, false, true), 512, settings);
        std::vector<std::vector<float>> data(2, std::vector<float>(512, 1.f));
        std::vector<const float*> in{data[0].data(), data[1].data()};
        std::vector<float*> out{data[0].data(), data[1].data()};
        const float heard = bypassed ? 1.f : 0.5f;
        const auto what = std::string(bypassed ? "bypass" : "Mix 50%");
        const auto block = [&] {
            for (auto& c : data)
                std::fill(c.begin(), c.end(), 1.f);
            audio_thread = true;
            p.set_bypass(bypassed);
            const bool ok =
                p.process(std::span<const float* const>(in), std::span<float* const>(out), 512);
            audio_thread = false;
            require(ok, "a block refused");
        };
        HostFilter f(8);
        const auto load = [&](float gain) {
            auto slow = Configuration{44100, 2, 2, 0, {}, {}, {{{gain}, {{0, 1}}, {{0, 1}}}}};
            f.load(HostState{slow}, pair, [&](const HostPlayback& c) { p.play(c); }, {}, &p, check);
            require(f.failure() && f.failure()->silent, what + ": the 44.1 kHz filter not kept");
        };
        load(1);
        for (int k = 0; k < 20; ++k) // Past the kept playback's warm-up.
            block();
        require(std::abs(data[0][511] - heard) < 1e-5f && std::abs(data[1][511] - heard) < 1e-5f,
                what + ": the kept filter does not pass its dry signal");
        for (const float gain : {1.f, .25f}) { // The same filter again, then another.
            load(gain);
            for (int k = 0; k < 20; ++k) {
                block();
                for (std::size_t i = 0; i < 512; ++i)
                    require(std::abs(data[0][i] - heard) < 1e-5f &&
                                std::abs(data[1][i] - heard) < 1e-5f,
                            what + ": a kept filter replacing a kept one cut the dry signal");
            }
        }
    }
    // Codex's review of #258: a kept filter with no dry signal (a 2-to-6
    // crossover on six channels) replacing a kept one that had one (6 to 6)
    // mutes the bypassed input at once, rather than for the warm-up.
    {
        const HostLayout wide{48000, 6, 6};
        HostSettings settings;
        settings.latency = 4096;
        HostProcessor p(unfiltered_playback(wide, false, true), 512, settings);
        std::vector<std::vector<float>> data(6, std::vector<float>(512, 1.f));
        std::vector<const float*> in;
        std::vector<float*> out;
        for (auto& c : data) {
            in.push_back(c.data());
            out.push_back(c.data());
        }
        const auto block = [&] {
            for (auto& c : data)
                std::fill(c.begin(), c.end(), 1.f);
            audio_thread = true;
            p.set_bypass(true);
            const bool ok =
                p.process(std::span<const float* const>(in), std::span<float* const>(out), 512);
            audio_thread = false;
            require(ok, "a block refused");
        };
        HostFilter f(8);
        const auto load = [&](const Configuration& c) {
            f.load(HostState{c}, wide, [&](const HostPlayback& c) { p.play(c); }, {}, &p, check);
            require(f.failure() && f.failure()->silent, "the 44.1 kHz filter not kept");
        };
        std::vector<FilterPath> six_paths;
        for (std::size_t c = 0; c < 6; ++c)
            six_paths.push_back({{1}, {{c, 1}}, {{c, 1}}});
        load(Configuration{44100, 6, 6, 0, {}, {}, six_paths});
        for (int k = 0; k < 20; ++k)
            block();
        require(std::abs(data[5][511] - 1.f) < 1e-5f, "the kept 6-to-6 filter not bypassed");
        load(Configuration{44100, 2, 6, 0, {}, {}, {{{1}, {{0, 1}}, {{5, 1}}}}});
        block();
        for (std::size_t i = 64; i < 512; ++i)
            require(data[5][i] == 0.f, "a kept crossover left the bypassed input playing");
    }
    // LV2 (refuse_mismatch): its layout never changes, so a filter that does
    // not suit is refused even with nothing playing.
    {
        HostFilter f(8);
        static_cast<void>(test::started(f, stereo));
        require(refused([&] { f.load(HostState{crossover}, stereo); }) && !f.filter() &&
                    f.failure() && f.failure()->silent,
                "refuse_mismatch kept a filter that does not suit");
    }
}
// PLUG-39 step 1b: HostFilter::load() decides what an accepted request
// changes besides the filter (HostChange), from the request's kind and the
// state, so that no adapter decides it again (the scope table in
// docs/develop/plugin-contract.md#scope-of-each-change).
void request_changes() {
    const HostLayout layout{48000, 1, 1};
    const Configuration a = mono(.5f);
    std::optional<bool> playing;
    const auto install = [&](const HostPlayback& c) { playing = c.playing; };
    HostRequest session;
    session.kind = HostRequestKind::session;
    HostRequest chosen{HostFilterSource::file, "chosen.cfg", 0};
    chosen.kind = HostRequestKind::chosen_file;
    HostFilter f(8);
    // Restored state brings its bypass; without settings it leaves them.
    HostState restored{a};
    restored.bypass = true;
    auto change = f.load(restored, layout, install);
    require(change.bypass == true && !change.settings, "restored state's bypass not handed back");
    // A session the host is opening replaces even the filter already playing.
    playing.reset();
    change = f.load(HostState{a}, layout, install, session);
    require(playing == false && change.bypass == false, "a session kept the playing filter");
    // A chosen file keeps it, never brings a bypass, and brings a preset's
    // settings when it holds them.
    HostState preset{a};
    preset.bypass = true;
    preset.settings = HostSettings{-6, .5, 256, GainMode::set};
    playing.reset();
    change = f.load(preset, layout, install, chosen);
    require(playing == true && !change.bypass && change.settings &&
                change.settings->gain_db == -6 && change.settings->latency == 256,
            "a chosen preset's settings not handed back, or its bypass was");
    // The settings' Gain mode decides whether the load finds the estimate,
    // only once it is accepted: a refused request leaves estimating() as it was.
    HostState estimate{a};
    estimate.settings = HostSettings{0, 1, 0, GainMode::estimate};
    bool refused = false;
    try {
        f.load(estimate, HostLayout{44100, 1, 1}, install, chosen);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && !f.estimating() && !f.gains().estimate_db,
            "a refused request changed estimating()");
    f.load(estimate, layout, install, chosen);
    require(f.estimating() && f.gains().estimate_db, "settings with Estimated found no estimate");
    f.load(HostState{a}, layout, install);
    require(f.estimating(), "a request without settings changed estimating()");
    // The tail the adapters report while no processor plays: the accepted
    // filter's, kept by a request for the same filter and by a refused one.
    f.load(HostState{a}, std::nullopt);
    require(f.filter_tail() > 0 && f.filter_tail() == configuration_tail_frames(a),
            "the tail while inactive: " + std::to_string(f.filter_tail()));
    Configuration longer = a;
    longer.paths[0].impulse.resize(100);
    require(configuration_tail_frames(longer) != configuration_tail_frames(a),
            "the longer filter's tail is not different");
    bool refused_longer = false;
    try {
        f.load(HostState{longer}, HostLayout{44100, 1, 1}, install, chosen);
    } catch (const std::invalid_argument&) {
        refused_longer = true;
    }
    require(refused_longer && f.filter_tail() == configuration_tail_frames(a),
            "a refused request changed the tail");
    f.load(HostState{}, std::nullopt);
    require(f.filter_tail() == 0, "an empty instance has a tail");
    HostProcessor host(filter_playback(a), 64, 8);
    f.load(HostState{a}, layout, [&](const HostPlayback& c) { host.play(c); }, {}, &host);
    require(f.filter_tail() == configuration_tail_frames(a),
            "the accepted filter's tail went while a processor plays");

    // A failed request silences the processor only while no filter is
    // accepted: a failed first load plays silence, not the unfiltered input.
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    const auto heard = [&](HostProcessor& p) {
        audio_thread = true;
        require(p.process(in, out, input.size()), "process rejected");
        audio_thread = false;
        return std::ranges::any_of(output, [](float x) { return x != 0; });
    };
    HostFilter empty(8);
    HostProcessor unfiltered(unfiltered_playback(layout, false, true), 64);
    empty.fail_request(&unfiltered);
    require(empty.failed() && !heard(unfiltered), "a failed first load did not silence");
    f.fail_request(&host);
    require(!f.failed() && heard(host), "a failed request silenced an accepted filter");
}
void processing_faults() {
    const auto huge = std::numeric_limits<float>::max() / 4;
    const Configuration loud = mono(huge);
    const HostLayout layout{48000, 1, 1};
    HostProcessor host(filter_playback(loud), 64, 8);
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
    require(host_status(f, layout).status == HostStatus::active, "playing before observing");
    const auto fresh = f.observe(host.faults());
    require(fresh.faults.output_overflow == 1 && !fresh.changed && !fresh.held &&
                f.faults().output_overflow == 1,
            "observe");
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
    require(fault_message(HostFaults{}).empty() && fault_message(HostFaultReport{}).empty(),
            "message without faults");
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
    require(!f.faults().any() && host_status(f, layout).status == HostStatus::active,
            "an accepted load clears the fault");
    // Without a processor passed to load(), faults() counts on from the last
    // observe(), so the cleared fault does not come back (DIAG-3 review).
    require(!f.observe(host.faults()).any() && !f.faults().any(),
            "a load without a processor did not keep the fault cleared");
    // ROBUST-8: an activation whose processor was not built (a new Latency
    // when memory ran out) records nothing. The running processor counts on,
    // so the fault the load cleared stays cleared.
    static_cast<void>(f.activation(layout));
    require(!f.observe(host.faults()).any() && !f.faults().any() &&
                host_status(f, layout).status == HostStatus::active,
            "an activation that never played brought a cleared fault back");
    // Nor does it clear why a kept filter plays silence (nor the speaker
    // notice, checked in speaker_notices()) until activate() records it.
    HostFilter kept(8);
    kept.load(HostState{mono()}, std::nullopt);
    static_cast<void>(test::started(kept, HostLayout{44100, 1, 1}));
    require(kept.failure() && kept.failure()->silent, "a kept filter at another rate");
    auto suited = kept.activation(layout);
    require(!suited.failure && suited.playback.filter->sample_rate == 48000 && kept.failure() &&
                kept.failure()->silent,
            "an activation that never played cleared why the filter is silent");
    kept.activate(std::move(suited));
    require(!kept.failure(), "an activation that plays did not clear why it was silent");
    // A new processor counts from 0 after activate().
    static_cast<void>(test::started(f, layout));
    HostFaults restarted;
    restarted.rejected_input = 1;
    require(f.observe(restarted).faults.rejected_input == 1 &&
                host_status(f, layout).text.find("invalid input samples") != std::string::npos,
            "counts after activation");
    HostFaults every{3, 2, 1, 1};
    const auto all = fault_message(every);
    require(all.find("(2 times)") != std::string::npos &&
                all.find("the host sent invalid samples (3 times). Cause: Samples that are not a "
                         "number, infinite or beyond the 32-bit float range") !=
                    std::string::npos &&
                all.find("fault in Convolver") != std::string::npos &&
                all.find("host's developers") != std::string::npos &&
                all.find('\n') == std::string::npos,
            "every fault explained, on one line");
    HostFilter g(8);
    g.load(HostState{mono()}, layout);
    // Positional braces fill faults, changed and held in order.
    HostFaults one;
    one.invalid_call = 1;
    const HostFaultReport braced{every, true};
    require(braced.faults.total() == 7 && braced.changed && !braced.held && braced.any(),
            "HostFaultReport{faults, changed}");
    require(fault_message(HostFaultReport{one}).find("host's developers") != std::string::npos,
            "HostFaultReport{faults}");
    static_cast<void>(g.observe(every));
    require(host_status(g, layout).text.find("the filter failed while playing") !=
                std::string::npos,
            "most serious fault first");

    // jrp's review of 0.4.43: audio goes on while a replacement is prepared.
    // A fault in that window is logged, as met around a filter change
    // (DIAG-3), but neither shown against the filter that replaces it nor
    // signalled by the audio thread, because both measure from the accepted
    // load's one snapshot.
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    HostProcessor p(filter_playback(mono()), 64, 8);
    HostFilter h(8);
    h.load(HostState{mono()}, layout, {}, {}, &p);
    std::array<float, 64> buffer{};
    const float* pin[] = {buffer.data()};
    float* pout[] = {buffer.data()};
    const auto play = [&](float first) {
        buffer.fill(0.f);
        buffer[0] = first;
        audio_thread = true;
        static_cast<void>(p.process(pin, pout, 64));
        audio_thread = false;
    };
    static_cast<void>(h.observe(p.faults()));
    h.load(
        HostState{mono(.5f)}, layout,
        [&](const HostPlayback& c) {
            p.replace(c);
            play(nan); // While the replacement is prepared.
        },
        {}, &p);
    play(0.f);
    const auto window = h.observe(p.faults());
    require(window.faults.rejected_input == 1 && window.changed && !h.faults().any() &&
                host_status(h, layout).status == HostStatus::active,
            "a fault counted while the replacement was prepared is shown against it");
    require(!window.held, "no load failure holds the report");
    // Its log text must not claim the status shows it.
    const auto changed = fault_message(window);
    require(changed.find("around a filter change") != std::string::npos &&
                changed.find("shows only those met after it took over") != std::string::npos &&
                changed.find("status shows Fault") == std::string::npos &&
                changed.find("invalid samples") != std::string::npos,
            "faults around a filter change explained as shown");
    require(!p.faults_since_clear() && !p.fault_to_signal(),
            "the audio side measures from the same snapshot");
    // The new filter's own fault: shown, and signalled once per clear.
    play(nan);
    require(p.faults_since_clear() && p.fault_to_signal(), "new fault not signalled");
    p.fault_signalled();
    play(nan);
    require(!p.fault_to_signal(), "Fault signalled twice");
    const auto later = h.observe(p.faults());
    require(later.faults.rejected_input == 2 && !later.changed &&
                host_status(h, layout).status == HostStatus::fault,
            "new faults not shown");
    // A load failure comes first, on the audio side too.
    h.load(HostState{mono()}, layout, {}, {}, &p);
    try {
        h.load(HostState{mono()}, HostLayout{44100, 1, 1}, {}, {}, &p);
    } catch (const std::invalid_argument&) {
    }
    require(h.failure_shown(), "failure_shown");
    p.hold_fault_signal(h.failure_shown());
    play(nan);
    require(!p.fault_to_signal() && p.faults_since_clear(), "Fault signalled over Load failed");
    // Nor does the log say Fault shows (the 0.5.2 review).
    const auto held = h.observe(p.faults());
    require(held.held && held.faults.rejected_input == 1 && !held.changed &&
                host_status(h, layout).status == HostStatus::load_failed,
            "a fault after a rejected replacement");
    const auto held_text = fault_message(held);
    require(held_text.find("status shows Load failed instead of Fault") != std::string::npos &&
                held_text.find("status shows Fault") == std::string::npos &&
                held_text.find("invalid samples") != std::string::npos,
            "the log says the status shows Fault over Load failed");
    h.load(HostState{mono()}, layout, {}, {}, &p);
    require(!h.failure_shown(), "an accepted load clears failure_shown");
    p.hold_fault_signal(h.failure_shown());
    play(nan);
    require(p.fault_to_signal(), "signal not released after an accepted load");
    // An accepted load that restores a saved failure holds the signal itself,
    // before the adapter updates its status.
    HostState saved_failure;
    saved_failure.failed = true;
    h.load(saved_failure, layout, {}, {}, &p);
    play(nan);
    require(h.failure_shown() && !p.fault_to_signal(), "saved failure not held at load");

    // The adapters' failed first load: an empty instance's processor plays on
    // with silence. Every fault is explained once: before the request, while
    // the rejected filter was prepared, and after.
    HostFilter empty(8);
    HostProcessor q(filter_playback(*test::started(empty, layout).filter), 64, 8);
    const auto play_q = [&](float first) {
        buffer.fill(0.f);
        buffer[0] = first;
        audio_thread = true;
        static_cast<void>(q.process(pin, pout, 64));
        audio_thread = false;
    };
    play_q(nan);
    auto explained = empty.observe(q.faults()).faults.rejected_input;
    try {
        empty.load(
            HostState{mono()}, layout,
            [&](const HostPlayback&) {
                play_q(nan);
                throw std::runtime_error("out of memory");
            },
            {}, &q);
    } catch (const std::invalid_argument&) {
    }
    require(empty.failed(), "failed first load");
    q.replace(unfiltered_playback(layout, true, false)); // As HostFilter::activation() gives it.
    play_q(nan);
    const auto after = empty.observe(q.faults());
    explained += after.faults.rejected_input;
    require(explained == 3 && q.faults().rejected_input == 3,
            "a fault while a failed first load was prepared is lost from the log");
}
// PLUG-39 step 4a (jrp, 20 November 2026): the accepted filter is shared,
// immutable, so a copy of a HostFilter (an adapter's diagnostic snapshot)
// costs no copy of the filter and stays whole after a replacement; a refused
// replacement leaves the accepted filter, the same object; the processor's
// playback shares it too.
void shared_filter() {
    const HostLayout layout{48000, 1, 1};
    HostFilter f(8);
    std::shared_ptr<const Configuration> played;
    f.load(HostState{mono(.5f)}, layout, [&](const HostPlayback& p) { played = p.filter; });
    require(played && played == f.filter(), "the processor's filter is a copy");
    const auto first = f.filter();
    const HostFilter snapshot = f;
    require(snapshot.filter() == first, "a copy of the HostFilter copied the filter");
    // Replaced: the snapshot keeps the old filter whole, and its record.
    f.load(HostState{mono(.25f)}, layout);
    require(f.filter() != first && snapshot.filter() == first &&
                snapshot.filter()->paths.at(0).impulse.at(0) == .5f &&
                snapshot.filter_identity() == configuration_identity(mono(.5f)) &&
                !snapshot.failure(),
            "an old snapshot changed after a replacement");
    // Refused: the accepted filter is the same object, unchanged.
    const auto second = f.filter();
    auto slow = mono();
    slow.sample_rate = 44100;
    try {
        f.load(HostState{slow}, layout);
    } catch (const std::invalid_argument&) {
    }
    require(f.failure() && f.filter() == second && f.filter()->paths.at(0).impulse.at(0) == .25f,
            "a refused replacement did not keep the accepted filter");
    // The same filter restored again keeps the object it shares.
    f.load(HostState{mono(.25f)}, layout);
    require(f.filter() == second, "the same filter restored was not shared");
    // Activation hands the processor the shared filter, not a copy.
    require(f.activation(layout).playback.filter == second, "activation copied the filter");
    // The last owner frees it: nothing else holds the old one.
    played.reset();
    require(first.use_count() == 2, "the replaced filter is held elsewhere");
    // A playback with no filter is refused, never dereferenced.
    const auto refused = [](auto&& build) {
        try {
            build();
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    require(refused([] { filter_playback(std::shared_ptr<const Configuration>{}); }) &&
                refused([] { HostProcessor(HostPlayback{}, 64, HostSettings{}); }),
            "a playback with no filter was not refused");
}
// PLUG-22, PLUG-39 step 4: a name kept apart from the filter's state
// (VST3's controller state) shows only while its filter plays, whichever
// order the host sends the two in; a refused filter never renames the one
// that plays (jrp's v0.12.8 REAPER check). The VST3 adapter's own test
// (vst3.adapter, the controller state) replays the same through its API.
void filter_names() {
    const HostLayout layout{48000, 1, 1};
    const auto room = mono(.5f), hall = mono(.25f), slow = [] {
        auto c = mono();
        c.sample_rate = 44100;
        return c;
    }();
    const auto identity = [](const Configuration& c) { return configuration_identity(c); };
    const auto name = [](const HostFilter& f) { return f.accepted().name; };
    const auto state = [](std::string n = {}) {
        HostRequest r;
        r.name = std::move(n);
        return r;
    };
    {
        // The name first, then the state it names; then the state first.
        HostFilter f(8);
        f.offer_name("room.wav", identity(room));
        require(name(f).empty(), "a name shown before its filter");
        f.load(HostState{room}, layout);
        require(name(f) == "room.wav", "an offered name not taken by its filter");
        HostFilter g(8);
        g.load(HostState{room}, layout);
        g.offer_name("room.wav", identity(room));
        require(name(g) == "room.wav", "a name offered for the playing filter not shown");
    }
    {
        // A refused preset's name, in either order, leaves the playing name;
        // the state of the playing filter brought again keeps it.
        for (const bool name_first : {false, true}) {
            HostFilter f(8);
            f.offer_name("room.wav", identity(room));
            f.load(HostState{room}, layout);
            if (name_first)
                f.offer_name("slow.wav", identity(slow));
            try {
                f.load(HostState{slow}, layout);
            } catch (const std::invalid_argument&) {
            }
            if (!name_first)
                f.offer_name("slow.wav", identity(slow));
            require(f.filter_identity() == identity(room) && name(f) == "room.wav",
                    "a refused preset renamed the playing filter");
            f.load(HostState{room}, layout);
            require(name(f) == "room.wav",
                    "the playing filter's state brought again lost its name");
            // Another filter's state names nothing.
            f.load(HostState{hall}, layout);
            require(name(f).empty(), "a name shown for another filter");
        }
    }
    {
        // A file's own name, for the state saved from it.
        // Never read: the filter comes with the request.
        const auto path = std::filesystem::path("filters") / "room.wav";
        HostFilter f(8);
        HostRequest file{HostFilterSource::file, path.string(), 0};
        file.kind = HostRequestKind::chosen_file;
        f.load(HostState{room}, layout, {}, file);
        f.load(HostState{hall}, layout);
        f.load(HostState{room}, layout);
        require(name(f) == "room.wav", "the state of a chosen file lost the file's name");
        // A name the state brings itself (the Audio Unit's preset name) stands.
        f.load(HostState{room}, layout, {}, state("Room preset"));
        require(name(f) == "Room preset", "a state's own name replaced");
    }
}
// PLUG-39 step 4: host_status_value() is the one status ordering, which
// host_status(), the report and LV2's status port follow: a load failure,
// then a fault, then bypass with a filter, then the filter, then a failed
// first load, then no filter.
void status_ordering() {
    using Status = HostStatus;
    const auto expect = [](const HostFilter& f, const std::optional<HostLayout>& host,
                           bool bypassed, Status status, const char* what) {
        const auto value = host_status_value(f, host.has_value(), bypassed);
        if (value != status || host_status(f, host, true, {}, bypassed).status != value)
            throw std::runtime_error(std::string("status ordering: ") + what);
    };
    const HostLayout layout{48000, 1, 1}, other{44100, 1, 1};
    for (const bool bypassed : {false, true}) {
        HostFilter f(8);
        expect(f, std::nullopt, bypassed, Status::no_filter, "empty, host unknown");
        expect(f, layout, bypassed, Status::no_filter, "empty");
        try {
            f.load(HostState{mono()}, other);
        } catch (const std::invalid_argument&) {
        }
        expect(f, layout, bypassed, Status::load_failed, "failed first load");
        f.load(HostState{mono()}, std::nullopt);
        expect(f, std::nullopt, bypassed, bypassed ? Status::bypassed : Status::unchecked,
               "filter, host unknown");
        f.load(HostState{mono()}, layout);
        expect(f, layout, bypassed, bypassed ? Status::bypassed : Status::active, "filter");
        // A fault comes before bypass and the filter.
        const auto huge = std::numeric_limits<float>::max() / 4;
        HostProcessor host(filter_playback(mono(huge)), 64, 8);
        std::array<float, 64> data{};
        data[0] = huge;
        const float* in[] = {data.data()};
        float* out[] = {data.data()};
        audio_thread = true;
        static_cast<void>(host.process(in, out, 64));
        audio_thread = false;
        static_cast<void>(f.observe(host.faults()));
        require(f.faults().any(), "status ordering: no fault counted");
        expect(f, layout, bypassed, Status::fault, "fault");
        // A refused load comes before the fault, which it leaves.
        try {
            f.load(HostState{mono()}, other);
        } catch (const std::invalid_argument&) {
        }
        require(f.faults().any(), "status ordering: a refused load cleared the fault");
        expect(f, layout, bypassed, Status::load_failed, "refused load over a fault");
    }
}
// A file name of this process's own, so that concurrent runs never share it.
std::filesystem::path private_temp(const char* name) {
    return std::filesystem::temp_directory_path() /
           (std::string(name) + "-" + std::to_string(std::random_device{}()));
}
// DIAG-1: the status line a host shows, and the Convolver log.
void status_lines() {
    using Status = HostStatus;
    const auto contains = [](const auto& text, const char* part) {
        return text_of(text).find(part) != std::string::npos;
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
          "Filter loaded: 44100 Hz, 2 in, 2 out; waiting for audio settings from the host.",
          "unchecked");
    // Checked at activation: silence, and what it waits for (PLUG-44:
    // Waiting, as nothing failed to load).
    static_cast<void>(test::started(f, stereo));
    check(host_status(f, stereo), Status::unchecked,
          "Silent, waiting for 44100 Hz: the host runs at 48000 Hz. Details in the Convolver log.",
          "mismatch at activation");
    f.load(HostState{a}, stereo);
    check(host_status(f, stereo), Status::active, "Filter loaded: 48000 Hz, 2 in, 2 out.",
          "active");
    // PLUG-22: bypassed, as HostProcessor bypasses: the input when there is
    // a dry signal, silence otherwise (dry_signal_rule()); nothing is
    // bypassed before the host starts.
    check(host_status(f, stereo, true, {}, true), Status::bypassed,
          "Bypassed: the input plays unfiltered.", "bypassed");
    {
        // A crossover on a six-channel track (PLUG-23) has no dry signal.
        const HostLayout six{48000, 6, 6, 0, 0};
        Configuration x{48000, 2, 6, 0, {}, {}, {}};
        x.paths.push_back({{1.f}, {{0, 1}}, {{0, 1}}});
        x.paths.push_back({{1.f}, {{1, 1}}, {{5, 1}}});
        HostFilter split(8);
        split.load(HostState{x}, six);
        check(host_status(split, six, true, {}, true), Status::bypassed,
              "Bypassed: silent, as the filter's inputs and outputs differ.",
              "bypassed, unequal counts");
    }
    check(host_status(f, std::nullopt, true, {}, true), Status::bypassed,
          "Bypassed: no filter is applied.", "bypassed before the host starts");
    // A refused replacement: the previous filter plays, and the line says so.
    refused([&] { f.load(HostState{crossover}, stereo); });
    check(host_status(f, stereo), Status::load_failed,
          "Load failed, previous filter kept: the filter has 2 in, 6 out, the host 2 in, 2 out.",
          "refused replacement");
    // The host then gives buses the refused filter would suit (PLUG-23: six
    // inputs and six outputs): the line names only what was refused, not
    // counts that look as if they conflict (jrp's REAPER retest, 7 November).
    const HostLayout six{48000, 6, 6};
    check(host_status(f, six), Status::load_failed,
          "Load failed, previous filter kept: the filter has 2 in, 6 out; the channels have "
          "changed since. Details in the Convolver log.",
          "refused replacement, suitable buses now");
    refused([&] { f.load(HostState{other_rate}, stereo); });
    check(host_status(f, HostLayout{44100, 2, 2}), Status::load_failed,
          "Load failed, previous filter kept: the filter is for 44100 Hz; the rate has changed "
          "since. Details in the Convolver log.",
          "refused replacement, suitable rate now");
    // Then started at a rate the playing filter does not suit: it is kept,
    // silent, with why, but stays Load failed rather than Waiting (PLUG-44),
    // as a refused request shows until a load is accepted.
    static_cast<void>(test::started(f, HostLayout{44100, 2, 2}));
    check(host_status(f, HostLayout{44100, 2, 2}), Status::load_failed,
          "Silent: the filter is for 48000 Hz, the host runs at 44100 Hz.",
          "kept after a refusal, unsuited");
    static_cast<void>(test::started(f, stereo));
    // Restored while the host may still change its buses (VST3 before audio
    // has played): a filter that does not suit is kept, silent, with the
    // reason, and plays once the host's buses suit it.
    {
        HostFilter kept(8);
        kept.load(HostState{a}, stereo);
        kept.load(HostState{crossover}, stereo, {}, {}, nullptr, HostLoadCheck::keep_until_suited);
        require(kept.filter() && kept.filter()->output_channels == 6 && kept.failure() &&
                    kept.failure()->silent && kept.failure()->kind == HostFailureKind::channels &&
                    !kept.rejection(),
                "a restored filter that does not suit yet was not kept silent");
        check(host_status(kept, stereo), Status::unchecked,
              "Silent, waiting for 2 in, 6 out: the host has 2 in, 2 out.", "kept, unsuited");
        // Before the host's settings are known again, only what it waits for.
        check(host_status(kept, std::nullopt), Status::unchecked,
              "Filter kept, waiting for 2 in, 6 out.", "kept, unsuited, host unknown");
        // Mix lets the dry signal through where the counts are all equal:
        // not here (a crossover), so still silent.
        check(host_status(kept, stereo, true, HostSettings{0, .5}), Status::unchecked,
              "Silent, waiting for", "kept crossover, Mix 50%");
        static_cast<void>(test::started(kept, six));
        require(!kept.failure(), "the kept filter does not play on buses that suit it");
        check(host_status(kept, six), Status::active, "Filter loaded: 48000 Hz, 2 in, 6 out",
              "kept, then suited");
    }
    // PLUG-36: Bypass changes what plays without a filter, so the line follows
    // it as it follows Mix. A kept filter that does not suit the host, with
    // equal counts on both, passes the dry signal while bypassed; with a
    // crossover there is none. An empty instance and a failed load say what
    // bypass plays too: the input, or silence.
    {
        HostFilter kept(8);
        kept.load(HostState{other_rate}, std::nullopt);
        static_cast<void>(test::started(kept, stereo));
        check(host_status(kept, stereo, true, {}, true), Status::unchecked,
              "Bypass is on: only the dry signal plays. Waiting for 44100 Hz: the host runs at "
              "48000 Hz.",
              "kept at another rate, bypassed");
        // Whatever Gain and Mix are: bypass passes the input untouched.
        check(host_status(kept, stereo, true, HostSettings{-6, .5}, true), Status::unchecked,
              "Bypass is on: only the dry signal plays.",
              "kept at another rate, Gain -6 dB, Mix 50%, bypassed");
        // Not bypassed, Mix 50%: the dry share plays (PLUG-36's rule).
        check(host_status(kept, stereo, true, HostSettings{0, .5}), Status::unchecked,
              "Only the dry signal plays, waiting for 44100 Hz: the host runs at 48000 Hz.",
              "kept at another rate, Mix 50%");
        HostFilter unsuited(8);
        unsuited.load(HostState{crossover}, std::nullopt);
        static_cast<void>(test::started(unsuited, stereo));
        check(host_status(unsuited, stereo, true, {}, true), Status::unchecked,
              "Silent, waiting for 2 in, 6 out", "kept crossover, bypassed");
        // A kept filter the adapter could not start has no processor: nothing
        // plays, bypassed or not.
        HostFilter unstarted(8);
        unstarted.load(HostState{a}, stereo);
        unstarted.fail_activation("no memory");
        check(host_status(unstarted, stereo, true, {}, true), Status::load_failed,
              "Silent: the plug-in could not start the filter.", "activation failure, bypassed");
        HostFilter empty(8);
        check(host_status(empty, stereo, true, HostSettings{-6, .5}, true), Status::no_filter,
              "No filter. Bypass is on: audio passes unchanged.", "empty, Gain -6 dB, bypassed");
        check(host_status(empty, crossover_bus, true, {}, true), Status::no_filter,
              "No filter: silent, as inputs and outputs differ.", "empty, 2->6, bypassed");
        check(host_status(empty, stereo, true, HostSettings{-6, .5}), Status::no_filter,
              "No filter: audio passes at", "empty, Gain -6 dB");
        refused([&] { empty.load(HostState{other_rate}, stereo); });
        check(host_status(empty, stereo, true, {}, true), Status::load_failed,
              "Silent: the filter is for 44100 Hz", "failed first load, bypassed");
    }
    // The longest common line still fits when the log cannot be written.
    refused([&] { f.load(HostState{other_rate}, stereo); });
    check(host_status(f, stereo, false), Status::load_failed,
          "Load failed, previous filter kept: the filter is for 44100 Hz, the host runs at 48000 "
          "Hz. The log could not be written.",
          "unwritable log, refused rate");
    const std::array<std::pair<HostFailureKind, const char*>, 8> kinds{{
        {HostFailureKind::filter_file, "filter file could not be read"},
        // PLUG-40: the host is unknown here, so what to do first.
        {HostFailureKind::settings_unknown, "play audio once, then choose it again."},
        {HostFailureKind::too_large, "too large to save with the project"},
        {HostFailureKind::state_unreadable, "saved project or preset could not be read"},
        {HostFailureKind::state_newer, "saved by a newer Convolver"},
        {HostFailureKind::preparation, "could not be prepared"},
        // Rejected with no filter carried: no "0 Hz" or "0 in, 0 out" (jrp's
        // GarageBand run); the host is unknown here.
        {HostFailureKind::sample_rate, "the filter is for another rate."},
        {HostFailureKind::channels, "the filter has other channel counts."},
    }};
    for (const auto& [kind, part] : kinds) {
        f.reject(kind, {"why"});
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
    check(host_status(labelled, HostLayout{48000, 2, 6, 0x3, 0x633}), Status::active,
          "Speaker labels differ", "speaker notice");

    // The log lines adapters write.
    HostFilter g(8);
    require(loaded_line(g, true) == "Request 0: Loaded a saved project or preset with no filter." &&
                contains(started_line(g, stereo, 512, nullptr, false),
                         "Started at 48000 Hz, 2 in, 2 out, blocks of up to 512 frames. No "
                         "filter, so audio passes unchanged.") &&
                contains(started_line(g, crossover_bus, 512, nullptr, false), "plays silence") &&
                contains(started_line(g, stereo, 512, nullptr, true),
                         "No filter. Bypass is on: audio passes unchanged."),
            "empty instance log lines");
    {
        // PLUG-39 step 4: with bypass on, the start line says what bypass
        // plays, not that the filter plays.
        HostFilter h(8);
        h.load(HostState{a}, stereo);
        require(contains(started_line(h, stereo, 512, nullptr, true),
                         "Bypass is on: the input plays unfiltered. Loaded: a filter for 48000 Hz, "
                         "2 in, 2 out.") &&
                    !contains(started_line(h, stereo, 512, nullptr, true), "Playing"),
                "bypassed start line");
        // A crossover has no dry signal, so bypass plays silence.
        HostFilter x(8);
        x.load(HostState{crossover}, crossover_bus);
        require(contains(started_line(x, crossover_bus, 512, nullptr, true),
                         "Bypass is on: the outputs are silent. Loaded: a filter for 48000 Hz, "
                         "2 in, 6 out."),
                "bypassed crossover start line");
        // A kept filter that does not suit the host: bypass plays its dry
        // signal, or silence where it has none (as Status says).
        HostFilter kept(8);
        kept.load(HostState{other_rate}, std::nullopt);
        static_cast<void>(test::started(kept, stereo));
        require(contains(started_line(kept, stereo, 512, nullptr, true),
                         "Bypass is on: only the dry signal plays. The saved filter is for "
                         "44100 Hz"),
                "bypassed start line of a kept filter");
        HostFilter kept_x(8);
        auto slow_crossover = crossover;
        slow_crossover.sample_rate = 44100;
        kept_x.load(HostState{slow_crossover}, std::nullopt);
        static_cast<void>(test::started(kept_x, crossover_bus));
        require(contains(started_line(kept_x, crossover_bus, 512, nullptr, true),
                         "Convolver plays silence."),
                "bypassed start line of a kept crossover");
        // A refused replacement while a crossover plays bypassed.
        try {
            x.load(HostState{slow_crossover}, crossover_bus);
        } catch (const std::invalid_argument&) {
        }
        require(failure_consequence(*x.failure(), &crossover_bus, {}, dry_signal(x, crossover_bus),
                                    true) ==
                    "The previous filter is kept. Bypass is on: the outputs are silent.",
                "bypassed refusal of a crossover");
    }
    g.load(HostState{other_rate}, std::nullopt);
    require(loaded_line(g, false) ==
                "Request 1: Loaded a filter for 44100 Hz, 2 in, 2 out; waiting "
                "for audio settings from the host.",
            "unchecked load line");
    require(rejected_line(g, true, false).empty(), "rejected line without a rejection");
    try {
        g.load(HostState{crossover}, stereo);
    } catch (const std::invalid_argument&) {
    }
    require(rejected_line(g, true, false)
                        .rfind("Request 2: Not loaded; the previous filter keeps "
                               "playing: the filter has 2 inputs and 6 outputs, and "
                               "the host gives the plug-in 2 inputs and 2 outputs. Cause: ",
                               0) == 0 &&
                rejected_line(g, true, false).find('\n') == std::string::npos,
            "rejected line");
    require(rejected_line(g, true, true)
                    .rfind("Request 2: Not loaded; the previous filter is kept (bypass is on): ",
                           0) == 0,
            "rejected line while bypassed");
    // Before the host starts the plug-in, the kept filter does not play yet.
    require(rejected_line(g, false, false)
                    .rfind("Request 2: Not loaded; the previous filter is kept: ", 0) == 0,
            "rejected line before the host starts the plug-in");
    static_cast<void>(test::started(g, stereo));
    require(contains(started_line(g, stereo, 64, nullptr, false),
                     "Convolver plays silence. The saved filter is for 44100 Hz") &&
                contains(started_line(g, stereo, 64, nullptr, false), "Convolver never resamples"),
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
    // CI-6: an adapter names the SDK it was built with.
    require(!contains(session, "built with"), "SDK named before an adapter set it");
    set_host_log_sdk("Example SDK 1.2.3, commit 0123456789ab");
    require(
        contains(execution_line(), ", built with Example SDK 1.2.3, commit 0123456789ab, process "),
        "SDK missing from the execution line");
    set_host_log_sdk(nullptr);
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
        h.reject({"why"});
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

// PERF-5 acceptance: a live replacement and a reset, at partition sizes that
// are not powers of two and host callback sizes unrelated to them, match the
// double-precision oracle. The new filter's engine starts with empty history
// when the audio thread adopts it, and a reset invalidates the stale history,
// so both follow the oracle of the input from that frame on (after the warm-up
// and crossfade for a replacement).
void costly_moments_against_oracle() {
    std::mt19937 random(0xc0571);
    std::uniform_real_distribution<float> sample(-.5f, .5f);
    auto filter = [&](std::size_t b) {
        Configuration c{48000, 2, 2, 0, {}, {}, {}};
        for (const auto& [in, out] : {std::pair{0, 0}, std::pair{1, 1}, std::pair{0, 1}}) {
            FilterPath p;
            p.impulse.resize(3 * b + 1 + random() % b);
            for (auto& x : p.impulse)
                x = sample(random);
            p.inputs = {{std::size_t(in), 1}};
            p.outputs = {{std::size_t(out), sample(random)}};
            c.paths.push_back(std::move(p));
        }
        return c;
    };
    for (const auto& [b, callback] : {std::pair{std::size_t(257), std::size_t(64)},
                                      std::pair{std::size_t(257), std::size_t(1000)},
                                      std::pair{std::size_t(255), std::size_t(1)},
                                      std::pair{std::size_t(64), std::size_t(100)}}) {
        const auto first = filter(b), second = filter(b);
        HostProcessor host(filter_playback(first), callback, b);
        // Interleaved input and output of every frame processed so far.
        std::vector<float> input, output;
        std::vector<float> in_block(2 * callback), out_block(2 * callback);
        auto play = [&](std::size_t frames, bool silent) {
            for (std::size_t done = 0; done < frames; done += callback) {
                std::array<const float*, 2> in{in_block.data(), in_block.data() + callback};
                std::array<float*, 2> out{out_block.data(), out_block.data() + callback};
                for (auto& x : in_block)
                    x = silent ? 0.f : sample(random);
                require(host.process(in, out, callback), "host refused a callback");
                for (std::size_t t = 0; t < callback; ++t)
                    for (std::size_t c = 0; c < 2; ++c) {
                        input.push_back(in[c][t]);
                        output.push_back(out[c][t]);
                    }
            }
        };
        // The oracle of the input from frame `start`, compared from frame `from`.
        auto check = [&](const Configuration& c, std::size_t start, std::size_t from,
                         const char* message) {
            const std::vector<float> after(input.begin() + std::ptrdiff_t(2 * start), input.end());
            const auto expected = test::oracle(c, after, b);
            for (std::size_t i = 2 * from; i < output.size() - 2 * start && i < expected.size();
                 ++i)
                if (std::abs(output[2 * start + i] - expected[i]) >
                    2e-5 * (1 + std::abs(expected[i])))
                    throw std::runtime_error(std::string(message) + " at sample " +
                                             std::to_string(i) + " (block " + std::to_string(b) +
                                             ", callback " + std::to_string(callback) + ")");
        };
        play(4 * b + 17, false);
        host.replace(filter_playback(second));
        const auto adopted = input.size() / 2;
        play(5 * b, false);
        play(5 * b + 512, true);
        host.collect();
        // Warm-up (one block) and the 256-frame crossfade.
        check(second, adopted, b + 256, "replacement differs from the oracle");
        play(3 * b + 5, false); // The history must hold audio when it is reset.
        host.reset();
        const auto reset = input.size() / 2;
        play(2 * b + 3, false);
        play(5 * b + 512, true);
        check(second, reset, 0, "output after reset differs from the oracle");
    }
}
int main() {
    const auto failed = convolver::test::run_cases(
        {
            {"numeric<float>", numeric<float>},
            {"numeric<double>", numeric<double>},
            {"lifecycle", lifecycle},
            {"queued_replacements", queued_replacements},
            {"unequal_channels", unequal_channels},
            {"direct_installation", direct_installation},
            {"prepared_replacements", prepared_replacements},
            {"reset_during_transition", reset_during_transition},
            {"filter_contract", filter_contract},
            {"failure_reasons", failure_reasons},
            {"speaker_notices", speaker_notices},
            {"unequal_bypass", unequal_bypass},
            {"wider_inputs", wider_inputs},
            {"muting", muting},
            {"control_silence", control_silence},
            {"allocation_failures", allocation_failures},
            {"unrecorded_failures", unrecorded_failures},
            {"saved_state_limit", saved_state_limit},
            {"status_lines", status_lines},
            {"status_ordering", status_ordering},
            {"filter_names", filter_names},
            {"shared_filter", shared_filter},
            {"processing_faults", processing_faults},
            {"same_filter_kept", same_filter_kept},
            {"kept_unless_one_plays", kept_unless_one_plays},
            {"request_changes", request_changes},
            {"costly_moments_against_oracle", costly_moments_against_oracle},
        },
        [] {
            audio_thread = false;
            allocations_left = -1;
        });
    if (failed)
        return 1;
    std::cout << "host lifecycle passed\n";
}
