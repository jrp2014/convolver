// SPDX-License-Identifier: GPL-2.0-or-later
// The Audio Unit (AUv2 effect) adapter. This source is GPL-2.0-or-later like
// the rest of Convolver; the built component, which includes Apple's
// Apache-2.0 AudioUnitSDK, is distributed under GPL-3.0-or-later (NOTICE.txt).
#pragma once
#include "convolver/host.hpp"
#include "convolver_identities.hpp"
#include <AudioUnitSDK/AUBase.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <string>

namespace convolver::au {
inline constexpr UInt32 max_channels = identity::max_channels;
// DIAG-1: the read-only Status parameter, an indexed HostStatus whose current
// value reads (kAudioUnitProperty_ParameterStringFromValue) as the unit's
// HostStatusLine. AUBase::SaveState writes its value with the other
// parameters, but RestoreState ignores parameter data, so it never comes back.
// Its ID comes from plugins/identities.json; never change it.
inline constexpr AudioUnitParameterID status_parameter = identity::au_status_parameter_id;
// AU-7: PLUG-10's settings as global parameters, saved by AUBase::SaveState
// with ClassInfo and restored by RestoreState (docs/develop/plugin-contract.md#settings).
// Gain in dB (min_gain_db to max_gain_db), Gain mode indexed (GainMode),
// Mix in percent, Latency indexed (host_block_sizes). Gain mode and Latency
// are not for automation (kAudioUnitParameterFlag_NonRealTime). IDs from
// plugins/identities.json; never change them.
inline constexpr AudioUnitParameterID gain_parameter = identity::au_gain_parameter_id;
inline constexpr AudioUnitParameterID gain_mode_parameter = identity::au_gain_mode_parameter_id;
inline constexpr AudioUnitParameterID mix_parameter = identity::au_mix_parameter_id;
inline constexpr AudioUnitParameterID latency_parameter = identity::au_latency_parameter_id;

// For the tests only, never offered to hosts as a feature: a custom global
// property (custom IDs start at 64000) giving the PLUG-8 failure record, which
// a host sees only as an OSStatus and the system log. Not a stable interface;
// it may change any time. The message is cut to fit.
inline constexpr AudioUnitPropertyID test_failure_property = 64800;
struct TestFailure {
    UInt32 present = 0, kind = 0, source = 0, silent = 0;
    UInt64 request = 0;
    SInt32 sample_rate = 0;
    UInt32 inputs = 0, outputs = 0;
    char message[4096] = {};
};

// One input bus and one output bus, 1 to 8 channels each (counts may differ),
// following the shared adapter contract
// (docs/develop/plugin-contract.md).
//
// Derived from AUBase rather than AUEffectBase on purpose: AUEffectBase's
// bypass copies input to output, skipping Convolver's latency-aligned dry
// path and 64-sample ramp, and does not handle unequal channel counts. Here
// the bypass property only stores a flag; Render applies it through
// HostProcessor::set_bypass.
//
// Threads: properties, state and initialization on the host's control
// threads, serialized by `control_`; Render on one audio thread. The host
// does not render while it initializes or uninitializes the unit.
class Effect final : public ausdk::AUBase {
  public:
    explicit Effect(AudioComponentInstance);

    OSStatus Initialize() override;
    void Cleanup() override;
    OSStatus Reset(AudioUnitScope, AudioUnitElement) override;

    OSStatus GetPropertyInfo(AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, UInt32&,
                             bool&) override;
    OSStatus GetProperty(AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, void*) override;
    OSStatus SetProperty(AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, const void*,
                         UInt32) override;
    // Settings first, Status last, as generic host views list them.
    OSStatus GetParameterList(AudioUnitScope, AudioUnitParameterID*, UInt32&) override;
    OSStatus GetParameterInfo(AudioUnitScope, AudioUnitParameterID,
                              AudioUnitParameterInfo&) override;
    OSStatus GetParameterValueStrings(AudioUnitScope, AudioUnitParameterID, CFArrayRef*) override;
    // The Status parameter is read-only: a host's write is ignored. Settings
    // are stored within their ranges. Off the render thread, a new Gain mode
    // is applied at once (computing the noise estimate if needed) and a new
    // Latency is reported; from the render thread they wait for the next
    // control call (sync_settings). Latency takes effect at the next
    // Initialize, which builds the processor.
    OSStatus SetParameter(AudioUnitParameterID, AudioUnitScope, AudioUnitElement,
                          AudioUnitParameterValue, UInt32) AUSDK_RTSAFE override;

