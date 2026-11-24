// SPDX-License-Identifier: GPL-2.0-or-later
// The edit controller (VST3-14: moved from processor.cpp, unchanged).
#include "editor.hpp"
#include "host_report.hpp"
#include "pluginterfaces/base/ibstream.h"
#include "processor.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace convolver::vst3 {
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
constexpr std::size_t max_controller_state = 4096; // Name, identity and headers fit easily.

void copy(const std::string& text, String128 out) {
    std::size_t i = 0;
    for (; i < text.size() && i < 127; ++i) // ASCII (HostStatusLine), widened.
        out[i] = static_cast<TChar>(static_cast<unsigned char>(text[i]));
    out[i] = 0;
}
} // namespace
tresult PLUGIN_API Controller::setState(IBStream* stream) {
    if (!stream)
        return kResultOk;
    std::string bytes;
    constexpr int32 chunk = 512;
    char buffer[chunk];
    while (bytes.size() <= max_controller_state) {
        int32 count = 0;
        if (stream->read(buffer, chunk, &count) != kResultOk || count <= 0)
            break;
        bytes.append(buffer, static_cast<std::size_t>(std::min(count, chunk)));
    }
    // Too long to be ours, or not ours: no name, as before PLUG-22.
    auto state =
        bytes.size() > max_controller_state ? ControllerState{} : decode_controller_state(bytes);
    {
        std::lock_guard lock(status_lock_);
        name_ = state.name;
        identity_ = state.identity;
    }
    // The processor puts the name in its report and the report's text; it
    // answers with a new status_message.
    if (auto message = owned(allocateMessage())) {
        message->setMessageID(name_message);
        if (auto* attributes = message->getAttributes()) {
            // No name sends neither (the SDK's host attributes leak an empty
            // binary value); decode_controller_state() gives both or neither.
            if (!state.name.empty()) {
                attributes->setBinary("name", state.name.data(),
                                      static_cast<uint32>(state.name.size()));
                attributes->setBinary("identity", state.identity.data(),
                                      static_cast<uint32>(state.identity.size()));
            }
            sendMessage(message);
        }
    }
    return kResultOk;
}
tresult PLUGIN_API Controller::getState(IBStream* stream) {
    std::string bytes;
    {
        std::lock_guard lock(status_lock_);
        // The name of the filter that plays, as the processor's filter gives
        // it (a refused preset's controller state names one that does not
        // play); otherwise the pair setState brought.
        const bool named =
            report_ && !report_->filter_identity.empty() && !report_->filter_saved_name.empty();
        const auto& name = named ? report_->filter_saved_name : name_;
        const auto& identity = named ? report_->filter_identity : identity_;
        if (name.empty() || identity.empty())
            return kResultOk; // Nothing to keep: the empty state of earlier versions.
        bytes = encode_controller_state({name, identity});
    }
    if (!stream)
        return kResultFalse;
    int32 written = 0;
    return stream->write(bytes.data(), static_cast<int32>(bytes.size()), &written) == kResultOk &&
                   written == static_cast<int32>(bytes.size())
               ? kResultOk
               : kResultFalse;
}
tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    auto result = EditController::initialize(context);
    if (result == kResultOk) {
        // VST3-4: the settings first, as generic host views list them in
        // this order (the Audio Unit lists the same).
        // PLUG-44: short titles for control surfaces and narrow host displays.
        auto* gain =
            new RangeParameter(STR16("Gain"), gain_id, STR16("dB"), min_gain_db, max_gain_db, 0, 0,
                               ParameterInfo::kCanAutomate, kRootUnitId, STR16("Gain"));
        gain->setPrecision(1);
        parameters.addParameter(gain);
        // PLUG-10: not automatable, as Latency: Estimate may need time to
        // find the filter's figure (gain_mode_message).
        auto* mode = new StringListParameter(STR16("Gain mode"), gain_mode_id, nullptr,
                                             ParameterInfo::kIsList, kRootUnitId, STR16("Mode"));
        for (std::size_t i = 0; i < gain_mode_count; ++i) {
            String128 text{};
            copy(gain_mode_name(static_cast<GainMode>(i)), text);
            mode->appendString(text);
        }
        parameters.addParameter(mode);
        auto* mix = new RangeParameter(STR16("Mix"), mix_id, STR16("%"), 0, 100, 100, 0,
                                       ParameterInfo::kCanAutomate, kRootUnitId, STR16("Mix"));
        mix->setPrecision(0);
        parameters.addParameter(mix);
        // Not automatable: a new Latency restarts the component.
        // PLUG-44: getParamStringByValue() adds the time at the host's rate.
        auto* latency = new StringListParameter(STR16("Latency"), latency_id, nullptr,
                                                ParameterInfo::kIsList, kRootUnitId, STR16("Lat"));
        for (const auto frames : host_latencies) {
            String128 text{};
            copy(latency_value_text(frames, 0), text);
            latency->appendString(text);
        }
        latency->setNormalized(normalized_latency(default_host_latency));
        latency->getInfo().defaultNormalizedValue = latency->getNormalized();
        parameters.addParameter(latency);
        parameters.addParameter(STR16("Bypass"), nullptr, 1, 0,
                                ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, bypass_id,
                                kRootUnitId, STR16("Bypass"));
        auto* status = new StringListParameter(STR16("Status"), status_id, nullptr,
                                               ParameterInfo::kIsReadOnly | ParameterInfo::kIsList,
                                               kRootUnitId, STR16("Status"));
        for (const auto* name : status_names) {
            String128 text{};
            copy(name, text);
            status->appendString(text);
        }
        status->setNormalized(status->toNormalized(static_cast<ParamValue>(HostStatus::no_filter)));
        status->getInfo().defaultNormalizedValue = status->getNormalized();
        parameters.addParameter(status);
    }
    return result;
}
tresult PLUGIN_API Controller::notify(IMessage* message) {
    if (!message || !message->getMessageID() ||
        std::string_view(message->getMessageID()) != status_message)
        return EditController::notify(message);
    auto* attributes = message->getAttributes();
    // PLUG-39 step 5: the report, which holds the Status line too.
    const void* data = nullptr;
    uint32 size = 0;
    if (!attributes || attributes->getBinary("report", data, size) != kResultOk || (size && !data))
        return kInvalidArgument;
    // VST3-15: the answer to a request, when the message follows one.
    int64 load_request = 0, load_retried = 0, load_accepted = 0, latency_request = 0;
    attributes->getInt("load request", load_request);
    attributes->getInt("retried", load_retried); // PLUG-40 (StatusAnswer).
    const bool loaded = load_request || load_retried;
    attributes->getInt("accepted", load_accepted);
    attributes->getInt("latency request", latency_request);
    // VST3-24: the processor did not load the file, as a restore it had
    // accepted since the request superseded it.
    int64 load_superseded = 0;
    if (load_request)
        attributes->getInt("superseded", load_superseded);
    // PLUG-26: the settings an accepted .vstpreset applied, which the
    // processor already plays.
    double preset_gain = 0, preset_mix = 0;
    int64 preset_latency = -1, preset_mode = 0;
    const bool preset_settings =
        loaded && load_accepted && attributes->getFloat("gain", preset_gain) == kResultOk &&
        attributes->getFloat("mix", preset_mix) == kResultOk &&
        attributes->getInt("latency frames", preset_latency) == kResultOk &&
        std::isfinite(preset_gain) && std::isfinite(preset_mix) &&
        std::find(host_latencies.begin(), host_latencies.end(), preset_latency) !=
            host_latencies.end();
    // PLUG-10: the preset's Gain mode; absent from an older processor, so Set.
    attributes->getInt("gain mode", preset_mode);
    if (preset_mode < 0 || preset_mode >= static_cast<int64>(gain_mode_count))
        preset_mode = 0;
    // PLUG-48: the settings the processor plays after its outcome for the
    // state setComponentState mirrored (`followed_state`, its
    // read_state()'s digest); absent otherwise or from a damaged message, which
    // then changes no parameter.
    std::optional<HostSettings> playing;
    int64 followed_state = 0, heard_latency = 0, heard_gain_mode = 0;
    attributes->getInt("followed state", followed_state);
    // VST3-18: the last Latency and Gain mode choices the processor had heard.
    attributes->getInt("heard latency request", heard_latency);
    attributes->getInt("heard gain mode request", heard_gain_mode);
    // VST3-19: the latency that plays changed since the host last read it.
    int64 latency_restored = 0, restored_after_mirror = 0;
    attributes->getInt("latency restored", latency_restored);
    attributes->getInt("restored after mirror", restored_after_mirror);
    // VST3-24: that outcome was not an accepted install.
    int64 followed_refused = 0;
    attributes->getInt("followed refused", followed_refused);
    if (followed_state) {
        double gain = 0, mix = 0;
        int64 latency = -1, mode = -1;
        if (attributes->getFloat("playing gain", gain) == kResultOk &&
            attributes->getFloat("playing mix", mix) == kResultOk &&
            attributes->getInt("playing latency", latency) == kResultOk &&
            attributes->getInt("playing gain mode", mode) == kResultOk && std::isfinite(gain) &&
            std::isfinite(mix) &&
            std::find(host_latencies.begin(), host_latencies.end(), latency) !=
                host_latencies.end() &&
            mode >= 0 && mode < static_cast<int64>(gain_mode_count)) {
            playing.emplace();
            playing->gain_db = gain;
            playing->mix = mix;
            playing->latency = static_cast<std::size_t>(latency);
            playing->gain_mode = static_cast<GainMode>(mode);
        }
    }
    std::optional<HostReport> report;
    std::string format;
    try {
        report = decode_report(std::string_view(static_cast<const char*>(data), size));
        const void* bytes = nullptr;
        uint32 length = 0;
        if (attributes->getBinary("format", bytes, length) == kResultOk && (bytes || !length))
            format.assign(static_cast<const char*>(bytes), length);
    } catch (...) {
        return kOutOfMemory;
    }
    // A report that does not decode (a damaged message) is not shown, but
    // the answer the message carries still counts, so a load or Latency
    // request is never left waiting.
    const bool readable = report.has_value();
    const auto status = readable ? report->status.status : HostStatus{};
    auto* parameter = getParameterObject(status_id);
    if (!parameter) // Not initialized, or terminated: nothing to show it on.
        return kResultFalse;
    bool changed = false, restart = false;
    std::optional<HostSettings> followed;
    // VST3-18: the settings the answer leaves as they are.
    bool keep_gain = false, keep_mix = false, keep_mode = false, keep_latency = false;
    // VST3-24: the load was answered after a later setComponentState, so
    // its settings are left to that restore's outcome; and, when such a
    // load's choice still plays because that restore was refused, the
    // restore's number (`held`: its settings are reported below).
    bool after_restore = false;
    int64 held = 0;
    {
        std::lock_guard lock(status_lock_);
        if (load_request) {
            load_answered_ = load_request;
            load_accepted_ = load_accepted != 0;
            after_restore = mirrored_ > load_request;
            load_held_ = after_restore && load_accepted_ ? load_request : 0;
        }
        // The Latency sent last: an earlier one's answer asks for nothing,
        // as the processor has later choices still to come.
        if (latency_waiting_ && latency_request == latency_requests_) {
            latency_waiting_ = false;
            restart = true;
        }
        if (readable) {
            changed = !report_ || report_->status.text != report->status.text ||
                      report_->status.status != status;
            report_.swap(report);
            format_.swap(format);
        }
        if (follow_state_ && playing &&
            static_cast<std::uint64_t>(followed_state) == follow_state_) {
            followed = playing;
            // Compared with actions since the restore's mirror, as the
            // settings below are (VST3-18): the mirror replaced earlier ones.
            held = followed_refused && load_held_ ? mirrored_ : 0;
            load_held_ = 0;
        }
        keep_gain = gain_set_ > mirrored_;
        keep_mix = mix_set_ > mirrored_;
        // Only a choice made since the restore: one before it was replaced
        // by the state, and one the processor never heard (a message lost)
        // must not keep a setting for later restores.
        keep_latency = latency_requests_ > std::max<int64>(heard_latency, mirrored_);
        keep_mode = gain_mode_requests_ > std::max<int64>(heard_gain_mode, mirrored_);
        // VST3-19: setComponentState asked the host to read a changed
        // latency. That was before the processor restored the state if the
        // outcome came from the processor's own restore, or if this answer
        // arrives later than the request's own delivery (a host that
        // delivers messages later may have restored the processor between).
        if (followed && (latency_restored ||
                         (restart_at_restore_ && (restored_after_mirror || !sending_follow_))))
            restart = true;
        if (followed)
            restart_at_restore_ = false;
    }
    // PLUG-48: after restored state, the settings the processor plays, which
    // differ from the state's when it refused that state. Not edits:
    // the host restored them, so nothing is recorded as the user's.
    // VST3-18 (jrp's review of 0.18.12): a host that delivers messages later
    // may bring this answer after the user chose another setting. A Latency
    // or Gain mode the processor had not yet heard, which it will play once
    // it does, and a Gain or Mix the host set since the restore, keep their values.
    if (followed && !held) {
        const auto set = [&](ParamID id, ParamValue value, bool keep) {
            if (keep || getParamNormalized(id) == value)
                return;
            EditController::setParamNormalized(id, value);
            changed = true;
        };
        const auto before = latency_frames(getParamNormalized(latency_id));
        set(gain_id, normalized_gain(followed->gain_db), keep_gain);
        set(mix_id, std::clamp(followed->mix, 0.0, 1.0), keep_mix);
        set(gain_mode_id, normalized_gain_mode(followed->gain_mode), keep_mode);
        set(latency_id, normalized_latency(followed->latency), keep_latency);
        if (latency_frames(getParamNormalized(latency_id)) != before)
            restart = true;
    }
    // A window asks for the report twice a second; only a new status line
    // is news for the host.
    if (changed) {
        // VST3-22: a report that does not decode says nothing of the status,
        // though the settings it carries changed: Status keeps its value.
        if (readable)
            EditController::setParamNormalized(
                status_id, parameter->toNormalized(static_cast<ParamValue>(status)));
        // Ask the host to read the parameters again (the status is not edited
        // by the user, so performEdit does not apply). Hosts may ignore this.
        if (componentHandler)
            componentHandler->restartComponent(kParamValuesChanged);
    }
    // PLUG-26: a chosen preset's Gain, Gain mode, Mix and Latency, which the
    // processor already plays, reach the host as the user's edits would
    // (publish_preset). VST3-24: not after a later restore, whose outcome
    // decides: an accepted restore replaced them (PLUG-48 sets what plays);
    // after a refused one the chosen file still plays, and its settings, as
    // that outcome gives them, are reported now with the project marked
    // changed, as if the load's answer had come first (jrp's review of PR
    // #281 at 4cf4712b: a window preset, then a refused host preset).
    bool edited = false;
    if (preset_settings && !after_restore) {
        HostSettings preset;
        preset.gain_db = preset_gain;
        preset.mix = preset_mix;
        preset.latency = static_cast<std::size_t>(preset_latency);
        preset.gain_mode = static_cast<GainMode>(preset_mode);
        edited = publish_preset(load_request, preset, restart);
    } else if (held) {
        edited = publish_preset(held, *followed, restart);
    }
    // The component state now holds the file's filter: the host saves it
    // only if it knows the project changed. Edits reported for a preset's
    // settings tell a host without setDirty already.
    if (((loaded && load_accepted && !after_restore) || held) && setDirty(true) != kResultOk &&
        !edited) {
        // VST3-17, a narrow fix: setDirty did not succeed, so the host may
        // not know the state changed. Audacity 4.0.2 offers no
        // IComponentHandler2 and, while the window stays open and playback
        // is stopped, stores the state only after the plug-in reports an
        // edit, so a filter chosen in the window played but was missing
        // from exports and saved projects. Report an edit of Latency that
        // keeps its value: not automatable, ignored by process() (the
        // controller sends Latency as a message, only when it changes), so
        // the sound, automation and Gain and Mix are untouched. A host whose
        // setDirty succeeds sees nothing new. The general fix is the host's:
        // implementing IComponentHandler2 (draft Audacity issue, VST3-17).
        // Like setDirty, this runs on the thread that delivered the answer:
        // the UI thread for a direct or forwarding connection.
        // VST3-23: a host that hands the edit back to setParamNormalized
        // must not make it a Latency choice (chosen_again(), jrp's review
        // of PR #281 at 4cf4712b and the independent review).
        const auto latency = getParamNormalized(latency_id);
        {
            std::lock_guard lock(status_lock_);
            publishing_ = true;
        }
        beginEdit(latency_id);
        performEdit(latency_id, latency);
        endEdit(latency_id);
        std::lock_guard lock(status_lock_);
        publishing_ = false;
    }
    // The SDK's way to report a new latency: the host deactivates the
    // component, reads getLatencySamples() and activates it again; some
    // (REAPER 7.85) only read it. The processor already plays the new
    // Latency when it answers (VST3-16), so either way the host reads the
    // latency that plays. Last,
    // after this message's Status: with a direct connection this runs inside
    // Processor::notify, and a host that restarts at once sends newer
    // status messages from setActive.
    if (restart && componentHandler)
        componentHandler->restartComponent(kLatencyChanged);
    return readable ? kResultOk : kInvalidArgument;
}
bool Controller::publish_preset(int64 request, const HostSettings& preset, bool& restart) {
    bool kept_gain = false, kept_mix = false, kept_mode = false, kept_latency = false;
    {
        std::lock_guard lock(status_lock_);
        // VST3-23 (jrp's review of PR #279): the user changed these after
        // choosing the preset. A later Latency or Gain mode reaches the
        // processor after the preset, which then plays it: it keeps its
        // value. A later Gain or Mix may have reached the processor in
        // process() before the preset replaced it: it is reported again
        // below, so the host hands it to the processor once more.
        kept_gain = gain_set_ > request;
        kept_mix = mix_set_ > request;
        kept_mode = gain_mode_requests_ > request;
        kept_latency = latency_requests_ > request;
        publishing_ = true;
    }
    // One gesture for each setting that changes, so the host's parameters and
    // the state it saves agree with what plays. Where the host is writing
    // automation, they are recorded as edits would be. The host hands Gain
    // and Mix back in process() with the values the processor has; Latency in
    // process() is ignored, and handed back to setParamNormalized it is no
    // change, so no latency_message follows. A changed Latency asks for the
    // restart, as a restored one does (setComponentState).
    bool edited = false;
    const auto report = [&](ParamID id, ParamValue value, bool kept) {
        if (getParamNormalized(id) == value)
            return;
        if (!kept)
            EditController::setParamNormalized(id, value);
        beginEdit(id);
        performEdit(id, getParamNormalized(id));
        endEdit(id);
        edited = true;
    };
    const auto before = latency_frames(getParamNormalized(latency_id));
    report(gain_id, normalized_gain(preset.gain_db), kept_gain);
    if (!kept_mode)
        report(gain_mode_id, normalized_gain_mode(preset.gain_mode), false);
    report(mix_id, std::clamp(preset.mix, 0.0, 1.0), kept_mix);
    if (!kept_latency)
        report(latency_id, normalized_latency(preset.latency), false);
    if (latency_frames(getParamNormalized(latency_id)) != before)
        restart = true;
    std::lock_guard lock(status_lock_);
    publishing_ = false;
    return edited;
}
bool Controller::chosen_again() {
    std::lock_guard lock(status_lock_);
    return !publishing_ && load_requests_ > load_answered_;
}
void Controller::latency_chosen(std::size_t frames) {
    int64 request = 0;
    {
        std::lock_guard lock(status_lock_);
        request = latency_requests_ = next_action();
        latency_waiting_ = true;
    }
    // The processor's answer asks the host to restart (notify).
    if (auto message = owned(allocateMessage())) {
        message->setMessageID(latency_message);
        if (auto* attributes = message->getAttributes()) {
            attributes->setInt("frames", static_cast<int64>(frames));
            attributes->setInt("request", request);
            sendMessage(message);
        }
    }
}
tresult PLUGIN_API Controller::setParamNormalized(ParamID id, ParamValue value) {
    if (id == bypass_id) {
        // PLUG-22: the processor shows Bypassed at once (bypass_message).
        const bool before = getParamNormalized(bypass_id) >= .5;
        const auto result = EditController::setParamNormalized(id, value);
        const bool after = getParamNormalized(bypass_id) >= .5;
        if (result == kResultOk && after != before) {
            if (auto message = owned(allocateMessage())) {
                message->setMessageID(bypass_message);
                if (auto* attributes = message->getAttributes()) {
                    attributes->setInt("bypass", after ? 1 : 0);
                    sendMessage(message);
                }
            }
        }
        return result;
    }
    if (id == gain_mode_id) {
        const auto before = gain_mode(getParamNormalized(gain_mode_id));
        const auto result = EditController::setParamNormalized(id, value);
        const auto after = gain_mode(getParamNormalized(gain_mode_id));
        if (result == kResultOk && (after != before || chosen_again())) {
            int64 request = 0;
            {
                std::lock_guard lock(status_lock_);
                request = gain_mode_requests_ = next_action();
            }
            // The processor answers with its status_message.
            if (auto message = owned(allocateMessage())) {
                message->setMessageID(gain_mode_message);
                if (auto* attributes = message->getAttributes()) {
                    attributes->setInt("mode", static_cast<int64>(after));
                    attributes->setInt("request", request);
                    sendMessage(message);
                }
            }
        }
        return result;
    }
    if (id == latency_id) {
        const auto before = latency_frames(getParamNormalized(latency_id));
        const auto result = EditController::setParamNormalized(id, value);
        const auto after = latency_frames(getParamNormalized(latency_id));
        if (result == kResultOk && (after != before || chosen_again()))
            latency_chosen(after);
        return result;
    }
    if (id == gain_id || id == mix_id) {
        // VST3-18: a value the host passes here it passes to the processor
        // in process() too (automation, or the window's edit through the
        // host), also when it is the value shown: automation to the refused
        // preset's Gain plays that Gain (jrp's review of PR #278).
        const auto result = EditController::setParamNormalized(id, value);
        if (result == kResultOk) {
            std::lock_guard lock(status_lock_);
            if (!publishing_)
                (id == gain_id ? gain_set_ : mix_set_) = next_action();
        }
        return result;
    }
    const auto result = EditController::setParamNormalized(id, value);
    if (id != status_id || result != kResultOk ||
        status_index(value) != static_cast<long>(HostStatus::fault))
        return result;
    {
        std::lock_guard lock(status_lock_);
        if (report_ && report_->status.status == HostStatus::fault)
            return result; // The processor has explained it already.
    }
    // Outside the lock: the processor answers with its status_message.
    if (auto message = owned(allocateMessage())) {
        message->setMessageID(faults_message);
        sendMessage(message);
    }
    return result;
}
tresult PLUGIN_API Controller::getParamStringByValue(ParamID id, ParamValue value,
                                                     String128 string) {
    if (id == latency_id) {
        // PLUG-44: the time the choice adds at the host's rate, once known.
        int rate = 0;
        {
            std::lock_guard lock(status_lock_);
            if (report_)
                rate = report_->sample_rate;
        }
        copy(latency_value_text(latency_frames(value), rate), string);
        return kResultOk;
    }
    if (id == status_id && value == getParamNormalized(status_id)) {
        std::lock_guard lock(status_lock_);
        // The text belongs to the status the processor sent last; a Fault
        // value from the audio thread reads as its name until it explains.
        if (report_ && !report_->status.text.empty() &&
            status_index(value) == static_cast<long>(report_->status.status)) {
            copy(report_->status.text, string);
            return kResultOk;
        }
    }
    return EditController::getParamStringByValue(id, value, string);
}
tresult PLUGIN_API Controller::getParamValueByString(ParamID id, TChar* string, ParamValue& value) {
    if (!string)
        return kInvalidArgument;
    if (id == bypass_id) {
        // Case-insensitive ASCII comparison of the host's UTF-16 text.
        auto is = [string](const char* word) {
            std::size_t i = 0;
            for (; word[i]; ++i) {
                auto c = string[i];
                if (c >= u'A' && c <= u'Z')
                    c = static_cast<TChar>(c - u'A' + u'a');
                if (c != static_cast<TChar>(word[i]))
                    return false;
            }
            return string[i] == 0;
        };
        if (is("off") || is("0")) {
            value = 0;
            return kResultOk;
        }
        if (is("on") || is("1")) {
            value = 1;
            return kResultOk;
        }
        // Not the SDK's conversion: it reads any text as 0 on macOS and
        // leaves the value unset on Windows.
        return kResultFalse;
    }
    if (id == latency_id) {
        // PLUG-44: the text names the time too, so read the frames that
        // start it; only a value in the menu counts.
        std::size_t frames = 0;
        std::size_t i = 0;
        while (string[i] == u' ')
            ++i;
        const std::size_t first = i;
        for (; string[i] >= u'0' && string[i] <= u'9' && frames < 1000000; ++i)
            frames = frames * 10 + static_cast<std::size_t>(string[i] - u'0');
        if (i == first ||
            std::find(host_latencies.begin(), host_latencies.end(), frames) == host_latencies.end())
            return kResultFalse;
        value = normalized_latency(frames);
        return kResultOk;
    }
    return EditController::getParamValueByString(id, string, value);
}
tresult PLUGIN_API Controller::setComponentState(IBStream* stream) {
    HostState state;
    std::uint64_t digest = 0;
    try {
        state = read_state(stream, &digest);
    } catch (...) {
        return kResultFalse;
    }
    const auto saved = state.settings.value_or(HostSettings{});
    EditController::setParamNormalized(gain_id, normalized_gain(saved.gain_db));
    EditController::setParamNormalized(mix_id, std::clamp(saved.mix, 0.0, 1.0));
    // The processor has the Gain mode from the same state: no message.
    EditController::setParamNormalized(gain_mode_id, normalized_gain_mode(saved.gain_mode));
    // The processor has the Latency from the same state: no message, but the
    // host is asked to read it again when it changed.
    const auto before = latency_frames(getParamNormalized(latency_id));
    EditController::setParamNormalized(latency_id, normalized_latency(saved.latency));
    const bool latency_changed = latency_frames(getParamNormalized(latency_id)) != before;
    if (latency_changed && componentHandler)
        componentHandler->restartComponent(kLatencyChanged);
    const auto result = EditController::setParamNormalized(bypass_id, state.bypass ? 1 : 0);
    // PLUG-48 (jrp's live session, 22 November 2026): the processor may
    // have refused this state, as REAPER's preset menu offers a preset made
    // for another rate while a filter plays; it then keeps its own settings,
    // but the values above came from the refused preset (REAPER showed
    // Latency 0 while 1024 played). This message names the state (its
    // digest); the processor's status after its outcome for this state says
    // what plays, and the parameters take it (notify). That status answers
    // this message when the processor refused the state already (it read it
    // first, as the SDK says hosts do); otherwise it follows the processor's
    // restore. A refusal of the same state in an earlier restore also
    // answers this message, and the restore that follows corrects it
    // (Codex's review).
    int64 latency_before = 0, gain_mode_before = 0;
    {
        std::lock_guard lock(status_lock_);
        follow_state_ = digest;
        mirrored_ = next_action(); // VST3-23: later actions number above it.
        restart_at_restore_ = latency_changed;
        sending_follow_ = true;
        latency_before = latency_requests_;
        gain_mode_before = gain_mode_requests_;
    }
    if (auto message = owned(allocateMessage())) {
        message->setMessageID(report_message);
        if (auto* attributes = message->getAttributes(); attributes && digest) {
            attributes->setInt("follow state", static_cast<int64>(digest));
            attributes->setInt("latency requests", latency_before);
            attributes->setInt("gain mode requests", gain_mode_before);
        }
        sendMessage(message);
    }
    {
        std::lock_guard lock(status_lock_);
        sending_follow_ = false;
    }
    return result;
}
IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (!name || std::string_view(name) != ViewType::kEditor)
        return nullptr;
    return create_editor(*this);
}
std::optional<HostReport> Controller::report() {
    // Outside the lock: the processor answers with its status_message.
    if (auto message = owned(allocateMessage())) {
        message->setMessageID(report_message);
        sendMessage(message);
    }
    std::lock_guard lock(status_lock_);
    return report_;
}
std::string Controller::diagnostics() {
    std::optional<HostReport> report;
    std::string format;
    {
        std::lock_guard lock(status_lock_);
        report = report_;
        format = format_;
    }
    if (!report || format.empty())
        return {};
    return diagnostics_text(*report, format);
}
bool Controller::load_file(const std::string& path) {
    auto message = owned(allocateMessage());
    if (!message || path.empty())
        return false;
    message->setMessageID(load_message);
    int64 request = 0;
    {
        std::lock_guard lock(status_lock_);
        request = next_action();
    }
    auto* attributes = message->getAttributes();
    if (!attributes ||
        attributes->setBinary("path", path.data(), static_cast<uint32>(path.size())) != kResultOk ||
        attributes->setInt("request", request) != kResultOk)
        return false;
    {
        // Numbered as sent only now: a load that never left has no answer
        // to wait for (chosen_again(), jrp's review of PR #281 at 4cf4712b).
        std::lock_guard lock(status_lock_);
        load_requests_ = request;
    }
    if (sendMessage(message) != kResultOk) {
        std::lock_guard lock(status_lock_);
        if (load_answered_ < request) { // Not delivered: no answer is to come.
            load_answered_ = request;
            load_accepted_ = false;
        }
        return false;
    }
    // A direct connection or a forwarding proxy has brought the answer
    // (notify, which marks the project changed); a deferring one brings it
    // later.
    std::lock_guard lock(status_lock_);
    return load_answered_ != request || load_accepted_;
}
tresult PLUGIN_API Controller::getParameterIDFromFunctionName(UnitID unit, FIDString name,
                                                              ParamID& id) {
    if (unit == kRootUnitId && name && std::string_view(name) == FunctionNameType::kDryWetMix) {
        id = mix_id;
        return kResultTrue;
    }
    id = kNoParamId;
    return kResultFalse;
}
} // namespace convolver::vst3
