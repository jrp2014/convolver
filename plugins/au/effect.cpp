// SPDX-License-Identifier: GPL-2.0-or-later
#include "effect.hpp"
#include "host_log.hpp"
#include "presets.hpp"
#include <AudioToolbox/AudioUnitUtilities.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <span>
#include <stdexcept>
#include <vector>

namespace convolver::au {
namespace {
// The Status parameter's values, in HostStatus order.
constexpr std::array<const char*, 4> status_names{"Playing", "No filter", "Load failed",
                                                  "Not checked yet"};

std::string next_tag() {
    static std::atomic<unsigned> instances{0};
    return "Audio Unit #" + std::to_string(++instances);
}

// Every pairing of 1 to 8 input and output channels (equal counts first), or
// equal counts only if the registry forbids unequal ones.
const std::vector<AUChannelInfo>& channel_infos() {
    static const auto infos = [] {
        std::vector<AUChannelInfo> v;
        const auto lo = static_cast<SInt16>(identity::min_channels);
        const auto hi = static_cast<SInt16>(identity::max_channels);
        for (SInt16 n = lo; n <= hi; ++n)
            v.push_back({n, n});
        if (identity::unequal_counts)
            for (SInt16 i = lo; i <= hi; ++i)
                for (SInt16 o = lo; o <= hi; ++o)
                    if (i != o)
                        v.push_back({i, o});
        return v;
    }();
    return infos;
}

CFStringRef state_key() {
    static const CFStringRef key = CFStringCreateWithBytes(
        nullptr, reinterpret_cast<const UInt8*>(presets::au_state_key.data()),
        static_cast<CFIndex>(presets::au_state_key.size()), kCFStringEncodingUTF8, false);
    return key;
}

std::string hz(double rate) {
    return std::to_string(static_cast<long long>(rate));
}

bool same(const HostLayout& a, const HostLayout& b) {
    return a.sample_rate == b.sample_rate && a.inputs == b.inputs && a.outputs == b.outputs;
}

// The checks AUBase::RestoreState makes, before anything changes: Apple's
// preset version and this component's subtype and manufacturer.
std::string foreign(CFDictionaryRef dict, const AudioComponentDescription& desc) {
    const auto number = [&](const char* key, SInt32& out) {
        const auto name = CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8);
        const auto value = CFDictionaryGetValue(dict, name);
        CFRelease(name);
        return value && CFGetTypeID(value) == CFNumberGetTypeID() &&
               CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type, &out);
    };
    SInt32 version = -1, subtype = 0, manufacturer = 0;
    if (CFDictionaryContainsKey(dict, CFSTR(kAUPresetPartKey)))
        return "The preset holds the state of one part of a multi-part instrument, not of a "
               "whole effect. Make a Convolver preset with convolver-preset.";
    if (!number(kAUPresetVersionKey, version) || version != 0)
        return "The preset is not an Audio Unit preset this plug-in can read (its version "
               "entry is missing or not 0).";
    if (!number(kAUPresetSubtypeKey, subtype) || !number(kAUPresetManufacturerKey, manufacturer) ||
        static_cast<UInt32>(subtype) != desc.componentSubType ||
        static_cast<UInt32>(manufacturer) != desc.componentManufacturer)
        return "The preset belongs to another plug-in. Make a Convolver preset with "
               "convolver-preset.";
    return {};
}
} // namespace

Effect::Effect(AudioComponentInstance instance)
    : AUBase(instance, 1, 1), status_(host_status(filter_, std::nullopt)), tag_(next_tag()) {
    Globals()->UseIndexedParameters(1);
    Globals()->SetParameter(status_parameter, static_cast<AudioUnitParameterValue>(status_.status));
}

void Effect::log(const std::string& message, bool problem) noexcept {
    try {
        std::lock_guard lock(logging_);
        if (message.empty() || message == logged_)
            return;
        host_log(tag_.c_str(), message, problem);
        logged_ = message;
    } catch (...) { // Out of memory: the line is dropped.
    }
}

