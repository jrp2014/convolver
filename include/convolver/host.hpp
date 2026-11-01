// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/engine.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace convolver {
// PLUG-10: the settings every adapter offers as host parameters
// (docs/develop/plugin-contract.md#settings). The defaults leave the sound as
// it was before the settings existed.
inline constexpr double min_gain_db = -100, max_gain_db = 24; // jrp, 25 October 2026.
// The latencies adapters offer, in frames: HostProcessor's block size, the
// engine's uniform partition (the CLI's --block-size). Smaller is less delay
// and more processor load. jrp, 25 October 2026: powers of two from 64 to
// 4096, default 256.
inline constexpr std::array<std::size_t, 7> host_latencies{64, 128, 256, 512, 1024, 2048, 4096};
inline constexpr std::size_t default_host_latency = 256;
struct HostSettings {
    // The filtered signal's gain in decibels, within min_gain_db and
    // max_gain_db (clamped there by HostProcessor).
    double gain_db = 0;
    // The share of the filtered signal, 0 to 1; the rest is the input delayed
    // by the latency. Ignored (always 1) when input and output counts differ,
    // as there is no dry signal (the bypass rule). Clamped to 0..1.
    double mix = 1;
    // The latency in frames, one of host_latencies. Unlike gain and mix it
    // cannot change while a processor runs: it is the block size the
    // processor is built with.
    std::size_t latency = default_host_latency;
    bool operator==(const HostSettings&) const = default;
};

// Versioned self-contained host state. All codec calls are control-thread only.
struct HostState {
    // Absent means an empty instance: identity at the host's rate and layout for
    // equal counts, otherwise (or when failed is set) silence.
    std::optional<Configuration> configuration;
    // Saved and applied by the VST3 adapter only. The LV2 and AU adapters leave
    // bypass to the host, as those formats do, and ignore this flag.
    bool bypass = false;
    // A load failed and nothing was accepted, so the instance plays silence;
    // saved so that reopening a project stays silent. Never with a configuration.
    // Encoded as version 2; all other states stay version 1.
    bool failed = false;
    // Saved and applied by the VST3 adapter only (VST3-4): the settings its
    // parameters held. Encoded as version 3, only when they differ from the
    // defaults, so every other state keeps its older version. Absent in
    // older states, presets from convolver-preset and the other formats'
    // states; the Audio Unit and LV2 save their settings as parameters and
    // ports, and ignore these.
    std::optional<HostSettings> settings = std::nullopt;
};
inline constexpr std::size_t max_host_state_bytes = 64U * 1024 * 1024;
// Throws std::length_error when the encoding would exceed max_host_state_bytes
// (encoded_host_state_size()), before allocating it.
std::vector<std::byte> encode_host_state(const HostState&);
// PLUG-12: the size in bytes encode_host_state() writes for this state,
// counted without encoding or allocating. It may exceed max_host_state_bytes;
// such state cannot be saved (saturates at SIZE_MAX).
std::size_t encoded_host_state_size(const HostState&) noexcept;
// PLUG-12: why this state is too large to save, in full sentences saying what
// to change, or empty when it fits. HostFilter::load refuses such a filter
// before accepting it, so a plug-in never plays a filter it cannot save, and
// convolver-preset refuses to write it. PLUG-13: a filter fits only with room
// for the settings (version 3's five words), even when the state holds none,
// so changing a VST3 setting never makes an accepted filter unsaveable.
std::string host_state_size_error(const HostState&);
// Throws NewerHostState for state from a newer Convolver, std::length_error
// above the size limit, and std::invalid_argument for anything else malformed.
HostState decode_host_state(std::span<const std::byte>);
// Host state with a version this build does not read; what() explains.
class NewerHostState : public std::invalid_argument {
  public:
    using std::invalid_argument::invalid_argument;
};

// The sample rate and channel counts a host has negotiated, and the speakers
// its buses carry where the format says: WAV channel-mask bits 0 to 17 (the
// speaker table in plugins/identities.json), one bit per channel in bus order,
// or 0 when the host names no positions a WAV mask can express.
struct HostLayout {
    int sample_rate = 0;
    std::size_t inputs = 0, outputs = 0;
    std::uint32_t input_speakers = 0, output_speakers = 0;
};

// Advisory check of the speakers a filter's outputs are labelled for (its
// channel mask) against the host's output speakers. Empty when they agree, or
// when either side names no speakers: a mask of 0, one that does not give one
// speaker per channel, and one-channel buses are never compared. Channels are
// compared in bus order; in a layout with side (SL SR) and no back (BL BR)
// speakers, SL and SR count as BL and BR (5.1 is written as 0x3F or 0x60F).
// Never a reason to reject a filter: channels are routed by bus index whatever
// the labels say (docs/develop/plugin-identities.md#channel-layouts).
std::string speaker_notice(const Configuration&, const HostLayout&);

