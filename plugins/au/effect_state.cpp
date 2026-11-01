// SPDX-License-Identifier: GPL-2.0-or-later
// The Audio Unit's saved state and filter loading: preset checks, the saved
// settings, SaveState and RestoreState, a file chosen in the window, and
// installing a new filter or recording its refusal (see effect.cpp).
#include "core_foundation.hpp"
#include "effect.hpp"
#include "filter_file.hpp"
#include "host_log.hpp"
#include "presets.hpp"
#include <AudioToolbox/AudioUnitUtilities.h>
#include <algorithm>
#include <bit>
#include <exception>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

namespace convolver::au {
namespace {
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

CFStringRef state_key() {
    static const CFStringRef key = CFStringCreateWithBytes(
        nullptr, reinterpret_cast<const UInt8*>(presets::au_state_key.data()),
        static_cast<CFIndex>(presets::au_state_key.size()), kCFStringEncodingUTF8, false);
    return key;
}

// The checks AUBase::RestoreState makes, before anything changes: Apple's
// preset version and this component's subtype and manufacturer.
std::string foreign(CFDictionaryRef dict, const AudioComponentDescription& desc) {
    const auto number = [&](CFStringRef key, SInt32& out) {
        const auto value = CFDictionaryGetValue(dict, key);
        return value && CFGetTypeID(value) == CFNumberGetTypeID() &&
               CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type, &out);
    };
    SInt32 version = -1, subtype = 0, manufacturer = 0;
    if (CFDictionaryContainsKey(dict, CFSTR(kAUPresetPartKey)))
        return "The preset holds the state of one part of a multi-part instrument, not of a "
               "whole effect. Make a Convolver preset with convolver-preset.";
    if (!number(CFSTR(kAUPresetVersionKey), version) || version != 0)
        return "The preset is not an Audio Unit preset this plug-in can read (its version "
               "entry is missing or not 0).";
    if (!number(CFSTR(kAUPresetSubtypeKey), subtype) ||
        !number(CFSTR(kAUPresetManufacturerKey), manufacturer) ||
        static_cast<UInt32>(subtype) != desc.componentSubType ||
        static_cast<UInt32>(manufacturer) != desc.componentManufacturer)
        return "The preset belongs to another plug-in. Make a Convolver preset with "
               "convolver-preset.";
    return {};
}
} // namespace

std::string Effect::error() {
    std::lock_guard lock(control_);
    return filter_.error();
}

std::string Effect::rejection_line() noexcept {
    try {
        std::lock_guard lock(control_);
        return rejected_line(filter_);
    } catch (...) { // Out of memory for the line: it is dropped.
        return {};
    }
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
    // AU-7: AUBase writes the parameter values (Gain, Mix and Latency, and
    // Status, which RestoreState ignores) under "data".
    if (const auto result = AUBase::SaveState(out); result != noErr)
        return result;
    // AUBase::SaveState creates a mutable dictionary.
    auto* dict = static_cast<CFMutableDictionaryRef>(const_cast<void*>(*out));
    const cf_owned<CFDataRef> data(CFDataCreate(
        nullptr, reinterpret_cast<const UInt8*>(bytes.data()), static_cast<CFIndex>(bytes.size())));
    if (!data) {
        CFRelease(*out);
        *out = nullptr;
        return kAudioUnitErr_InvalidPropertyValue;
    }
    CFDictionarySetValue(dict, state_key(), data.get());
    return noErr;
}

// An empty instance that fails its first load plays silence, not identity,
// without allocating: memory may have run out (ROBUST-3).
void Effect::silence_failed_first_load(bool was_failed) noexcept {
    if (host_ && filter_.failed() && !was_failed)
        host_->silence();
    if (!host_ && filter_.failed())
        tail_.store(0);
}

void Effect::reject(HostFailureKind kind, const std::string& why, const HostRequest& request,
                    const Configuration* carried) {
    std::string faults;
    {
        std::lock_guard lock(control_);
        faults = observe_faults(); // Explained under the filter that played.
        const bool was_failed = filter_.failed();
        filter_.reject(kind, why, request, carried);
        silence_failed_first_load(was_failed);
        update_status();
    }
    log_faults(faults);
    log(rejection_line());
    notify_status();
}

