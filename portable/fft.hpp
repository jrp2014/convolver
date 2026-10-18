// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/engine.hpp"
#include <cstddef>
#include <memory>

namespace convolver::detail {
// Internal callers validate 1 <= block <= 2^20 before these size calculations.
std::size_t fft_size(std::size_t block) noexcept;
std::size_t fft_storage(std::size_t n) noexcept; // owned float buffer elements

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
