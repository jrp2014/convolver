// SPDX-License-Identifier: GPL-2.0-or-later
// A minimal Audio Unit host for the AU tests. The adapter is registered in
// this process with AudioComponentRegister, under the component's permanent
// codes, and driven only through the public Audio Unit API (properties,
// AudioUnitInitialize, AudioUnitRender, AudioUnitReset), as a host would.
#pragma once
#include "convolver/host.hpp"
#include "convolver_identities.hpp"
#include "effect.hpp"
#include "presets.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnitSDK/AUPlugInDispatch.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace convolver::au_test {
inline void check(OSStatus status, const std::string& what) {
    if (status != noErr)
        throw std::runtime_error("AU " + what + " failed with status " + std::to_string(status));
}

inline AudioComponentDescription description() {
    return {identity::four_char(identity::au_type), identity::four_char(identity::au_subtype),
            identity::four_char(identity::au_manufacturer), 0, 0};
}

// Registered once per process. The returned component is used directly, so an
// installed copy of the real bundle with the same codes cannot be picked instead.
inline AudioComponent component() {
    static const AudioComponent registered = [] {
        const auto d = description();
        auto c = ausdk::AUBaseFactory<au::Effect>::Register(
            d.componentType, d.componentSubType, d.componentManufacturer,
            CFSTR("Convolver: Convolver (test)"), 0x00010000);
        if (!c)
            throw std::runtime_error("AudioComponentRegister failed");
        return c;
    }();
    return registered;
}

inline AudioStreamBasicDescription format(double rate, UInt32 channels) {
    AudioStreamBasicDescription f{};
    f.mSampleRate = rate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked) |
                     static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved);
    f.mBytesPerPacket = f.mBytesPerFrame = sizeof(float);
    f.mFramesPerPacket = 1;
    f.mChannelsPerFrame = channels;
    f.mBitsPerChannel = 32;
    return f;
}

// The ClassInfo dictionary that a .aupreset from convolver-preset holds.
inline CFPropertyListRef class_info(const HostState& state, const std::string& name = "test") {
    const auto text = presets::au_preset(state, name);
    const auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(text.data()),
                                   static_cast<CFIndex>(text.size()));
    const auto plist =
        CFPropertyListCreateWithData(nullptr, data, kCFPropertyListImmutable, nullptr, nullptr);
    CFRelease(data);
    if (!plist)
        throw std::runtime_error("the .aupreset text is not a property list");
    return plist;
}

inline CFStringRef state_key() {
    return CFStringCreateWithBytes(
        nullptr, reinterpret_cast<const UInt8*>(presets::au_state_key.data()),
        static_cast<CFIndex>(presets::au_state_key.size()), kCFStringEncodingUTF8, false);
}

