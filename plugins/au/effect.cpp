// SPDX-License-Identifier: GPL-2.0-or-later
// The Audio Unit's lifecycle and audio: construction, initialization and
// cleanup, stream formats, Render, latency and tail. Saved state and filter
// loading are in effect_state.cpp; parameters, properties, the window's
// report and logging in effect_properties.cpp.
#include "effect.hpp"
#include "advice.hpp"
#include "convolver_sdk.hpp"
#include "host_log.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <dispatch/dispatch.h>
#include <exception>
#include <mutex>
#include <new>
#include <pthread.h>
#include <span>
#include <thread>
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
} // namespace

// AU-12: Apple's coalescing wake-up ("Dispatch Sources" in the Dispatch
// documentation). Merging data into a DISPATCH_SOURCE_TYPE_DATA_OR source
// runs its handler on the serial queue; merges made before the handler runs
// are combined, so requests made while the work runs make it run once more,
// not once each. Cancelling stops further runs, and the cancel handler runs
// once a run in progress has finished, which the destructor waits for.
struct Effect::LatencyQueue {
    // AU-13: the source is created last, so that a failure (dispatch_*_create
    // returns NULL) never leaves an inactive source to release: releasing the
    // last reference to an inactive object is undefined (dispatch_activate's
    // documentation). Once activated, the source is released only after it
    // is cancelled and its cancel handler has run.
    explicit LatencyQueue(Effect& e) : effect(e) {
        const auto fail = latency_queue_failure_for_tests;
        ended = fail == 1 ? nullptr : dispatch_semaphore_create(0);
        if (ended)
            queue = fail == 2 ? nullptr
                              : dispatch_queue_create("Convolver Audio Unit Latency",
                                                      DISPATCH_QUEUE_SERIAL);
        if (queue)
            source = fail == 3 ? nullptr
                               : dispatch_source_create(DISPATCH_SOURCE_TYPE_DATA_OR, 0, 0, queue);
        if (!source) {
            if (queue)
                dispatch_release(queue);
            if (ended)
                dispatch_release(ended);
            throw std::bad_alloc();
        }
        dispatch_set_context(source, this);
        // The data is the ORed bits of the wakes since the last run (AU-16).
        dispatch_source_set_event_handler_f(source, [](void* self) {
            auto& q = *static_cast<LatencyQueue*>(self);
            q.effect.apply_latency(dispatch_source_get_data(q.source));
        });
        dispatch_source_set_cancel_handler_f(source, [](void* self) {
            dispatch_semaphore_signal(static_cast<LatencyQueue*>(self)->ended);
        });
        dispatch_activate(source);
    }
    LatencyQueue(const LatencyQueue&) = delete;
    LatencyQueue& operator=(const LatencyQueue&) = delete;
    ~LatencyQueue() {
        dispatch_source_cancel(source);
        if (const auto hook = latency_closing_for_tests)
            hook(effect.GetComponentInstance());
        dispatch_semaphore_wait(ended, DISPATCH_TIME_FOREVER);
        dispatch_release(source);
        dispatch_release(queue);
        dispatch_release(ended);
    }
    void request(std::uintptr_t why) noexcept {
        dispatch_source_merge_data(source, why);
    }

    Effect& effect;
    dispatch_semaphore_t ended = nullptr;
    dispatch_queue_t queue = nullptr;
    dispatch_source_t source = nullptr;
};

// AU-17: the main-queue end of tell_listeners(). The source's handler runs on
// the main thread with the ORed notices posted since its last run; `shared`
// lives until the source's cancel handler, which runs on the main queue
// after any handler already running, so a late handler finds `effect`
// cleared rather than a closed unit. If the main queue never runs (a host
// with no main run loop), the pending notices and this small record are
// never freed: nothing is told, which is what such a host can hear anyway.
struct Effect::MainNotices {
    struct Shared {
        std::mutex lock;
        Effect* effect = nullptr; // Cleared, under `lock`, when the unit closes.
        dispatch_source_t source = nullptr;
    };

