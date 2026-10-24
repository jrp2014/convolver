// SPDX-License-Identifier: GPL-2.0-or-later
// Opt-in experiment: keep the engine's split storage and partition sum order.
#include "kernels.hpp"
#include <Accelerate/Accelerate.h>
namespace convolver::detail::vdsp {
namespace {
DSPSplitComplex view(const float* data, std::size_t bins) noexcept {
    // vDSP input structs contain mutable pointers, but input arrays are read-only.
    return {const_cast<float*>(data), const_cast<float*>(data + bins)};
}
void multiply(float* re, float* im, const float* x, const float* h, std::size_t bins) noexcept {
    const auto a = view(x, bins), b = view(h, bins);
    const DSPSplitComplex out{re, im};
    vDSP_zvmul(&a, 1, &b, 1, &out, 1, bins, 1); // +1: no conjugation
}
void multiply_accumulate(float* re, float* im, const float* x, const float* h,
                         std::size_t bins) noexcept {
    const auto a = view(x, bins), b = view(h, bins);
    const DSPSplitComplex out{re, im};
    // Equal-sized vDSP vectors support in-place operation unless documented otherwise.
    vDSP_zvma(&a, 1, &b, 1, &out, 1, &out, 1, bins);
}
void accumulate(float* re, float* im, const float* history, std::size_t head, std::size_t ring_size,
                const float* filters, std::size_t partitions, std::size_t bins) noexcept {
    const std::size_t stride = 2 * bins;
    multiply(re, im, history + head * stride, filters, bins);
    std::size_t slot = head;
    for (std::size_t p = 1; p < partitions; ++p) {
        slot = slot == 0 ? ring_size - 1 : slot - 1;
        multiply_accumulate(re, im, history + slot * stride, filters + p * stride, bins);
    }
}
} // namespace
const Kernels& kernel_set() noexcept {
    static const Kernels set{baseline_kernels().deinterleave,
                             baseline_kernels().interleave,
                             multiply,
                             multiply_accumulate,
                             accumulate,
                             "vDSP"};
    return set;
}
} // namespace convolver::detail::vdsp
