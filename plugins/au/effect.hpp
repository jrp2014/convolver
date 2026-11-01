// SPDX-License-Identifier: GPL-2.0-or-later
// The Audio Unit (AUv2 effect) adapter. This source is GPL-2.0-or-later like
// the rest of Convolver; the built component, which includes Apple's
// Apache-2.0 AudioUnitSDK, is distributed under GPL-3.0-or-later (NOTICE.txt).
#pragma once
#include "convolver/host.hpp"
#include "convolver_identities.hpp"
#include "host_report.hpp"
#include "view_properties.hpp"
#include <AudioUnitSDK/AUBase.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <string>
#include <utility>
#include <vector>

namespace convolver::au {
inline constexpr UInt32 max_channels = identity::max_channels;
// DIAG-1: the read-only Status parameter, an indexed HostStatus whose current
// value reads (kAudioUnitProperty_ParameterStringFromValue) as the unit's
// HostStatusLine. AUBase::SaveState writes its value with the other
// parameters, but RestoreState ignores it, so it never comes back.
// Its ID comes from plugins/identities.json; never change it.
inline constexpr AudioUnitParameterID status_parameter = identity::au_status_parameter_id;
// AU-7: PLUG-10's settings as global parameters, saved by AUBase::SaveState
// with ClassInfo and restored by RestoreState (docs/develop/plugin-contract.md#settings):
// Gain in dB (min_gain_db to max_gain_db), Mix in percent, and Latency, an
// index into host_latencies (see apply_latency). IDs from
// plugins/identities.json; never change them.
inline constexpr AudioUnitParameterID gain_parameter = identity::au_gain_parameter_id;
inline constexpr AudioUnitParameterID mix_parameter = identity::au_mix_parameter_id;
inline constexpr AudioUnitParameterID latency_parameter = identity::au_latency_parameter_id;
inline constexpr AudioUnitParameterValue default_latency_index = 2;
static_assert(host_latencies[2] == default_host_latency);
// The global parameters in the order GetParameterList gives them, which
// RestoreState also notifies.
inline constexpr std::array<AudioUnitParameterID, 4> parameter_order{
    gain_parameter, mix_parameter, latency_parameter, status_parameter};

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

// For the tests only: when set, a load (RestoreState or a chosen file) calls
// it while holding the control lock, before preparing the filter, so that
// au.editor can hold a load open and check that the window's report read
// does not wait for it. Null in the component; set it only while no unit
// is loading.
inline void (*load_started_for_tests)() noexcept = nullptr;

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
    // The Status parameter is read-only: a host's write is ignored. Gain, Mix
    // and Latency are stored within their ranges; Render hands Gain and Mix to
    // the processor, and a new Latency is applied as apply_latency says.
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

    // In seconds, as Audio Units report them: frames / sample rate. The
    // latency is the newest processor's, or uninitialized the chosen one.
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
    // Why the newest request failed, numbered for the log (rejected_line);
    // empty if memory runs out.
    std::string rejection_line() noexcept;
    // What the Status parameter shows (DIAG-1).
    HostStatusLine status();
    // AU-7: the settings the parameters hold, within their ranges.
    HostSettings settings();
    // AU-6: the editor report, after explaining any new faults (PLUG-9).
    HostReport report();

