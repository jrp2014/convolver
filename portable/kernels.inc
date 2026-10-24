// SPDX-License-Identifier: GPL-2.0-or-later
// Kernel bodies, compiled once per instruction set (see kernels.hpp). This file
// must not use inline functions from headers: an out-of-line copy compiled for
// a newer instruction set could be chosen by the linker for every caller.
// Include kernels.hpp and define CONVOLVER_KERNEL_NAMESPACE and
// CONVOLVER_KERNEL_NAME (a string literal) before including it.
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
