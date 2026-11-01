// SPDX-License-Identifier: GPL-2.0-or-later
// Audio Unit specifics that the shared PLUG-2 harness does not cover: channel
// capabilities and formats, latency and tail in seconds with change
// notifications, ClassInfo contents and .aupreset loading, foreign and
// malformed presets, bypass as a property, the output silence flag, and no
// allocation while rendering.
#include "au/au_host.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {
// Allocations are counted only on a thread that sets `counting`, so the
// unit's own worker thread (a Latency change) is not counted with a render.
thread_local bool counting = false;
std::atomic<std::size_t> allocations{0};
// When positive, the allocation that brings it to 0 fails, on any thread
// that has not set `never_fail` (the test's own).
std::atomic<int> fail_countdown{0};
thread_local bool never_fail = false;
} // namespace
void* operator new(std::size_t n) {
    if (counting)
        allocations.fetch_add(1, std::memory_order_relaxed);
    if (!never_fail && fail_countdown.load(std::memory_order_relaxed) > 0 &&
        fail_countdown.fetch_sub(1) == 1)
        throw std::bad_alloc();
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

using namespace convolver;
using au_test::check;
using au_test::Unit;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

// Two inputs to six outputs, a distinct response on every route, as a crossover.
Configuration crossover(int rate) {
    Configuration c{rate, 2, 6, 0, {}, {}, {}};
    for (std::size_t o = 0; o < 6; ++o) {
        std::vector<float> taps(300 + 40 * o);
        for (std::size_t t = 0; t < taps.size(); ++t)
            taps[t] = static_cast<float>(std::sin(0.01 * (t + 1) * (o + 1)) / (t + 1));
        c.paths.push_back({taps, {{o % 2, 1}}, {{o, 1}}});
    }
    return c;
}
Configuration stereo(int rate, std::size_t length = 1000) {
    Configuration c{rate, 2, 2, 0, {}, {}, {}};
    for (std::size_t ch = 0; ch < 2; ++ch) {
        std::vector<float> taps(length);
        for (std::size_t t = 0; t < taps.size(); ++t)
            taps[t] = static_cast<float>(std::exp(-0.01 * t) * (ch ? -0.5 : 0.5));
        c.paths.push_back({taps, {{ch, 1}}, {{ch, 1}}});
    }
    return c;
}
HostState state_of(const Configuration& c) {
    HostState s;
    s.configuration = c;
    return s;
}

// Planar buffers for one channel layout.
struct Buffers {
    std::vector<std::vector<float>> in, out;
    std::vector<const float*> ins;
    std::vector<float*> outs;
    Buffers(std::size_t inputs, std::size_t outputs, std::size_t frames)
        : in(inputs, std::vector<float>(frames)), out(outputs, std::vector<float>(frames)) {
        for (auto& b : in)
            ins.push_back(b.data());
        for (auto& b : out)
            outs.push_back(b.data());
    }
    void noise(std::uint32_t& seed) {
        for (auto& b : in)
            for (auto& v : b) {
                seed = seed * 1664525U + 1013904223U;
                v = static_cast<float>(seed >> 8) / 16777216.f - .5f;
            }
    }
    void silence() {
        for (auto& b : in)
            std::fill(b.begin(), b.end(), 0.f);
    }
    bool output_silent() const {
        for (auto& b : out)
            for (auto v : b)
                if (v != 0)
                    return false;
        return true;
    }
};

OSStatus render(Unit& unit, Buffers& b, UInt32 frames, bool silent_input, bool& silent_output) {
    return unit.render(b.ins, b.outs, frames, silent_input, silent_output);
}

void channel_capabilities() {
    Unit unit;
    UInt32 size = 0;
    Boolean writable = false;
    check(AudioUnitGetPropertyInfo(unit.get(), kAudioUnitProperty_SupportedNumChannels,
                                   kAudioUnitScope_Global, 0, &size, &writable),
          "SupportedNumChannels size");
    std::vector<AUChannelInfo> infos(size / sizeof(AUChannelInfo));
    check(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_SupportedNumChannels,
                               kAudioUnitScope_Global, 0, infos.data(), &size),
          "SupportedNumChannels");
    const auto lo = identity::min_channels, hi = identity::max_channels;
    const auto expected = identity::unequal_counts ? (hi - lo + 1) * (hi - lo + 1) : hi - lo + 1;
    require(static_cast<int>(infos.size()) == expected,
            "SupportedNumChannels lists " + std::to_string(infos.size()) + " pairs, expected " +
                std::to_string(expected));
    bool crossover_listed = false;
    for (auto& i : infos) {
        require(i.inChannels >= lo && i.inChannels <= hi && i.outChannels >= lo &&
                    i.outChannels <= hi,
                "a channel pair outside the registry's range");
        crossover_listed = crossover_listed || (i.inChannels == 2 && i.outChannels == 6);
    }
    require(crossover_listed, "2 to 6 channels is not offered");

    // Formats the engine cannot take are refused when the host proposes them.
    auto nine = au_test::format(48000, 9);
    require(AudioUnitSetProperty(unit.get(), kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Output, 0, &nine, sizeof nine) != noErr,
            "9 channels accepted");
    auto fractional = au_test::format(44100.5, 2);
    require(AudioUnitSetProperty(unit.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
                                 0, &fractional, sizeof fractional) != noErr,
            "a fractional sample rate accepted");
    auto interleaved = au_test::format(48000, 2);
    interleaved.mFormatFlags &= ~kAudioFormatFlagIsNonInterleaved;
    interleaved.mBytesPerFrame = interleaved.mBytesPerPacket = 2 * sizeof(float);
    require(AudioUnitSetProperty(unit.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
                                 0, &interleaved, sizeof interleaved) != noErr,
            "interleaved stereo accepted");
    // Different input and output rates: Convolver does not resample.
    auto in = au_test::format(44100, 2), out = au_test::format(48000, 2);
    check(AudioUnitSetProperty(unit.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
                               0, &in, sizeof in),
          "input format");
    check(AudioUnitSetProperty(unit.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
                               0, &out, sizeof out),
          "output format");
    require(unit.initialize() != noErr, "initialized with different input and output rates");
    // 2 to 6 initializes and renders.
    check(unit.configure(48000, 2, 6, 512), "2 to 6 formats");
    check(unit.initialize(), "2 to 6 initialization");
    Buffers b(2, 6, 512);
    std::uint32_t seed = 1;
    b.noise(seed);
    bool silent = false;
    check(render(unit, b, 512, false, silent), "2 to 6 render");
    // An empty instance with unequal counts plays silence (shared adapter contract).
    require(b.output_silent(), "an empty 2 to 6 instance is not silent");
}

// Counts property notifications, which may arrive on the unit's worker
// thread (a Latency change).
struct Notifications {
    std::mutex lock;
    std::map<AudioUnitPropertyID, int> counts;
    static void listener(void* self, AudioUnit, AudioUnitPropertyID id, AudioUnitScope,
                         AudioUnitElement) {
        auto& n = *static_cast<Notifications*>(self);
        std::lock_guard guard(n.lock);
        ++n.counts[id];
    }
    int of(AudioUnitPropertyID id) {
        std::lock_guard guard(lock);
        return counts[id];
    }
    void clear() {
        std::lock_guard guard(lock);
        counts.clear();
    }
};