void Effect::update_status() noexcept {
    try {
        status_ = host_status(filter_, known_layout());
        Globals()->SetParameter(status_parameter,
                                static_cast<AudioUnitParameterValue>(status_.status), true);
    } catch (...) { // Out of memory: the previous status stays.
    }
}

void Effect::notify_status() noexcept {
    AudioUnitEvent event{};
    event.mEventType = kAudioUnitEvent_ParameterValueChange;
    event.mArgument.mParameter = {GetComponentInstance(), status_parameter, kAudioUnitScope_Global,
                                  0};
    AUEventListenerNotify(nullptr, nullptr, &event);
}

HostStatusLine Effect::status() {
    std::lock_guard lock(control_);
    return status_;
}

OSStatus Effect::GetParameterInfo(AudioUnitScope scope, AudioUnitParameterID id,
                                  AudioUnitParameterInfo& info) {
    if (scope != kAudioUnitScope_Global || id != status_parameter)
        return kAudioUnitErr_InvalidParameter;
    FillInParameterName(info, CFSTR("Status"), false);
    info.unit = kAudioUnitParameterUnit_Indexed;
    info.minValue = 0;
    info.maxValue = static_cast<AudioUnitParameterValue>(status_names.size() - 1);
    info.defaultValue = static_cast<AudioUnitParameterValue>(HostStatus::no_filter);
    // Readable, not writable: hosts show it and never automate it.
    info.flags = kAudioUnitParameterFlag_HasCFNameString | kAudioUnitParameterFlag_IsReadable |
                 kAudioUnitParameterFlag_ValuesHaveStrings;
    return noErr;
}

OSStatus Effect::GetParameterValueStrings(AudioUnitScope scope, AudioUnitParameterID id,
                                          CFArrayRef* out) {
    if (scope != kAudioUnitScope_Global || id != status_parameter)
        return kAudioUnitErr_InvalidProperty;
    if (!out) // Asked whether the strings exist.
        return noErr;
    std::array<CFStringRef, status_names.size()> names{};
    for (std::size_t i = 0; i < names.size(); ++i)
        names[i] = CFStringCreateWithCString(nullptr, status_names[i], kCFStringEncodingUTF8);
    *out = CFArrayCreate(nullptr, reinterpret_cast<const void**>(names.data()),
                         static_cast<CFIndex>(names.size()), &kCFTypeArrayCallBacks);
    for (auto* name : names)
        if (name)
            CFRelease(name);
    return *out ? noErr : kAudioUnitErr_InvalidProperty;
}

OSStatus Effect::SetParameter(AudioUnitParameterID id, AudioUnitScope scope,
                              AudioUnitElement element, AudioUnitParameterValue value,
                              UInt32 offset) AUSDK_RTSAFE {
    if (scope == kAudioUnitScope_Global && id == status_parameter)
        return noErr;
    return AUBase::SetParameter(id, scope, element, value, offset);
}

HostLayout Effect::layout() {
    const auto in = Input(0).GetStreamFormat(), out = Output(0).GetStreamFormat();
    return {static_cast<int>(out.mSampleRate), in.mChannelsPerFrame, out.mChannelsPerFrame};
}

// Initialized, the host's configuration is the current formats. Uninitialized,
// it is known only while the formats still match the last initialization that
// rendered audio (see Cleanup):
// hosts may restore a project's state before setting its formats, and the
// default format (44.1 kHz stereo) is not a choice the host has made.
std::optional<HostLayout> Effect::known_layout() {
    const auto now = layout();
    if (host_ || (committed_ && same(*committed_, now)))
        return now;
    return std::nullopt;
}

