// SPDX-License-Identifier: GPL-2.0-or-later
// The Audio Unit's parameters and properties: Gain, Mix, Latency and Status, property
// dispatch, the window's report and diagnostics, status notifications and
// the Convolver log (see effect.cpp).
#include "convolver/io.hpp"
#include "core_foundation.hpp"
#include "effect.hpp"
#include "host_log.hpp"
#include <AudioToolbox/AudioUnitUtilities.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <string_view>

namespace convolver::au {
namespace {
// The Status parameter's values, in HostStatus order.
constexpr auto& status_names = host_status_names;

// The Latency parameter's value names, in host_latencies order.
std::string latency_name(std::size_t frames) {
    return std::to_string(frames) + " frames";
}

// A Latency value (an index) as a position in host_latencies: rounded,
// within range, and the default if it is not a number.
std::size_t latency_index(AudioUnitParameterValue value) noexcept {
    if (std::isnan(value))
        return static_cast<std::size_t>(default_latency_index);
    const auto last = static_cast<AudioUnitParameterValue>(host_latencies.size() - 1);
    return static_cast<std::size_t>(
        std::clamp<AudioUnitParameterValue>(std::round(value), 0, last));
}

// A path from the window as the file system spells it; empty if it has none.
std::string file_path(CFStringRef text) {
    const auto most = CFStringGetMaximumSizeOfFileSystemRepresentation(text);
    if (most <= 0)
        return {};
    std::string out(static_cast<std::size_t>(most), '\0');
    if (!CFStringGetFileSystemRepresentation(text, out.data(), most))
        return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

// AU-6: the component bundle that holds the window's classes: the bundle of
// this very image, because with two Convolver builds loaded in one host the
// shared identifier may name the other one, whose classes have other names.
// Not found when the unit was registered in-process without its bundle (the
// AU tests). Worked out once and kept for the life of the process.
CFBundleRef own_bundle() {
    static const int anchor = 0;
    Dl_info info{};
    if (!dladdr(&anchor, &info) || !info.dli_fname)
        return nullptr;
    // The executable is BUNDLE/Contents/MacOS/NAME.
    cf_owned<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(info.dli_fname),
        static_cast<CFIndex>(std::strlen(info.dli_fname)), false));
    for (int level = 0; level < 3 && url; ++level)
        url.reset(CFURLCreateCopyDeletingLastPathComponent(nullptr, url.get()));
    if (!url)
        return nullptr;
    cf_owned<CFBundleRef> bundle(CFBundleCreate(nullptr, url.get()));
    const auto id = bundle ? CFBundleGetIdentifier(bundle.get()) : nullptr;
    if (!id || CFStringCompare(id, CFSTR(CONVOLVER_AU_BUNDLE_ID), 0) != kCFCompareEqualTo)
        return nullptr;
    return bundle.release();
}

CFBundleRef view_bundle() {
    static const CFBundleRef bundle = own_bundle();
    return bundle;
}

// A numbered line without its "Request 3: " prefix; any other line whole.
std::string_view unnumbered(std::string_view line) {
    constexpr std::string_view prefix = "Request ";
    if (line.substr(0, prefix.size()) != prefix)
        return line;
    const auto end = line.find(": ", prefix.size());
    return end == std::string_view::npos ? line : line.substr(end + 2);
}
} // namespace

HostSettings Effect::settings() {
    HostSettings s;
    s.gain_db = Globals()->GetParameterRT(gain_parameter);
    s.mix = Globals()->GetParameterRT(mix_parameter) / 100.0;
    s.latency = host_latencies[latency_index(Globals()->GetParameterRT(latency_parameter))];
    return s; // Within range: store() clamps every value.
}

void Effect::store(AudioUnitParameterID id, AudioUnitParameterValue value) noexcept {
    if (id == gain_parameter)
        value = std::isnan(value)
                    ? 0.f
                    : std::clamp(value, static_cast<AudioUnitParameterValue>(min_gain_db),
                                 static_cast<AudioUnitParameterValue>(max_gain_db));
    else if (id == mix_parameter)
        value = std::isnan(value) ? 100.f : std::clamp<AudioUnitParameterValue>(value, 0, 100);
    else if (id == latency_parameter)
        value = static_cast<AudioUnitParameterValue>(latency_index(value));
    else
        return;
    static_cast<void>(Globals()->SetParameterOrError(id, value));
}

