// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/engine.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace convolver {
// Versioned self-contained host state. All codec calls are control-thread only.
struct HostState {
    // Absent means an empty instance: identity at the host's rate and layout for
    // equal counts, otherwise (or when failed is set) silence.
    std::optional<Configuration> configuration;
    bool bypass = false;
    // A load failed and nothing was accepted, so the instance plays silence;
    // saved so that reopening a project stays silent. Never with a configuration.
    // Encoded as version 2; all other states stay version 1.
    bool failed = false;
};
inline constexpr std::size_t max_host_state_bytes = 64U * 1024 * 1024;
std::vector<std::byte> encode_host_state(const HostState&);
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
// the labels say (docs/plugin-identities.md#channel-layouts).
std::string speaker_notice(const Configuration&, const HostLayout&);

// What an instance plays without a usable filter, under the shared adapter
// contract: identity (input c to output c) for an empty instance with equal
// counts; exact silence for unequal counts or when `failed` (a failed initial
// load or a filter that does not suit the host).
Configuration unfiltered_configuration(const HostLayout&, bool failed);

// PLUG-8: why a request to load a filter failed, or why an instance plays
// silence, kept as a value on the control side and never touched by the audio
// thread (docs/plans/2026-10-21-plugin-formats.md#failure-reasons). Adapters
// read it from HostFilter::failure(); where it is shown is DIAG-1's decision.
enum class HostFailureKind {
    filter_file,      // The filter file is missing or cannot be read.
    state_unreadable, // The saved state or preset is malformed or the wrong type.
    state_newer,      // The saved state comes from a newer Convolver.
    sample_rate,      // The filter was made for another sample rate.
    channels,         // The filter's channel counts or layout do not suit the plug-in.
    preparation,      // The filter could not be prepared for playing.
    activation,       // The processor could not be built when the host activated.
    saved_failure,    // The restored state records an earlier failed load.
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

// Control side of the shared adapter contract, for every plug-in format
// (docs/plans/2026-10-21-plugin-formats.md#shared-adapter-contract): which
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
    // True after a failed initial load: nothing accepted, and a load failed.
    bool failed() const noexcept {
        return failed_;
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
    // Checks on the state itself always run. Checks against the host run when
    // `host` is given (its configuration is known); `install` then receives
    // what to play and may throw to reject, or throw SupersededRequest to
    // abandon the request with nothing recorded or changed (a newer request
    // has arrived). Without `host`, well-formed state
    // is accepted and checked at activation. On rejection this records the
    // error, keeps the accepted filter (a failed initial load if there was
    // none) and throws std::invalid_argument with an explanation.
    // `request` says where the state came from (numbered if 0).
    void load(HostState, const std::optional<HostLayout>& host,
              const std::function<void(const Configuration&)>& install = {},
              HostRequest request = {});
    // A load that failed before decoding (malformed state, unreadable file):
    // recorded as load() records a rejection. Does not throw.
    void reject(HostFailureKind, std::string why, HostRequest request = {});
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

  private:
    HostRequest numbered(HostRequest);
    [[noreturn]] void refuse(HostFailureKind, std::string why, const HostRequest&,
                             const Configuration*);
    void record(HostFailureKind, std::string why, const HostRequest&, const Configuration*);
    std::size_t max_channels_;
    std::optional<Configuration> filter_;
    HostRequest accepted_; // The request that supplied filter_ (or the saved failure).
    std::uint64_t requests_ = 0;
    bool failed_ = false;
    std::optional<HostFailure> load_failure_, activation_failure_;
    std::string notice_;
};

// Prepared planar host boundary. Float DSP with float/double host buffers.
// Input and output channel counts follow the configuration and may differ.
// Control: construction, replace, prepare, publish, collect, destruction;
// serialize these calls. Audio: process, set_bypass, reset; one audio thread
// per instance. Control and audio may overlap only for replace, prepare,
// publish and collect. Teardown requires stopped audio.
class HostProcessor {
  public:
    HostProcessor(const Configuration&, std::size_t max_frames, std::size_t block = 256);
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
    std::size_t latency_frames() const noexcept;
    std::size_t tail_frames() const noexcept;
    // Missing input channels mean silence; null output channels are discarded.
    // In-place input/output is supported. Nonfinite input/output is silenced.
    // False means invalid dimensions, with no state advance or output guarantee.
    bool process(std::span<const float* const>, std::span<float* const>, std::size_t) noexcept;
    bool process(std::span<const double* const>, std::span<double* const>, std::size_t) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace convolver
