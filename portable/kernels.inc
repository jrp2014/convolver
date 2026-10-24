// SPDX-License-Identifier: GPL-2.0-or-later
// Kernel bodies, compiled once per instruction set (see kernels.hpp). This file
// must not use inline functions from headers: an out-of-line copy compiled for
// a newer instruction set could be chosen by the linker for every caller.
// Include kernels.hpp and define CONVOLVER_KERNEL_NAMESPACE and
// CONVOLVER_KERNEL_NAME (a string literal) before including it. Define
// CONVOLVER_KERNEL_FMA as well for the AVX2 set, which then multiplies with
// explicit fused multiply-add intrinsics (immintrin.h's are compiler built-ins).
// Nothing is contracted implicitly, whatever instruction set or flags the file is
// compiled with: the baseline set rounds every product, and the FMA set fuses
// exactly the operations written below, so each set's results do not depend on
// the compiler. GCC gets -ffp-contract=off from CMakeLists.txt.
#if defined(CONVOLVER_AVX2_KERNELS) && (defined(__x86_64__) || defined(_M_X64)) && !defined(_M_ARM64EC)
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif
#endif
#if defined(CONVOLVER_KERNEL_FMA)
#include <immintrin.h>
#endif
namespace convolver::detail::CONVOLVER_KERNEL_NAMESPACE {
#if defined(_MSC_VER)
#define CONVOLVER_RESTRICT __restrict
#else
#define CONVOLVER_RESTRICT __restrict__
#endif
// Prepared spectra are stored split per partition: bins real parts, then bins
// imaginary parts. Split storage lets the multiply-accumulate vectorize with plain
// loads on SSE/AVX and NEON instead of shuffling interleaved complex pairs.
void deinterleave(const float* CONVOLVER_RESTRICT from, float* CONVOLVER_RESTRICT to,
                  std::size_t bins, float scale) noexcept {
    for (std::size_t j = 0; j < bins; ++j) {
        to[j] = from[2 * j] * scale;
        to[bins + j] = from[2 * j + 1] * scale;
    }
}
void interleave(const float* CONVOLVER_RESTRICT re, const float* CONVOLVER_RESTRICT im,
                float* CONVOLVER_RESTRICT to, std::size_t bins) noexcept {
    for (std::size_t j = 0; j < bins; ++j) {
        to[2 * j] = re[j];
        to[2 * j + 1] = im[j];
    }
}
#if defined(CONVOLVER_KERNEL_FMA)
// Each bin is computed the same way in the vector loop and in the scalar tail,
// so a bin's value does not depend on its position. Per bin, with one rounding
// per fused operation:
//   multiply:            re = fma(xr, hr, -(xi * hi)), im = fma(xr, hi, xi * hr)
//   multiply_accumulate: re += fma(xr, hr, -(xi * hi)), im += fma(xr, hi, xi * hr)
// The accumulation is a separate add, as in the baseline set, so the running
// sum is rounded once per partition. Chaining two fused operations through the
// sum (re = fma(-xi, hi, fma(xr, hr, re))) rounds it twice, and was measurably
// further from the double-precision oracle on long filters (about 1.3 times the
// baseline's RMS error); this form is at least as close as the baseline.
void multiply(float* CONVOLVER_RESTRICT re, float* CONVOLVER_RESTRICT im,
              const float* CONVOLVER_RESTRICT x, const float* CONVOLVER_RESTRICT h,
              std::size_t bins) noexcept {
    const float *xr = x, *xi = x + bins, *hr = h, *hi = h + bins;
    std::size_t j = 0;
    for (; j + 8 <= bins; j += 8) {
        const __m256 a = _mm256_loadu_ps(xr + j), b = _mm256_loadu_ps(xi + j);
        const __m256 c = _mm256_loadu_ps(hr + j), d = _mm256_loadu_ps(hi + j);
        _mm256_storeu_ps(re + j, _mm256_fmsub_ps(a, c, _mm256_mul_ps(b, d)));
        _mm256_storeu_ps(im + j, _mm256_fmadd_ps(a, d, _mm256_mul_ps(b, c)));
    }
    for (; j < bins; ++j) {
        const __m128 a = _mm_load_ss(xr + j), b = _mm_load_ss(xi + j);
        const __m128 c = _mm_load_ss(hr + j), d = _mm_load_ss(hi + j);
        _mm_store_ss(re + j, _mm_fmsub_ss(a, c, _mm_mul_ss(b, d)));
        _mm_store_ss(im + j, _mm_fmadd_ss(a, d, _mm_mul_ss(b, c)));
    }
}
void multiply_accumulate(float* CONVOLVER_RESTRICT re, float* CONVOLVER_RESTRICT im,
                         const float* CONVOLVER_RESTRICT x, const float* CONVOLVER_RESTRICT h,
                         std::size_t bins) noexcept {
    const float *xr = x, *xi = x + bins, *hr = h, *hi = h + bins;
    std::size_t j = 0;
    for (; j + 8 <= bins; j += 8) {
        const __m256 a = _mm256_loadu_ps(xr + j), b = _mm256_loadu_ps(xi + j);
        const __m256 c = _mm256_loadu_ps(hr + j), d = _mm256_loadu_ps(hi + j);
        const __m256 r = _mm256_loadu_ps(re + j), i = _mm256_loadu_ps(im + j);
        _mm256_storeu_ps(re + j, _mm256_add_ps(r, _mm256_fmsub_ps(a, c, _mm256_mul_ps(b, d))));
        _mm256_storeu_ps(im + j, _mm256_add_ps(i, _mm256_fmadd_ps(a, d, _mm256_mul_ps(b, c))));
    }
    for (; j < bins; ++j) {
        const __m128 a = _mm_load_ss(xr + j), b = _mm_load_ss(xi + j);
        const __m128 c = _mm_load_ss(hr + j), d = _mm_load_ss(hi + j);
        const __m128 r = _mm_load_ss(re + j), i = _mm_load_ss(im + j);
        _mm_store_ss(re + j, _mm_add_ss(r, _mm_fmsub_ss(a, c, _mm_mul_ss(b, d))));
        _mm_store_ss(im + j, _mm_add_ss(i, _mm_fmadd_ss(a, d, _mm_mul_ss(b, c))));
    }
}
#else
void multiply(float* CONVOLVER_RESTRICT re, float* CONVOLVER_RESTRICT im,
              const float* CONVOLVER_RESTRICT x, const float* CONVOLVER_RESTRICT h,
              std::size_t bins) noexcept {
    const float *xr = x, *xi = x + bins, *hr = h, *hi = h + bins;
    for (std::size_t j = 0; j < bins; ++j) {
        re[j] = xr[j] * hr[j] - xi[j] * hi[j];
        im[j] = xr[j] * hi[j] + xi[j] * hr[j];
    }
}
void multiply_accumulate(float* CONVOLVER_RESTRICT re, float* CONVOLVER_RESTRICT im,
                         const float* CONVOLVER_RESTRICT x, const float* CONVOLVER_RESTRICT h,
                         std::size_t bins) noexcept {
    const float *xr = x, *xi = x + bins, *hr = h, *hi = h + bins;
    for (std::size_t j = 0; j < bins; ++j) {
        re[j] += xr[j] * hr[j] - xi[j] * hi[j];
        im[j] += xr[j] * hi[j] + xi[j] * hr[j];
    }
}
#endif
// One path's spectrum for this block: the sum over partitions p of input
// spectrum (head - p) times filter partition p, in increasing p. history is a
// ring of ring_size spectra; each spectrum and filter partition is 2 * bins floats.
void accumulate(float* CONVOLVER_RESTRICT re, float* CONVOLVER_RESTRICT im,
                const float* CONVOLVER_RESTRICT history, std::size_t head, std::size_t ring_size,
                const float* CONVOLVER_RESTRICT filters, std::size_t partitions,
                std::size_t bins) noexcept {
    const std::size_t stride = 2 * bins;
    multiply(re, im, history + head * stride, filters, bins);
    std::size_t slot = head;
    for (std::size_t p = 1; p < partitions; ++p) {
        slot = slot == 0 ? ring_size - 1 : slot - 1;
        multiply_accumulate(re, im, history + slot * stride, filters + p * stride, bins);
    }
}
#undef CONVOLVER_RESTRICT
// The set, for kernels.cpp. Declaring the functions there instead would not link
// with MSVC, which puts __restrict into their mangled names.
const Kernels& kernel_set() noexcept {
    static constexpr Kernels set{deinterleave, interleave, multiply, multiply_accumulate, accumulate,
                                 CONVOLVER_KERNEL_NAME};
    return set;
}
} // namespace convolver::detail::CONVOLVER_KERNEL_NAMESPACE
