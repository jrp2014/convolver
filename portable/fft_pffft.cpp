// SPDX-License-Identifier: GPL-2.0-or-later
// FFT backend using PFFFT's ordered real transforms (FFT-2): SSE on x86,
// NEON on 64-bit ARM, portable scalar code elsewhere. PFFFT's SIMD real
// transform takes lengths of at least 32 points; shorter ones, which only
// the tests' tiny partition layouts ask for, use the built-in Ooura transform.
#include "fft.hpp"
#include "convolver/engine.hpp"
#include "fft_ooura_transform.hpp"
#include "third_party/pffft/include/pffft/pffft.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>

namespace convolver {
const char* fft_backend_name() noexcept {
    return "PFFFT";
}
const char* fft_backend_version() noexcept {
    // The vendored release (portable/third_party/pffft/README.md) and the
    // instructions it was compiled for: SSE1, NEON or scalar.
    static const auto text = [] {
        std::array<char, 64> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "1.3.1, %s", pffft_simd_arch());
        return buffer;
    }();
    return text.data();
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
// PFFFT's SIMD loads need aligned buffers; its allocator aligns to 64 bytes.
struct AlignedFree {
    void operator()(float* p) const noexcept {
        pffft_aligned_free(p);
    }
};
using Aligned = std::unique_ptr<float[], AlignedFree>;
Aligned aligned(std::size_t n) {
    Aligned p(static_cast<float*>(pffft_aligned_malloc(n * sizeof(float))));
    if (!p)
        throw std::bad_alloc();
    std::fill_n(p.get(), n, 0.f);
    return p;
}
struct SetupFree {
    void operator()(PFFFT_Setup* p) const noexcept {
        pffft_destroy_setup(p);
    }
};
bool pffft_takes(std::size_t n) noexcept {
    return n >= static_cast<std::size_t>(pffft_min_fft_size(PFFFT_REAL)) &&
           pffft_is_valid_size(static_cast<int>(n), PFFFT_REAL) != 0;
}
} // namespace
void check_fft_options(const FftOptions& options) {
    if (options.planning != FftPlanning::estimate || options.wisdom_only ||
        options.time_limit_seconds != 0)
        throw std::invalid_argument("measured planning and wisdom require the FFTW backend");
}
std::size_t fft_storage(std::size_t n) noexcept {
    // time, frequency, sum and work, then PFFFT's setup (n floats of
    // twiddles and a small header) or, for short transforms, Ooura's buffers.
    return n + 3 * (n + 2) + (pffft_takes(n) ? n + 64 : OouraTransform::storage(n));
}
struct RealFft::Impl {
    std::size_t n;
    Aligned time, frequency, sum, work;
    std::unique_ptr<PFFFT_Setup, SetupFree> setup;
    std::optional<OouraTransform> ooura; // Lengths PFFFT does not take.
    explicit Impl(std::size_t length, FftOptions options)
        : n(length), time(aligned(length)), frequency(aligned(length + 2)),
          sum(aligned(length + 2)), work(aligned(length + 2)) {
        check_fft_options(options);
        if (pffft_takes(length)) {
            setup.reset(pffft_new_setup(static_cast<int>(length), PFFFT_REAL));
            if (!setup)
                throw std::bad_alloc();
        } else {
            ooura.emplace(length);
        }
    }
};
RealFft::RealFft(std::size_t n, FftOptions options) : impl_(std::make_unique<Impl>(n, options)) {}
RealFft::~RealFft() = default;
float* RealFft::time() noexcept {
    return impl_->time.get();
}
float* RealFft::frequency() noexcept {
    return impl_->frequency.get();
}
float* RealFft::sum() noexcept {
    return impl_->sum.get();
}
void RealFft::forward() noexcept {
    auto& s = *impl_;
    float* f = s.frequency.get();
    if (!s.setup) {
        s.ooura->forward(s.time.get(), f);
        return;
    }
    // PFFFT's ordered real spectrum: DC, Nyquist, then the real and
    // imaginary parts of bins 1 to n/2 - 1, unscaled, with the DFT's sign.
    pffft_transform_ordered(s.setup.get(), s.time.get(), f, s.work.get(), PFFFT_FORWARD);
    const float nyquist = f[1];
    f[1] = 0;
    f[s.n] = nyquist;
    f[s.n + 1] = 0;
}
void RealFft::inverse() noexcept {
    auto& s = *impl_;
    float* in = s.sum.get();
    if (!s.setup) {
        s.ooura->inverse(in, s.time.get());
        return;
    }
    // The ordered layout again: Nyquist's real part goes in slot 1, DC's
    // imaginary part, which is zero. sum() is the caller's scratch,
    // rewritten before every inverse. PFFFT's inverse returns n times the
    // signal, as RealFft's does.
    in[1] = in[s.n];
    pffft_transform_ordered(s.setup.get(), in, s.time.get(), s.work.get(), PFFFT_BACKWARD);
}
} // namespace detail
} // namespace convolver