OSStatus Effect::Initialize() {
    const auto in = Input(0).GetStreamFormat(), out = Output(0).GetStreamFormat();
    if (in.mSampleRate != out.mSampleRate) {
        log("The host set different input and output sample rates (" + hz(in.mSampleRate) +
            " and " + hz(out.mSampleRate) +
            " Hz). Convolver does not resample, so both must be the same.");
        return kAudioUnitErr_FormatNotSupported;
    }
    if (!identity::unequal_counts && in.mChannelsPerFrame != out.mChannelsPerFrame)
        return kAudioUnitErr_FormatNotSupported;
    std::string message;
    bool started = false, problem = true;
    {
        std::lock_guard lock(control_);
        try {
            const auto host = layout();
            // A filter that does not suit the host plays silence, and is kept so
            // that correcting the rate or channels brings it back.
            auto next = std::make_unique<HostProcessor>(filter_.activate(host),
                                                        GetMaxFramesPerSlice(), latency_frames);
            next->set_bypass(bypass_.load());
            next->reset();
            tail_.store(next->tail_frames());
            host_ = std::move(next);
            initialized_ = host;
            rendered_.store(false);
            reset_requested_.store(false);
            started = true;
        } catch (const std::exception& e) {
            message = activation_failed(e.what());
        } catch (...) {
            message = activation_failed("the processor could not be built.");
        }
        if (started) {
            // What plays and why: a filter restored before the formats were
            // known may not suit them. A rejected replacement's reason (the
            // working filter plays) was logged when it happened.
            try {
                const auto& f = filter_.failure();
                problem = (f && f->silent) || !filter_.notice().empty();
                message = started_line(filter_, *initialized_, GetMaxFramesPerSlice());
            } catch (...) { // Out of memory for the message: the unit still plays.
            }
        }
        update_status();
    }
    log(message, problem);
    notify_status();
    if (!started)
        return kAudioUnitErr_FailedInitialization;
    timing_changed();
    return noErr;
}

std::string Effect::activation_failed(const char* why) noexcept {
    try {
        filter_.fail_activation(why);
        return filter_.error();
    } catch (...) { // Out of memory for the message: nothing more to keep.
        return {};
    }
}

// An initialization binds the formats only once audio has rendered with it.
// JUCE hosts initialize and uninitialize at AUBase's default 44.1 kHz just to
// try a channel layout, then restore the project before preparing to play;
// counting that probe would turn a 48 kHz project's filter into a failed load.
void Effect::Cleanup() {
    std::lock_guard lock(control_);
    if (rendered_.load())
        committed_ = initialized_;
    host_.reset();
    update_status(); // Read when the host next asks; no notification while it tears down.
}

// The host may call Reset from any thread, so the audio thread performs it at
// the start of the next render, as it applies bypass.
OSStatus Effect::Reset(AudioUnitScope, AudioUnitElement) {
    reset_requested_.store(true);
    return noErr;
}

OSStatus Effect::GetPropertyInfo(AudioUnitPropertyID id, AudioUnitScope scope,
                                 AudioUnitElement element, UInt32& size, bool& writable) {
    if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_BypassEffect) {
        size = sizeof(UInt32);
        writable = true;
        return noErr;
    }
    if (scope == kAudioUnitScope_Global && id == test_failure_property) {
        size = sizeof(TestFailure);
        writable = false;
        return noErr;
    }
    if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_ParameterStringFromValue) {
        size = sizeof(AudioUnitParameterStringFromValue);
        writable = false;
        return noErr;
    }
    return AUBase::GetPropertyInfo(id, scope, element, size, writable);
}

OSStatus Effect::GetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element,
                             void* data) {
    if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_BypassEffect) {
        const UInt32 value = bypass_.load() ? 1 : 0;
        std::memcpy(data, &value, sizeof value);
        return noErr;
    }
    if (scope == kAudioUnitScope_Global && id == test_failure_property) {
        TestFailure t;
        std::lock_guard lock(control_);
        if (const auto& f = filter_.failure()) {
            t.present = 1;
            t.kind = static_cast<UInt32>(f->kind);
            t.source = static_cast<UInt32>(f->request.source);
            t.silent = f->silent ? 1 : 0;
            t.request = f->request.number;
            t.sample_rate = f->sample_rate;
            t.inputs = static_cast<UInt32>(f->inputs);
            t.outputs = static_cast<UInt32>(f->outputs);
            f->message.copy(t.message, sizeof t.message - 1);
        }
        std::memcpy(data, &t, sizeof t);
        return noErr;
    }
    if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_ParameterStringFromValue) {
        // The Status parameter's current value reads as the unit's status
        // line; any other value (or no value: the current one) as its name.
        auto* request = static_cast<AudioUnitParameterStringFromValue*>(data);
        if (request->inParamID != status_parameter)
            return kAudioUnitErr_InvalidParameter;
        std::string text;
        try {
            std::lock_guard lock(control_);
            const auto current = static_cast<AudioUnitParameterValue>(status_.status);
            const auto value = request->inValue ? *request->inValue : current;
            const auto index = static_cast<long>(std::lround(value));
            if (value == current)
                text = status_.text;
            else if (index >= 0 && index < static_cast<long>(status_names.size()))
                text = status_names[static_cast<std::size_t>(index)];
        } catch (...) {
            return kAudioUnitErr_InvalidPropertyValue;
        }
        if (text.empty())
            return kAudioUnitErr_InvalidPropertyValue;
        request->outString =
            CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8);
        return request->outString ? noErr : kAudioUnitErr_InvalidPropertyValue;
    }
    return AUBase::GetProperty(id, scope, element, data);
}