    bool StreamFormatWritable(AudioUnitScope, AudioUnitElement) override;
    bool ValidFormat(AudioUnitScope, AudioUnitElement, const AudioStreamBasicDescription&) override;
    OSStatus ChangeStreamFormat(AudioUnitScope, AudioUnitElement,
                                const AudioStreamBasicDescription&,
                                const AudioStreamBasicDescription&) override;
    UInt32 SupportedNumChannels(const AUChannelInfo**) override;
    [[nodiscard]] bool CanScheduleParameters() const noexcept AUSDK_RTSAFE override {
        return false;
    }

    // In seconds, as Audio Units report them: frames / sample rate.
    Float64 GetLatency() noexcept AUSDK_RTSAFE override;
    Float64 GetTailTime() noexcept AUSDK_RTSAFE override;
    bool SupportsTail() noexcept AUSDK_RTSAFE override {
        return true;
    }

    // ClassInfo: Apple's standard keys plus the encoded host state under
    // presets::au_state_key, as the .aupreset files from convolver-preset hold.
    OSStatus SaveState(CFPropertyListRef*) override;
    OSStatus RestoreState(CFPropertyListRef) override;

    OSStatus Render(AudioUnitRenderActionFlags&, const AudioTimeStamp&,
                    UInt32) noexcept AUSDK_RTSAFE override;

    // Why the unit plays silence or rejected its last filter; empty if neither.
    // The same text goes to the system log when it happens.
    std::string error();
    // Why the newest request failed, numbered for the log (rejected_line).
    std::string rejection_line();
    // What the Status parameter shows (DIAG-1).
    HostStatusLine status();
    // AU-7: the settings the parameters hold, within their ranges.
    HostSettings settings();
    // The processor's meters (PLUG-10); all zero while uninitialized.
    HostMeters meters();

  private:
    HostLayout layout();
    std::optional<HostLayout> known_layout();
    // Records a load that failed before it reached HostFilter (unreadable or
    // foreign state), as HostFilter::load records its own rejections.
    // Returns the explanation of faults counted before it (observe_faults()),
    // to log before the rejection.
    std::string reject(HostFailureKind, const std::string& why);
    // Records an initialization that could not build the processor (PLUG-8);
    // returns the message to log. Called under `control_`.
    std::string activation_failed(const char* why) noexcept;
    void timing_changed();
    // Writes to the Convolver log (host_log), skipping a repeat of the line
    // this instance wrote last, as hosts restart plug-ins often.
    void log(const std::string&, bool problem = true) noexcept;
    // A fault explanation (observe_faults), logged even if it repeats the
    // last line: each episode counts.
    void log_faults(const std::string&) noexcept;
    // Recomputes the status under `control_`, and tells the processor whether
    // a load failure holds Render's Fault signal (it comes first);
    // notify_status() then tells listeners, outside the lock, as they may
    // call back into the unit.
    void update_status() noexcept;
    void notify_status() noexcept;
    // DIAG-1, under `control_`: takes the processor's fault counts into
    // filter_ and returns their explanation, to log; empty if none are new.
    std::string observe_faults() noexcept;
    // AU-7, under `control_`: hands a Gain mode that the processor does not
    // have yet to it (HostProcessor::set_gain_mode, which computes the noise
    // estimate for Estimate), and returns what to log; empty if nothing changed.
    std::string sync_settings() noexcept;
    // AU-7: stores a parameter value clamped to its range.
    void store(AudioUnitParameterID, AudioUnitParameterValue) noexcept;
    // The block size the Latency parameter chooses.
    std::size_t chosen_block() noexcept;
    bool on_render_thread() const noexcept;

    std::mutex control_;
    HostFilter filter_{max_channels}; // The shared adapter contract's control side.
    std::unique_ptr<HostProcessor> host_;
    std::atomic<bool> bypass_{false};
    std::atomic<bool> reset_requested_{false};
    std::atomic<std::size_t> tail_{0};
    // AU-7: the latency reported to the host, in frames: the processor's
    // block size while initialized, otherwise the one Latency chooses.
    std::atomic<std::size_t> latency_{default_host_block};
    // The thread that last called Render, to keep heavy work off it.
    std::atomic<pthread_t> render_thread_{};
    // The rate and channels of the current or last initialization, whether
    // it has rendered, and the last initialization that did. Uninitialized,
    // the host's configuration counts as known only while it still matches
    // `committed_`: hosts may restore state before they set the formats, and
    // may initialize briefly at a placeholder rate (see Cleanup).
    std::optional<HostLayout> initialized_;
    std::atomic<bool> rendered_{false};
    std::optional<HostLayout> committed_;
    HostStatusLine status_;          // Under control_.
    bool status_log_written_ = true; // host_log_written() when status_ was set; under control_.
    std::mutex logging_;
    std::string logged_;    // The last line log() wrote, under logging_.
    const std::string tag_; // "Audio Unit #n": this instance in the Convolver log.
};
} // namespace convolver::au
