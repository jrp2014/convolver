// SPDX-License-Identifier: GPL-2.0-or-later
// Dependency-free FFT backend using Ooura's split-radix real DFT.
#include "convolver/engine.hpp"
#include "fft.hpp"
#include "third_party/ooura/fftsg.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace convolver {
const char* fft_backend_name() noexcept {
    return "Ooura";
}
const char* fft_backend_version() noexcept {
    return "fftsg (built in)";
}
bool fft_supports_wisdom() noexcept {
    return false;
}
void import_fft_wisdom(std::string_view) {
    throw std::invalid_argument("wisdom requires the FFTW backend");
}
std::string export_fft_wisdom() {
    throw std::invalid_argument("wisdom requires the FFTW backend");
}
namespace detail {
// Ooura transforms powers of two; padding leaves audio block/latency unchanged.
std::size_t fft_size(std::size_t b) noexcept {
    return std::max(std::size_t(4), std::bit_ceil(2 * b));
}
std::size_t fft_storage(std::size_t n) noexcept {
    // time, frequency, sum, work, twiddles (n/2) and bit-reversal ints.
    return 2 * n + 2 * (n + 2) + n / 2 + 2 + static_cast<std::size_t>(std::sqrt(double(n)));
}
struct RealFft::Impl {
    int n;
    std::vector<float> time, frequency, sum, work, twiddles;
    std::vector<int> bit_reversal;
    explicit Impl(std::size_t length, FftOptions options)
        : n(static_cast<int>(length)), time(length), frequency(length + 2), sum(length + 2),
          work(length), twiddles(length / 2),
          bit_reversal(2 + static_cast<std::size_t>(std::sqrt(double(length / 2))) + 1) {
        if (options.planning != FftPlanning::estimate || options.wisdom_only ||
            options.time_limit_seconds != 0)
            throw std::invalid_argument("measured planning and wisdom require the FFTW backend");
        // Build the twiddle and bit-reversal tables now, not on the audio thread.
        bit_reversal[0] = 0;
        ooura::rdft(n, 1, work.data(), bit_reversal.data(), twiddles.data());
    }
};
RealFft::RealFft(std::size_t n, FftOptions options) : impl_(std::make_unique<Impl>(n, options)) {}
RealFft::~RealFft() = default;
float* RealFft::time() noexcept {
    return impl_->time.data();
}
float* RealFft::frequency() noexcept {
    return impl_->frequency.data();
}
float* RealFft::sum() noexcept {
    return impl_->sum.data();
}
void RealFft::forward() noexcept {
    auto& s = *impl_;
    const auto n = static_cast<std::size_t>(s.n);
    std::copy(s.time.begin(), s.time.end(), s.work.begin());
    ooura::rdft(s.n, 1, s.work.data(), s.bit_reversal.data(), s.twiddles.data());
    // Ooura packs R[N/2] into slot one and uses sin(+), the conjugate of the DFT.
    s.frequency[0] = s.work[0];
    s.frequency[1] = 0;
    for (std::size_t k = 1; k < n / 2; ++k) {
        s.frequency[2 * k] = s.work[2 * k];
        s.frequency[2 * k + 1] = -s.work[2 * k + 1];
    }
    s.frequency[n] = s.work[1];
    s.frequency[n + 1] = 0;
}
void RealFft::inverse() noexcept {
    auto& s = *impl_;
    const auto n = static_cast<std::size_t>(s.n);
    // Ooura's unscaled inverse returns N/2 times the signal; doubling the input
    // gives the N-times convention the other backends use.
    s.time[0] = 2 * s.sum[0];
    s.time[1] = 2 * s.sum[n];
    for (std::size_t k = 1; k < n / 2; ++k) {
        s.time[2 * k] = 2 * s.sum[2 * k];
        s.time[2 * k + 1] = -2 * s.sum[2 * k + 1];
    }
    ooura::rdft(s.n, -1, s.time.data(), s.bit_reversal.data(), s.twiddles.data());
}
} // namespace detail
} // namespace convolver