// RestoreState's and a chosen file's common step: `next` through
// HostFilter::load as `request`, then the log lines and notifications.
// `saved`: the settings from a restored state, applied if it is accepted.
bool Effect::install(HostState next, const HostRequest& request, const SavedSettings* saved,
                     bool settings_unreadable) {
    bool accepted = false, problem = true;
    std::string message, faults;
    {
        std::lock_guard lock(control_);
        if (const auto hook = load_started_for_tests)
            hook();
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
                                   "The filter could not be prepared.", request);
                    throw;
                }
            }
            filter_.load(
                std::move(next), host,
                [&](const Configuration& c) {
                    if (host_)
                        host_->replace(c);
                },
                request, host_.get());
            // Nothing from here on can throw: the request is accepted.
            tail_.store(host_ ? host_->tail_frames() : uninitialized_tail);
            accepted = true;
            if (saved)
                for (const auto& [id, value] : *saved)
                    store(id, value); // Status and unknown IDs are ignored.
        } catch (...) {
            silence_failed_first_load(was_failed);
        }
        try {
            // Why this request failed, or what was loaded (with the saved
            // failure it restored); not an older failure found at initialization.
            if (!accepted) {
                message = rejected_line(filter_);
            } else {
                problem = filter_.failure().has_value() || !filter_.notice().empty();
                message = loaded_line(filter_, host.has_value());
                // Hosts such as GarageBand restore a project after starting the
                // unit, so its "Started at" line shows the settings before this.
                if (saved && std::ranges::any_of(*saved, [](const auto& p) {
                        return p.first == gain_parameter || p.first == mix_parameter ||
                               p.first == latency_parameter;
                    })) {
                    const auto counts = host ? *host : layout();
                    message += " Restored settings: " +
                               settings_words(settings(), counts.inputs == counts.outputs) + ".";
                }
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
        log("The preset's parameter values (its \"data\" entry) could not be read, so Gain, Mix "
            "and Latency keep their values.");
    notify_status();
    return accepted;
}

// AU-6: a filter file chosen in Convolver's window, read with the step
// convolver-preset uses (PLUG-9) for the unit's rate and channels, so a list
// chooses its entry as in the CLI. The state then saved holds the filter
// itself, not the path, as a preset does. The file is read before the
// control lock is taken; preparing the filter runs under it, on the host's
// thread that set the property, as a state restore does.
OSStatus Effect::load_file(const std::string& path) {
    const HostRequest request{HostFilterSource::file, path, 0};
    HostLayout host;
    {
        std::lock_guard lock(control_);
        host = known_layout().value_or(layout());
    }
    FilterFileFailure why;
    // macOS's reader, not libsndfile, so the component needs nothing beyond
    // macOS (jrp's decision, 30 October 2026); CONVOLVER_AU_FILE_READER=LIBSNDFILE
    // builds the fallback.
#if defined(CONVOLVER_AU_READER_LIBSNDFILE)
    const auto& reader = detail::libsndfile_files();
#else
    const auto& reader = detail::audio_toolbox_files();
#endif
    auto next = read_filter_file(path, host, reader, why);
    bool accepted = false;
    if (!next) {
        reject(why.kind, why.message, request, why.filter());
    } else {
        accepted = install(std::move(*next), request, nullptr, false);
    }
    // The state the host saves has changed either way (a refusal is recorded
    // in it, and an empty instance then plays silence): hosts that listen for
    // ClassInfo changes, as for a preset chosen in the plug-in, can mark the
    // project as changed.
    PropertyChanged(kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0);
    // GarageBand 10.4.14 ignored that (jrp's run of d1dc0a2: no save prompt,
    // and the chosen filter was lost on reopening). An accepted file also
    // becomes the present preset, a user preset (number -1) named after the
    // file, as choosing a preset would; GarageBand then asks to save (jrp's
    // run of bf7adb6).
    if (accepted) {
        const auto name = std::filesystem::path(path).stem().string();
        if (const auto preset_name = cf_string(name.empty() ? path : name)) {
            static_cast<void>(NewCustomPresetSet(AUPreset{-1, preset_name.get()}));
            PropertyChanged(kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
        }
    }
    return accepted ? noErr : kAudioUnitErr_InvalidPropertyValue;
}

OSStatus Effect::RestoreState(CFPropertyListRef plist) {
    if (!plist || CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
        reject(HostFailureKind::state_unreadable,
               "The saved state is not an Audio Unit preset dictionary.");
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
        return kAudioUnitErr_InvalidPropertyValue;
    } catch (const std::exception& e) {
        reject(HostFailureKind::state_unreadable,
               std::string("The saved state could not be read: ") + e.what());
        return kAudioUnitErr_InvalidPropertyValue;
    }

    // AU-7: the settings as saved with the state, applied if the state is
    // accepted. A preset from convolver-preset has none, and settings it does
    // not hold keep their values.
    std::optional<SavedSettings> saved;
    bool settings_unreadable = false;
    if (const auto data = CFDictionaryGetValue(dict, CFSTR(kAUPresetDataKey));
        data && CFGetTypeID(data) == CFDataGetTypeID()) {
        try {
            saved = saved_parameters(static_cast<CFDataRef>(data));
        } catch (...) { // Out of memory: the settings are left as they are.
        }
        settings_unreadable = !saved;
    }

    const bool accepted =
        install(std::move(next), {}, saved ? &*saved : nullptr, settings_unreadable);
    if (!accepted)
        return kAudioUnitErr_InvalidPropertyValue;
    // A restored Latency plays at once. GarageBand starts the unit before it
    // restores a project, so waiting for the next start would leave it unused
    // (the independent review).
    request_latency();
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
    // (parameter values) is left out: the settings were read above with bounds
    // checks, and AUBase reads that blob without them.
    const cf_owned<CFMutableDictionaryRef> copy(CFDictionaryCreateMutableCopy(nullptr, 0, dict));
    if (!copy)
        return noErr;
    CFDictionaryRemoveValue(copy.get(), CFSTR(kAUPresetDataKey));
    static_cast<void>(AUBase::RestoreState(copy.get()));
    return noErr;
}
} // namespace convolver::au