// Waits up to five seconds for `done`, as a host waits for a notification.
template <class Done> bool eventually(Done done) {
    for (int i = 0; i < 5000; ++i) {
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

void timing_in_seconds() {
    Unit unit;
    Notifications seen;
    for (auto id : {kAudioUnitProperty_Latency, kAudioUnitProperty_TailTime})
        check(AudioUnitAddPropertyListener(unit.get(), id, &Notifications::listener, &seen),
              "property listener");
    const auto filter = crossover(48000);
    const auto tail = configuration_tail_frames(filter);
    check(unit.configure(48000, 2, 6, 512), "formats");
    require(unit.seconds(kAudioUnitProperty_Latency) == 256.0 / 48000,
            "latency at 48 kHz is not 256 frames in seconds");
    check(unit.load(state_of(filter)), "load before initialization");
    require(seen.of(kAudioUnitProperty_TailTime) > 0, "no tail notification after a load");
    require(unit.seconds(kAudioUnitProperty_TailTime) == static_cast<double>(tail) / 48000,
            "tail before initialization is not the filter's tail in seconds");
    check(unit.initialize(), "initialization");
    require(unit.seconds(kAudioUnitProperty_TailTime) == static_cast<double>(tail) / 48000,
            "tail after initialization is not the filter's tail in seconds");
    unit.uninitialize();
    seen.clear();
    // A new rate changes both values in seconds, and the host is told.
    check(unit.configure(96000, 2, 6, 512), "formats at 96 kHz");
    require(seen.of(kAudioUnitProperty_Latency) > 0 && seen.of(kAudioUnitProperty_TailTime) > 0,
            "no timing notifications after a sample-rate change");
    require(unit.seconds(kAudioUnitProperty_Latency) == 256.0 / 96000,
            "latency at 96 kHz is not 256 frames in seconds");
}

std::string string_value(CFDictionaryRef dict, const char* key) {
    const auto name = CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8);
    const auto value = CFDictionaryGetValue(dict, name);
    CFRelease(name);
    if (!value || CFGetTypeID(value) != CFStringGetTypeID())
        return {};
    char text[256] = {};
    CFStringGetCString(static_cast<CFStringRef>(value), text, sizeof text, kCFStringEncodingUTF8);
    return text;
}
std::optional<SInt64> number_value(CFDictionaryRef dict, const char* key) {
    const auto name = CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8);
    const auto value = CFDictionaryGetValue(dict, name);
    CFRelease(name);
    SInt64 n = 0;
    if (!value || CFGetTypeID(value) != CFNumberGetTypeID() ||
        !CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &n))
        return std::nullopt;
    return n;
}

// Render the same noise through two units and require identical output.
void same_output(Unit& a, Unit& b, std::size_t inputs, std::size_t outputs) {
    Buffers x(inputs, outputs, 300), y(inputs, outputs, 300);
    std::uint32_t seed = 7;
    for (int block = 0; block < 20; ++block) {
        x.noise(seed);
        y.in = x.in;
        bool s = false;
        check(render(a, x, 300, false, s), "render");
        check(render(b, y, 300, false, s), "render");
        require(x.out == y.out, "restored unit sounds different from the original");
    }
}

void class_info() {
    const auto filter = stereo(48000);
    Unit unit;
    check(unit.configure(48000, 2, 2, 512), "formats");
    check(unit.load(state_of(filter), "Living room"), "loading a .aupreset dictionary");
    const auto plist = unit.class_info_now();
    const auto dict = static_cast<CFDictionaryRef>(plist);
    const auto d = au_test::description();
    require(number_value(dict, kAUPresetVersionKey) == 0, "ClassInfo version is not 0");
    require(number_value(dict, kAUPresetTypeKey) == static_cast<SInt32>(d.componentType) &&
                number_value(dict, kAUPresetSubtypeKey) ==
                    static_cast<SInt32>(d.componentSubType) &&
                number_value(dict, kAUPresetManufacturerKey) ==
                    static_cast<SInt32>(d.componentManufacturer),
            "ClassInfo does not carry the component's codes");
    require(string_value(dict, kAUPresetNameKey) == "Living room",
            "the preset name was not restored");
    CFRelease(plist);
    const auto saved = unit.saved();
    require(saved.configuration && !saved.failed && !saved.bypass,
            "ClassInfo does not hold the loaded filter");
    require(encode_host_state(saved) == encode_host_state(state_of(filter)),
            "ClassInfo's filter differs from the loaded one");

    // A host saving and restoring the project: ClassInfo into a new instance.
    Unit copy;
    check(copy.configure(48000, 2, 2, 512), "formats");
    const auto again = unit.class_info_now();
    check(copy.restore(again), "restoring ClassInfo");
    CFRelease(again);
    check(unit.initialize(), "initialization");
    check(copy.initialize(), "initialization");
    same_output(unit, copy, 2, 2);
}

