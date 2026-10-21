// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include "fft.hpp"
#include <Accelerate/Accelerate.h>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <vector>

namespace convolver {
const char* fft_backend_name() noexcept {
    return "Accelerate";
}
const char* fft_backend_version() noexcept {
    return "vDSP (system framework)";
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
namespace {
// vDSP may pick a different code path, and so round differently, depending on
// how its buffers are aligned. Plain vectors land at varying alignments from one
// process to the next (macOS 26 randomises heap placement), so separate runs on
// the same input could differ in the last bit. A fixed alignment avoids that.
template <class T> struct Aligned {
    using value_type = T;
    static constexpr std::align_val_t alignment{64};
    Aligned() = default;
    template <class U> Aligned(const Aligned<U>&) noexcept {}
    T* allocate(std::size_t n) {
        return static_cast<T*>(::operator new(n * sizeof(T), alignment));
    }
    void deallocate(T* p, std::size_t) noexcept {
        ::operator delete(p, alignment);
    }
    template <class U> bool operator==(const Aligned<U>&) const noexcept {
        return true;
    }
};
} // namespace
// Use at least four real points; padding leaves audio block/latency unchanged.
std::size_t fft_size(std::size_t b) noexcept {
    return std::max(std::size_t(4), std::bit_ceil(2 * b));
}
std::size_t fft_storage(std::size_t n) noexcept {
    return 2 * n + 2 * (n + 2);
}
struct RealFft::Impl {
    std::size_t n, half;
    vDSP_Length log2;
    std::vector<float, Aligned<float>> time, frequency, sum, real, imag;
    FFTSetup setup = nullptr;
    explicit Impl(std::size_t length, FftOptions options)
        : n(length), half(n / 2), log2(std::bit_width(n) - 1), time(n), frequency(n + 2),
          sum(n + 2), real(half), imag(half) {
        if (options.planning != FftPlanning::estimate || options.wisdom_only ||
            options.time_limit_seconds != 0)
            throw std::invalid_argument("measured planning and wisdom require the FFTW backend");
        setup = vDSP_create_fftsetup(log2, kFFTRadix2);
        if (!setup)
            throw std::runtime_error("Accelerate FFT setup creation failed");
    }
    ~Impl() {
        if (setup)
            vDSP_destroy_fftsetup(setup);
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
    DSPSplitComplex z{s.real.data(), s.imag.data()};
    vDSP_ctoz(reinterpret_cast<const DSPComplex*>(s.time.data()), 2, &z, 1, s.half);
    vDSP_fft_zrip(s.setup, &z, 1, s.log2, FFT_FORWARD);
    // vDSP's real forward FFT returns 2*DFT; DC and Nyquist share slot zero.
    s.frequency[0] = .5f * s.real[0];
    s.frequency[1] = 0;
    for (std::size_t k = 1; k < s.half; ++k) {
        s.frequency[2 * k] = .5f * s.real[k];
        s.frequency[2 * k + 1] = .5f * s.imag[k];
    }
    s.frequency[s.n] = .5f * s.imag[0];
    s.frequency[s.n + 1] = 0;
}
void RealFft::inverse() noexcept {
    auto& s = *impl_;
    s.real[0] = s.sum[0];
    s.imag[0] = s.sum[s.n];
    for (std::size_t k = 1; k < s.half; ++k) {
        s.real[k] = s.sum[2 * k];
        s.imag[k] = s.sum[2 * k + 1];
    }
    DSPSplitComplex z{s.real.data(), s.imag.data()};
    vDSP_fft_zrip(s.setup, &z, 1, s.log2, FFT_INVERSE);
    // The inverse is N*IDFT, matching FFTW; Engine normalizes IRs by 1/N.
    vDSP_ztoc(&z, 1, reinterpret_cast<DSPComplex*>(s.time.data()), 2, s.half);
}
} // namespace detail
} // namespace convolver
