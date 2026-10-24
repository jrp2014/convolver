// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>

namespace convolver::detail {
// The partition multiply-accumulate and spectrum layout loops, which take almost
// all of the processing time for long filters. Each set is the same C++ compiled
// for a different instruction set. Every set gives bit-identical results: the
// AVX2 set is compiled without fused multiply-add, and each frequency bin is
// computed on its own, so wider vectors do not change the order of operations.
struct Kernels {
    void (*deinterleave)(const float* from, float* to, std::size_t bins, float scale) noexcept;
    void (*interleave)(const float* re, const float* im, float* to, std::size_t bins) noexcept;
    // Products of split spectra x and h, assigned or added to re and im.
    void (*multiply)(float* re, float* im, const float* x, const float* h,
                     std::size_t bins) noexcept;
    void (*multiply_accumulate)(float* re, float* im, const float* x, const float* h,
                                std::size_t bins) noexcept;
    // A path's whole partition sum, so the loop over partitions is in the kernel too.
    void (*accumulate)(float* re, float* im, const float* history, std::size_t head,
                       std::size_t ring_size, const float* filters, std::size_t partitions,
                       std::size_t bins) noexcept;
    const char* name; // "AVX2" or "baseline"
};
// The fastest set this CPU and build support, chosen once. Engines take it when
// they are constructed, so the choice never happens on the audio thread.
const Kernels& kernels() noexcept;
const Kernels& baseline_kernels() noexcept;
// Tests only: make kernels() return the baseline set, so both sets can be checked
// against the oracle on a machine that supports AVX2.
void force_baseline_kernels(bool force) noexcept;
} // namespace convolver::detail