void rejected_presets() {
    const auto filter = stereo(48000);
    Unit unit;
    check(unit.configure(48000, 2, 2, 512), "formats");
    check(unit.initialize(), "initialization");
    check(unit.load(state_of(filter)), "first filter");
    require(!unit.failure().present, "a valid first filter left a failure");
    // PLUG-8: what failed, which request, and whether the unit plays silence.
    // `request` 0: any request number.
    const auto recorded = [&](HostFailureKind kind, UInt64 request, bool silent, const char* what) {
        const auto f = unit.failure();
        require(f.present && f.kind == static_cast<UInt32>(kind) &&
                    (!request || f.request == request) && (f.silent != 0) == silent &&
                    f.message[0] != 0,
                std::string(what) + " not recorded as its failure");
    };
    const auto keep = encode_host_state(state_of(filter));
    const auto still_kept = [&](const char* what) {
        require(encode_host_state(unit.saved()) == keep,
                std::string(what) + " replaced the running filter");
    };
    const auto modified = [&](auto&& change) {
        const auto original = au_test::class_info(state_of(stereo(48000, 50)));
        const auto copy =
            CFDictionaryCreateMutableCopy(nullptr, 0, static_cast<CFDictionaryRef>(original));
        CFRelease(original);
        change(copy);
        const auto status = unit.restore(copy);
        CFRelease(copy);
        return status;
    };
    const auto set_number = [](CFMutableDictionaryRef dict, const char* key, SInt32 value) {
        const auto name = CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8);
        const auto number = CFNumberCreate(nullptr, kCFNumberSInt32Type, &value);
        CFDictionarySetValue(dict, name, number);
        CFRelease(number);
        CFRelease(name);
    };
    require(modified([&](CFMutableDictionaryRef d) {
                set_number(d, kAUPresetSubtypeKey, 0x41424344);
            }) != noErr,
            "another plug-in's preset accepted");
    still_kept("another plug-in's preset");
    recorded(HostFailureKind::state_unreadable, 2, false, "another plug-in's preset");
    // The state of one part of a multi-part instrument, not of the whole unit.
    require(modified([&](CFMutableDictionaryRef d) { set_number(d, kAUPresetPartKey, 0); }) !=
                noErr,
            "a part preset accepted");
    still_kept("a part preset");
    require(modified([&](CFMutableDictionaryRef d) { set_number(d, kAUPresetVersionKey, 1); }) !=
                noErr,
            "a preset of an unknown version accepted");
    still_kept("a preset of an unknown version");
    require(modified([&](CFMutableDictionaryRef d) {
                const auto key = au_test::state_key();
                CFDictionaryRemoveValue(d, key);
                CFRelease(key);
            }) != noErr,
            "a preset without convolver-state accepted");
    still_kept("a preset without convolver-state");
    require(modified([&](CFMutableDictionaryRef d) {
                const auto key = au_test::state_key();
                const UInt8 junk[] = {'C', 'N', 'V', 'S', 1, 0};
                const auto data = CFDataCreate(nullptr, junk, sizeof junk);
                CFDictionarySetValue(d, key, data);
                CFRelease(data);
                CFRelease(key);
            }) != noErr,
            "a truncated convolver-state accepted");
    still_kept("a truncated convolver-state");
    recorded(HostFailureKind::state_unreadable, 0, false, "a truncated convolver-state");
    require(modified([&](CFMutableDictionaryRef d) {
                const auto key = au_test::state_key();
                const UInt8 newer[] = {'C', 'N', 'V', 'S', 3, 0, 0, 0, 0, 0, 0, 0};
                const auto data = CFDataCreate(nullptr, newer, sizeof newer);
                CFDictionarySetValue(d, key, data);
                CFRelease(data);
                CFRelease(key);
            }) != noErr,
            "state from a newer Convolver accepted");
    still_kept("state from a newer Convolver");
    recorded(HostFailureKind::state_newer, 0, false, "state from a newer Convolver");
    // AUBase reads "data" (parameter values) without bounds checks; this unit
    // reads it itself with them, so a damaged one is harmless.
    require(modified([&](CFMutableDictionaryRef d) {
                const UInt8 junk[] = {0, 0, 0, 0, 0xff, 0xff, 0xff};
                const auto data = CFDataCreate(nullptr, junk, sizeof junk);
                CFDictionarySetValue(d, CFSTR(kAUPresetDataKey), data);
                CFRelease(data);
            }) == noErr,
            "a preset with unreadable parameter data rejected");
    require(encode_host_state(unit.saved()) == encode_host_state(state_of(stereo(48000, 50))),
            "the preset with unreadable parameter data did not load");
    require(!unit.failure().present, "a successful load did not clear the failure");
    // Not a dictionary at all.
    const auto text = CFSTR("not a preset");
    require(unit.restore(text) != noErr, "a string accepted as ClassInfo");

    // A failed first load is saved as such, and restoring it plays silence.
    Unit failed;
    check(failed.configure(48000, 2, 2, 512), "formats");
    check(failed.initialize(), "initialization");
    require(failed.load(state_of(stereo(44100))) != noErr, "a 44.1 kHz filter accepted at 48 kHz");
    {
        const auto f = failed.failure();
        require(f.present && f.kind == static_cast<UInt32>(HostFailureKind::sample_rate) &&
                    f.request == 1 && f.silent && f.sample_rate == 44100 &&
                    std::string(f.message).find("44100 Hz") != std::string::npos,
                "a failed first load not recorded as a sample-rate failure");
    }
    const auto saved = failed.saved();
    require(saved.failed && !saved.configuration, "a failed first load was not saved as failed");
    Unit reopened;
    check(reopened.configure(48000, 2, 2, 512), "formats");
    check(reopened.load(saved), "restoring a saved failed load");
    check(reopened.initialize(), "initialization");
    require(reopened.failure().present &&
                reopened.failure().kind == static_cast<UInt32>(HostFailureKind::saved_failure) &&
                reopened.failure().silent,
            "a reopened failed load not recorded as a saved failure");
    Buffers b(2, 2, 512);
    std::uint32_t seed = 3;
    b.noise(seed);
    bool silent = false;
    check(render(reopened, b, 512, false, silent), "render");
    require(b.output_silent(), "a reopened failed load is not silent");
}

void bypass_property() {
    Unit unit;
    check(unit.configure(48000, 2, 2, 512), "formats");
    check(unit.load(state_of(stereo(48000))), "load");
    require(!unit.bypass(), "a new unit is bypassed");
    unit.set_bypass(true);
    require(unit.bypass(), "bypass does not read back");
    // The host owns bypass in Audio Units: ClassInfo does not record it, and
    // restoring a preset (even one whose state says bypassed) leaves it alone.
    require(!unit.saved().bypass, "bypass was saved in ClassInfo");
    auto bypassed = state_of(stereo(48000));
    bypassed.bypass = true;
    unit.set_bypass(false);
    check(unit.load(bypassed), "load a state that says bypassed");
    require(!unit.bypass(), "restoring a preset switched bypass on");
    unit.set_bypass(true);
    check(unit.load(state_of(stereo(48000))), "load a state that says not bypassed");
    require(unit.bypass(), "restoring a preset switched bypass off");
    unit.set_bypass(false);
    check(unit.initialize(), "initialization");
    // Filtered and dry signals are crossfaded with weights that sum to one, so
    // no output can exceed the larger of the filter's gain bound (the sum of
    // its absolute taps, times the input's peak of 0.5) and the dry peak.
    float bound = 0.5f;
    for (const auto& path : stereo(48000).paths) {
        double sum = 0;
        for (auto tap : path.impulse)
            sum += std::abs(tap);
        bound = std::max(bound, static_cast<float>(0.5 * sum));
    }
    bound *= 1.001f;
    // Hosts switch bypass from their own threads while audio runs.
    std::atomic<bool> done{false};
    std::thread toggler([&] {
        for (int i = 0; !done.load(); ++i) {
            unit.set_bypass(i % 2 == 0);
            std::this_thread::yield();
        }
    });
    Buffers b(2, 2, 256);
    std::uint32_t seed = 11;
    try {
        for (int block = 0; block < 400; ++block) {
            b.noise(seed);
            bool silent = false;
            check(render(unit, b, 256, false, silent), "render while bypass changes");
            for (auto& channel : b.out)
                for (auto v : channel)
                    require(std::isfinite(v) && std::abs(v) <= bound,
                            "bypass switching broke the output");
        }
    } catch (...) {
        done = true;
        toggler.join();
        throw;
    }
    done = true;
    toggler.join();
}

// The output is flagged silent only once the filter's tail has drained.
void silence_flag() {
    const auto filter = stereo(48000, 2000);
    const auto tail = configuration_tail_frames(filter);
    Unit unit;
    check(unit.configure(48000, 2, 2, 256), "formats");
    check(unit.load(state_of(filter)), "load");
    check(unit.initialize(), "initialization");
    Buffers b(2, 2, 256);
    std::uint32_t seed = 5;
    b.noise(seed);
    bool silent = true;
    check(render(unit, b, 256, false, silent), "render");
    require(!silent, "output with signal flagged silent");
    b.silence();
    std::size_t frames = 0;
    for (; frames < tail + 4 * 256; frames += 256) {
        check(render(unit, b, 256, true, silent), "render");
        if (silent) {
            require(b.output_silent(), "output flagged silent but not silent");
            break;
        }
    }
    require(silent, "silent input never gave a silent output flag");
    require(frames + 256 >= tail, "the output was flagged silent before the tail drained");
}