void Effect::follow_chosen_latency() {
    if (IsInitialized())
        return;
    const auto chosen = settings().latency;
    if (latency_.exchange(chosen) != chosen)
        PropertyChanged(kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
}

void Effect::log(const std::string& message, bool problem) noexcept {
    try {
        std::lock_guard lock(logging_);
        // The same line again is left out. A problem is left out even under
        // a new request number: GarageBand restored one preset five times in
        // a second on a mono track, and five identical failures pushed the
        // useful lines out of Copy diagnostics (jrp's run of AU-6). An
        // accepted load is always logged, so each reload has its line.
        if (message.empty() || message == logged_ ||
            (problem && unnumbered(message) == unnumbered(logged_)))
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
        // Gain and Mix change what plays without a filter (PLUG-10).
        status_ = host_status(filter_, known_layout(), status_log_written_, settings());
        if (host_) // Render must not show Fault over a load failure.
            host_->hold_fault_signal(filter_.failure_shown());
        Globals()->SetParameter(status_parameter,
                                static_cast<AudioUnitParameterValue>(status_.status), true);
    } catch (...) { // Out of memory: the previous status stays.
    }
}

// The text only, for the Gain and Mix set now. It never writes the Status
// value: Render may have written Fault there since the last update_status(),
// and only an update that has observed the faults may replace it (the
// independent review).
void Effect::refresh_status_text() noexcept {
    try {
        auto line = host_status(filter_, known_layout(), status_log_written_, settings());
        if (line.status == status_.status)
            status_.text = std::move(line.text);
    } catch (...) { // Out of memory: the previous text stays.
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
    // AU-6: Convolver's window rereads the report.
    PropertyChanged(report_property, kAudioUnitScope_Global, 0);
}

HostReport Effect::report_locked(std::string& faults) {
    // As for the Status text: faults Render signalled are explained here,
    // and Gain or Mix may have changed on another thread. Only an update
    // that has observed new faults rewrites the Status value, which Render
    // may have just set to Fault (refresh_status_text). A Fault Render wrote
    // that is not explained yet is taken in at once; faults that go on after
    // it at most every report_fault_interval, so an open window does not log
    // a line twice a second (the independent review).
    const auto now = std::chrono::steady_clock::now();
    const bool unexplained = status_.status != HostStatus::fault &&
                             Globals()->GetParameter(status_parameter) ==
                                 static_cast<AudioUnitParameterValue>(HostStatus::fault);
    if (unexplained || now - report_faults_at_ >= report_fault_interval) {
        faults = observe_faults();
        report_faults_at_ = now;
    }
    if (faults.empty())
        refresh_status_text();
    else
        update_status();
    HostReportFacts facts;
    facts.format = tag_;
    facts.host = known_layout();
    facts.max_frames = host_ ? GetMaxFramesPerSlice() : 0;
    facts.settings = settings();
    facts.latency_frames = latency_.load();
    facts.tail_frames = tail_.load();
    facts.log_written = status_log_written_;
    facts.chooses_files = true; // Choose Filter in the window (load_property).
#if defined(CONVOLVER_AU_READER_LIBSNDFILE)
    facts.file_reader = sndfile_version();
#else
    facts.file_reader = "macOS AudioToolbox";
#endif
    return host_report(filter_, facts);
}

HostReport Effect::report() {
    std::string faults;
    HostReport r;
    {
        // A load holds the lock while it prepares a filter, which can take a
        // while: the window then gets the last report at once rather than
        // waiting, and rereads when the load notifies (TODO AU-6).
        std::unique_lock lock(control_, std::try_to_lock);
        if (!lock.owns_lock()) {
            {
                std::lock_guard cached(report_cache_lock_);
                if (report_cache_)
                    return *report_cache_;
            }
            lock.lock(); // Nothing read yet: wait this once.
        }
        r = report_locked(faults);
        // Stored under control_, so a slower reader cannot replace a newer copy.
        try {
            std::lock_guard cached(report_cache_lock_);
            report_cache_ = r;
        } catch (...) { // Out of memory: the older copy stays.
        }
    }
    if (!faults.empty()) { // As when a host reads the Status text.
        log_faults(faults);
        notify_status();
    }
    return r;
}

HostStatusLine Effect::status() {
    std::lock_guard lock(control_);
    refresh_status_text(); // With the Gain and Mix set now, as hosts read it.
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
    case latency_parameter:
        // A menu of block sizes. A change needs a new processor, built at the
        // next Initialize, so hosts are asked not to automate it.
        FillInParameterName(info, CFSTR("Latency"), false);
        info.unit = kAudioUnitParameterUnit_Indexed;
        info.minValue = 0;
        info.maxValue = static_cast<AudioUnitParameterValue>(host_latencies.size() - 1);
        info.defaultValue = default_latency_index;
        info.flags = settable | kAudioUnitParameterFlag_ValuesHaveStrings |
                     kAudioUnitParameterFlag_NonRealTime;
        return noErr;
    default:
        return kAudioUnitErr_InvalidParameter;
    }
}

OSStatus Effect::GetParameterValueStrings(AudioUnitScope scope, AudioUnitParameterID id,
                                          CFArrayRef* out) {
    if (scope != kAudioUnitScope_Global || (id != status_parameter && id != latency_parameter))
        return kAudioUnitErr_InvalidProperty;
    if (!out) // Asked whether the strings exist.
        return noErr;
    std::vector<cf_owned<CFStringRef>> names;
    std::vector<CFTypeRef> values;
    try {
        if (id == status_parameter)
            for (const auto* name : status_names)
                names.push_back(cf_string(name));
        else
            for (const auto frames : host_latencies)
                names.push_back(cf_string(latency_name(frames)));
        for (const auto& name : names) {
            if (!name) // Out of memory: CFArrayCreate must not be handed a null.
                return kAudioUnitErr_InvalidProperty;
            values.push_back(name.get());
        }
    } catch (...) { // Out of memory.
        return kAudioUnitErr_InvalidProperty;
    }
    *out = CFArrayCreate(nullptr, values.data(), static_cast<CFIndex>(values.size()),
                         &kCFTypeArrayCallBacks);
    return *out ? noErr : kAudioUnitErr_InvalidProperty;
}

OSStatus Effect::SetParameter(AudioUnitParameterID id, AudioUnitScope scope,
                              AudioUnitElement element, AudioUnitParameterValue value,
                              UInt32 offset) AUSDK_RTSAFE {
    if (scope != kAudioUnitScope_Global || element != 0)
        return AUBase::SetParameter(id, scope, element, value, offset);
    if (id == status_parameter)
        return noErr;
    if (id != gain_parameter && id != mix_parameter && id != latency_parameter)
        return kAudioUnitErr_InvalidParameter;
    AUSDK_RT_UNSAFE_BEGIN("Clamping, then a store into the existing parameter map; a Latency "
                          "change is reported only while uninitialized, when nothing renders")
    store(id, value);
    if (id == latency_parameter)
        follow_chosen_latency();
    AUSDK_RT_UNSAFE_END
    return noErr;
}

OSStatus Effect::GetPropertyInfo(AudioUnitPropertyID id, AudioUnitScope scope,
                                 AudioUnitElement element, UInt32& size, bool& writable) {
    // Convolver's own properties are all global.
    if (scope != kAudioUnitScope_Global)
        return AUBase::GetPropertyInfo(id, scope, element, size, writable);
    if (id == kAudioUnitProperty_BypassEffect) {
        size = sizeof(UInt32);
        writable = true;
        return noErr;
    }
    if (id == test_failure_property) {
        size = sizeof(TestFailure);
        writable = false;
        return noErr;
    }
    if (id == kAudioUnitProperty_ParameterStringFromValue) {
        size = sizeof(AudioUnitParameterStringFromValue);
        writable = false;
        return noErr;
    }
    if (id == report_property || id == diagnostics_property) {
        size = sizeof(CFTypeRef);
        writable = false;
        return noErr;
    }
    if (id == load_property) {
        size = sizeof(CFStringRef);
        writable = true;
        return noErr;
    }
    if (id == kAudioUnitProperty_CocoaUI) {
        if (!view_bundle())
            return kAudioUnitErr_InvalidProperty; // Registered without the bundle (the tests).
        size = sizeof(AudioUnitCocoaViewInfo);
        writable = false;
        return noErr;
    }
    return AUBase::GetPropertyInfo(id, scope, element, size, writable);
}

OSStatus Effect::GetProperty(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element,
                             void* data) {
    if (scope != kAudioUnitScope_Global)
        return AUBase::GetProperty(id, scope, element, data);
    if (id == kAudioUnitProperty_BypassEffect) {
        const UInt32 value = bypass_.load() ? 1 : 0;
        std::memcpy(data, &value, sizeof value);
        return noErr;
    }
    if (id == test_failure_property) {
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
    if (id == report_property || id == diagnostics_property) {
        CFTypeRef value = nullptr;
        try {
            const auto r = report();
            if (id == report_property) {
                const auto fields = report_fields(r);
                cf_owned<CFMutableDictionaryRef> dict(CFDictionaryCreateMutable(
                    nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
                for (const auto& [key, text] : fields) {
                    const auto k = cf_string(key), v = cf_string(text);
                    if (dict && k && v)
                        CFDictionarySetValue(dict.get(), k.get(), v.get());
                }
                value = dict.release();
            } else {
                value = cf_string(diagnostics_text(r, tag_)).release();
            }
        } catch (...) { // Out of memory.
        }
        if (!value)
            return kAudioUnitErr_InvalidPropertyValue;
        std::memcpy(data, &value, sizeof value); // The caller releases it.
        return noErr;
    }
    if (id == load_property) {
        std::string path;
        try {
            std::lock_guard lock(control_);
            if (const auto& r = filter_.accepted(); r.source == HostFilterSource::file)
                path = r.path;
        } catch (...) {
            return kAudioUnitErr_InvalidPropertyValue;
        }
        const CFStringRef value = cf_string(path).release();
        if (!value)
            return kAudioUnitErr_InvalidPropertyValue;
        std::memcpy(data, &value, sizeof value); // The caller releases it.
        return noErr;
    }
    if (id == kAudioUnitProperty_CocoaUI) {
        const auto bundle = view_bundle();
        if (!bundle)
            return kAudioUnitErr_InvalidProperty;
        // The class names carry the version, so two Convolver builds in one
        // host keep their own windows (Objective-C has one class namespace).
        cf_owned<CFURLRef> location(CFBundleCopyBundleURL(bundle));
        cf_owned<CFStringRef> name(CFStringCreateWithCString(
            nullptr, CONVOLVER_AU_VIEW_FACTORY_NAME, kCFStringEncodingUTF8));
        if (!location || !name)
            return kAudioUnitErr_InvalidProperty;
        AudioUnitCocoaViewInfo info{location.release(), {name.release()}};
        std::memcpy(data, &info, sizeof info); // The caller releases the URL and the name.
        return noErr;
    }
    if (id == kAudioUnitProperty_ParameterStringFromValue) {
        // The Status parameter's current value reads as the unit's status
        // line; any other value (or no value: the current one) as its name.
        auto* request = static_cast<AudioUnitParameterStringFromValue*>(data);
        if (request->inParamID == latency_parameter) {
            // A Latency value reads as its menu name (auval asks, as for any
            // parameter whose values have strings).
            const auto value =
                request->inValue ? *request->inValue : Globals()->GetParameter(latency_parameter);
            try {
                request->outString =
                    cf_string(latency_name(host_latencies[latency_index(value)])).release();
            } catch (...) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            return request->outString ? noErr : kAudioUnitErr_InvalidPropertyValue;
        }
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
            else // Gain or Mix may have changed, on any thread (PLUG-10).
                refresh_status_text();
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
    if (scope != kAudioUnitScope_Global)
        return AUBase::SetProperty(id, scope, element, data, size);
    if (id == kAudioUnitProperty_BypassEffect) {
        if (!data || size < sizeof(UInt32))
            return kAudioUnitErr_InvalidPropertyValue;
        UInt32 value = 0;
        std::memcpy(&value, data, sizeof value);
        bypass_.store(value != 0);
        return noErr;
    }
    if (id == load_property) {
        CFStringRef path = nullptr;
        if (!data || size < sizeof path)
            return kAudioUnitErr_InvalidPropertyValue;
        std::memcpy(&path, data, sizeof path);
        if (!path || CFGetTypeID(path) != CFStringGetTypeID())
            return kAudioUnitErr_InvalidPropertyValue;
        try {
            return load_file(file_path(path));
        } catch (...) { // Out of memory before anything changed.
            return kAudioUnitErr_InvalidPropertyValue;
        }
    }
    return AUBase::SetProperty(id, scope, element, data, size);
}
} // namespace convolver::au