// Bypass is stored for Render to apply with HostProcessor::set_bypass: a 64-
// sample ramp to the input delayed by the latency (equal counts) or to silence
// (unequal counts), with the filter running on underneath. Never a reset.
OSStatus Effect::SetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element,
                             const void* data, UInt32 size) {
    if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_BypassEffect) {
        if (!data || size < sizeof(UInt32))
            return kAudioUnitErr_InvalidPropertyValue;
        UInt32 value = 0;
        std::memcpy(&value, data, sizeof value);
        bypass_.store(value != 0);
        return noErr;
    }
    return AUBase::SetProperty(id, scope, element, data, size);
}

bool Effect::StreamFormatWritable(AudioUnitScope, AudioUnitElement) {
    return !IsInitialized();
}

// Planar 32-bit float (AUBase's check), 1 to 8 channels, and a whole-number
// sample rate the engine accepts.
bool Effect::ValidFormat(AudioUnitScope scope, AudioUnitElement element,
                         const AudioStreamBasicDescription& format) {
    const auto rate = format.mSampleRate;
    return AUBase::ValidFormat(scope, element, format) &&
           format.mChannelsPerFrame >= static_cast<UInt32>(identity::min_channels) &&
           format.mChannelsPerFrame <= max_channels && std::isfinite(rate) && rate >= 1 &&
           rate <= 768000 && std::floor(rate) == rate;
}

OSStatus Effect::ChangeStreamFormat(AudioUnitScope scope, AudioUnitElement element,
                                    const AudioStreamBasicDescription& previous,
                                    const AudioStreamBasicDescription& next) {
    const auto result = AUBase::ChangeStreamFormat(scope, element, previous, next);
    if (result == noErr && previous.mSampleRate != next.mSampleRate)
        timing_changed(); // Seconds depend on the rate.
    return result;
}

UInt32 Effect::SupportedNumChannels(const AUChannelInfo** out) {
    const auto& infos = channel_infos();
    if (out)
        *out = infos.data();
    return static_cast<UInt32>(infos.size());
}

Float64 Effect::GetLatency() noexcept AUSDK_RTSAFE {
    const auto rate = GetOutput0().GetStreamFormat().mSampleRate;
    return rate > 0 ? static_cast<Float64>(latency_frames) / rate : 0;
}

Float64 Effect::GetTailTime() noexcept AUSDK_RTSAFE {
    const auto rate = GetOutput0().GetStreamFormat().mSampleRate;
    return rate > 0 ? static_cast<Float64>(tail_.load()) / rate : 0;
}