    explicit MainNotices(Effect& e) : shared(new Shared) {
        shared->effect = &e;
        shared->source =
            dispatch_source_create(DISPATCH_SOURCE_TYPE_DATA_OR, 0, 0, dispatch_get_main_queue());
        if (!shared->source) {
            delete shared;
            throw std::bad_alloc();
        }
        dispatch_set_context(shared->source, shared);
        // Listeners are called with `lock` held, so a listener that closes
        // the unit from its callback cannot proceed; doing so was already
        // undefined, as AUBase would go on reading the closed unit's list.
        dispatch_source_set_event_handler_f(shared->source, [](void* context) {
            auto& s = *static_cast<Shared*>(context);
            const auto notices = dispatch_source_get_data(s.source);
            std::lock_guard guard(s.lock);
            if (s.effect)
                s.effect->deliver_notices(notices);
        });
        dispatch_source_set_cancel_handler_f(shared->source, [](void* context) {
            auto* s = static_cast<Shared*>(context);
            // Orders the delete after ~MainNotices on another thread for
            // ThreadSanitizer, which does not model cancel handlers.
            {
                std::lock_guard guard(s->lock);
            }
            dispatch_release(s->source);
            delete s;
        });
        dispatch_activate(shared->source);
    }
    MainNotices(const MainNotices&) = delete;
    MainNotices& operator=(const MainNotices&) = delete;
    ~MainNotices() {
        {
            // Waits for a handler running on the main thread when the unit
            // closes on another thread.
            std::lock_guard guard(shared->lock);
            shared->effect = nullptr;
        }
        dispatch_source_cancel(shared->source); // The cancel handler frees `shared`.
    }
    void post(std::uintptr_t notices) noexcept {
        dispatch_source_merge_data(shared->source, notices);
    }

    Shared* shared;
};

Effect::Effect(AudioComponentInstance instance)
    : AUBase(instance, 1, 1),
      diagnostics_(std::make_shared<const Diagnostics>(Diagnostics{filter_, std::nullopt})),
      tag_(next_tag()) {
    set_host_log_sdk(CONVOLVER_SDK_DESCRIPTION); // The Convolver log names the SDK (CI-6).
    // Not UseIndexedParameters: that numbers parameters from 0, and Status is
    // 1001 (plugins/identities.json), so the parameter map is used instead.
    Globals()->SetParameter(status_parameter,
                            static_cast<AudioUnitParameterValue>(HostStatus::no_filter));
    // AU-7: the settings at their defaults, which keep the sound unchanged.
    const HostSettings defaults;
    Globals()->SetParameter(gain_parameter, static_cast<AudioUnitParameterValue>(defaults.gain_db));
    Globals()->SetParameter(mix_parameter,
                            static_cast<AudioUnitParameterValue>(100 * defaults.mix));
    Globals()->SetParameter(latency_parameter, default_latency_index);
    Globals()->SetParameter(gain_mode_parameter,
                            static_cast<AudioUnitParameterValue>(defaults.gain_mode));
    main_notices_ = std::make_unique<MainNotices>(*this);
    latency_queue_ = std::make_unique<LatencyQueue>(*this);
}

Effect::~Effect() {
    latency_queue_.reset(); // Waits for Latency work already running.
    main_notices_.reset();  // After it: that work may have posted notices.
}

HostLayout Effect::layout() {
    const auto in = Input(0).GetStreamFormat(), out = Output(0).GetStreamFormat();
    return {static_cast<int>(out.mSampleRate), in.mChannelsPerFrame, out.mChannelsPerFrame};
}

