// SPDX-License-Identifier: GPL-2.0-or-later
#include "processor.hpp"
#include "bytes.hpp"
#include "host_log.hpp"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace convolver::vst3 {
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
bool transfer(IBStream* stream, void* data, std::size_t size, bool writing) {
    if (!stream)
        return false;
    auto p = static_cast<char*>(data);
    while (size) {
        int32 count = 0;
        auto n = static_cast<int32>(size);
        auto status = writing ? stream->write(p, n, &count) : stream->read(p, n, &count);
        if (count <= 0 || count > n ||
            (status != kResultOk && static_cast<std::size_t>(count) != size))
            return false;
        p += count;
        size -= count;
    }
    return true;
}
} // namespace
std::uint32_t wav_speakers(SpeakerArrangement arrangement) noexcept {
    constexpr SpeakerArrangement shared = (SpeakerArrangement(1) << 18) - 1;
    return arrangement & ~shared ? 0 : static_cast<std::uint32_t>(arrangement);
}
HostState read_state(IBStream* stream) {
    std::array<std::byte, 4> header{};
    if (!transfer(stream, header.data(), 4, false))
        throw std::invalid_argument("missing state length");
    const auto size = detail::read_le32(header);
    if (size > max_host_state_bytes || size < 12)
        throw std::invalid_argument("invalid state length");
    std::vector<std::byte> bytes(size);
    if (!transfer(stream, bytes.data(), size, false))
        throw std::invalid_argument("truncated state");
    return decode_host_state(bytes);
}
bool write_state(IBStream* stream, const HostState& state) {
    auto bytes = encode_host_state(state);
    std::array<std::byte, 4> header{};
    detail::write_le32(header, static_cast<std::uint32_t>(bytes.size()));
    return transfer(stream, header.data(), 4, true) &&
           transfer(stream, bytes.data(), bytes.size(), true);
}
namespace {
std::string next_tag() {
    static std::atomic<unsigned> instances{0};
    return "VST3 #" + std::to_string(++instances);
}
} // namespace
Processor::Processor() : tag_(next_tag()) {
    setControllerClass(controller_id);
}
tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    auto result = AudioEffect::initialize(context);
    if (result == kResultOk) {
        addAudioInput(STR16("Input"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
    }
    return result;
}
tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* in, int32 ni,
                                                 SpeakerArrangement* out, int32 no) {
    std::lock_guard lock(control_);
    if (active_ || ni != 1 || no != 1 || !in || !out)
        return kResultFalse;
    const auto inputs = SpeakerArr::getChannelCount(in[0]);
    const auto outputs = SpeakerArr::getChannelCount(out[0]);
    if (inputs < min_bus_channels || inputs > max_bus_channels || outputs < min_bus_channels ||
        outputs > max_bus_channels || (!identity::unequal_counts && inputs != outputs))
        return kResultFalse;
    auto result = AudioEffect::setBusArrangements(in, ni, out, no);
    if (result == kResultOk) {
        inputs_ = inputs;
        outputs_ = outputs;
        input_arrangement_ = in[0];
        output_arrangement_ = out[0];
    }
    return result;
}
tresult PLUGIN_API Processor::canProcessSampleSize(int32 size) {
    return size == kSample32 || size == kSample64 ? kResultOk : kResultFalse;
}
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    std::lock_guard lock(control_);
    if (active_ || !std::isfinite(setup.sampleRate) || setup.sampleRate < 1 ||
        setup.sampleRate > 768000 || std::floor(setup.sampleRate) != setup.sampleRate ||
        setup.maxSamplesPerBlock < 1 || setup.maxSamplesPerBlock > (1 << 20) ||
        canProcessSampleSize(setup.symbolicSampleSize) != kResultOk)
        return kInvalidArgument;
    return AudioEffect::setupProcessing(setup);
}
HostLayout Processor::layout() const {
    return {static_cast<int>(processSetup.sampleRate), static_cast<std::size_t>(inputs_),
            static_cast<std::size_t>(outputs_), wav_speakers(input_arrangement_),
            wav_speakers(output_arrangement_)};
}
// The host's configuration is known once the plug-in has been activated with
// it, and stays known while the rate and buses are unchanged. setupProcessing
// is not binding on its own: JUCE-based hosts (pluginval among them) call it
// with placeholder values before restoring a project's state at another rate.
// Until then, restored state is kept and checked at activation (the shared
// adapter contract's "state that arrives before the host is configured").
std::optional<HostLayout> Processor::known_layout() const {
    const auto now = layout();
    if (!activated_ || activated_->sample_rate != now.sample_rate ||
        activated_->inputs != now.inputs || activated_->outputs != now.outputs)
        return std::nullopt;
    return now;
}
tresult PLUGIN_API Processor::setActive(TBool state) {
    const auto result = activate(state);
    send_status();
    return result;
}
tresult Processor::activate(TBool state) {
    std::lock_guard lock(control_);
    try {
        if (state) {
            // A filter that does not suit the host plays silence, and is kept so
            // that correcting the rate or layout brings it back.
            auto next = std::make_unique<HostProcessor>(filter_.activate(layout()),
                                                        processSetup.maxSamplesPerBlock);
            tail_.store(static_cast<uint32>(next->tail_frames()));
            next->set_bypass(bypass_.load());
            next->reset();
            host_ = std::move(next);
            activated_ = layout();
            // What plays and why: a filter restored before the host's rate
            // and buses were known may not suit them.
            try { // Started: nothing here may turn that into a failure.
                const auto& f = filter_.failure();
                log(started_line(filter_, *activated_,
                                 static_cast<std::size_t>(processSetup.maxSamplesPerBlock)),
                    (f && f->silent) || !filter_.notice().empty());
            } catch (...) { // Out of memory for the line: it is dropped.
            }
        } else
            host_.reset();
        active_ = state != 0;
        return AudioEffect::setActive(state);
    } catch (const std::exception& e) {
        record_activation_failure(e.what());
        return kResultFalse;
    } catch (...) {
        record_activation_failure("the processor could not be built.");
        return kResultFalse;
    }
}
void Processor::record_activation_failure(const char* why) noexcept {
    try {
        filter_.fail_activation(why);
        log(filter_.error());
    } catch (...) { // Out of memory for the message: nothing more to keep.
    }
}
void Processor::log(const std::string& message, bool problem) noexcept {
    try {
        if (message.empty() || message == logged_)
            return;
        host_log(tag_.c_str(), message, problem);
        logged_ = message;
    } catch (...) { // Out of memory: the line is dropped.
    }
}
HostStatusLine Processor::status_locked() const {
    return host_status(filter_, host_ ? std::optional(layout()) : known_layout());
}
HostStatusLine Processor::status() {
    std::lock_guard lock(control_);
    return status_locked();
}
void Processor::send_status() noexcept {
    try {
        const auto line = status();
        if (!getPeer())
            return;
        auto message = owned(allocateMessage());
        if (!message)
            return;
        message->setMessageID(status_message);
        auto* attributes = message->getAttributes();
        if (!attributes)
            return;
        attributes->setInt("status", static_cast<int64>(line.status));
        attributes->setBinary("text", line.text.data(), static_cast<uint32>(line.text.size()));
        sendMessage(message);
    } catch (...) { // Out of memory: the controller keeps the previous status.
    }
}
tresult PLUGIN_API Processor::connect(IConnectionPoint* other) {
    const auto result = AudioEffect::connect(other);
    if (result == kResultOk)
        send_status();
    return result;
}
tresult PLUGIN_API Processor::setProcessing(TBool state) {
    if (state && host_) {
        host_->set_bypass(bypass_.load());
        host_->reset();
    }
    return kResultOk;
}
tresult PLUGIN_API Processor::setState(IBStream* stream) {
    const auto result = load_state(stream);
    send_status();
    return result;
}
tresult Processor::load_state(IBStream* stream) {
    std::lock_guard lock(control_);
    const bool was_failed = filter_.failed();
    try {
        HostState next;
        try {
            next = read_state(stream);
        } catch (const NewerHostState& e) {
            filter_.reject(HostFailureKind::state_newer,
                           std::string("The saved state could not be read: ") + e.what());
            throw;
        } catch (const std::exception& e) {
            filter_.reject(HostFailureKind::state_unreadable,
                           std::string("The saved state could not be read: ") + e.what());
            throw;
        }
        // While active, the host's configuration is known and a prepared
        // replacement is published; inactive, the next activation installs it.
        const auto host = host_ ? std::optional(layout()) : known_layout();
        const bool bypass = next.bypass;
        filter_.load(std::move(next), host, [&](const Configuration& c) {
            if (host_)
                host_->replace(c);
        });
        if (host_)
            tail_.store(static_cast<uint32>(host_->tail_frames()));
        else
            tail_.store(filter_.filter()
                            ? static_cast<uint32>(configuration_tail_frames(*filter_.filter()))
                            : 0);
        bypass_.store(bypass);
        logged_.clear(); // A later failure is logged even if it repeats an earlier one.
        try {            // Accepted: nothing here may turn that into a rejection.
            // A restored failed load says why it plays silence.
            log(loaded_line(filter_, host.has_value()),
                filter_.failure().has_value() || !filter_.notice().empty());
        } catch (...) { // Out of memory for the line: it is dropped.
        }
        return kResultOk;
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
        if (const auto& r = filter_.rejection())
            log(r->message);
        return kResultFalse;
    }
}
std::string Processor::notice() {
    std::lock_guard lock(control_);
    return filter_.notice();
}
std::optional<HostFailure> Processor::failure() {
    std::lock_guard lock(control_);
    return filter_.failure();
}
tresult PLUGIN_API Processor::getState(IBStream* stream) {
    std::lock_guard lock(control_);
    try {
        HostState state;
        state.configuration = filter_.filter();
        state.bypass = bypass_.load();
        state.failed = filter_.failed(); // Reopening stays silent (state version 2).
        if (host_)
            host_->collect();
        return write_state(stream, state) ? kResultOk : kResultFalse;
    } catch (...) {
        return kResultFalse;
    }
}
template <class T> bool Processor::segment(ProcessData& d, int32 start, int32 count) noexcept {
    std::array<const T*, max_bus_channels> in{};
    std::array<T*, max_bus_channels> out{};
    if (d.numInputs && d.inputs && getAudioInput(0)->isActive()) {
        auto& bus = d.inputs[0];
        if (bus.numChannels != inputs_)
            return false;
        for (int c = 0; c < inputs_; ++c)
            if (!(bus.silenceFlags & (uint64(1) << c))) {
                if constexpr (std::is_same_v<T, float>) {
                    if (bus.channelBuffers32 && bus.channelBuffers32[c])
                        in[c] = bus.channelBuffers32[c] + start;
                } else {
                    if (bus.channelBuffers64 && bus.channelBuffers64[c])
                        in[c] = bus.channelBuffers64[c] + start;
                }
            }
    }
    if (d.numOutputs && d.outputs && getAudioOutput(0)->isActive()) {
        auto& bus = d.outputs[0];
        if (bus.numChannels != outputs_)
            return false;
        for (int c = 0; c < outputs_; ++c) {
            if constexpr (std::is_same_v<T, float>) {
                if (bus.channelBuffers32 && bus.channelBuffers32[c])
                    out[c] = bus.channelBuffers32[c] + start;
            } else {
                if (bus.channelBuffers64 && bus.channelBuffers64[c])
                    out[c] = bus.channelBuffers64[c] + start;
            }
        }
        bus.silenceFlags = 0; // Do not propagate input silence across a convolution tail.
    }
    host_->set_bypass(bypass_.load());
    return host_->process(std::span(in).first(inputs_), std::span(out).first(outputs_), count);
}
tresult PLUGIN_API Processor::process(ProcessData& d) {
    if (d.numSamples < 0 || d.numSamples > processSetup.maxSamplesPerBlock || d.numInputs < 0 ||
        d.numInputs > 1 || d.numOutputs < 0 || d.numOutputs > 1)
        return kInvalidArgument;
    if (d.numSamples && (!host_ || canProcessSampleSize(d.symbolicSampleSize) != kResultOk))
        return kInvalidArgument;
    auto render = [&](int32 first, int32 count) {
        if (!count)
            return true;
        return d.symbolicSampleSize == kSample32 ? segment<float>(d, first, count)
                                                 : segment<double>(d, first, count);
    };
    int32 position = 0;
    if (d.inputParameterChanges) {
        for (int32 i = 0; i < d.inputParameterChanges->getParameterCount(); ++i) {
            auto* queue = d.inputParameterChanges->getParameterData(i);
            if (!queue || queue->getParameterId() != bypass_id)
                continue;
            for (int32 j = 0; j < queue->getPointCount(); ++j) {
                int32 offset = 0;
                ParamValue value = 0;
                if (queue->getPoint(j, offset, value) != kResultOk || !std::isfinite(value))
                    continue;
                offset = std::clamp(offset, position, d.numSamples);
                if (!render(position, offset - position))
                    return kInvalidArgument;
                position = offset;
                bypass_.store(value >= .5);
            }
        }
    }
    return render(position, d.numSamples - position) ? kResultOk : kInvalidArgument;
}
namespace {
// The Status parameter's values, in HostStatus order.
constexpr std::array<const char*, 4> status_names{"Playing", "No filter", "Load failed",
                                                  "Not checked yet"};
void copy(const std::string& text, String128 out) {
    std::size_t i = 0;
    for (; i < text.size() && i < 127; ++i) // ASCII (HostStatusLine), widened.
        out[i] = static_cast<TChar>(static_cast<unsigned char>(text[i]));
    out[i] = 0;
}
} // namespace
tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    auto result = EditController::initialize(context);
    if (result == kResultOk) {
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
    try {
        status_text_.assign(static_cast<const char*>(data), size);
    } catch (...) {
        return kOutOfMemory;
    }
    auto* parameter = getParameterObject(status_id);
    setParamNormalized(status_id, parameter->toNormalized(static_cast<ParamValue>(status)));
    // Ask the host to read the parameters again (the status is not edited
    // by the user, so performEdit does not apply). Hosts may ignore this.
    if (componentHandler)
        componentHandler->restartComponent(kParamValuesChanged);
    return kResultOk;
}
tresult PLUGIN_API Controller::getParamStringByValue(ParamID id, ParamValue value,
                                                     String128 string) {
    if (id == status_id && !status_text_.empty() && value == getParamNormalized(status_id)) {
        copy(status_text_, string);
        return kResultOk;
    }
    return EditController::getParamStringByValue(id, value, string);
}
tresult PLUGIN_API Controller::setComponentState(IBStream* stream) {
    try {
        auto state = read_state(stream);
        return setParamNormalized(bypass_id, state.bypass ? 1 : 0);
    } catch (...) {
        return kResultFalse;
    }
}
} // namespace convolver::vst3
