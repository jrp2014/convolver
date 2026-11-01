// SPDX-License-Identifier: GPL-2.0-or-later
// The Audio Unit's lifecycle and audio: construction, initialization and
// cleanup, stream formats, Render, latency and tail. Saved state and filter
// loading are in effect_state.cpp; parameters, properties, the window's
// report and logging in effect_properties.cpp.
#include "effect.hpp"
#include "convolver_sdk.hpp"
#include "host_log.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <span>
#include <vector>

namespace convolver::au {
namespace {
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

std::string hz(double rate) {
    return std::to_string(static_cast<long long>(rate));
}

bool same(const HostLayout& a, const HostLayout& b) {
    return a.sample_rate == b.sample_rate && a.inputs == b.inputs && a.outputs == b.outputs;
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
    Globals()->SetParameter(latency_parameter, default_latency_index);
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
            // apply from the first frame, and the Latency is the block size.
            auto next = std::make_unique<HostProcessor>(filter_.activate(host),
                                                        GetMaxFramesPerSlice(), current);
            next->set_bypass(bypass_.load());
            next->reset();
            tail_.store(next->tail_frames());
            latency_.store(next->latency_frames());
            refused_latency_.store(0);
            install_processor(std::move(next));
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
    timing_changed(); // The latency may be a newly chosen one.
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
        // The host does not render while it uninitializes the unit.
        playing_.store(nullptr);
        host_.reset();
        retired_.clear();
        latency_.store(settings().latency); // What the next initialization uses.
        refused_latency_.store(0);
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
    return rate > 0 ? static_cast<Float64>(latency_.load()) / rate : 0;
}

Float64 Effect::GetTailTime() noexcept AUSDK_RTSAFE {
    const auto rate = GetOutput0().GetStreamFormat().mSampleRate;
    return rate > 0 ? static_cast<Float64>(tail_.load()) / rate : 0;
}

void Effect::install_processor(std::unique_ptr<HostProcessor> next) {
    reclaim();
    if (host_)
        retired_.push_back(std::move(host_)); // May throw before anything changed.
    host_ = std::move(next);
    playing_.store(host_.get());
    reclaim(); // Render may not have started on the previous one.
}

void Effect::reclaim() noexcept {
    // Render marks its processor in `rendering_` and then checks that it is
    // still `playing_` (sequentially consistent), so a processor that is no
    // longer playing and not marked is not used and never will be again.
    const auto* in_use = rendering_.load();
    std::erase_if(retired_, [&](const auto& p) { return p.get() != in_use; });
}

void Effect::apply_latency(bool wait) {
    std::string message, faults;
    bool changed = false, problem = false;
    {
        std::unique_lock lock(control_, std::defer_lock);
        if (wait)
            lock.lock();
        else if (!lock.try_lock())
            return; // A load is running; the window's next report tries again.
        reclaim();
        const auto chosen = settings().latency;
        if (!host_) {
            changed = latency_.exchange(chosen) != chosen; // The next Initialize uses it.
            refused_latency_.store(0);
        } else if (host_->latency_frames() != chosen && initialized_) {
            const auto before = host_->latency_frames();
            faults = observe_faults(); // Before the processor that counted them goes.
            try {
                // As Initialize builds it; Render switches at its next call.
                auto next = std::make_unique<HostProcessor>(filter_.activate(*initialized_),
                                                            GetMaxFramesPerSlice(), settings());
                next->set_bypass(bypass_.load());
                next->reset();
                const auto tail = next->tail_frames();
                install_processor(std::move(next));
                tail_.store(tail);
                latency_.store(chosen);
                refused_latency_.store(0);
                changed = true;
                message = "Latency changed from " + std::to_string(before) + " to " +
                          std::to_string(chosen) +
                          " frames; the sound paused briefly while the new processor started.";
            } catch (const std::exception& e) {
                // The previous processor keeps playing at its latency; the
                // window's report says the chosen one waits.
                problem = true;
                refused_latency_.store(chosen);
                message = "Latency " + std::to_string(chosen) +
                          " frames could not be applied, so the plug-in keeps " +
                          std::to_string(before) + " frames: " + e.what();
            } catch (...) {
                problem = true;
                refused_latency_.store(chosen);
            }
            update_status();
        }
    }
    log_faults(faults);
    log(message, problem);
    if (changed)
        timing_changed();
    if (!message.empty())
        notify_status();
}

void Effect::timing_changed() {
    PropertyChanged(kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
    PropertyChanged(kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0);
}

OSStatus Effect::Render(AudioUnitRenderActionFlags& flags, const AudioTimeStamp& time,
                        UInt32 frames) noexcept AUSDK_RTSAFE {
    // The processor to use, marked for the length of this call so that the
    // control side does not free it (reclaim()): marked, then checked to be
    // still the one playing. Atomic loads and stores only.
    HostProcessor* host = nullptr;
    do {
        host = playing_.load();
        rendering_.store(host);
    } while (host != playing_.load());
    struct Unmark {
        std::atomic<HostProcessor*>& marked;
        ~Unmark() {
            marked.store(nullptr, std::memory_order_release);
        }
    } unmark{rendering_};
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
