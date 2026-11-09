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
#include <string>
#include <string_view>
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
// index into host_latencies (see request_latency). IDs from
// plugins/identities.json; never change them.
inline constexpr AudioUnitParameterID gain_parameter = identity::au_gain_parameter_id;
inline constexpr AudioUnitParameterID mix_parameter = identity::au_mix_parameter_id;
inline constexpr AudioUnitParameterID latency_parameter = identity::au_latency_parameter_id;
inline constexpr AudioUnitParameterValue default_latency_index = 0;
static_assert(host_latencies[0] == default_host_latency);
// PERF-2: SaveState also writes the Latency in frames under this ClassInfo
// key, so that a later change to the menu cannot change a saved Latency.
// State without it was saved before 0 frames led the menu; RestoreState
// reads its Latency position in the earlier menu (64 to 4096 frames).
inline constexpr std::string_view latency_frames_key = "convolver-latency-frames";
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
// For the tests only (AU-13): when 1, 2 or 3, creating the Latency work's
// semaphore, serial queue or dispatch source fails as if out of memory, so
// au.adapter can check that the unit then fails to open cleanly. 0 in the
// component.
inline int latency_queue_failure_for_tests = 0;
// For the tests only (AU-14, from jrp's reviews of db54160 and 5f47ecb): a
// witness that a unit's Latency work has finished, in place of a sleep. Each
// unit numbers its requests (wake()) from 1. When set, a unit
// calls this with itself and `requested` after each request (`done` 0), on
// the requesting thread, and with `done` at the end of each run of its work
// (logged, the host told), the newest number that run had seen when it
// began (`requested` 0), on the work's queue. A test that waits until a
// unit's `done` reaches the `requested` of its change knows that a run of
// that unit's work that saw the change has finished. Null in the component;
// change it only while no unit's Latency work is running.
inline void (*latency_progress_for_tests)(AudioComponentInstance unit, std::uint64_t requested,
                                          std::uint64_t done) noexcept = nullptr;
// For the tests only (PLUG-21): when set, each run of a unit's Latency work
// calls it first, on the work's queue, so that au.adapter can hold a run;
// and closing a unit calls the second once it has cancelled the work and
// before it waits for a run in progress, so that the test releases a held
// run only after closing has reached that point. Null in the component;
// change them only while no unit's Latency work is running.
inline void (*latency_started_for_tests)(AudioComponentInstance unit) noexcept = nullptr;
inline void (*latency_closing_for_tests)(AudioComponentInstance unit) noexcept = nullptr;
// For the tests only (AU-16): when set, each run of a unit's Latency work
// calls it at the end of the part that holds the control lock, after it has
// shown any bypass change, on the work's queue, so that au.adapter can
// switch bypass while the lock is taken and only a later run can show it.
// Null in the component; change it only while no unit's Latency work is
// running.
inline void (*latency_locked_for_tests)(AudioComponentInstance unit) noexcept = nullptr;

