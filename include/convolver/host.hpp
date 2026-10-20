// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/engine.hpp"
#include <optional>

namespace convolver {
// Versioned self-contained host state. All codec calls are control-thread only.
struct HostState {
    std::optional<Configuration> configuration; // Absent means identity at host rate/layout.
    bool bypass = false;
};
inline constexpr std::size_t max_host_state_bytes = 64U * 1024 * 1024;
std::vector<std::byte> encode_host_state(const HostState&);
HostState decode_host_state(std::span<const std::byte>);

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
    void replace(const Configuration&); // Prepare off-thread; latest pending state wins.
    void collect() noexcept;            // Retire old FFT plans off-thread.
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
