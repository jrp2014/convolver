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
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <new>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> counting{false};
std::atomic<std::size_t> allocations{0};
} // namespace
void* operator new(std::size_t n) {
    if (counting.load(std::memory_order_relaxed))
        allocations.fetch_add(1, std::memory_order_relaxed);
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

struct Notifications {
    std::map<AudioUnitPropertyID, int> count;
    static void listener(void* self, AudioUnit, AudioUnitPropertyID id, AudioUnitScope,
                         AudioUnitElement) {
        ++static_cast<Notifications*>(self)->count[id];
    }
};

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
    require(seen.count[kAudioUnitProperty_TailTime] > 0, "no tail notification after a load");
    require(unit.seconds(kAudioUnitProperty_TailTime) == static_cast<double>(tail) / 48000,
            "tail before initialization is not the filter's tail in seconds");
    check(unit.initialize(), "initialization");
    require(unit.seconds(kAudioUnitProperty_TailTime) == static_cast<double>(tail) / 48000,
            "tail after initialization is not the filter's tail in seconds");
    unit.uninitialize();
    seen.count.clear();
    // A new rate changes both values in seconds, and the host is told.
    check(unit.configure(96000, 2, 6, 512), "formats at 96 kHz");
    require(seen.count[kAudioUnitProperty_Latency] > 0 &&
                seen.count[kAudioUnitProperty_TailTime] > 0,
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
    // has no parameters and ignores it, so a damaged one is harmless.
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
    require(unit.saved().bypass, "bypass is not saved");
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

void render_requires_initialization() {
    Unit unit;
    check(unit.configure(48000, 2, 2, 512), "formats");
    Buffers b(2, 2, 64);
    bool silent = false;
    require(render(unit, b, 64, false, silent) == kAudioUnitErr_Uninitialized,
            "rendered without initialization");
}
} // namespace

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
        std::cout << "Audio Unit adapter checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        counting = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