// What an instance plays without a usable filter, under the shared adapter
// contract: identity (input c to output c) for an empty instance with equal
// counts; exact silence for unequal counts or when `failed` (a failed initial
// load or a filter that does not suit the host).
Configuration unfiltered_configuration(const HostLayout&, bool failed);

class HostProcessor;

// PLUG-8: why a request to load a filter failed, or why an instance plays
// silence, kept as a value on the control side and never touched by the audio
// thread (docs/develop/plugin-contract.md#failure-reasons). Adapters
// read it from HostFilter::failure(), show it with host_status() and log it
// (docs/develop/plugin-contract.md#status-and-log).
enum class HostFailureKind {
    filter_file,      // The filter file is missing or cannot be read.
    state_unreadable, // The saved state or preset is malformed or the wrong type.
    state_newer,      // The saved state comes from a newer Convolver.
    sample_rate,      // The filter was made for another sample rate.
    channels,         // The filter's channel counts or layout do not suit the plug-in.
    preparation,      // The filter could not be prepared for playing.
    activation,       // The processor could not be built when the host activated.
    saved_failure,    // The restored state records an earlier failed load.
    too_large,        // The filter is too large to save in the plug-in's state (PLUG-12).
};
// Where a filter came from. Plug-ins cannot tell a preset from a saved
// session: both arrive as state.
enum class HostFilterSource { state, file };
struct HostRequest {
    HostFilterSource source = HostFilterSource::state;
    std::string path; // The filter file, for HostFilterSource::file.
    // Numbers requests in order, so a later request's outcome is never
    // mistaken for this one's. 0 asks HostFilter to number it. An adapter
    // that numbers requests itself (LV2's generations, counted on the audio
    // thread) numbers all of them, each larger than every earlier one. An
    // adapter that needs one number before it calls load() takes it from
    // HostFilter::next_request(), so numbers never repeat.
    // A failure at activation carries the accepted filter's request, which is
    // 0 for an instance that never loaded one.
    std::uint64_t number = 0;
    // VST3-11: every file an accepted file's filter was read from (the file
    // itself, then for a .cfg or list the lists and configurations visited
    // and the chosen configuration's IRs, but not the IRs of list entries
    // passed over), as read_filter_file() gives them, so a window can reload
    // the filter when one changes. Empty for state and for a refused file.
    std::vector<std::string> files{};
};
struct HostFailure {
    HostFailureKind kind = HostFailureKind::state_unreadable;
    HostRequest request;
    // The filter the request carried, when it could be read: rate and counts.
    int sample_rate = 0;
    std::size_t inputs = 0, outputs = 0;
    // True: the instance plays silence. False: a working filter keeps playing.
    bool silent = false;
    // What failed, why and what to change, as the CLI explains its errors.
    std::string message;
};

// Thrown by a HostFilter::load install callback to abandon a request that a
// newer one has overtaken: load() rethrows it and changes neither the filter
// nor the failure, though the request's number is used up.
class SupersededRequest : public std::exception {
  public:
    const char* what() const noexcept override {
        return "superseded by a newer request";
    }
};

// DIAG-1: problems HostProcessor::process meets while it plays, counted per
// call on the audio thread without locking or allocating, and read on the
// control side (HostProcessor::faults), where adapters explain and log them.
// Each count is the number of process() calls the problem affected.
struct HostFaults {
    // Input samples that were not finite or beyond the float range: played
    // as silence (the call itself succeeds).
    std::uint64_t rejected_input = 0;
    // Output that was not finite or beyond the float range, from extreme but
    // finite input or filter values: those samples are silenced and the
    // filter restarts (reset) after the call, so its tail is lost.
    std::uint64_t output_overflow = 0;
    // The engine failed: the call is silenced, the filter restarts and
    // process() returns false.
    std::uint64_t processing_failure = 0;
    // The host passed more frames or channels than were set up: the call is
    // refused (false), its output not written.
    std::uint64_t invalid_call = 0;
    std::uint64_t total() const noexcept {
        return rejected_input + output_overflow + processing_failure + invalid_call;
    }
    bool any() const noexcept {
        return total() != 0;
    }
};
// What happened, why and what to change, for the log: one sentence per kind
// of fault. Empty when there are none.
std::string fault_message(const HostFaults&);
// What HostFilter::observe() found. `current`: faults met by the filter that
// plays now, which faults() (and so the status) shows. `earlier`: faults
// counted before an accepted load replaced the filter they met, while the new
// one was prepared; logged, but never shown. `held`: the status shows a load
// failure, which comes before Fault (HostFilter::failure_shown()), so it does
// not show `current` either.
struct HostFaultReport {
    HostFaults current, earlier;
    bool held = false;
    bool any() const noexcept {
        return current.any() || earlier.any();
    }
};
// The log text for both parts, each with its own ending, so the log never
// says the status shows Fault for faults it does not show.
std::string fault_message(const HostFaultReport&);