// One instance, configured and rendered through the Audio Unit API.
class Unit {
  public:
    Unit() {
        check(AudioComponentInstanceNew(component(), &unit_), "instantiation");
        AURenderCallbackStruct callback{&Unit::pull, this};
        check(AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback,
                                   kAudioUnitScope_Input, 0, &callback, sizeof callback),
              "render callback");
        // The host provides the input buffers, so in-place blocks really share memory.
        UInt32 allocate = 0;
        check(AudioUnitSetProperty(unit_, kAudioUnitProperty_ShouldAllocateBuffer,
                                   kAudioUnitScope_Input, 0, &allocate, sizeof allocate),
              "input buffer ownership");
    }
    ~Unit() {
        if (unit_) {
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
        }
    }
    Unit(const Unit&) = delete;
    Unit& operator=(const Unit&) = delete;
    AudioUnit get() const {
        return unit_;
    }

    OSStatus configure(double rate, UInt32 inputs, UInt32 outputs, UInt32 max_frames) {
        auto in = format(rate, inputs), out = format(rate, outputs);
        if (auto s = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                                          kAudioUnitScope_Input, 0, &in, sizeof in))
            return s;
        if (auto s = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                                          kAudioUnitScope_Output, 0, &out, sizeof out))
            return s;
        return AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice,
                                    kAudioUnitScope_Global, 0, &max_frames, sizeof max_frames);
    }
    OSStatus initialize() {
        return AudioUnitInitialize(unit_);
    }
    void uninitialize() {
        check(AudioUnitUninitialize(unit_), "uninitialization");
    }
    OSStatus restore(CFPropertyListRef plist) {
        return AudioUnitSetProperty(unit_, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                                    &plist, sizeof plist);
    }
    OSStatus load(const HostState& state, const std::string& name = "test") {
        const auto plist = class_info(state, name);
        const auto status = restore(plist);
        CFRelease(plist);
        return status;
    }
    // The ClassInfo dictionary; the caller releases it.
    CFPropertyListRef class_info_now() {
        CFPropertyListRef plist = nullptr;
        UInt32 size = sizeof plist;
        check(AudioUnitGetProperty(unit_, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                                   &plist, &size),
              "ClassInfo");
        return plist;
    }
    HostState saved() {
        const auto plist = class_info_now();
        const auto key = state_key();
        const auto value = CFDictionaryGetValue(static_cast<CFDictionaryRef>(plist), key);
        CFRelease(key);
        if (!value || CFGetTypeID(value) != CFDataGetTypeID()) {
            CFRelease(plist);
            throw std::runtime_error("ClassInfo has no convolver-state entry");
        }
        const auto data = static_cast<CFDataRef>(value);
        const auto bytes = std::span(reinterpret_cast<const std::byte*>(CFDataGetBytePtr(data)),
                                     static_cast<std::size_t>(CFDataGetLength(data)));
        HostState state;
        try {
            state = decode_host_state(bytes);
        } catch (...) {
            CFRelease(plist);
            throw;
        }
        CFRelease(plist);
        return state;
    }
    Float64 seconds(AudioUnitPropertyID id) {
        Float64 value = -1;
        UInt32 size = sizeof value;
        check(AudioUnitGetProperty(unit_, id, kAudioUnitScope_Global, 0, &value, &size),
              "timing property");
        return value;
    }
    void set_bypass(bool on) {
        const UInt32 value = on ? 1 : 0;
        check(AudioUnitSetProperty(unit_, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global,
                                   0, &value, sizeof value),
              "bypass");
    }
    bool bypass() {
        UInt32 value = 2, size = sizeof value;
        check(AudioUnitGetProperty(unit_, kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global,
                                   0, &value, &size),
              "bypass");
        return value != 0;
    }
    void reset() {
        check(AudioUnitReset(unit_, kAudioUnitScope_Global, 0), "reset");
    }
    // One render: planar host buffers (inputs and outputs may be the same
    // memory). `silent_input` marks the pulled input with OutputIsSilence.
    // Returns the status; `silent_output` tells whether the unit flagged its output.
    OSStatus render(std::span<const float* const> in, std::span<float* const> out, UInt32 frames,
                    bool silent_input, bool& silent_output) {
        inputs_ = in;
        frames_ = frames;
        silent_input_ = silent_input;
        std::vector<std::byte> storage(offsetof(AudioBufferList, mBuffers) +
                                       sizeof(AudioBuffer) * std::max<std::size_t>(out.size(), 1));
        auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
        list->mNumberBuffers = static_cast<UInt32>(out.size());
        for (std::size_t c = 0; c < out.size(); ++c)
            list->mBuffers[c] = {1, frames * static_cast<UInt32>(sizeof(float)), out[c]};
        AudioTimeStamp time{};
        time.mSampleTime = sample_time_;
        time.mFlags = kAudioTimeStampSampleTimeValid;
        sample_time_ += frames;
        AudioUnitRenderActionFlags flags = 0;
        const auto status = AudioUnitRender(unit_, &flags, &time, 0, frames, list);
        silent_output = (flags & kAudioUnitRenderAction_OutputIsSilence) != 0;
        return status;
    }

  private:
    static OSStatus pull(void* self, AudioUnitRenderActionFlags* flags, const AudioTimeStamp*,
                         UInt32, UInt32 frames, AudioBufferList* list) {
        auto& unit = *static_cast<Unit*>(self);
        if (frames != unit.frames_ || list->mNumberBuffers != unit.inputs_.size())
            return kAudio_ParamError;
        for (UInt32 c = 0; c < list->mNumberBuffers; ++c) {
            // The adapter only reads its input.
            list->mBuffers[c].mData = const_cast<float*>(unit.inputs_[c]);
            list->mBuffers[c].mDataByteSize = frames * sizeof(float);
            list->mBuffers[c].mNumberChannels = 1;
        }
        if (unit.silent_input_)
            *flags |= kAudioUnitRenderAction_OutputIsSilence;
        return noErr;
    }
    AudioUnit unit_ = nullptr;
    std::span<const float* const> inputs_;
    UInt32 frames_ = 0;
    bool silent_input_ = false;
    Float64 sample_time_ = 0;
};
} // namespace convolver::au_test