void no_allocation_while_rendering() {
    Unit unit;
    check(unit.configure(48000, 2, 6, 512), "formats");
    check(unit.load(state_of(crossover(48000))), "load");
    check(unit.initialize(), "initialization");
    Buffers b(2, 6, 512);
    std::uint32_t seed = 9;
    bool silent = false;
    for (int block = 0; block < 4; ++block) { // Warm up.
        b.noise(seed);
        check(render(unit, b, 512, false, silent), "render");
    }
    unit.set_bypass(true);
    unit.reset();
    allocations = 0;
    counting = true;
    OSStatus status = noErr;
    for (int block = 0; block < 50 && status == noErr; ++block) {
        b.noise(seed);
        status = render(unit, b, block % 2 ? 512 : 61, false, silent);
    }
    counting = false;
    check(status, "render");
    require(allocations == 0,
            std::to_string(allocations.load()) + " C++ allocations while rendering");
}

// JUCE hosts initialize and uninitialize at the default 44.1 kHz to try a
// channel layout, then restore the project, then prepare at its real rate.
// The probe must not make a 48 kHz project's filter a failed first load.
void layout_probe_before_restore() {
    const auto filter = crossover(48000);
    Unit unit;
    check(unit.configure(44100, 2, 6, 512), "probe formats");
    check(unit.initialize(), "probe initialization");
    unit.uninitialize();
    check(unit.load(state_of(filter)), "restoring a 48 kHz project after a 44.1 kHz probe");
    check(unit.configure(48000, 2, 6, 512), "formats");
    check(unit.initialize(), "initialization");
    Buffers b(2, 6, 512);
    std::uint32_t seed = 13;
    b.noise(seed);
    bool silent = false;
    check(render(unit, b, 512, false, silent), "render");
    require(!b.output_silent(), "a layout probe turned the project's filter into a failed load");
    require(unit.saved().configuration.has_value(), "the project's filter was not kept");

    // Once audio has rendered, the formats are the host's: a later restore
    // that does not fit them is rejected at once and the filter is kept.
    unit.uninitialize();
    require(unit.load(state_of(crossover(44100))) != noErr,
            "a 44.1 kHz filter accepted after rendering at 48 kHz");
    require(encode_host_state(unit.saved()) == encode_host_state(state_of(filter)),
            "a rejected restore replaced the filter");
}

// PLUG-8: a processor that cannot be built at initialization is recorded as an
// activation failure, and the unit refuses to initialize.
void failed_initialization() {
    Unit unit;
    const UInt32 too_many = (1U << 20) + 1; // More frames per slice than HostProcessor takes.
    check(unit.configure(48000, 2, 2, too_many), "formats with too many frames per slice");
    require(unit.initialize() == kAudioUnitErr_FailedInitialization,
            "initialized with more frames per slice than the processor takes");
    const auto f = unit.failure();
    require(f.present && f.kind == static_cast<UInt32>(HostFailureKind::activation) && f.silent &&
                f.request == 0 &&
                std::string(f.message).find("could not be prepared") != std::string::npos,
            "a failed initialization was not recorded as an activation failure");
}

void render_requires_initialization() {
    Unit unit;
    check(unit.configure(48000, 2, 2, 512), "formats");
    Buffers b(2, 2, 64);
    bool silent = false;
    require(render(unit, b, 64, false, silent) == kAudioUnitErr_Uninitialized,
            "rendered without initialization");
}