// Control side of the shared adapter contract, for every plug-in format
// (docs/develop/plugin-contract.md): which
// filter a load replaces, which filter is saved, and what plays. It does no
// audio work; adapters call it under their control lock.
class HostFilter {
  public:
    // `max_channels` is the format's channel bound, a check on the state itself.
    explicit HostFilter(std::size_t max_channels = 256) : max_channels_(max_channels) {}
    // The accepted filter, which a host saves. Absent for an empty instance and
    // after a failed initial load.
    const std::optional<Configuration>& filter() const noexcept {
        return filter_;
    }
    // worst_case_peak() of filter(), found when it was accepted; 0 without one.
    double filter_peak() const noexcept {
        return peak_;
    }
    // True after a failed initial load: nothing accepted, and a load failed.
    bool failed() const noexcept {
        return failed_;
    }
    // DIAG-1: whether host_status() shows a load failure, which comes before
    // Fault. Adapters pass it to HostProcessor::hold_fault_signal() after
    // every load, rejection and activation, so the audio thread does not show
    // Fault over it either.
    bool failure_shown() const noexcept {
        return activation_failure_.has_value() || load_failure_.has_value() || failed_;
    }
    // Why the instance plays silence or rejected its last load (PLUG-8).
    // A failure found at activation comes first; it clears at the next
    // activation that suits the filter. A rejected request's failure stays
    // until a later request succeeds, also across activations.
    const std::optional<HostFailure>& failure() const noexcept {
        return activation_failure_ ? activation_failure_ : load_failure_;
    }
    // failure()'s message, or empty.
    const std::string& error() const noexcept;
    // Why the newest request failed (or the saved failure it restored), even
    // while a failure found at activation comes first in failure(). This is
    // what to log when a load is rejected.
    const std::optional<HostFailure>& rejection() const noexcept {
        return load_failure_;
    }
    // The request that supplied the accepted filter (or the saved failure);
    // number 0 before any was accepted.
    const HostRequest& accepted() const noexcept {
        return accepted_;
    }
    // Reserves the next request number, for a request numbered before load().
    // The in-tree adapters do not use it (LV2 numbers every request itself).
    std::uint64_t next_request() noexcept;
    // A possible problem that does not stop the accepted filter playing, from
    // the last check against the host (speaker_notice); empty if none. It
    // describes the host as of the last load() with a host or activate(); an
    // adapter that shows it must recheck after the host changes its buses.
    const std::string& notice() const noexcept {
        return notice_;
    }
    // Load decoded state: a filter, none to return to an empty instance, or a
    // saved failed load (plays silence). Bypass is the adapter's to apply.
    // Checks on the state itself always run, including that it fits the
    // saved-state limit (PLUG-12), so whatever is accepted can be saved. Checks against the host
    // run when `host` is given (its configuration is known); `install` then receives what to play
    // and may throw to reject, or throw SupersededRequest to abandon the request with nothing
    // recorded or changed (a newer request has arrived). Without `host`, well-formed state is
    // accepted and checked at activation. On rejection this records the error, keeps the accepted
    // filter (a failed initial load if there was none) and throws std::invalid_argument with an
    // explanation. A transaction: everything kept is built before `install` is called and nothing
    // after it throws, so `install` must publish as its last step (prepare, which may throw, then
    // publish, which cannot). What plays is then always what is saved, even when memory runs out.
    // `request` says where the state came from (numbered if 0).
    // `counting`: the processor that plays (if any). An accepted load clears
    // its fault baseline (HostProcessor::clear_faults()) and observes from the
    // same snapshot, so faults shown afterwards are only the new filter's.
    void load(HostState, const std::optional<HostLayout>& host,
              const std::function<void(const Configuration&)>& install = {},
              HostRequest request = {}, HostProcessor* counting = nullptr);
    // A load that failed before decoding (malformed state, unreadable file):
    // recorded as load() records a rejection. Does not throw.
    // `carried`, when given, is the filter the request held (a readable file
    // made for another rate or other channels): the failure keeps its rate
    // and counts for the short status text. Its paths are not used.
    void reject(HostFailureKind, std::string why, HostRequest request = {},
                const Configuration* carried = nullptr);
    // The same, as unreadable state.
    void reject(std::string why) {
        reject(HostFailureKind::state_unreadable, std::move(why));
    }
    // What to play under `host` at activation: the accepted filter when it
    // suits the host. Otherwise silence and an error, keeping the filter, so
    // correcting the host's rate or layout brings it back.
    Configuration activate(const HostLayout& host);
    // The adapter could not build a processor for what activate() returned,
    // so it plays silence; recorded until the next activate().
    void fail_activation(std::string why);
    // DIAG-1: processing faults since the filter that plays was accepted or
    // the instance last started (activate()), whichever is later. host_status()
    // shows them as HostStatus::fault after any load failure.
    const HostFaults& faults() const noexcept {
        return faults_;
    }
    // Takes the processor's counts (HostProcessor::faults(), totals since it
    // was built) and returns the faults that no earlier call returned, for the
    // adapter to log; they are added to faults(). activate() starts the count
    // again for a new processor, so call this before discarding the old one,
    // and before load(): an accepted load clears faults(). Faults counted
    // between the last call and an accepted load (while it was prepared) are
    // returned as `earlier`, for the log, and not added to faults(). `held`
    // is failure_shown().
    HostFaultReport observe(const HostFaults& totals) noexcept;

