// SPDX-License-Identifier: GPL-2.0-or-later
#include "processor.hpp"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
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
HostState read_state(IBStream* stream) {
    std::array<unsigned char, 4> header{};
    if (!transfer(stream, header.data(), 4, false))
        throw std::invalid_argument("missing state length");
    std::uint32_t size = 0;
    for (int i = 0; i < 4; ++i)
        size |= std::uint32_t(header[i]) << (8 * i);
    if (size > max_host_state_bytes || size < 12)
        throw std::invalid_argument("invalid state length");
    std::vector<std::byte> bytes(size);
    if (!transfer(stream, bytes.data(), size, false))
        throw std::invalid_argument("truncated state");
    return decode_host_state(bytes);
}
bool write_state(IBStream* stream, const HostState& state) {
    auto bytes = encode_host_state(state);
    std::array<unsigned char, 4> header{};
    for (int i = 0; i < 4; ++i)
        header[i] = static_cast<unsigned char>((bytes.size() >> (8 * i)) & 255);
    return transfer(stream, header.data(), 4, true) &&
           transfer(stream, bytes.data(), bytes.size(), true);
}
Processor::Processor() {
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
    if (active_ || ni != 1 || no != 1 || !in || !out || in[0] != out[0] ||
        (in[0] != SpeakerArr::kMono && in[0] != SpeakerArr::kStereo))
        return kResultFalse;
    auto result = AudioEffect::setBusArrangements(in, ni, out, no);
    if (result == kResultOk)
        channels_ = SpeakerArr::getChannelCount(in[0]);
    return result;
}
tresult PLUGIN_API Processor::canProcessSampleSize(int32 size) {
    return size == kSample32 || size == kSample64 ? kResultOk : kResultFalse;
}
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    if (active_ || !std::isfinite(setup.sampleRate) || setup.sampleRate < 1 ||
        setup.sampleRate > 768000 || std::floor(setup.sampleRate) != setup.sampleRate ||
        setup.maxSamplesPerBlock < 1 || setup.maxSamplesPerBlock > (1 << 20) ||
        canProcessSampleSize(setup.symbolicSampleSize) != kResultOk)
        return kInvalidArgument;
    return AudioEffect::setupProcessing(setup);
}
Configuration Processor::configuration(const HostState& state) const {
    if (state.configuration) {
        const auto& c = *state.configuration;
        if (c.sample_rate != processSetup.sampleRate ||
            c.input_channels != static_cast<std::size_t>(channels_) ||
            c.output_channels != static_cast<std::size_t>(channels_))
            throw std::invalid_argument("preset rate/layout does not match the host");
        return c;
    }
    Configuration c;
    c.sample_rate = static_cast<int>(processSetup.sampleRate);
    c.input_channels = c.output_channels = channels_;
    for (int i = 0; i < channels_; ++i)
        c.paths.push_back(
            {{1}, {{static_cast<std::size_t>(i), 1}}, {{static_cast<std::size_t>(i), 1}}});
    return c;
}
tresult PLUGIN_API Processor::setActive(TBool state) {
    std::lock_guard lock(control_);
    try {
        if (state) {
            auto next = std::make_unique<HostProcessor>(configuration(state_),
                                                        processSetup.maxSamplesPerBlock);
            tail_.store(static_cast<uint32>(next->tail_frames()));
            next->set_bypass(bypass_.load());
            next->reset();
            host_ = std::move(next);
        } else
            host_.reset();
        active_ = state != 0;
        return AudioEffect::setActive(state);
    } catch (...) {
        return kResultFalse;
    }
}
tresult PLUGIN_API Processor::setProcessing(TBool state) {
    if (state && host_) {
        host_->set_bypass(bypass_.load());
        host_->reset();
    }
    return kResultOk;
}
tresult PLUGIN_API Processor::setState(IBStream* stream) {
    std::lock_guard lock(control_);
    try {
        auto next = read_state(stream);
        if (host_) {
            host_->replace(configuration(next));
            tail_.store(static_cast<uint32>(host_->tail_frames()));
        } else if (next.configuration &&
                   (next.configuration->input_channels != next.configuration->output_channels ||
                    next.configuration->input_channels > 2))
            return kResultFalse;
        if (!host_)
            tail_.store(next.configuration
                            ? static_cast<uint32>(configuration_tail_frames(*next.configuration))
                            : 0);
        bypass_.store(next.bypass);
        state_ = std::move(next);
        return kResultOk;
    } catch (...) {
        return kResultFalse;
    }
}
tresult PLUGIN_API Processor::getState(IBStream* stream) {
    std::lock_guard lock(control_);
    try {
        state_.bypass = bypass_.load();
        if (host_)
            host_->collect();
        return write_state(stream, state_) ? kResultOk : kResultFalse;
    } catch (...) {
        return kResultFalse;
    }
}
template <class T> bool Processor::segment(ProcessData& d, int32 start, int32 count) noexcept {
    std::array<const T*, 2> in{};
    std::array<T*, 2> out{};
    if (d.numInputs && d.inputs && getAudioInput(0)->isActive()) {
        auto& bus = d.inputs[0];
        if (bus.numChannels != channels_)
            return false;
        for (int c = 0; c < channels_; ++c)
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
        if (bus.numChannels != channels_)
            return false;
        for (int c = 0; c < channels_; ++c) {
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
    return host_->process(std::span(in).first(channels_), std::span(out).first(channels_), count);
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
tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    auto result = EditController::initialize(context);
    if (result == kResultOk)
        parameters.addParameter(STR16("Bypass"), nullptr, 1, 0,
                                ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, bypass_id);
    return result;
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