// While it exists, the Convolver log goes to a file of this process's own
// (CTest's is shared with the other AU tests).
class PrivateLog {
  public:
    explicit PrivateLog(const std::string& name)
        : path_(std::filesystem::temp_directory_path() /
                ("convolver-au-" + name + "-" + std::to_string(std::random_device{}()) + ".log")) {
        if (const char* configured = std::getenv("CONVOLVER_LOG"))
            earlier_ = configured;
        setenv("CONVOLVER_LOG", path_.c_str(), 1);
    }
    PrivateLog(const PrivateLog&) = delete;
    PrivateLog& operator=(const PrivateLog&) = delete;
    ~PrivateLog() {
        if (earlier_)
            setenv("CONVOLVER_LOG", earlier_->c_str(), 1);
        else
            unsetenv("CONVOLVER_LOG");
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    std::string text() const {
        std::ifstream in(path_, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

  private:
    std::filesystem::path path_;
    std::optional<std::string> earlier_;
};
} // namespace

// AU-7: PLUG-10's settings as global parameters. Ranges and defaults, what
// they do to the audio, and saving and restoring them with ClassInfo.
AudioUnitParameterValue parameter(Unit& unit, AudioUnitParameterID id) {
    AudioUnitParameterValue v = -1;
    check(AudioUnitGetParameter(unit.get(), id, kAudioUnitScope_Global, 0, &v), "parameter value");
    return v;
}
void set_parameter(Unit& unit, AudioUnitParameterID id, AudioUnitParameterValue v) {
    check(AudioUnitSetParameter(unit.get(), id, kAudioUnitScope_Global, 0, v, 0), "set parameter");
}
// Renders constant input 0.5 on both channels for `blocks` blocks of 512 and
// returns the last sample of the left output.
float steady_output(Unit& unit, int blocks) {
    Buffers b(2, 2, 512);
    for (auto& channel : b.in)
        channel.assign(channel.size(), .5f);
    bool silent = false;
    for (int k = 0; k < blocks; ++k)
        check(render(unit, b, 512, false, silent), "settings render");
    return b.out[0].back();
}
void settings_parameters() {
    struct Expected {
        AudioUnitParameterID id;
        AudioUnitParameterUnit unit;
        float min, max, def;
    };
    for (const auto& e :
         {Expected{au::gain_parameter, kAudioUnitParameterUnit_Decibels, -100, 24, 0},
          Expected{au::mix_parameter, kAudioUnitParameterUnit_Percent, 0, 100, 100}}) {
        Unit unit;
        AudioUnitParameterInfo info{};
        UInt32 size = sizeof info;
        check(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_ParameterInfo,
                                   kAudioUnitScope_Global, e.id, &info, &size),
              "settings parameter info");
        require(info.unit == e.unit && info.minValue == e.min && info.maxValue == e.max &&
                    info.defaultValue == e.def &&
                    (info.flags & kAudioUnitParameterFlag_IsReadable) &&
                    (info.flags & kAudioUnitParameterFlag_IsWritable),
                "parameter " + std::to_string(e.id) + " info");
        if (info.flags & kAudioUnitParameterFlag_CFNameRelease)
            CFRelease(info.cfNameString);
        require(parameter(unit, e.id) == e.def, "parameter " + std::to_string(e.id) + " default");
        // Values outside the range are stored within it.
        set_parameter(unit, e.id, e.max + 1000);
        require(parameter(unit, e.id) == e.max, "parameter not clamped above");
        set_parameter(unit, e.id, e.min - 1000);
        require(parameter(unit, e.id) == e.min, "parameter not clamped below");
    }

    // Gain and Mix act on the audio: a one-tap filter of 0.5 on each channel.
    Configuration half{48000, 2, 2, 0, {}, {}, {}};
    for (std::size_t ch = 0; ch < 2; ++ch)
        half.paths.push_back({{.5f}, {{ch, 1}}, {{ch, 1}}});
    Unit unit;
    check(unit.configure(48000, 2, 2, 512), "settings formats");
    check(unit.load(state_of(half)), "settings filter");
    check(unit.initialize(), "settings initialization");
    require(std::abs(steady_output(unit, 2) - .25f) < 1e-5, "default settings changed the sound");
    set_parameter(unit, au::gain_parameter, -6);
    const double gain = std::pow(10.0, -6.0 / 20);
    require(std::abs(steady_output(unit, 2) - .25 * gain) < 1e-5, "Gain not applied");
    set_parameter(unit, au::mix_parameter, 50);
    require(std::abs(steady_output(unit, 2) - (.5 * .25 * gain + .25)) < 1e-5, "Mix not applied");

    // Saved with ClassInfo, restored into a new instance before it is
    // initialized, and applied when it is. The log line for the restore lists
    // them, since hosts may start the unit before restoring a project.
    const auto saved = unit.class_info_now();
    Unit copy;
    check(copy.configure(48000, 2, 2, 512), "copy formats");
    {
        PrivateLog log("settings");
        check(copy.restore(saved), "restore with settings");
        require(log.text().find("Restored settings: gain -6 dB, mix 50% wet, latency 256 "
                                "frames.") != std::string::npos,
                "the restore's log line does not list the settings");
    }
    require(parameter(copy, au::gain_parameter) == -6 && parameter(copy, au::mix_parameter) == 50,
            "settings not restored");
    check(copy.initialize(), "copy initialization");
    require(std::abs(steady_output(copy, 3) - (.5 * .25 * gain + .25)) < 1e-5,
            "restored settings not applied");
    // A preset from convolver-preset has no parameter values: settings keep theirs.
    check(copy.load(state_of(half)), "preset without settings");
    require(parameter(copy, au::gain_parameter) == -6 && parameter(copy, au::mix_parameter) == 50,
            "a preset without settings reset them");
    // Malformed parameter data: the filter loads, the settings keep their values.
    const auto mutable_copy =
        CFDictionaryCreateMutableCopy(nullptr, 0, static_cast<CFDictionaryRef>(saved));
    const UInt8 truncated[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0};
    const auto bad = CFDataCreate(nullptr, truncated, sizeof truncated);
    CFDictionarySetValue(mutable_copy, CFSTR(kAUPresetDataKey), bad);
    CFRelease(bad);
    set_parameter(copy, au::gain_parameter, 3);
    check(copy.restore(mutable_copy), "restore with malformed parameter data");
    require(parameter(copy, au::gain_parameter) == 3, "malformed data changed a setting");
    CFRelease(mutable_copy);
    // A rejected preset leaves the settings as they were.
    const auto wrong = au_test::class_info(state_of(stereo(44100)));
    const auto wrong_copy =
        CFDictionaryCreateMutableCopy(nullptr, 0, static_cast<CFDictionaryRef>(wrong));
    const auto good_data =
        CFDictionaryGetValue(static_cast<CFDictionaryRef>(saved), CFSTR(kAUPresetDataKey));
    CFDictionarySetValue(wrong_copy, CFSTR(kAUPresetDataKey), good_data);
    require(copy.restore(wrong_copy) != noErr, "wrong-rate preset accepted");
    require(parameter(copy, au::gain_parameter) == 3, "a rejected preset changed a setting");
    CFRelease(wrong_copy);
    CFRelease(wrong);
    CFRelease(saved);
    copy.uninitialize();
    unit.uninitialize();
}

// The frame at which a unit with a one-tap filter of 1 plays an impulse
// given at frame 0, rendering blocks of 512 frames; -1 if it never does.
long impulse_delay(Unit& unit) {
    Buffers b(2, 2, 512);
    bool silent = false;
    for (int block = 0; block < 20; ++block) {
        b.silence();
        if (block == 0)
            b.in[0][0] = b.in[1][0] = 1;
        check(render(unit, b, 512, false, silent), "latency render");
        for (std::size_t t = 0; t < 512; ++t)
            if (std::abs(b.out[0][t]) > .5f)
                return block * 512 + static_cast<long>(t);
    }
    return -1;
}

std::string value_name(Unit& unit, AudioUnitParameterID id, AudioUnitParameterValue value) {
    AudioUnitParameterStringFromValue request{id, &value, nullptr};
    UInt32 bytes = sizeof request;
    check(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_ParameterStringFromValue,
                               kAudioUnitScope_Global, 0, &request, &bytes),
          "parameter value name");
    require(request.outString != nullptr, "parameter value name missing");
    char text[256] = {};
    CFStringGetCString(request.outString, text, sizeof text, kCFStringEncodingUTF8);
    CFRelease(request.outString);
    return text;
}