  private:
    HostRequest numbered(HostRequest);
    [[noreturn]] void refuse(HostFailureKind, std::string why, const HostRequest&,
                             const Configuration*);
    void record(HostFailureKind, std::string why, const HostRequest&, const Configuration*);
    std::size_t max_channels_;
    std::optional<Configuration> filter_;
    double peak_ = 0;      // filter_peak()
    HostRequest accepted_; // The request that supplied filter_ (or the saved failure).
    std::uint64_t requests_ = 0;
    bool failed_ = false;
    std::optional<HostFailure> load_failure_, activation_failure_;
    std::string notice_;
    HostFaults faults_, observed_; // Shown since the last clear; processor totals seen.
    HostFaults unshown_; // Counted before an accepted load and not yet returned by observe().
};

// DIAG-1: what a host's status display shows for an instance, for formats
// with no editor (a read-only parameter in VST3 and AU). The status follows
// failure(): a refused replacement shows "load failed" although the previous
// filter keeps playing, as LV2's status port does.
enum class HostStatus {
    playing = 0,     // A filter is accepted and suits the host.
    no_filter = 1,   // An empty instance: identity, or silence for unequal counts.
    load_failed = 2, // The newest request failed, or the instance plays silence.
    unchecked = 3,   // A filter is accepted, but the host's configuration is not known yet.
    fault = 4,       // HostFilter::faults(): the audio met a problem while playing.
};
struct HostStatusLine {
    HostStatus status = HostStatus::no_filter;
    // One line of at most max_status_text characters (ASCII), saying what the
    // instance plays and, after a failure, why, in short; the full
    // explanation is HostFilter::failure()'s message, which adapters log.
    std::string text;
};
inline constexpr std::size_t max_status_text = 127; // VST3's String128, less its terminator.
// `host` is the configuration the instance plays under, or nullopt while it
// is not known (VST3 before activation, AU before initialization).
// `log_written` false (host_log_written()) says the Convolver log could not
// be written, instead of pointing to it. `settings` are the instance's Gain
// and Mix (PLUG-10): without a filter, or after a failed load, the text says
// what they let through (the input at a changed level, or the dry signal),
// not only what the filter does.
HostStatusLine host_status(const HostFilter&, const std::optional<HostLayout>& host,
                           bool log_written = true, const HostSettings& settings = {});

// Prepared planar host boundary. Float DSP with float/double host buffers.
// Input and output channel counts follow the configuration and may differ.
// Control: construction, replace, prepare, publish, silence, collect,
// destruction; serialize these calls. Audio: process, set_bypass, reset, mute,
// publications; one audio thread per instance. Control and audio may overlap
// only for replace, prepare, publish, silence and collect. Teardown requires
// stopped audio.
class HostProcessor {
  public:
    HostProcessor(const Configuration&, std::size_t max_frames, std::size_t block = 256);
    // PLUG-10: with the settings applied from the first frame: gain and mix,
    // and the latency as the block size. Throws std::invalid_argument if the
    // latency is not one of host_latencies.
    HostProcessor(const Configuration&, std::size_t max_frames, const HostSettings&);
    ~HostProcessor();
    HostProcessor(const HostProcessor&) = delete;
    HostProcessor& operator=(const HostProcessor&) = delete;
    // Prepare off-thread; latest pending state wins. A filter that is published
    // before any frame has been processed since construction or reset is
    // installed directly; otherwise the new filter warms up for one block and
    // crossfades over 256 frames. reset() ends a transition in progress (the
    // newer filter plays at once), so a filter published before audio resumes
    // is installed directly.
    void replace(const Configuration&);
    // replace() in two steps, so an adapter can check between them that the
    // request is still the newest (LV2-2). prepare() builds and plans the
    // filter off the audio thread and may throw; publish() hands it to the
    // audio thread exactly as replace() does and cannot fail. A Replacement
    // that is never published is simply discarded. Control calls, serialized
    // with replace() and collect(); publish only to the processor that
    // prepared it.
    class Replacement {
      public:
        Replacement(Replacement&&) noexcept;
        Replacement& operator=(Replacement&&) noexcept;
        ~Replacement();

