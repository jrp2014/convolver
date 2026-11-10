// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/engine.hpp"
#include <cstddef>
#include <memory>

namespace convolver::detail {
// Internal callers validate the transform length before this size calculation.
std::size_t fft_storage(std::size_t n) noexcept; // owned float buffer elements
// Throws std::invalid_argument for options this backend cannot honour, as
// RealFft would. The engine checks them even when a filter needs no
// transform (a filter short enough for the direct sum alone).
void check_fft_options(const FftOptions&);

// Canonical bins 0..N/2, interleaved real/imaginary; separate DC and Nyquist.
// Forward is the mathematical DFT; inverse returns N times the real signal.
// Buffers and setup are owned, prepared once, and never allocated in execution.
class RealFft {
  public:
    explicit RealFft(std::size_t n, FftOptions = {});
    ~RealFft();
    RealFft(const RealFft&) = delete;
    RealFft& operator=(const RealFft&) = delete;
    float* time() noexcept;
    float* frequency() noexcept;
    float* sum() noexcept;
    void forward() noexcept;
    void inverse() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace convolver::detail
