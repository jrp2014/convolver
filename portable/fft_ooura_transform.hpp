// SPDX-License-Identifier: GPL-2.0-or-later
// Ooura's split-radix real DFT (third_party/ooura) with the conventions of
// RealFft: the OOURA backend's transform, and the PFFFT backend's for the
// lengths PFFFT's SIMD real transform does not take.
#pragma once
#include "third_party/ooura/fftsg.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace convolver::detail {
class OouraTransform {
  public:
    // A power-of-two length of at least 4. The tables are built here, not on
    // the audio thread.
    explicit OouraTransform(std::size_t length)
        : n_(static_cast<int>(length)), work_(length), twiddles_(length / 2),
          bit_reversal_(2 + static_cast<std::size_t>(std::sqrt(double(length / 2))) + 1) {
        bit_reversal_[0] = 0;
        ooura::rdft(n_, 1, work_.data(), bit_reversal_.data(), twiddles_.data());
    }
    // Float elements owned: work, twiddles (n/2) and the bit-reversal ints.
    static std::size_t storage(std::size_t n) noexcept {
        return n + n / 2 + 2 + static_cast<std::size_t>(std::sqrt(double(n)));
    }
    // n time samples to canonical bins 0..n/2 (n + 2 floats).
    void forward(const float* time, float* frequency) noexcept {
        const auto n = static_cast<std::size_t>(n_);
        std::copy_n(time, n, work_.data());
        ooura::rdft(n_, 1, work_.data(), bit_reversal_.data(), twiddles_.data());
        // Ooura packs R[N/2] into slot one and uses sin(+), the conjugate of the DFT.
        frequency[0] = work_[0];
        frequency[1] = 0;
        for (std::size_t k = 1; k < n / 2; ++k) {
            frequency[2 * k] = work_[2 * k];
            frequency[2 * k + 1] = -work_[2 * k + 1];
        }
        frequency[n] = work_[1];
        frequency[n + 1] = 0;
    }
    // Canonical bins to n times the n time samples, transformed in place in
    // `time`; `spectrum` is left as it was.
    void inverse(const float* spectrum, float* time) noexcept {
        const auto n = static_cast<std::size_t>(n_);
        // Ooura's unscaled inverse returns N/2 times the signal; doubling the input
        // gives the N-times convention the other backends use.
        time[0] = 2 * spectrum[0];
        time[1] = 2 * spectrum[n];
        for (std::size_t k = 1; k < n / 2; ++k) {
            time[2 * k] = 2 * spectrum[2 * k];
            time[2 * k + 1] = -2 * spectrum[2 * k + 1];
        }
        ooura::rdft(n_, -1, time, bit_reversal_.data(), twiddles_.data());
    }

  private:
    int n_;
    std::vector<float> work_, twiddles_;
    std::vector<int> bit_reversal_;
};
} // namespace convolver::detail