// One input bus and one output bus, 1 to 8 channels each (counts may differ),
// following the shared adapter contract
// (docs/develop/plugin-contract.md).
//
// Derived from AUBase rather than AUEffectBase on purpose: AUEffectBase's
// bypass copies input to output, skipping Convolver's latency-aligned dry
// path and 1.33 ms ramp, and does not handle unequal channel counts. Here
// the bypass property only stores a flag; Render applies it through
// HostProcessor::set_bypass.
//
// Threads: properties, state and initialization on the host's control
// threads, serialized by `control_`; Render on one audio thread. The host
// does not render while it initializes or uninitializes the unit.
class Effect final : public ausdk::AUBase {
  public:
    explicit Effect(AudioComponentInstance);
    ~Effect() override;
    Effect(const Effect&) = delete;
    Effect& operator=(const Effect&) = delete;

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
    // the processor, and a new Latency is applied as request_latency says.
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
    void reject(HostFailureKind, const Advice& why, const HostRequest& = {},
                const Configuration* carried = nullptr);
    // Under `control_`, after a request failed: marks it failed
    // (HostFilter::fail_request(), in case it could not be recorded); if it
    // was the first load to fail (`was_failed` is filter_.failed() from
    // before it), the unit plays silence rather than the unfiltered input;
    // uninitialized, a failed filter's tail is 0.
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
    // RestoreState's checks and decoding, before anything changes: false
    // after recording why the state was refused (reject()).
    bool read_state(CFPropertyListRef, HostState& next, std::optional<SavedSettings>& saved,
                    bool& settings_unreadable);
    // ROBUST-7: a request that failed before its failure could be recorded
    // (memory ran out building the message) still fails a first load, as
    // silence_failed_first_load() says; then notifies. Takes `control_`.
    void fail_unrecorded() noexcept;
    // Records an initialization that could not build the processor (PLUG-8);
    // returns the message to log. Called under `control_`.
    std::string activation_failed(const char* why) noexcept;
    void timing_changed();
    // AU-17: tells the host's property listeners about the unit's own
    // changes (the `notice_*` bits), on the main thread. AUBase keeps its
    // listeners in a plain vector that hosts change on their main thread
    // without a lock, so a call from any other thread (the Latency queue)
    // is handed to the main queue, as the JUCE Audio Unit wrapper does.
    static constexpr std::uintptr_t notice_latency = 1, notice_tail = 2, notice_report = 4;
    void tell_listeners(std::uintptr_t notices) noexcept;
    void deliver_notices(std::uintptr_t notices) noexcept;
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
    // PLUG-10: a new Latency is applied only on the unit's own serial queue
    // (latency_queue_), never by the thread that chose it: SetParameter can
    // arrive on any thread, including an audio thread or inside a render
    // callback, so it only stores the value and wakes the queue (AU-12:
    // dispatch_source_merge_data: an atomic OR, then a wake-up that may make a
    // system call to ask for a thread; the open-source libdispatch neither
    // allocates nor takes a user-space lock there. Apple does not call it
    // real-time safe, which jrp accepted). RestoreState wakes it too. Uninitialized, the work makes
    // the reported latency follow the choice. Initialized, it builds a processor with the chosen
    // block size and hands it to Render, which switches at its next call: the
    // new one starts with no history, so the sound has a gap (the new latency
    // plus the filter's own delay). The host is told of the new latency and
    // tail, from the queue, and the log says what happened. A Latency
    // that cannot be built is tried again only when chosen again. A bypass
    // switched while another call held `control_` wakes the work too, with
    // its own bit of the source's data (request_status()), and such a run
    // only shows the bypass in the Status: it does not touch the Latency
    // (AU-16).
    static constexpr std::uintptr_t latency_wake = 1, status_wake = 2;
    void request_latency() noexcept {
        wake(latency_wake);
    }
    void request_status() noexcept {
        wake(status_wake);
    }
    void wake(std::uintptr_t why) noexcept;
    void apply_latency(std::uintptr_t wakes) noexcept; // The wakes' bits, ORed.
    // Under `control_`: makes `next` (null when uninitializing) the processor
    // Render uses and returns the previous one, for release_processor().
    std::unique_ptr<HostProcessor> install_processor(std::unique_ptr<HostProcessor> next) noexcept;
    // Without `control_` (a render callback may call into the unit): frees a
    // replaced processor once Render no longer uses it, a wait of at most the
    // rest of one render call. Never on a thread that is rendering.
    void release_processor(std::unique_ptr<HostProcessor> previous) noexcept;

    std::mutex control_;
    HostFilter filter_{max_channels}; // The shared adapter contract's control side.
    // The newest processor, which the control side uses. Render uses
    // `playing_`, which install_processor() sets to it, and marks the one it
    // is using in `rendering_` for the length of each call, so a replaced
    // processor is freed only once Render has let it go.
    std::unique_ptr<HostProcessor> host_;
    std::atomic<HostProcessor*> playing_{nullptr};
    std::atomic<HostProcessor*> rendering_{nullptr};
    std::atomic<bool> bypass_{false};
    std::atomic<bool> reset_requested_{false};
    std::atomic<std::size_t> tail_{0};
    std::atomic<std::size_t> latency_{default_host_latency}; // In frames; see GetLatency.
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
    bool status_bypassed_ = false;   // bypass_ when status_ was set; under control_ (AU-16).
    std::chrono::steady_clock::time_point
        report_faults_at_{}; // Last report_locked() look; under control_.
    // AU-6: the last report read, for a read while another call holds
    // `control_` (a load or the Latency work preparing a filter, which can
    // take a while). update_status() forgets it whenever what the unit plays
    // changes, so a read after a load never gets the report from before it;
    // a read with no copy waits for the lock.
    std::mutex report_cache_lock_;
    std::optional<HostReport> report_cache_; // Under report_cache_lock_.
    std::mutex logging_;
    std::string logged_;    // The last line log() wrote, under logging_.
    const std::string tag_; // "Audio Unit #n": this instance in the Convolver log.
    // AU-17: hands tell_listeners() calls to the main queue through a
    // DISPATCH_SOURCE_TYPE_DATA_OR source; defined in effect.cpp. Declared,
    // made before latency_queue_ and closed after it, so the Latency work can
    // always post; a notice still pending when the unit closes is dropped.
    struct MainNotices;
    std::unique_ptr<MainNotices> main_notices_;
    // AU-12: the Latency work (request_latency()), on a serial dispatch
    // queue woken by a DISPATCH_SOURCE_TYPE_DATA_OR source; defined in
    // effect.cpp, so that no Objective-C file sees a dispatch object. Made at
    // the end of the constructor, after every member it uses, and ended in
    // the destructor, which waits for work already running.
    struct LatencyQueue;
    std::unique_ptr<LatencyQueue> latency_queue_;
    std::atomic<std::uint64_t> latency_requests_{0}; // Numbers the requests (AU-14).
};
} // namespace convolver::au