  private:
    HostLayout layout();
    std::optional<HostLayout> known_layout();
    // Records a load that failed before it reached HostFilter (unreadable or
    // foreign state, or a file that could not be read), as HostFilter::load
    // records its own rejections; then, outside the lock, logs the faults
    // counted before it (observe_faults()) and the refusal, and notifies.
    void reject(HostFailureKind, const std::string& why, const HostRequest& = {},
                const Configuration* carried = nullptr);
    // Under `control_`, after a request failed: if it was the first load to
    // fail (`was_failed` is filter_.failed() from before it), the unit plays
    // silence rather than the unfiltered input; uninitialized, a failed
    // filter's tail is 0.
    void silence_failed_first_load(bool was_failed) noexcept;
    // The settings as a saved state holds them.
    using SavedSettings = std::vector<std::pair<AudioUnitParameterID, AudioUnitParameterValue>>;
    // The common step of RestoreState and a chosen file: loads `next`, then
    // logs and notifies outside the lock. `saved` settings are applied if
    // it is accepted. Returns whether it was.
    bool install(HostState next, const HostRequest&, const SavedSettings* saved,
                 bool settings_unreadable);
    // AU-6: a filter file chosen in the window (load_property).
    OSStatus load_file(const std::string& path);
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
    // Under `control_`: the status text again, for the current Gain and Mix,
    // leaving the Status value alone (PLUG-10).
    void refresh_status_text() noexcept;
    void notify_status() noexcept;
    // DIAG-1, under `control_`: takes the processor's fault counts into
    // filter_ and returns their explanation, to log; empty if none are new.
    std::string observe_faults() noexcept;
    // AU-6, under `control_`: the report as it stands; `faults` receives the
    // explanation of faults counted since the last look, to log. The window
    // reads it twice a second, so while faults go on it takes them in at most
    // every report_fault_interval, once Render's Fault is explained.
    HostReport report_locked(std::string& faults);
    static constexpr std::chrono::seconds report_fault_interval{10};
    // AU-7: stores a Gain, Mix or Latency value clamped to its range.
    void store(AudioUnitParameterID, AudioUnitParameterValue) noexcept;
    // PLUG-10: makes the chosen Latency the one that plays, on a control
    // thread, never in Render. Uninitialized, the reported latency follows the
    // chosen one. Initialized, a new processor is built with the chosen block
    // size and handed to Render, which switches to it at its next call: the
    // new one starts with no history, so the sound has a gap (the new latency
    // plus the filter's own delay). The host is told of the new latency and
    // tail, and the log says what happened. SetParameter calls this unless it
    // runs on the thread that last rendered (it could be between renders
    // there); the change then waits for the next control-thread call that
    // applies it (RestoreState, SaveState, the window's report, Initialize).
    // Never call it with `control_` held. With
    // `wait` false (the window's report) it does nothing while a load holds
    // `control_`, and the next report tries again. A Latency that could not
    // be built is not retried until it is chosen again or the unit restarts.
    void apply_latency(bool wait = true);
    // Whether the chosen Latency differs from the one reported and was not
    // refused (lock-free).
    bool latency_pending() noexcept;
    // Under `control_`: makes `next` (null when uninitializing) the processor
    // Render uses, then frees the previous one once Render no longer uses it:
    // a wait of at most the rest of one render call. Not on the render thread.
    void install_processor(std::unique_ptr<HostProcessor> next) noexcept;

    std::mutex control_;
    HostFilter filter_{max_channels}; // The shared adapter contract's control side.
    // The newest processor, which the control side uses. Render uses
    // `playing_`, which install_processor() sets to it, and marks the one it
    // is using in `rendering_` for the length of each call, so a replaced
    // processor is freed only once Render has let it go.
    std::unique_ptr<HostProcessor> host_;
    std::atomic<HostProcessor*> playing_{nullptr};
    std::atomic<HostProcessor*> rendering_{nullptr};
    std::atomic<pthread_t> render_thread_{}; // The thread of the last Render call.
    std::atomic<bool> bypass_{false};
    std::atomic<bool> reset_requested_{false};
    std::atomic<std::size_t> tail_{0};
    std::atomic<std::size_t> latency_{default_host_latency}; // In frames; see GetLatency.
    std::atomic<std::size_t> refused_latency_{0};            // Could not be built; 0 for none.
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
    std::chrono::steady_clock::time_point
        report_faults_at_{}; // Last report_locked() look; under control_.
    // AU-6: the last report read, for a read while a load holds `control_`.
    std::mutex report_cache_lock_;
    std::optional<HostReport> report_cache_; // Under report_cache_lock_.
    std::mutex logging_;
    std::string logged_;    // The last line log() wrote, under logging_.
    const std::string tag_; // "Audio Unit #n": this instance in the Convolver log.
};
} // namespace convolver::au