// PLUG-10, AU-7: Latency, a menu of block sizes from 64 to 4096 frames, which
// takes effect at once (jrp, 1 November 2026). Uninitialized, the reported
// latency follows the choice. Initialized, a new processor with the chosen
// block size replaces the running one at the next render, and the host is
// told. Whatever thread sets it, the unit's own worker thread applies it. A
// restored Latency applies even when the host started the unit first, as
// GarageBand does. Saved with the settings.
void latency_parameter() {
    Unit unit;
    AudioUnitParameterInfo info{};
    UInt32 size = sizeof info;
    check(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global,
                               au::latency_parameter, &info, &size),
          "Latency parameter info");
    require(info.unit == kAudioUnitParameterUnit_Indexed && info.minValue == 0 &&
                info.maxValue == 6 && info.defaultValue == 2 &&
                (info.flags & kAudioUnitParameterFlag_IsWritable) &&
                (info.flags & kAudioUnitParameterFlag_ValuesHaveStrings) &&
                (info.flags & kAudioUnitParameterFlag_NonRealTime),
            "Latency is not an indexed, non-real-time parameter from 0 to 6, default 2");
    if (info.flags & kAudioUnitParameterFlag_CFNameRelease)
        CFRelease(info.cfNameString);
    CFArrayRef names = nullptr;
    size = sizeof names;
    check(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_ParameterValueStrings,
                               kAudioUnitScope_Global, au::latency_parameter, &names, &size),
          "Latency value strings");
    require(names && CFArrayGetCount(names) == 7, "Latency value strings");
    char first[32] = {};
    CFStringGetCString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(names, 0)), first,
                       sizeof first, kCFStringEncodingUTF8);
    CFRelease(names);
    require(std::string(first) == "64 frames", std::string("first Latency name: ") + first);
    require(value_name(unit, au::latency_parameter, 4) == "1024 frames",
            "Latency 4 reads " + value_name(unit, au::latency_parameter, 4));
    require(parameter(unit, au::latency_parameter) == 2, "Latency default");
    // Stored as a whole index within range.
    set_parameter(unit, au::latency_parameter, 99);
    require(parameter(unit, au::latency_parameter) == 6, "Latency not clamped above");
    set_parameter(unit, au::latency_parameter, -5);
    require(parameter(unit, au::latency_parameter) == 0, "Latency not clamped below");
    set_parameter(unit, au::latency_parameter, 2.4f);
    require(parameter(unit, au::latency_parameter) == 2, "Latency not rounded");

    Notifications seen;
    check(AudioUnitAddPropertyListener(unit.get(), kAudioUnitProperty_Latency,
                                       &Notifications::listener, &seen),
          "latency listener");
    Configuration one{48000, 2, 2, 0, {}, {}, {}};
    for (std::size_t ch = 0; ch < 2; ++ch)
        one.paths.push_back({{1.f}, {{ch, 1}}, {{ch, 1}}});
    check(unit.configure(48000, 2, 2, 512), "latency formats");
    check(unit.load(state_of(one)), "latency filter");
    const auto reports = [](Unit& u, double frames) {
        return eventually([&] { return u.seconds(kAudioUnitProperty_Latency) == frames / 48000; });
    };
    // The unit's worker applies a choice: uninitialized, the reported latency
    // follows it, and the host is told once.
    seen.clear();
    set_parameter(unit, au::latency_parameter, 3);
    require(eventually([&] { return seen.of(kAudioUnitProperty_Latency) == 1; }),
            "no notification for a new Latency");
    require(reports(unit, 512), "uninitialized latency is not the chosen 512 frames");
    set_parameter(unit, au::latency_parameter, 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(seen.of(kAudioUnitProperty_Latency) == 1, "a notification without a change");
    check(unit.initialize(), "initialization at 512 frames");
    require(impulse_delay(unit) == 512, "the audio is not delayed by 512 frames");

    // Chosen while initialized: applied at once, the host is told, and the
    // log says so.
    seen.clear();
    {
        PrivateLog log("latency-change");
        set_parameter(unit, au::latency_parameter, 0);
        require(eventually([&] {
                    return log.text().find("Latency changed from 512 to 64 frames") !=
                           std::string::npos;
                }),
                "no log line for the Latency change: " + log.text());
    }
    require(reports(unit, 64) &&
                eventually([&] { return seen.of(kAudioUnitProperty_Latency) == 1; }),
            "a Latency chosen while running was not applied and reported");
    require(impulse_delay(unit) == 64, "the audio is not delayed by the new 64 frames");

    // Set from a pre-render callback on a new audio thread (john's review of
    // 1e0b045 found 63 allocations there): the render allocates nothing on
    // that thread, and the worker applies the change.
    struct PreRender {
        AudioUnit unit;
        AudioUnitParameterValue index;
        OSStatus set = noErr;
        static OSStatus callback(void* self, AudioUnitRenderActionFlags* flags,
                                 const AudioTimeStamp*, UInt32, UInt32, AudioBufferList*) {
            auto& p = *static_cast<PreRender*>(self);
            if (*flags & kAudioUnitRenderAction_PreRender)
                p.set = AudioUnitSetParameter(p.unit, au::latency_parameter, kAudioUnitScope_Global,
                                              0, p.index, 0);
            return noErr;
        }
    } pre{unit.get(), 3};
    check(AudioUnitAddRenderNotify(unit.get(), &PreRender::callback, &pre), "render notify");
    OSStatus rendered = noErr;
    std::size_t counted = 0;
    std::thread([&] {
        Buffers r(2, 2, 512);
        bool silent = false;
        const auto before = allocations.load();
        counting = true;
        rendered = render(unit, r, 512, false, silent);
        counting = false;
        counted = allocations.load() - before;
    }).join();
    check(AudioUnitRemoveRenderNotify(unit.get(), &PreRender::callback, &pre),
          "remove render notify");
    check(rendered, "render with a pre-render Latency change");
    check(pre.set, "Latency set before a render");
    require(counted == 0,
            std::to_string(counted) + " C++ allocations on an audio thread that set the Latency");
    require(reports(unit, 512), "a Latency set before a render was not applied");
    require(impulse_delay(unit) == 512, "the audio is not delayed by 512 frames again");

    // Set from the input callback while this (the main) thread renders
    // (john's review found a deadlock there): the render finishes, and the
    // worker applies the change.
    OSStatus set_inside = noErr;
    unit.while_pulling = [&] {
        set_inside = AudioUnitSetParameter(unit.get(), au::latency_parameter,
                                           kAudioUnitScope_Global, 0, 0, 0);
    };
    {
        Buffers r(2, 2, 512);
        bool silent = false;
        const auto status = render(unit, r, 512, false, silent);
        unit.while_pulling = nullptr;
        check(status, "render with a Latency change inside it");
    }
    check(set_inside, "Latency set inside a render");
    require(reports(unit, 64), "a Latency set inside a render was not applied");
    require(impulse_delay(unit) == 64, "the audio is not delayed by 64 frames after a render");

    // Choices from two threads at once while this one renders (john's review
    // reproduced 1024 frames of delay reported as 64): once they stop, the
    // reported latency is the last choice, and the audio has it. Render
    // never uses a processor after it is freed (the sanitizer job runs this
    // under ASan, which catches it when the timing hits it).
    std::atomic<bool> done{false};
    const auto chooser = [&](int seed) {
        return std::thread([&, seed] {
            for (int i = seed; !done.load(); ++i) {
                set_parameter(unit, au::latency_parameter, static_cast<float>(i % 5));
                std::this_thread::yield();
            }
        });
    };
    std::thread left = chooser(0), right = chooser(3);
    Buffers b(2, 2, 512);
    std::uint32_t seed = 5;
    const auto stop = [&] {
        done = true;
        left.join();
        right.join();
    };
    try {
        for (int block = 0; block < 300; ++block) {
            b.noise(seed);
            bool silent = false;
            check(render(unit, b, 512, false, silent), "render while Latency changes");
            for (auto& channel : b.out)
                for (auto v : channel)
                    require(std::isfinite(v) && std::abs(v) <= .5f,
                            "Latency switching broke the output");
        }
    } catch (...) {
        stop();
        throw;
    }
    stop();
    const auto last =
        host_latencies[static_cast<std::size_t>(parameter(unit, au::latency_parameter))];
    require(reports(unit, static_cast<double>(last)),
            "after concurrent choices the reported latency is not the last choice, " +
                std::to_string(last) + " frames");
    require(impulse_delay(unit) == static_cast<long>(last),
            "after concurrent choices the audio is not delayed by the reported " +
                std::to_string(last) + " frames");

    // Out of memory at each allocation the worker makes for a change: the
    // reported latency is always the one the audio has, and a change is
    // never logged as failed once it plays (john's review: a failure while
    // formatting the log line reported a successful change as failed).
    never_fail = true;
    set_parameter(unit, au::latency_parameter, 2);
    require(reports(unit, 256), "Latency 256 before the allocation failures");
    bool completed = false;
    for (int k = 1; k <= 400 && !completed; ++k) {
        const auto before = std::lround(unit.seconds(kAudioUnitProperty_Latency) * 48000);
        const AudioUnitParameterValue target = before == 256 ? 0 : 2;
        const long frames = target == 0 ? 64 : 256;
        PrivateLog log("latency-oom");
        fail_countdown = k;
        set_parameter(unit, au::latency_parameter, target);
        // Done when the change is logged, or when an allocation failed. The
        // worker changes what plays only while it holds the unit's lock,
        // which saving the state waits for.
        eventually([&] {
            return fail_countdown.load() == 0 ||
                   log.text().find("Latency changed") != std::string::npos;
        });
        completed = fail_countdown.exchange(0) > 0; // No allocation failed.
        CFRelease(unit.class_info_now());
        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // For its log line.
        const auto reported = std::lround(unit.seconds(kAudioUnitProperty_Latency) * 48000);
        require(reported == before || reported == frames, "allocation failure " +
                                                              std::to_string(k) + ": reported " +
                                                              std::to_string(reported) + " frames");
        require(impulse_delay(unit) == reported, "allocation failure " + std::to_string(k) +
                                                     ": reported " + std::to_string(reported) +
                                                     " frames, but the audio has another latency");
        require(reported == before || log.text().find("could not be applied") == std::string::npos,
                "allocation failure " + std::to_string(k) +
                    ": a change that plays was logged as failed: " + log.text());
        if (reported == before) {
            // A refused choice stays until chosen again; choosing it again
            // without failures applies it.
            set_parameter(unit, au::latency_parameter, target);
            require(reports(unit, static_cast<double>(frames)),
                    "allocation failure " + std::to_string(k) + ": choosing again did not apply");
        }
    }
    never_fail = false;
    require(completed, "every one of 400 allocations failed");
    set_parameter(unit, au::latency_parameter, 0);
    require(reports(unit, 64), "Latency 64 again");

    // Saved with the state and restored, before initialization, as reported.
    const auto saved = unit.class_info_now();
    Unit copy;
    check(copy.configure(48000, 2, 2, 512), "copy formats");
    check(copy.restore(saved), "restore with Latency");
    require(parameter(copy, au::latency_parameter) == 0 && reports(copy, 64),
            "Latency not restored");
    check(copy.initialize(), "copy initialization");
    require(impulse_delay(copy) == 64, "restored Latency not applied");
    copy.uninitialize();

    // GarageBand's order: the unit starts with the defaults, then the
    // project is restored. The saved Latency plays at once.
    Unit late;
    check(late.configure(48000, 2, 2, 512), "late formats");
    check(late.initialize(), "late initialization at the default 256 frames");
    require(late.seconds(kAudioUnitProperty_Latency) == 256.0 / 48000, "late default latency");
    check(late.restore(saved), "restore after initialization");
    CFRelease(saved);
    require(reports(late, 64), "a Latency restored after initialization is not reported");
    require(impulse_delay(late) == 64, "a Latency restored after initialization does not play");
    late.uninitialize();
    unit.uninitialize();
}

