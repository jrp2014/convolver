// SPDX-License-Identifier: GPL-2.0-or-later
#include "effect.hpp"
#include "convolver_sdk.hpp"
#include "host_log.hpp"
#include "presets.hpp"
#include <AudioToolbox/AudioUnitUtilities.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <exception>
#include <span>
#include <stdexcept>
#include <vector>

namespace convolver::au {
namespace {
// The Status parameter's values, in HostStatus order.
constexpr std::array<const char*, 5> status_names{"Playing", "No filter", "Load failed",
                                                  "Not checked yet", "Fault"};
static_assert(static_cast<std::size_t>(HostStatus::fault) == status_names.size() - 1);
constexpr std::array<AudioUnitParameterID, 3> parameter_order{gain_parameter, mix_parameter,
                                                              status_parameter};

// AU-7: the global parameter values in AUBase's saved "data" blob, read with
// bounds checks (AUBase's own reader has none): for each scope and element,
// big-endian scope, element and count, then count pairs of ID and value bits.
// Nothing when the blob is malformed.
std::optional<std::vector<std::pair<AudioUnitParameterID, AudioUnitParameterValue>>>
saved_parameters(CFDataRef data) {
    std::vector<std::pair<AudioUnitParameterID, AudioUnitParameterValue>> found;
    const auto* p = CFDataGetBytePtr(data);
    const auto size = static_cast<std::size_t>(CFDataGetLength(data));
    std::size_t at = 0;
    const auto word = [&](UInt32& out) {
        if (size - at < 4)
            return false;
        out = (UInt32{p[at]} << 24) | (UInt32{p[at + 1]} << 16) | (UInt32{p[at + 2]} << 8) |
              UInt32{p[at + 3]};
        at += 4;
        return true;
    };
    while (at < size) {
        UInt32 scope = 0, element = 0, count = 0;
        if (!word(scope) || !word(element) || !word(count) || count > (size - at) / 8)
            return std::nullopt;
        for (UInt32 i = 0; i < count; ++i) {
            UInt32 id = 0, bits = 0;
            word(id);
            word(bits);
            if (scope == kAudioUnitScope_Global && element == 0)
                found.emplace_back(id, std::bit_cast<AudioUnitParameterValue>(bits));
        }
    }
    return found;
}

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
    set_host_log_sdk(CONVOLVER_SDK_DESCRIPTION); // The Convolver log names the SDK (CI-6).
    // Not UseIndexedParameters: that numbers parameters from 0, and Status is
    // 1001 (plugins/identities.json), so the parameter map is used instead.
    Globals()->SetParameter(status_parameter, static_cast<AudioUnitParameterValue>(status_.status));
    // AU-7: the settings at their defaults, which keep the sound unchanged.
    const HostSettings defaults;
    Globals()->SetParameter(gain_parameter, static_cast<AudioUnitParameterValue>(defaults.gain_db));
    Globals()->SetParameter(mix_parameter,
                            static_cast<AudioUnitParameterValue>(100 * defaults.mix));
}

HostSettings Effect::settings() {
    HostSettings s;
    s.gain_db = Globals()->GetParameterRT(gain_parameter);
    s.mix = Globals()->GetParameterRT(mix_parameter) / 100.0;
    return checked_settings(s);
}

void Effect::store(AudioUnitParameterID id, AudioUnitParameterValue value) noexcept {
    if (id == gain_parameter)
        value = std::isnan(value)
                    ? 0.f
                    : std::clamp(value, static_cast<AudioUnitParameterValue>(min_gain_db),
                                 static_cast<AudioUnitParameterValue>(max_gain_db));
    else if (id == mix_parameter)
        value = std::isnan(value) ? 100.f : std::clamp<AudioUnitParameterValue>(value, 0, 100);
    else
        return;
    static_cast<void>(Globals()->SetParameterOrError(id, value));
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

void Effect::log_faults(const std::string& message) noexcept {
    try {
        std::lock_guard lock(logging_);
        if (message.empty())
            return;
        host_log(tag_.c_str(), message);
        logged_ = message; // Repeats are logged: the log counts each episode.
    } catch (...) {        // Out of memory: the line is dropped.
    }
}

std::string Effect::observe_faults() noexcept {
    if (!host_)
        return {};
    try {
        return fault_message(filter_.observe(host_->faults()));
    } catch (...) { // Out of memory for the message: the status still shows it.
        return {};
    }
}

void Effect::update_status() noexcept {
    try {
        status_log_written_ = host_log_written();
        status_ = host_status(filter_, known_layout(), status_log_written_);
        if (host_) // Render must not show Fault over a load failure.
            host_->hold_fault_signal(filter_.failure_shown());
        Globals()->SetParameter(status_parameter,
                                static_cast<AudioUnitParameterValue>(status_.status), true);
    } catch (...) { // Out of memory: the previous status stays.
    }
}

void Effect::notify_status() noexcept {
    {
        // The status was worked out before its lines were logged: if writing
        // them changed whether the log can be written, say so now.
        std::lock_guard lock(control_);
        if (status_log_written_ != host_log_written())
            update_status();
    }
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

OSStatus Effect::GetParameterList(AudioUnitScope scope, AudioUnitParameterID* list, UInt32& count) {
    if (scope != kAudioUnitScope_Global)
        return AUBase::GetParameterList(scope, list, count);
    count = static_cast<UInt32>(parameter_order.size());
    if (list)
        std::copy(parameter_order.begin(), parameter_order.end(), list);
    return noErr;
}

OSStatus Effect::GetParameterInfo(AudioUnitScope scope, AudioUnitParameterID id,
                                  AudioUnitParameterInfo& info) {
    if (scope != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidParameter;
    constexpr auto settable = kAudioUnitParameterFlag_HasCFNameString |
                              kAudioUnitParameterFlag_IsReadable |
                              kAudioUnitParameterFlag_IsWritable;
    switch (id) {
    case status_parameter:
        FillInParameterName(info, CFSTR("Status"), false);
        info.unit = kAudioUnitParameterUnit_Indexed;
        info.minValue = 0;
        info.maxValue = static_cast<AudioUnitParameterValue>(status_names.size() - 1);
        info.defaultValue = static_cast<AudioUnitParameterValue>(HostStatus::no_filter);
        // Readable, not writable: hosts show it and never automate it.
        info.flags = kAudioUnitParameterFlag_HasCFNameString | kAudioUnitParameterFlag_IsReadable |
                     kAudioUnitParameterFlag_ValuesHaveStrings;
        return noErr;
    case gain_parameter:
        FillInParameterName(info, CFSTR("Gain"), false);
        info.unit = kAudioUnitParameterUnit_Decibels;
        info.minValue = static_cast<AudioUnitParameterValue>(min_gain_db);
        info.maxValue = static_cast<AudioUnitParameterValue>(max_gain_db);
        info.defaultValue = 0;
        info.flags = settable;
        return noErr;
    case mix_parameter:
        FillInParameterName(info, CFSTR("Mix"), false);
        info.unit = kAudioUnitParameterUnit_Percent;
        info.minValue = 0;
        info.maxValue = 100;
        info.defaultValue = 100;
        info.flags = settable;
        return noErr;
    default:
        return kAudioUnitErr_InvalidParameter;
    }
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
    if (scope != kAudioUnitScope_Global || element != 0)
        return AUBase::SetParameter(id, scope, element, value, offset);
    if (id == status_parameter)
        return noErr;
    if (id != gain_parameter && id != mix_parameter)
        return kAudioUnitErr_InvalidParameter;
    AUSDK_RT_UNSAFE_BEGIN("Clamping, then a store into the existing parameter map")
    store(id, value);
    AUSDK_RT_UNSAFE_END
    return noErr;
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
    std::string message, faults;
    bool started = false, problem = true;
    HostSettings current;
    {
        std::lock_guard lock(control_);
        faults = observe_faults(); // Before the processor that counted them goes.
        try {
            const auto host = layout();
            current = settings();
            // A filter that does not suit the host plays silence, and is kept so
            // that correcting the rate or channels brings it back. Gain and Mix
            // apply from the first frame.
            auto next = std::make_unique<HostProcessor>(
                filter_.activate(host), GetMaxFramesPerSlice(), current, latency_frames);
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
                message = started_line(filter_, *initialized_, GetMaxFramesPerSlice(), &current);
            } catch (...) { // Out of memory for the message: the unit still plays.
            }
        }
        update_status();
    }
    log_faults(faults);
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
    std::string faults;
    {
        std::lock_guard lock(control_);
        faults = observe_faults(); // Before the processor that counted them goes.
        if (rendered_.load())
            committed_ = initialized_;
        host_.reset();
        update_status(); // Read when the host next asks; no notification while it tears down.
    }
    log_faults(faults);
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
        std::string text, faults;
        try {
            std::lock_guard lock(control_);
            // A host showing Fault, which Render wrote, asks for its text:
            // the faults are explained here, on the host's control thread.
            faults = observe_faults();
            if (!faults.empty())
                update_status();
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
        if (!faults.empty()) {
            log_faults(faults);
            notify_status();
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
    if (result != noErr)
        return result;
    if (previous.mSampleRate != next.mSampleRate)
        timing_changed(); // Seconds depend on the rate.
    {
        // Whether the filter counts as checked depends on the formats.
        std::lock_guard lock(control_);
        update_status();
    }
    notify_status();
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

std::string Effect::rejection_line() {
    std::lock_guard lock(control_);
    return rejected_line(filter_);
}

OSStatus Effect::SaveState(CFPropertyListRef* out) {
    if (!out)
        return kAudioUnitErr_InvalidPropertyValue;
    std::vector<std::byte> bytes;
    std::string faults;
    try {
        HostState state;
        {
            std::lock_guard lock(control_);
            // Hosts save often (autosave too): faults are explained here as well.
            faults = observe_faults();
            if (!faults.empty())
                update_status();
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
    if (!faults.empty()) {
        log_faults(faults);
        notify_status();
    }
    // AU-7: AUBase writes the parameter values (Gain and Mix, and Status,
    // which RestoreState ignores) under "data".
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

std::string Effect::reject(HostFailureKind kind, const std::string& why) {
    std::lock_guard lock(control_);
    auto faults = observe_faults(); // Explained under the filter that played.
    const bool was_failed = filter_.failed();
    filter_.reject(kind, why);
    // An empty instance that fails its first load plays silence, not identity.
    if (host_ && filter_.failed() && !was_failed) {
        try {
            // The filter is empty, so this is what activate() would give,
            // without restarting the fault count of a processor that counts on.
            host_->replace(unfiltered_configuration(layout(), true));
        } catch (...) {
        }
    }
    if (!host_ && filter_.failed())
        tail_.store(0);
    update_status();
    return faults;
}

OSStatus Effect::RestoreState(CFPropertyListRef plist) {
    if (!plist || CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
        log_faults(reject(HostFailureKind::state_unreadable,
                          "The saved state is not an Audio Unit preset dictionary."));
        log(rejection_line());
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
        log_faults(reject(HostFailureKind::state_newer,
                          std::string("The saved state could not be read: ") + e.what()));
        log(rejection_line());
        notify_status();
        return kAudioUnitErr_InvalidPropertyValue;
    } catch (const std::exception& e) {
        log_faults(reject(HostFailureKind::state_unreadable,
                          std::string("The saved state could not be read: ") + e.what()));
        log(rejection_line());
        notify_status();
        return kAudioUnitErr_InvalidPropertyValue;
    }

    // AU-7: Gain and Mix as saved with the state, applied if the state is
    // accepted. A preset from convolver-preset has none, and settings it does
    // not hold keep their values.
    std::optional<std::vector<std::pair<AudioUnitParameterID, AudioUnitParameterValue>>> saved;
    bool settings_unreadable = false;
    if (const auto data = CFDictionaryGetValue(dict, CFSTR(kAUPresetDataKey));
        data && CFGetTypeID(data) == CFDataGetTypeID()) {
        try {
            saved = saved_parameters(static_cast<CFDataRef>(data));
        } catch (...) { // Out of memory: the settings are left as they are.
        }
        settings_unreadable = !saved;
    }

    bool accepted = false, problem = true;
    std::string message, faults;
    {
        std::lock_guard lock(control_);
        faults = observe_faults(); // Explained under the filter that played; a load clears them.
        const bool was_failed = filter_.failed();
        // Initialized, a prepared replacement is published to the audio thread;
        // uninitialized, the next initialization installs the filter.
        const auto host = known_layout();
        // A bypass flag in the state (from another format's preset) is
        // ignored: the host owns bypass, as SaveState explains.
        try {
            // The uninitialized tail is worked out before anything changes: it
            // needs memory, and a request that runs out is rejected whole.
            std::size_t uninitialized_tail = 0;
            if (!host_ && next.configuration) {
                try {
                    uninitialized_tail = configuration_tail_frames(*next.configuration);
                } catch (...) {
                    filter_.reject(HostFailureKind::preparation,
                                   "The filter could not be prepared.");
                    throw;
                }
            }
            filter_.load(
                std::move(next), host,
                [&](const Configuration& c) {
                    if (host_)
                        host_->replace(c);
                },
                {}, host_.get());
            // Nothing from here on can throw: the request is accepted.
            tail_.store(host_ ? host_->tail_frames() : uninitialized_tail);
            accepted = true;
            if (saved)
                for (const auto& [id, value] : *saved)
                    store(id, value); // Status and unknown IDs are ignored.
        } catch (...) {
            // A failed first load plays silence rather than the unfiltered input.
            if (host_ && filter_.failed() && !was_failed) {
                try {
                    // The filter is empty, so this is what activate() would give,
                    // without restarting the fault count of a processor that counts on.
                    host_->replace(unfiltered_configuration(layout(), true));
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
                message = rejected_line(filter_);
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
    log_faults(faults);
    log(message, problem); // A restored failed load says why it plays silence.
    if (accepted && settings_unreadable)
        log("The preset's parameter values (its \"data\" entry) could not be read, so Gain and "
            "Mix keep their values.");
    notify_status();
    if (!accepted)
        return kAudioUnitErr_InvalidPropertyValue;
    if (saved) {
        // Hosts and generic views reread the settings.
        AudioUnitEvent event{};
        event.mEventType = kAudioUnitEvent_ParameterValueChange;
        for (const auto id : parameter_order) {
            event.mArgument.mParameter = {GetComponentInstance(), id, kAudioUnitScope_Global, 0};
            AUEventListenerNotify(nullptr, nullptr, &event);
        }
    }
    // The preset name and element names, as AUBase restores them. Its "data"
    // (parameter values) is left out: Gain and Mix were read above with bounds
    // checks, and AUBase reads that blob without them.
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
    // AU-7: Gain and Mix, ramped by the processor.
    host->set_gain_db(Globals()->GetParameterRT(gain_parameter));
    host->set_mix(Globals()->GetParameterRT(mix_parameter) / 100.0);
    processed = host->process(std::span(in).first(inputs), std::span(out).first(outputs), frames);
    // DIAG-1: a new processing fault shows as Status Fault at once, with no
    // logging or allocation here (an existing parameter's value is an atomic
    // store); the host's next request for its text explains it.
    // Not over a load failure, which comes first (hold_fault_signal).
    if (host->fault_to_signal()) {
        Globals()->SetParameter(status_parameter,
                                static_cast<AudioUnitParameterValue>(HostStatus::fault), true);
        host->fault_signalled();
    }
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