void Effect::timing_changed() {
    PropertyChanged(kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
    PropertyChanged(kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0);
}

std::string Effect::error() {
    std::lock_guard lock(control_);
    return filter_.error();
}

std::string Effect::rejection() {
    std::lock_guard lock(control_);
    const auto& r = filter_.rejection();
    return r ? r->message : std::string();
}

OSStatus Effect::SaveState(CFPropertyListRef* out) {
    if (!out)
        return kAudioUnitErr_InvalidPropertyValue;
    std::vector<std::byte> bytes;
    try {
        HostState state;
        {
            std::lock_guard lock(control_);
            state.configuration = filter_.filter();
            state.failed = filter_.failed(); // Reopening stays silent (state version 2).
            if (host_)
                host_->collect();
        }
        // Bypass stays out of ClassInfo: in Audio Units the host owns
        // kAudioUnitProperty_BypassEffect and restores it itself (owner's
        // decision, 23 October 2026: follow each format's convention).
        bytes = encode_host_state(state);
    } catch (...) {
        return kAudioUnitErr_InvalidPropertyValue;
    }
    if (const auto result = AUBase::SaveState(out); result != noErr)
        return result;
    // AUBase::SaveState creates a mutable dictionary.
    auto* dict = static_cast<CFMutableDictionaryRef>(const_cast<void*>(*out));
    const auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(bytes.data()),
                                   static_cast<CFIndex>(bytes.size()));
    if (!data) {
        CFRelease(*out);
        *out = nullptr;
        return kAudioUnitErr_InvalidPropertyValue;
    }
    CFDictionarySetValue(dict, state_key(), data);
    CFRelease(data);
    return noErr;
}

void Effect::reject(HostFailureKind kind, const std::string& why) {
    std::lock_guard lock(control_);
    const bool was_failed = filter_.failed();
    filter_.reject(kind, why);
    // An empty instance that fails its first load plays silence, not identity.
    if (host_ && filter_.failed() && !was_failed) {
        try {
            host_->replace(filter_.activate(layout()));
        } catch (...) {
        }
    }
    if (!host_ && filter_.failed())
        tail_.store(0);
    update_status();
}

OSStatus Effect::RestoreState(CFPropertyListRef plist) {
    if (!plist || CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
        reject(HostFailureKind::state_unreadable,
               "The saved state is not an Audio Unit preset dictionary.");
        log(rejection());
        notify_status();
        return kAudioUnitErr_InvalidPropertyValue;
    }
    const auto dict = static_cast<CFDictionaryRef>(plist);
    HostState next;
    try {
        if (auto why = foreign(dict, GetComponentDescription()); !why.empty())
            throw std::invalid_argument(why);
        const auto value = CFDictionaryGetValue(dict, state_key());
        if (!value || CFGetTypeID(value) != CFDataGetTypeID())
            throw std::invalid_argument(
                "The preset holds no Convolver filter (it has no convolver-state entry). Make "
                "the preset with convolver-preset, or save it from Convolver in the host.");
        const auto data = static_cast<CFDataRef>(value);
        const auto size = static_cast<std::size_t>(CFDataGetLength(data));
        if (size > max_host_state_bytes)
            throw std::invalid_argument("The saved filter is larger than the 64 MB limit.");
        next = decode_host_state(
            std::span(reinterpret_cast<const std::byte*>(CFDataGetBytePtr(data)), size));
    } catch (const NewerHostState& e) {
        reject(HostFailureKind::state_newer,
               std::string("The saved state could not be read: ") + e.what());
        log(rejection());
        notify_status();
        return kAudioUnitErr_InvalidPropertyValue;
    } catch (const std::exception& e) {
        reject(HostFailureKind::state_unreadable,
               std::string("The saved state could not be read: ") + e.what());
        log(rejection());
        notify_status();
        return kAudioUnitErr_InvalidPropertyValue;
    }

    bool accepted = false, problem = true;
    std::string message;
    {
        std::lock_guard lock(control_);
        const bool was_failed = filter_.failed();
        // Initialized, a prepared replacement is published to the audio thread;
        // uninitialized, the next initialization installs the filter.
        const auto host = known_layout();
        // A bypass flag in the state (from another format's preset) is
        // ignored: the host owns bypass, as SaveState explains.
        try {
            filter_.load(std::move(next), host, [&](const Configuration& c) {
                if (host_)
                    host_->replace(c);
            });
            if (host_)
                tail_.store(host_->tail_frames());
            else
                tail_.store(filter_.filter() ? configuration_tail_frames(*filter_.filter()) : 0);
            accepted = true;
        } catch (...) {
            // A failed first load plays silence rather than the unfiltered input.
            if (host_ && filter_.failed() && !was_failed) {
                try {
                    host_->replace(filter_.activate(layout()));
                } catch (...) {
                }
            }
            if (!host_ && filter_.failed())
                tail_.store(0);
        }
        try {
            // Why this request failed, or what was loaded (with the saved
            // failure it restored); not an older failure found at initialization.
            if (!accepted) {
                if (const auto& r = filter_.rejection())
                    message = r->message;
            } else {
                problem = filter_.failure().has_value() || !filter_.notice().empty();
                message = loaded_line(filter_, host.has_value());
            }
        } catch (...) { // Out of memory for the message: it is dropped.
        }
        update_status();
    }
    // Listeners may call back into the unit, so notify outside the lock.
    PropertyChanged(kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0);
    log(message, problem); // A restored failed load says why it plays silence.
    notify_status();
    if (!accepted)
        return kAudioUnitErr_InvalidPropertyValue;
    // The preset name and element names, as AUBase restores them. Its "data"
    // (parameter values) is left out: the only parameter is the read-only
    // Status, and AUBase reads that blob without bounds checks.
    const auto copy = CFDictionaryCreateMutableCopy(nullptr, 0, dict);
    if (!copy)
        return noErr;
    CFDictionaryRemoveValue(copy, CFSTR(kAUPresetDataKey));
    static_cast<void>(AUBase::RestoreState(copy));
    CFRelease(copy);
    return noErr;
}