// The text a host shows for the Status parameter's current value.
std::string status_text(Unit& unit) {
    AudioUnitParameterStringFromValue request{au::status_parameter, nullptr, nullptr};
    UInt32 bytes = sizeof request;
    check(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_ParameterStringFromValue,
                               kAudioUnitScope_Global, 0, &request, &bytes),
          "Status text");
    require(request.outString != nullptr, "Status text missing");
    char text[256] = {};
    CFStringGetCString(request.outString, text, sizeof text, kCFStringEncodingUTF8);
    CFRelease(request.outString);
    return text;
}

// jrp's review of 0.5.5, reproduced through the Audio Unit API: with no
// filter at Gain -20 dB an input of 0.5 played as 0.05 while Status said
// "audio passes unchanged", and after a refused first load at Mix 50% it
// played as 0.25 while Status said "Silent". Status and the start line now
// say what is heard.
void settings_in_diagnostics() {
    const auto contains = [](const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    };
    {
        PrivateLog log("gain-status");
        Unit unit;
        check(unit.configure(48000, 2, 2, 512), "gain status formats");
        set_parameter(unit, au::gain_parameter, -20);
        check(unit.initialize(), "gain status initialization");
        require(std::abs(steady_output(unit, 3) - .05f) < 1e-5, "Gain -20 dB without a filter");
        const auto shown = status_text(unit);
        require(shown == "No filter: audio passes at -20.0 dB (Gain and Mix). Load a Convolver "
                         "preset.",
                "no filter at Gain -20 dB: " + shown);
        require(contains(log.text(), "No filter, so audio passes at -20.0 dB, set by Gain and "
                                     "Mix. Settings: gain -20 dB, mix 100% wet, latency 256 "
                                     "frames."),
                "start line at Gain -20 dB: " + log.text());
        // Changed while it plays: the next read of Status follows.
        set_parameter(unit, au::gain_parameter, 0);
        require(contains(status_text(unit), "audio passes unchanged"),
                "Status after Gain returned to 0 dB: " + status_text(unit));
        unit.uninitialize();
    }
    PrivateLog log("mix-status");
    Unit unit;
    check(unit.configure(48000, 2, 2, 512), "mix status formats");
    set_parameter(unit, au::mix_parameter, 50);
    check(unit.initialize(), "mix status initialization");
    require(unit.load(state_of(stereo(44100))) != noErr, "wrong-rate preset accepted");
    require(std::abs(steady_output(unit, 3) - .25f) < 1e-5, "Mix 50% after a refused first load");
    const auto shown = status_text(unit);
    require(shown == "Load failed, only the dry signal plays: the filter is for 44100 Hz, the "
                     "host runs at 48000 Hz. Details in the Convolver log.",
            "refused first load at Mix 50%: " + shown);
    // Started again, the start line says the dry half plays.
    unit.uninitialize();
    check(unit.initialize(), "mix status restart");
    require(contains(log.text(), "With Mix at 50% wet, though, the dry signal still plays instead "
                                 "of silence: the input at -6.0 dB."),
            "start line after a refused first load at Mix 50%: " + log.text());
    // At Mix 100% it is silent again, and Status says so.
    set_parameter(unit, au::mix_parameter, 100);
    require(std::abs(steady_output(unit, 3)) < 1e-6, "Mix 100% after a refused first load");
    require(contains(status_text(unit), "Silent: the filter is for 44100 Hz"),
            "Status at Mix 100%: " + status_text(unit));
    unit.uninitialize();
}