      private:
        friend class HostProcessor;
        struct Data;
        explicit Replacement(std::unique_ptr<Data>) noexcept;
        std::unique_ptr<Data> data_;
    };
    Replacement prepare(const Configuration&) const;
    void publish(Replacement&&) noexcept;
    void collect() noexcept; // Retire old FFT plans off-thread.
    void reset() noexcept;
    void set_bypass(bool) noexcept;
    // PLUG-10, audio thread, like set_bypass: the gain in decibels (clamped
    // to min_gain_db..max_gain_db) and the mix (0 to 1, ignored for unequal
    // counts). Each change ramps over 64 frames.
    void set_gain_db(double) noexcept;
    void set_mix(double) noexcept;
    // Audio thread: the current filter ramps to silence over 64 frames (or is
    // silent at once, before any audio since construction or reset) and
    // stays silent until a filter published after this call takes over,
    // which then fades in from silence as any replacement does; one published
    // before it (still waiting for the audio thread) is silenced too. Bypass
    // still passes the dry signal. For a failure the audio thread finds
    // itself: LV2's run() refusing an empty instance's first load (no
    // worker, or a full worker queue). The control side uses silence().
    void mute() noexcept;
    // The same for publications up to `through`, a count publications()
    // returned. An adapter that decides to mute from a flag its control side
    // clears before publishing reads the count first, then the flag, so a
    // filter published after the flag was cleared is never silenced. A count
    // older than the filter already playing does nothing.
    void mute(std::uint64_t through) noexcept;
    // Control side (ROBUST-3): mute() for a failure the control side finds,
    // a failed first load, without allocating, locking or throwing, so the
    // silence cannot fail when memory has run out. The audio thread mutes at
    // the start of its next process() call, before it adopts a pending
    // filter: one published before this call stays silent, one published
    // after it takes over. With no audio since construction or reset, that
    // next call is silent from its first frame; otherwise the filter ramps to
    // silence over 64 frames. A call already running when this one is made
    // may still play a filter it adopted.
    void silence() noexcept;
    // Audio thread: how many filters replace() and publish() have handed
    // over so far.
    std::uint64_t publications() const noexcept;
    std::size_t latency_frames() const noexcept;
    std::size_t tail_frames() const noexcept;
    // DIAG-1: the faults process() has counted since construction. Any thread,
    // also while audio runs: each count is read on its own, so a snapshot
    // taken during a call may show one count of it and not another.
    HostFaults faults() const noexcept;
    // DIAG-1, the one place where an accepted load starts the fault count
    // afresh. Control side: takes a snapshot of faults() and makes it the
    // baseline that the audio-side calls below measure from, and returns it
    // for HostFilter::load(), which observes from the same snapshot.
    HostFaults clear_faults() noexcept;
    // Control side: while the status shows a load failure, which comes first
    // (host_status()), the audio thread must not show Fault over it.
    void hold_fault_signal(bool held) noexcept;
    // Any thread: faults counted since the last clear_faults() (all of them,
    // for a processor never cleared).
    bool faults_since_clear() const noexcept;
    // Audio thread: true when faults arrived since the last clear_faults(),
    // the adapter has not shown them yet, and no load failure holds the
    // signal. After the adapter has shown Fault, it calls fault_signalled(),
    // so each clear shows Fault at most once.
    bool fault_to_signal() const noexcept;
    void fault_signalled() noexcept;
    // Missing input channels mean silence; null output channels are discarded.
    // In-place input/output is supported. Nonfinite input/output is silenced.
    // False means invalid dimensions, with no state advance or output guarantee,
    // or an engine failure, with the output silenced. Each problem is counted
    // in faults().
    bool process(std::span<const float* const>, std::span<float* const>, std::size_t) noexcept;
    bool process(std::span<const double* const>, std::span<double* const>, std::size_t) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace convolver
