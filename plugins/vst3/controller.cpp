// SPDX-License-Identifier: GPL-2.0-or-later
// The edit controller (VST3-14: moved from processor.cpp, unchanged).
#include "bytes.hpp"
#include "editor.hpp"
#include "host_report.hpp"
#include "pluginterfaces/base/ibstream.h"
#include "processor.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace convolver::vst3 {
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
constexpr std::string_view controller_magic = "CVCS";
constexpr std::uint32_t controller_version = 1;
constexpr std::size_t max_controller_state = 4096; // Name, identity and headers fit easily.

void append_le32(std::string& out, std::uint32_t value) {
    std::array<std::byte, 4> bytes{};
    detail::write_le32(bytes, value);
    for (const auto byte : bytes)
        out.push_back(static_cast<char>(byte));
}
std::optional<std::uint32_t> take_le32(std::string_view& in) {
    if (in.size() < 4)
        return std::nullopt;
    const auto value = detail::read_le32(std::as_bytes(std::span(in.data(), 4)));
    in.remove_prefix(4);
    return value;
}
std::optional<std::string> take_text(std::string_view& in, std::size_t most) {
    const auto size = take_le32(in);
    if (!size || *size > most || *size > in.size())
        return std::nullopt;
    std::string text(in.substr(0, *size));
    in.remove_prefix(*size);
    return text;
}
// A report field's value, or empty.
std::string field(const std::vector<std::pair<std::string, std::string>>& report,
                  std::string_view name) {
    for (const auto& [key, value] : report)
        if (key == name)
            return value;
    return {};
}
void copy(const std::string& text, String128 out) {
    std::size_t i = 0;
    for (; i < text.size() && i < 127; ++i) // ASCII (HostStatusLine), widened.
        out[i] = static_cast<TChar>(static_cast<unsigned char>(text[i]));
    out[i] = 0;
}
} // namespace
std::string encode_controller_state(const ControllerState& state) {
    std::string out(controller_magic);
    append_le32(out, controller_version);
    append_le32(out, static_cast<std::uint32_t>(state.name.size()));
    out += state.name;
    append_le32(out, static_cast<std::uint32_t>(state.identity.size()));
    out += state.identity;
    return out;
}
ControllerState decode_controller_state(std::string_view in) {
    if (!in.starts_with(controller_magic))
        return {};
    in.remove_prefix(controller_magic.size());
    if (take_le32(in) != controller_version)
        return {};
    auto name = take_text(in, max_filter_name_bytes);
    auto identity = take_text(in, 64);
    if (!name || !identity || name->empty() || identity->empty())
        return {};
    return {std::move(*name), std::move(*identity)};
}
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
    std::lock_guard lock(status_lock_);
    name_ = std::move(state.name);
    identity_ = std::move(state.identity);
    return kResultOk;
}
tresult PLUGIN_API Controller::getState(IBStream* stream) {
    std::string bytes;
    {
        std::lock_guard lock(status_lock_);
        if (name_.empty() || identity_.empty())
            return kResultOk; // Nothing to keep: the empty state of earlier versions.
        bytes = encode_controller_state({name_, identity_});
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
        auto* gain = new RangeParameter(STR16("Gain"), gain_id, STR16("dB"), min_gain_db,
                                        max_gain_db, 0, 0, ParameterInfo::kCanAutomate);
        gain->setPrecision(1);
        parameters.addParameter(gain);
        auto* mix = new RangeParameter(STR16("Mix"), mix_id, STR16("%"), 0, 100, 100, 0,
                                       ParameterInfo::kCanAutomate);
        mix->setPrecision(0);
        parameters.addParameter(mix);
        // Not automatable: a new Latency restarts the component.
        auto* latency =
            new StringListParameter(STR16("Latency"), latency_id, nullptr, ParameterInfo::kIsList);
        for (const auto frames : host_latencies) {
            String128 text{};
            copy(std::to_string(frames) + " frames", text);
            latency->appendString(text);
        }
        latency->setNormalized(normalized_latency(default_host_latency));
        latency->getInfo().defaultNormalizedValue = latency->getNormalized();
        parameters.addParameter(latency);
        parameters.addParameter(STR16("Bypass"), nullptr, 1, 0,
                                ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, bypass_id);
        auto* status = new StringListParameter(STR16("Status"), status_id, nullptr,
                                               ParameterInfo::kIsReadOnly | ParameterInfo::kIsList);
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
    int64 status = 0;
    const void* data = nullptr;
    uint32 size = 0;
    if (!attributes || attributes->getInt("status", status) != kResultOk || status < 0 ||
        status >= static_cast<int64>(status_names.size()) ||
        attributes->getBinary("text", data, size) != kResultOk || (size && !data))
        return kInvalidArgument;
    // VST3-15: the answer to a request, when the message follows one.
    int64 load_request = 0, load_accepted = 0, latency_request = 0;
    attributes->getInt("load request", load_request);
    attributes->getInt("accepted", load_accepted);
    attributes->getInt("latency request", latency_request);
    std::string text, report_text, format;
    std::vector<std::pair<std::string, std::string>> report;
    try {
        text.assign(static_cast<const char*>(data), size);
        // VST3-3: the window's report, when the message carries it.
        const auto binary = [&](const char* id) -> std::optional<std::string> {
            const void* bytes = nullptr;
            uint32 length = 0;
            if (attributes->getBinary(id, bytes, length) != kResultOk || (length && !bytes))
                return std::nullopt;
            return std::string(static_cast<const char*>(bytes), length);
        };
        if (const auto fields = binary("report")) {
            report = decode_report(*fields);
            report_text = binary("report text").value_or("");
            format = binary("format").value_or("");
        }
    } catch (...) {
        return kOutOfMemory;
    }
    auto* parameter = getParameterObject(status_id);
    if (!parameter) // Not initialized, or terminated: nothing to show it on.
        return kResultFalse;
    bool changed = false, restart = false;
    {
        std::lock_guard lock(status_lock_);
        if (load_request) {
            load_answered_ = load_request;
            load_accepted_ = load_accepted != 0;
        }
        // The Latency sent last: an earlier one's answer asks for nothing,
        // as the processor has later choices still to come.
        if (latency_waiting_ && latency_request == latency_requests_) {
            latency_waiting_ = false;
            restart = true;
        }
        changed = status_text_ != text || status_value_ != static_cast<int>(status);
        status_text_.swap(text);
        status_value_ = static_cast<int>(status);
        if (!report.empty()) {
            // PLUG-22: a filter file the processor accepted: its name and
            // identity are what the controller state keeps.
            if (!field(report, "filter path").empty() &&
                !field(report, "filter identity").empty()) {
                name_ = field(report, "filter name");
                identity_ = field(report, "filter identity");
            }
            report_.swap(report);
            report_text_.swap(report_text);
            format_.swap(format);
        }
    }
    // A window asks for the report twice a second; only a new status line
    // is news for the host.
    if (changed) {
        EditController::setParamNormalized(
            status_id, parameter->toNormalized(static_cast<ParamValue>(status)));
        // Ask the host to read the parameters again (the status is not edited
        // by the user, so performEdit does not apply). Hosts may ignore this.
        if (componentHandler)
            componentHandler->restartComponent(kParamValuesChanged);
    }
    // The component state now holds the file's filter: the host saves it
    // only if it knows the project changed.
    if (load_request && load_accepted)
        setDirty(true); // IComponentHandler2, where the host offers it.
    // The SDK's way to report a new latency: the host deactivates the
    // component, reads getLatencySamples() and activates it again. Last,
    // after this message's Status: with a direct connection this runs inside
    // Processor::notify, and a host that restarts at once sends newer
    // status messages from setActive.
    if (restart && componentHandler)
        componentHandler->restartComponent(kLatencyChanged);
    return kResultOk;
}
void Controller::latency_chosen(std::size_t frames) {
    int64 request = 0;
    {
        std::lock_guard lock(status_lock_);
        request = ++latency_requests_;
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
    if (id == latency_id) {
        const auto before = latency_frames(getParamNormalized(latency_id));
        const auto result = EditController::setParamNormalized(id, value);
        const auto after = latency_frames(getParamNormalized(latency_id));
        if (result == kResultOk && after != before)
            latency_chosen(after);
        return result;
    }
    const auto result = EditController::setParamNormalized(id, value);
    if (id != status_id || result != kResultOk ||
        status_index(value) != static_cast<long>(HostStatus::fault))
        return result;
    {
        std::lock_guard lock(status_lock_);
        if (status_value_ == static_cast<int>(HostStatus::fault))
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
    if (id == status_id && value == getParamNormalized(status_id)) {
        std::lock_guard lock(status_lock_);
        // The text belongs to the status the processor sent last; a Fault
        // value from the audio thread reads as its name until it explains.
        if (!status_text_.empty() && status_index(value) == status_value_) {
            copy(status_text_, string);
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
    return EditController::getParamValueByString(id, string, value);
}
tresult PLUGIN_API Controller::setComponentState(IBStream* stream) {
    HostState state;
    try {
        state = read_state(stream);
    } catch (...) {
        return kResultFalse;
    }
    const auto saved = state.settings.value_or(HostSettings{});
    EditController::setParamNormalized(gain_id, normalized_gain(saved.gain_db));
    EditController::setParamNormalized(mix_id, std::clamp(saved.mix, 0.0, 1.0));
    // The processor has the Latency from the same state: no message, but the
    // host is asked to read it again when it changed.
    const auto before = latency_frames(getParamNormalized(latency_id));
    EditController::setParamNormalized(latency_id, normalized_latency(saved.latency));
    if (latency_frames(getParamNormalized(latency_id)) != before && componentHandler)
        componentHandler->restartComponent(kLatencyChanged);
    return EditController::setParamNormalized(bypass_id, state.bypass ? 1 : 0);
}
IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (!name || std::string_view(name) != ViewType::kEditor)
        return nullptr;
    return create_editor(*this);
}
std::vector<std::pair<std::string, std::string>> Controller::report() {
    // Outside the lock: the processor answers with its status_message.
    if (auto message = owned(allocateMessage())) {
        message->setMessageID(report_message);
        sendMessage(message);
    }
    std::lock_guard lock(status_lock_);
    return named_report();
}
std::vector<std::pair<std::string, std::string>> Controller::named_report() const {
    auto report = report_;
    // From saved state (no file path), and the filter the name belongs to.
    if (name_.empty() || identity_.empty() || !field(report, "filter path").empty() ||
        field(report, "filter identity") != identity_)
        return report;
    for (auto& [key, value] : report)
        if (key == "filter name")
            value = name_;
    return report;
}
std::string Controller::diagnostics() {
    std::string text, format;
    {
        std::lock_guard lock(status_lock_);
        text = report_text_;
        format = format_;
        // The saved name in the text's Filter line too.
        const auto saved = field(report_, "filter name"),
                   named = field(named_report(), "filter name");
        const auto line = text.find("\nFilter: " + saved);
        if (saved != named && line != std::string::npos)
            text.replace(line + 9, saved.size(), named);
    }
    if (format.empty())
        return {};
    return diagnostics_text(text, format);
}
bool Controller::load_file(const std::string& path) {
    auto message = owned(allocateMessage());
    if (!message || path.empty())
        return false;
    message->setMessageID(load_message);
    int64 request = 0;
    {
        std::lock_guard lock(status_lock_);
        request = ++load_requests_;
    }
    auto* attributes = message->getAttributes();
    if (!attributes ||
        attributes->setBinary("path", path.data(), static_cast<uint32>(path.size())) != kResultOk ||
        attributes->setInt("request", request) != kResultOk)
        return false;
    if (sendMessage(message) != kResultOk)
        return false; // Refused, or not delivered.
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