// Initialized, the host's configuration is the current formats. Uninitialized,
// it is known only while the formats still match the last initialization that
// rendered audio (see Cleanup; HostSettling, HostBinding::rendering, PLUG-39
// step 2): hosts may restore a project's state before setting its formats,
// and the default format (44.1 kHz stereo) is not a choice the host has made.
std::optional<HostLayout> Effect::known_layout() {
    return settling_.known(layout());
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
    bool bypassed = false; // Read once, so the log says the bypass the processor starts with.
    {
        const Locked lock(*this);
        faults = observe_faults(); // Before the processor that counted them goes.
        try {
            const auto now = layout();
            current = settings();
            // A filter that does not suit the host plays silence, and is kept so
            // that correcting the rate or channels brings it back. Gain and Mix
            // apply from the first frame, and the Latency is the block size.
            auto activation = filter_.activation(now);
            auto next = std::make_unique<HostProcessor>(activation.playback, GetMaxFramesPerSlice(),
                                                        current, now.inputs);
            bypassed = bypass_.load();
            next->set_bypass(bypassed);
            next->reset();
            playing_.offer(std::move(next));
            filter_.activate(std::move(activation));
            (void)publish_timing(); // Notified below, with the tail.
            settling_.started(now);
            reset_requested_.store(false);
            started = true;
        } catch (const std::exception& e) {
            message = activation_failed(&e);
        } catch (...) {
            message = activation_failed(nullptr);
        }
        if (started) {
            // What plays and why: a filter restored before the formats were
            // known may not suit them. A rejected replacement's reason (the
            // working filter plays) was logged when it happened.
            try {
                const auto& f = filter_.failure();
                problem = (f && f->silent) || !filter_.notice().empty();
                message = started_line(filter_, *settling_.last_start(), GetMaxFramesPerSlice(),
                                       &current, bypassed);
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

std::string Effect::activation_failed(const std::exception* error) noexcept {
    try {
        filter_.fail_activation(error);
        const auto& f = filter_.failure();
        return f ? detail::advice_line(failure_message(*f)) : std::string();
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
        const Locked lock(*this);
        faults = observe_faults(); // Before the processor that counted them goes.
        settling_.stopped();       // Binds the formats if audio rendered with them.
        // The host does not render while it uninitializes the unit, so the
        // processor is freed here; were Render still running, it would be
        // freed by the next control call instead, without waiting under the
        // lock for a render whose input callback may call into the unit.
        // Such a render, which breaks the host's side of the contract, can
        // still mark the formats as rendered or take a reset meant for the next
        // initialization; it can no longer use a freed processor.
        playing_.offer(nullptr);
        // Uninitialized: the chosen Latency, and the filter's tail; read when
        // the host next asks, with no notification while it tears down.
        (void)publish_timing();
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
           rate <= max_sample_rate && std::floor(rate) == rate;
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
        const Locked lock(*this);
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

bool Effect::publish_timing() noexcept {
    const auto* const p = host();
    tail_.store(p ? p->tail_frames() : filter_.filter_tail());
    // The filter's own frames count at its rate, which can differ from the
    // host's after a start at another rate (jrp's review, 21 November 2026).
    tail_rate_.store(!p && filter_.filter() ? filter_.filter()->sample_rate : 0);
    const auto latency = p ? p->latency_frames() : settings().latency;
    return latency_.exchange(latency) != latency;
}

Float64 Effect::GetLatency() noexcept AUSDK_RTSAFE {
    const auto rate = GetOutput0().GetStreamFormat().mSampleRate;
    return rate > 0 ? static_cast<Float64>(latency_.load()) / rate : 0;
}

Float64 Effect::GetTailTime() noexcept AUSDK_RTSAFE {
    const auto own = tail_rate_.load();
    const auto rate =
        own > 0 ? static_cast<Float64>(own) : GetOutput0().GetStreamFormat().mSampleRate;
    return rate > 0 ? static_cast<Float64>(tail_.load()) / rate : 0;
}

void Effect::wake(std::uintptr_t why) noexcept {
    // Numbered before the wake-up, which the run follows.
    const auto number = latency_requests_.fetch_add(1) + 1;
    if (const auto hook = latency_progress_for_tests)
        hook(GetComponentInstance(), number, 0);
    if (latency_queue_) // Absent only before the constructor's end and in the destructor.
        latency_queue_->request(why);
}

void Effect::apply_latency(std::uintptr_t wakes) noexcept {
    if (const auto hook = latency_started_for_tests)
        hook(GetComponentInstance());
    // Before the settings are read: the requests this run is sure to see.
    const auto seen = latency_requests_.load();
    std::string message, faults, bypass_faults, mode_message;
    bool changed = false, problem = false, bypass_shown = false, mode_problem = false;
    {
        const Locked lock(*this);
        // The settings are read once: what is built and what is reported
        // both come from this one reading, even if the host changes the
        // parameter meanwhile (that wakes the queue again).
        const auto current = settings();
        const auto chosen = current.latency;
        if (!(wakes & latency_wake)) {
            // Only a bypass to show (request_status()): a Latency that could
            // not be built is not tried again for it.
        } else if (!host()) {
            // The parameter as it is now, which the next Initialize uses; a
            // later change wakes the queue again.
            changed = publish_timing();
        } else if (const auto& started = settling_.last_start();
                   host()->latency_frames() != chosen && started) {
            const auto before = host()->latency_frames();
            faults = observe_faults(); // Before the processor that counted them goes.
            std::string why;
            try {
                // As Initialize builds it; Render switches at its next call.
                auto activation = filter_.activation(*started);
                if (const auto hook = latency_preparation_for_tests)
                    hook();
                auto next = std::make_unique<HostProcessor>(
                    activation.playback, GetMaxFramesPerSlice(), current, started->inputs);
                next->set_bypass(bypass_.load());
                next->reset();
                // Nothing below can fail: the audio, the reported latency and
                // the status change together, from the processor that now
                // plays (ROBUST-8).
                // Render plays it from its next call; the one it replaces is
                // freed by a later control call once Render has let it go.
                playing_.offer(std::move(next));
                filter_.activate(std::move(activation));
                (void)publish_timing();
                changed = true;
            } catch (const std::exception& e) {
                try {
                    why = detail::advice_line(detail::advice_of(e));
                } catch (...) {
                }
            } catch (...) {
            }
            // If it failed, the previous processor keeps playing at its
            // latency, and the window's report says the chosen one is not in
            // use.
            problem = !changed;
            try { // Best effort: out of memory for the message changes nothing else.
                message = changed ? "Latency changed from " + std::to_string(before) + " to " +
                                        std::to_string(chosen) +
                                        " frames; the sound paused briefly while the new "
                                        "processor started."
                                  : "Latency " + std::to_string(chosen) +
                                        " frames could not be applied, so the plug-in keeps " +
                                        std::to_string(before) + " frames" +
                                        (why.empty() ? std::string(".") : ": " + why);
            } catch (...) {
            }
            update_status();
        }
        // PLUG-10: a new Gain mode. Estimate's figure is found here, off the
        // audio thread, and given to the playing filter in place, which keeps
        // its history: Render, which plays the parameter's mode, ramps to it.
        // Until then Estimate plays the no-clipping figure.
        const bool estimate = current.gain_mode == GainMode::estimate;
        if ((wakes & gain_mode_wake) &&
            (current.gain_mode != applied_gain_mode_ || filter_.estimating() != estimate)) {
            std::string why;
            try {
                filter_.estimate(estimate, [&](const FilterGains& gains) {
                    if (host())
                        host()->update_gains(gains);
                });
            } catch (const std::exception& e) {
                try {
                    why = detail::advice_of(e).problem;
                } catch (...) {
                }
            } catch (...) {
            }
            applied_gain_mode_ = current.gain_mode;
            try { // Best effort: out of memory for the message changes nothing else.
                mode_message =
                    gain_mode_line(current, filter_.gains(), filter_.filter() != nullptr, why);
                mode_problem = !why.empty() ||
                               (filter_.filter() && !applied_gain_db(current, filter_.gains()));
            } catch (...) {
            }
            update_status();
        }
        // AU-16: a bypass switched while another call held the lock
        // (SetProperty) is shown here, with Render's faults explained first
        // as SetProperty does: the Status value is still the one for the
        // other bypass setting. Not needed when it already shows this one
        // (the update above read it). A Fault that Render wrote is neither,
        // and stays until the host reads the Status text, which explains it.
        const auto status_for = [&](bool bypassed) {
            return static_cast<AudioUnitParameterValue>(
                host_status_value(filter_, known_layout().has_value(), bypassed));
        };
        const auto shown = Globals()->GetParameter(status_parameter);
        const bool bypassed = bypass_.load();
        if (shown != status_for(bypassed) && shown == status_for(!bypassed)) {
            // Kept apart from any faults the old processor counted above.
            bypass_faults = observe_faults();
            update_status();
            bypass_shown = true;
        }
        if (const auto hook = latency_locked_for_tests)
            hook(GetComponentInstance());
    }
    log_faults(faults);
    log_faults(bypass_faults);
    log(message, problem);
    log(mode_message, mode_problem);
    if (changed) {
        try {
            timing_changed();
        } catch (...) {
        }
    }
    if (!message.empty() || problem || bypass_shown || !mode_message.empty())
        notify_status();
    // Finished: the tests' witness (AU-14).
    if (const auto hook = latency_progress_for_tests)
        hook(GetComponentInstance(), 0, seen);
}

void Effect::timing_changed() {
    tell_listeners(notice_latency | notice_tail);
}

void Effect::tell_listeners(std::uintptr_t notices) noexcept {
    if (pthread_main_np())
        deliver_notices(notices);
    else if (main_notices_) // Absent only before the constructor's end.
        main_notices_->post(notices);
}

void Effect::deliver_notices(std::uintptr_t notices) noexcept {
    // AUBase's listeners, as PropertyChanged calls them, on the main thread.
    if (notices & notice_latency)
        PropertyChanged(kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
    if (notices & notice_tail)
        PropertyChanged(kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0);
    if (notices & notice_report)
        PropertyChanged(report_property, kAudioUnitScope_Global, 0);
}

OSStatus Effect::Render(AudioUnitRenderActionFlags& flags, const AudioTimeStamp& time,
                        UInt32 frames) noexcept AUSDK_RTSAFE {
    // The processor to use: the newest offer, held for the length of this
    // call so that the control side does not free it (PlayingProcessor).
    // Atomic loads and stores only.
    const auto held = playing_.hold<0>();
    auto* const host = held.get();
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
    host->set_gain_mode(gain_mode_at(Globals()->GetParameterRT(gain_mode_parameter)));
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
    // Only a render with frames is the host playing audio (as in the VST3's
    // process()): after a zero-frame render a chosen file still waits, and
    // restored state is still kept until it suits (jrp's review, 21 November
    // 2026).
    if (frames)
        settling_.played();
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