OSStatus Effect::Render(AudioUnitRenderActionFlags& flags, const AudioTimeStamp& time,
                        UInt32 frames) noexcept AUSDK_RTSAFE {
    auto* host = host_.get();
    if (!host)
        return kAudioUnitErr_Uninitialized;
    auto& input = GetInput0();
    auto& output = GetOutput0();
    if (!input.IsActive())
        return kAudioUnitErr_NoConnection;
    if (const auto result = input.PullInput(flags, time, 0, frames); result != noErr)
        return result;
    // The source may mark its buffers silent instead of zeroing them.
    const bool silent_input = (flags & kAudioUnitRenderAction_OutputIsSilence) != 0;
    const auto in_list = input.GetBufferListOrError();
    const auto out_list = output.GetBufferListOrError();
    if (!in_list || !out_list)
        return kAudio_ParamError;
    const auto inputs = in_list->mNumberBuffers, outputs = out_list->mNumberBuffers;
    if (inputs > max_channels || outputs > max_channels)
        return kAudio_ParamError;
    std::array<const float*, max_channels> in{};
    std::array<float*, max_channels> out{};
    for (UInt32 c = 0; c < inputs; ++c)
        in[c] = silent_input ? nullptr : static_cast<const float*>(in_list->mBuffers[c].mData);
    for (UInt32 c = 0; c < outputs; ++c)
        out[c] = static_cast<float*>(out_list->mBuffers[c].mData);
    // HostProcessor's audio functions allocate nothing, lock nothing and do no
    // I/O (tests/native/host_tests.cpp and au.adapter count allocations), but
    // they are not annotated [[clang::nonblocking]], so Clang's effect analysis
    // is told about these three calls only; the rest of Render stays checked.
    bool processed = false;
    AUSDK_RT_UNSAFE_BEGIN("HostProcessor's audio functions are real-time safe, unannotated")
    if (reset_requested_.exchange(false))
        host->reset();
    host->set_bypass(bypass_.load(std::memory_order_relaxed));
    processed = host->process(std::span(in).first(inputs), std::span(out).first(outputs), frames);
    AUSDK_RT_UNSAFE_END
    if (!processed)
        return kAudio_ParamError;
    rendered_.store(true, std::memory_order_relaxed);
    // Silent only once the output is: a filter's tail outlasts silent input.
    bool silent = silent_input;
    for (UInt32 c = 0; silent && c < outputs; ++c)
        if (out[c])
            silent = std::all_of(out[c], out[c] + frames, [](float v) { return v == 0.f; });
    if (silent)
        flags |= kAudioUnitRenderAction_OutputIsSilence;
    else
        flags &= ~kAudioUnitRenderAction_OutputIsSilence;
    return noErr;
}
} // namespace convolver::au
