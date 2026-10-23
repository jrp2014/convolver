// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/engine.hpp"
#include <cstdint>
#include <functional>
#include <optional>
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
HostState decode_host_state(std::span<const std::byte>);

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
    // Why the instance plays silence or rejected its last load; empty if neither.
    const std::string& error() const noexcept {
        return error_;
    }
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
    // what to play and may throw to reject. Without `host`, well-formed state
    // is accepted and checked at activation. On rejection this records the
    // error, keeps the accepted filter (a failed initial load if there was
    // none) and throws std::invalid_argument with an explanation.
    void load(HostState, const std::optional<HostLayout>& host,
              const std::function<void(const Configuration&)>& install = {});
    // A load that failed before decoding (malformed state, unreadable file):
    // recorded as load() records a rejection. Does not throw.
    void reject(std::string why);
    // What to play under `host` at activation: the accepted filter when it
    // suits the host. Otherwise silence and an error, keeping the filter, so
    // correcting the host's rate or layout brings it back.
    Configuration activate(const HostLayout& host);

  private:
    std::size_t max_channels_;
    std::optional<Configuration> filter_;
    bool failed_ = false;
    std::string error_;
    std::string notice_;
};

// Prepared planar host boundary. Float DSP with float/double host buffers.
// Input and output channel counts follow the configuration and may differ.
// Control: construction, replace, collect, destruction; serialize these calls.
// Audio: process, set_bypass, reset; one audio thread per instance. Control and
// audio may overlap only for replace/collect. Teardown requires stopped audio.
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