// DIAG-1: the read-only Status parameter says in one line why the unit is
// silent or refused a preset, and the Convolver log explains in full.
void status_parameter() {
    const auto contains = [](const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    };
    // A log of this process's own: CTest's is shared with the other AU tests.
    const auto log_path =
        std::filesystem::temp_directory_path() /
        ("convolver-au-status-" + std::to_string(std::random_device{}()) + ".log");
    const char* configured = std::getenv("CONVOLVER_LOG");
    const std::optional<std::string> earlier =
        configured ? std::optional<std::string>(configured) : std::nullopt;
    setenv("CONVOLVER_LOG", log_path.c_str(), 1);
    const auto log_text = [&] {
        std::ifstream in(log_path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    Unit unit;
    const auto au = unit.get();
    AudioUnitParameterID ids[8]{};
    UInt32 size = sizeof ids;
    check(AudioUnitGetProperty(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids,
                               &size),
          "parameter list");
    // AU-7: the settings first, as generic views list them, then Status.
    require(size == 4 * sizeof(AudioUnitParameterID) && ids[0] == au::gain_parameter &&
                ids[1] == au::mix_parameter && ids[2] == au::latency_parameter &&
                ids[3] == au::status_parameter,
            "the parameters should be Gain, Mix, Latency and Status");
    AudioUnitParameterInfo info{};
    size = sizeof info;
    check(AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global,
                               au::status_parameter, &info, &size),
          "Status parameter info");
    require(info.unit == kAudioUnitParameterUnit_Indexed && info.minValue == 0 &&
                info.maxValue == 4 && (info.flags & kAudioUnitParameterFlag_IsReadable) &&
                !(info.flags & kAudioUnitParameterFlag_IsWritable) &&
                (info.flags & kAudioUnitParameterFlag_ValuesHaveStrings),
            "Status parameter not a read-only indexed parameter");
    if (info.flags & kAudioUnitParameterFlag_CFNameRelease)
        CFRelease(info.cfNameString);
    CFArrayRef names = nullptr;
    size = sizeof names;
    check(AudioUnitGetProperty(au, kAudioUnitProperty_ParameterValueStrings, kAudioUnitScope_Global,
                               au::status_parameter, &names, &size),
          "Status value strings");
    require(names && CFArrayGetCount(names) == 5, "Status value strings");
    CFRelease(names);
    const auto value = [&] {
        AudioUnitParameterValue v = -1;
        check(AudioUnitGetParameter(au, au::status_parameter, kAudioUnitScope_Global, 0, &v),
              "Status value");
        return static_cast<HostStatus>(std::lround(v));
    };
    // What a host shows for the current value.
    const auto shown = [&] {
        AudioUnitParameterStringFromValue request{au::status_parameter, nullptr, nullptr};
        UInt32 bytes = sizeof request;
        check(AudioUnitGetProperty(au, kAudioUnitProperty_ParameterStringFromValue,
                                   kAudioUnitScope_Global, 0, &request, &bytes),
              "Status text");
        require(request.outString != nullptr, "Status text missing");
        char text[256] = {};
        CFStringGetCString(request.outString, text, sizeof text, kCFStringEncodingUTF8);
        CFRelease(request.outString);
        return std::string(text);
    };
    require(shown() == "No filter loaded. Load a Convolver preset." &&
                value() == HostStatus::no_filter,
            "status before initialization: " + shown());
    check(unit.configure(48000, 2, 2, 512), "status formats");
    check(unit.initialize(), "status initialization");
    require(contains(shown(), "audio passes unchanged") && contains(log_text(), "Audio Unit #") &&
                contains(log_text(), "Started at 48000 Hz, 2 in, 2 out, blocks of up to 512 "
                                     "frames. No filter, so audio passes unchanged."),
            "empty start not shown or logged: " + shown());
    // A preset for another rate as the first load: silence, said and explained.
    require(unit.load(state_of(stereo(44100))) != noErr, "wrong-rate preset accepted");
    require(shown() == "Silent: the filter is for 44100 Hz, the host runs at 48000 Hz. Details in "
                       "the Convolver log." &&
                value() == HostStatus::load_failed,
            "failed first load not shown: " + shown());
    require(contains(log_text(), "Filter not loaded, so Convolver plays silence. The filter was "
                                 "made for 44100 Hz, but the host runs at 48000 Hz."),
            "failed first load not logged");
    check(unit.load(state_of(stereo(48000))), "status good preset");
    require(shown() == "Filter loaded: 48000 Hz, 2 in, 2 out." && value() == HostStatus::playing,
            "good load not shown: " + shown());
    // A host cannot write the status.
    check(AudioUnitSetParameter(au, au::status_parameter, kAudioUnitScope_Global, 0, 3, 0),
          "Status write");
    require(value() == HostStatus::playing, "a host write changed the status");
    // A wrong preset while a filter plays: it keeps playing, and both say so.
    require(unit.load(state_of(stereo(44100))) != noErr, "wrong-rate replacement accepted");
    require(contains(shown(), "Load failed, previous filter kept: the filter is for 44100 Hz") &&
                contains(log_text(), "Filter not loaded; the previous filter is kept."),
            "refused replacement not shown or logged: " + shown());
    // A state refused before it reaches the filter is logged too, with why.
    require(unit.restore(CFSTR("not a preset")) != noErr, "a string accepted as ClassInfo");
    require(contains(log_text(), "The saved state is not an Audio Unit preset dictionary."),
            "refused state not logged");
    // Another value reads as its name.
    AudioUnitParameterValue playing = 0;
    AudioUnitParameterStringFromValue request{au::status_parameter, &playing, nullptr};
    size = sizeof request;
    check(AudioUnitGetProperty(au, kAudioUnitProperty_ParameterStringFromValue,
                               kAudioUnitScope_Global, 0, &request, &size),
          "Status name");
    require(request.outString &&
                CFStringCompare(request.outString, CFSTR("Playing"), 0) == kCFCompareEqualTo,
            "Status value name");
    CFRelease(request.outString);
    // DIAG-1, jrp's reproduction: extreme but finite input and filter values
    // overflow the output. It is silenced, Render sets Status to Fault, and
    // the host's request for its text explains in the log.
    auto loud = stereo(48000, 8);
    for (auto& path : loud.paths)
        for (auto& v : path.impulse)
            v = std::numeric_limits<float>::max() / 4;
    check(unit.load(state_of(loud)), "loud preset");
    {
        Buffers b(2, 2, 512);
        bool silent = false;
        for (int block = 0; block < 3; ++block) {
            for (auto& channel : b.in)
                channel.assign(channel.size(), std::numeric_limits<float>::max() / 4);
            check(render(unit, b, 512, false, silent), "overflowing render");
            for (const auto& channel : b.out)
                for (auto v : channel)
                    require(std::isfinite(v), "overflowing output not silenced");
        }
    }
    require(value() == HostStatus::fault, "Render did not set Status to Fault");
    require(!contains(log_text(), "too large to represent"), "fault logged while rendering");
    require(shown() == "Fault: output too large, silenced and the filter restarted. Details in "
                       "the Convolver log.",
            "fault not shown: " + shown());
    require(contains(log_text(), "The output became too large to represent (") &&
                contains(log_text(), "Lower the level going into Convolver"),
            "fault not logged");
    check(unit.load(state_of(stereo(48000))), "load after a fault");
    require(value() == HostStatus::playing && contains(shown(), "Filter loaded"),
            "a load does not clear Fault: " + shown());
    // jrp's review of 0.4.43: Render must not write Fault over Load failed,
    // which comes first; an accepted load releases the signal.
    const auto render_invalid = [&] {
        Buffers b(2, 2, 512);
        bool silent = false;
        for (auto& channel : b.in)
            channel.assign(channel.size(), 0.f);
        b.in[0][0] = std::numeric_limits<float>::quiet_NaN();
        check(render(unit, b, 512, false, silent), "invalid-input render");
    };
    require(unit.load(state_of(stereo(44100))) != noErr && value() == HostStatus::load_failed,
            "refused replacement before invalid input");
    render_invalid();
    require(value() == HostStatus::load_failed, "Render wrote Fault over Load failed");
    check(unit.load(state_of(stereo(48000))), "good after refusal");
    render_invalid();
    require(value() == HostStatus::fault, "Render did not write Fault after an accepted load");
    unit.uninitialize();
    if (earlier)
        setenv("CONVOLVER_LOG", earlier->c_str(), 1);
    else
        unsetenv("CONVOLVER_LOG");
    std::filesystem::remove(log_path);
}

int main() {
    try {
        channel_capabilities();
        timing_in_seconds();
        class_info();
        rejected_presets();
        bypass_property();
        silence_flag();
        no_allocation_while_rendering();
        layout_probe_before_restore();
        render_requires_initialization();
        failed_initialization();
        status_parameter();
        settings_parameters();
        latency_parameter();
        settings_in_diagnostics();
        std::cout << "Audio Unit adapter checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        counting = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
