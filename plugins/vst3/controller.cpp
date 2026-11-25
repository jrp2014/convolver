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
#include <thread>
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
    // The processor puts the name in its report and the report's text.
    with_processor([&](ProcessorLink& p) { p.offer_name(state.name, state.identity); });
    if (on_ui_thread())
        refresh(false);
    return kResultOk;
}
tresult PLUGIN_API Controller::getState(IBStream* stream) {
    refresh_if_changed();
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
    ui_thread_ = std::this_thread::get_id(); // Hosts initialize on the UI thread.
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
        // find the filter's figure (chosen_gain_mode()).
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
tresult PLUGIN_API Controller::terminate() {
    set_bridge(nullptr);
    return EditController::terminate();
}
std::shared_ptr<Bridge> Controller::bridge() const {
    std::lock_guard lock(bridge_lock_);
    return bridge_;
}
void Controller::set_bridge(std::shared_ptr<Bridge> bridge) {
    std::lock_guard lock(bridge_lock_);
    bridge_ = std::move(bridge);
}
template <class F> bool Controller::with_processor(F&& f) {
    const auto bridge = this->bridge();
    return bridge && bridge->call(std::forward<F>(f));
}
bool Controller::on_ui_thread() const noexcept {
    return std::this_thread::get_id() == ui_thread_;
}
tresult PLUGIN_API Controller::connect(IConnectionPoint* other) {
    const auto result = EditController::connect(other);
    if (result != kResultOk)
        return result;
    {
        // VST3-27: a new connection, paired only by the answer to its own request.
        std::lock_guard lock(bridge_lock_);
        connection_ = Bridges::stamp();
        bridge_.reset();
    }
    ask_for_key();
    return result;
}
tresult PLUGIN_API Controller::disconnect(IConnectionPoint* other) {
    {
        std::lock_guard lock(bridge_lock_);
        connection_ = 0;
        bridge_.reset();
    }
    return EditController::disconnect(other);
}
void Controller::ask_for_key() {
    std::uint64_t connection = 0;
    {
        std::lock_guard lock(bridge_lock_);
        connection = connection_;
    }
    if (connection == 0)
        return; // Connected to nothing: no processor to ask.
    if (auto message = owned(allocateMessage())) {
        message->setMessageID(pair_request_message);
        if (auto* attributes = message->getAttributes()) {
            attributes->setInt("connection", static_cast<int64>(connection));
            sendMessage(message);
        }
    }
}
tresult PLUGIN_API Controller::notify(IMessage* message) {
    if (!message || !message->getMessageID())
        return EditController::notify(message);
    const std::string_view id = message->getMessageID();
    if (id == pair_message) {
        int64 key = 0, connection = 0;
        auto* attributes = message->getAttributes();
        if (!attributes || attributes->getInt("key", key) != kResultOk ||
            attributes->getInt("connection", connection) != kResultOk)
            return kInvalidArgument;
        // A key from another process, or a processor that has gone, finds
        // nothing: the controller stays unpaired.
        auto bridge = Bridges::find(static_cast<std::uint64_t>(key));
        if (!bridge)
            return kResultFalse;
        {
            // VST3-27: an answer to an earlier connection, which a host that
            // delivers late may pass on after it connected the controller to
            // another processor (or to none), names a processor this
            // controller no longer talks to, though its key is valid while it
            // lives. Checked and kept under one lock, so a disconnect on
            // another thread cannot come between.
            std::lock_guard lock(bridge_lock_);
            if (connection_ == 0 || static_cast<std::uint64_t>(connection) != connection_)
                return kResultFalse;
            if (bridge == bridge_)
                return kResultOk; // Paired already.
            bridge_ = bridge;
        }
        bridge->changed();
        // What the controller learned before the halves paired (a proxy
        // that delivers later) goes to the processor now, as a message would
        // have: the name a restored controller state gives the filter
        // (PLUG-22), and a Latency or Gain mode the host chose; a host that
        // read the latency before is told the new one.
        std::string name, identity;
        {
            std::lock_guard lock(status_lock_);
            name = name_;
            identity = identity_;
        }
        with_processor([&](ProcessorLink& p) {
            if (!name.empty())
                p.offer_name(name, identity);
            const auto frames = std::exchange(unsent_latency_, std::nullopt);
            const auto mode = std::exchange(unsent_gain_mode_, std::nullopt);
            if (!frames && !mode)
                return;
            // A restore the processor accepted after a choice wins over it.
            const auto view = p.view();
            latency_told_ = view.latency;
            if (frames && unsent_latency_at_ > view.restored_at)
                p.chosen_latency(*frames);
            if (mode && unsent_gain_mode_at_ > view.restored_at)
                p.chosen_gain_mode(*mode);
        });
        // From now on the parameters show the processor's, Gain, Mix and
        // Bypass included, except automation the host set while unpaired
        // after the processor's last restore.
        if (on_ui_thread())
            refresh(true);
        return kResultOk;
    }
    if (id == pair_ask_message) {
        // VST3-27: the processor connected before any request reached it (a
        // host that dropped the one sent when the controller connected): ask
        // again for the current connection. A late one only asks again.
        ask_for_key();
        return kResultOk;
    }
    if (id == changed_message) {
        // Off the UI thread (an offline restore) the next call on it reads
        // the processor: the host's callbacks belong there.
        if (on_ui_thread())
            refresh_if_changed();
        return kResultOk;
    }
    return EditController::notify(message);
}
void Controller::refresh_if_changed() {
    if (const auto bridge = this->bridge(); bridge && on_ui_thread() && bridge->take_changed())
        refresh(false);
}
void Controller::refresh(bool adopt_all, bool tell_host) {
    PairView view;
    if (!with_processor([&](ProcessorLink& p) { view = p.view(); }))
        return;
    if (const auto bridge = this->bridge(); bridge && on_ui_thread())
        bridge->take_changed(); // Read now; off it, read again on it.
    auto* status = getParameterObject(status_id);
    if (!status) // Not initialized, or terminated: nothing to show it on.
        return;
    bool changed = false;
    const auto shown = view.report.status.status;
    {
        std::lock_guard lock(status_lock_);
        changed = !report_ || report_->status.text != view.report.status.text ||
                  report_->status.status != shown;
        report_ = std::move(view.report);
        format_ = std::move(view.format);
    }
    if (changed)
        EditController::setParamNormalized(status_id,
                                           status->toNormalized(static_cast<ParamValue>(shown)));
    const auto set = [&](ParamID id, ParamValue value) {
        if (getParamNormalized(id) == value)
            return;
        EditController::setParamNormalized(id, value);
        changed = true;
    };
    // Compared by the entry, not the value: a host may hold a value with
    // less precision (Ardour, as float) and pass it back.
    if (latency_frames(getParamNormalized(latency_id)) != view.settings.latency)
        set(latency_id, normalized_latency(view.settings.latency));
    if (gain_mode(getParamNormalized(gain_mode_id)) != view.settings.gain_mode)
        set(gain_mode_id, normalized_gain_mode(view.settings.gain_mode));
    if (adopt_all || view.generation != generation_) {
        // Automation the host set while unpaired, after the processor's last
        // accepted restore, is newer: the host passes it in process().
        if (gain_edit_at_ <= view.restored_at)
            set(gain_id, normalized_gain(view.settings.gain_db));
        if (mix_edit_at_ <= view.restored_at)
            set(mix_id, std::clamp(view.settings.mix, 0.0, 1.0));
        if (bypass_edit_at_ <= view.restored_at)
            set(bypass_id, view.bypass ? 1 : 0);
    }
    generation_ = view.generation;
    gain_edit_at_ = mix_edit_at_ = bypass_edit_at_ = 0; // Only the first read after pairing.
    // Ask the host to read the parameters again (the status is not edited
    // by the user, so performEdit does not apply). Hosts may ignore this.
    if (changed && tell_host && on_ui_thread())
        restart(kParamValuesChanged);
    // The SDK's way to report a new latency: the host deactivates the
    // component, reads getLatencySamples() and activates it again; some
    // (REAPER 7.85) only read it. The processor already plays the new
    // Latency (VST3-16), so either way the host reads the latency that
    // plays. The first read after pairing only records it: the host reads
    // it when it activates the processor. Off the UI thread (a host that
    // restores the controller there) the next refresh on it tells the host.
    if (!on_ui_thread())
        return;
    const bool told = latency_told_.has_value();
    if (std::exchange(latency_told_, view.latency) != view.latency && told)
        restart(kLatencyChanged);
}
void Controller::restart(int32 flags) {
    // On the UI thread only (refresh()), so restarting_ needs no lock.
    restart_waiting_ |= flags;
    // Ardour 8 and 9 read parameters inside restartComponent, which passes
    // values it holds to setParamNormalized and so to a refresh; a second
    // restartComponent there waits for the first forever. The host is told
    // once the call it is in returns.
    if (restarting_)
        return;
    restarting_ = true;
    // The handler, held: a host may clear it inside restartComponent.
    const IPtr<IComponentHandler> handler = componentHandler;
    while (handler && restart_waiting_ != 0)
        handler->restartComponent(std::exchange(restart_waiting_, 0));
    restart_waiting_ = 0;
    restarting_ = false;
}
tresult PLUGIN_API Controller::setParamNormalized(ParamID id, ParamValue value) {
    // The host's value goes over what the processor said before it.
    refresh_if_changed();
    if (const auto bridge = this->bridge();
        (id == gain_id || id == mix_id || id == bypass_id) && !(bridge && bridge->attached())) {
        // Unpaired, or the processor has gone (its terminate reached only a
        // proxy): when, so that the first read after pairing keeps it if it
        // is newer than the processor's last restore.
        auto& at = id == gain_id ? gain_edit_at_ : id == mix_id ? mix_edit_at_ : bypass_edit_at_;
        at = Bridges::stamp();
    }
    if (id == bypass_id) {
        // PLUG-22: the processor shows Bypassed at once, and so does the
        // Status parameter, which hosts show without the window.
        const auto result = EditController::setParamNormalized(id, value);
        const bool after = getParamNormalized(bypass_id) >= .5;
        if (result == kResultOk &&
            with_processor([&](ProcessorLink& p) { p.show_bypass(after); }) && on_ui_thread())
            refresh(false);
        return result;
    }
    if (id == gain_mode_id || id == latency_id) {
        const auto before = getParamNormalized(id);
        const auto result = EditController::setParamNormalized(id, value);
        const auto chosen = getParamNormalized(id);
        // The same entry is no choice: Ardour passes back, as float, the
        // value the controller gave it when it reads the parameters.
        const bool same = id == latency_id ? latency_frames(chosen) == latency_frames(before)
                                           : gain_mode(chosen) == gain_mode(before);
        if (result != kResultOk || same)
            return result;
        const bool sent = with_processor([&](ProcessorLink& p) {
            if (id == latency_id)
                p.chosen_latency(latency_frames(chosen));
            else
                p.chosen_gain_mode(gain_mode(chosen));
        });
        if (!sent && id == latency_id) {
            unsent_latency_ = latency_frames(chosen);
            unsent_latency_at_ = Bridges::stamp();
        } else if (!sent) {
            unsent_gain_mode_ = gain_mode(chosen);
            unsent_gain_mode_at_ = Bridges::stamp();
        }
        if (on_ui_thread())
            refresh(false); // A new Latency asks the host to restart.
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
    with_processor([](ProcessorLink& p) { p.explain_faults(false); });
    if (on_ui_thread())
        refresh(false);
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
        // The text belongs to the status read last; a Fault
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
    if (const auto bridge = this->bridge(); bridge && bridge->attached()) {
        // VST3-26: what the processor plays, not the stream: the processor
        // may have refused the state (PLUG-48: REAPER's preset menu offers a
        // preset made for another rate while a filter plays, and it then
        // keeps its own settings), and a host that restores the controller
        // first brings the processor's own restore after this, which says
        // it changed. Gain, Mix and Bypass only if an accepted restore set
        // them: otherwise they stay the host's, whose automation may not
        // have reached process() yet.
        refresh(false, false);
        if (bridge->attached())
            return kResultOk;
    }
    // A choice or an edit made before pairing is older than this restore,
    // whose values the parameters now take; once paired, the processor's
    // replace them unless it refused the restore.
    unsent_latency_.reset();
    unsent_gain_mode_.reset();
    gain_edit_at_ = mix_edit_at_ = bypass_edit_at_ = 0;
    // Unpaired: the state's values, as the host restored them.
    HostState state;
    try {
        state = read_state(stream);
    } catch (...) {
        return kResultFalse;
    }
    const auto saved = state.settings.value_or(HostSettings{});
    EditController::setParamNormalized(gain_id, normalized_gain(saved.gain_db));
    EditController::setParamNormalized(mix_id, std::clamp(saved.mix, 0.0, 1.0));
    EditController::setParamNormalized(gain_mode_id, normalized_gain_mode(saved.gain_mode));
    EditController::setParamNormalized(latency_id, normalized_latency(saved.latency));
    return EditController::setParamNormalized(bypass_id, state.bypass ? 1 : 0);
}
IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (!name || std::string_view(name) != ViewType::kEditor)
        return nullptr;
    return create_editor(*this);
}
std::optional<HostReport> Controller::report() {
    // First what the processor changed before (accepted_file reports
    // edits against the values the controller shows).
    refresh_if_changed();
    LoadAnswer retried;
    with_processor([&](ProcessorLink& p) {
        retried = p.read_waiting_list();
        p.explain_faults(true);
    });
    if (retried.tried && retried.accepted)
        accepted_file(retried);
    // Gain and Mix change in process(), which says nothing: always read.
    if (on_ui_thread())
        refresh(false);
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
    if (path.empty())
        return false;
    // First what the processor changed before: VST3-17's edit keeps the
    // Latency the controller shows, which a host without setDirty hands
    // back to setParamNormalized; a stale one would be taken as a choice.
    refresh_if_changed();
    LoadAnswer answer;
    with_processor([&](ProcessorLink& p) { answer = p.load_chosen(path); });
    if (answer.accepted)
        accepted_file(answer);
    if (on_ui_thread())
        refresh(false);
    return answer.accepted;
}
void Controller::accepted_file(const LoadAnswer& answer) {
    // PLUG-26: a chosen preset's Gain, Gain mode, Mix and Latency, which the
    // processor already plays, reach the host as the user's edits would, one
    // gesture each for those that change, so the host's parameters and the
    // state it saves agree with what plays. Where the host is writing
    // automation, they are recorded as edits would be. The host hands Gain
    // and Mix back in process() with the values the processor has; Latency in
    // process() is ignored. The refresh that follows sees nothing more to
    // take, and asks the host to read a changed latency.
    bool edited = false;
    if (const auto& settings = answer.settings) {
        const auto report_edit = [&](ParamID id, ParamValue value) {
            if (getParamNormalized(id) == value)
                return;
            EditController::setParamNormalized(id, value);
            beginEdit(id);
            performEdit(id, getParamNormalized(id));
            endEdit(id);
            edited = true;
        };
        report_edit(gain_id, normalized_gain(settings->gain_db));
        report_edit(gain_mode_id, normalized_gain_mode(settings->gain_mode));
        report_edit(mix_id, std::clamp(settings->mix, 0.0, 1.0));
        report_edit(latency_id, normalized_latency(settings->latency));
    }
    // The component state now holds the file's filter: the host saves it
    // only if it knows the project changed. Edits reported for a preset's
    // settings tell a host without setDirty already.
    if (setDirty(true) != kResultOk && !edited) {
        // VST3-17, a narrow fix: setDirty did not succeed, so the host may
        // not know the state changed. Audacity 4.0.2 offers no
        // IComponentHandler2 and, while the window stays open and playback
        // is stopped, stores the state only after the plug-in reports an
        // edit, so a filter chosen in the window played but was missing
        // from exports and saved projects. Report an edit of Latency that
        // keeps its value: not automatable, ignored by process() (Latency
        // reaches the processor only when it changes), so the sound,
        // automation and Gain and Mix are untouched. A host whose setDirty
        // succeeds sees nothing new. The general fix is the host's:
        // implementing IComponentHandler2 (draft Audacity issue, VST3-17).
        const auto latency = getParamNormalized(latency_id);
        beginEdit(latency_id);
        performEdit(latency_id, latency);
        endEdit(latency_id);
    }
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
