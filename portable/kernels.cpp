// SPDX-License-Identifier: GPL-2.0-or-later
#include "kernels.hpp"
#include <atomic>
#define CONVOLVER_KERNEL_NAMESPACE baseline
#define CONVOLVER_KERNEL_NAME "baseline"
#include "kernels.inc"
#undef CONVOLVER_KERNEL_NAMESPACE
#undef CONVOLVER_KERNEL_NAME
#if defined(CONVOLVER_AVX2_KERNELS) && (defined(__x86_64__) || defined(_M_X64)) &&                 \
    !defined(_M_ARM64EC)
#define CONVOLVER_USE_AVX2_KERNELS 1
#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#endif
#endif

namespace convolver::detail {
namespace {
std::atomic<bool> baseline_forced{false};
}
#if defined(CONVOLVER_USE_AVX2_KERNELS)
namespace avx2 {
const Kernels& kernel_set() noexcept; // kernels_avx2.cpp
} // namespace avx2
namespace {
// The AVX2 set needs AVX2 and FMA from the CPU, and an operating system that
// saves the 256-bit registers. Rosetta 2 before macOS 15, for example, reports
// no AVX2. Every CPU sold with AVX2 has FMA, but a virtual machine may hide it.
bool cpu_has_avx2_fma() noexcept {
#if defined(_MSC_VER)
    int info[4];
    __cpuid(info, 0);
    if (info[0] < 7)
        return false;
    __cpuid(info, 1);
    const bool fma = (info[2] & (1 << 12)) != 0, osxsave = (info[2] & (1 << 27)) != 0,
               avx = (info[2] & (1 << 28)) != 0;
    if (!fma || !osxsave || !avx || (_xgetbv(0) & 6) != 6)
        return false;
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
#else
    // The compiler runtime's check includes the operating system's support.
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#endif
}
} // namespace
#endif
const Kernels& baseline_kernels() noexcept {
    return baseline::kernel_set();
}
const Kernels& kernels() noexcept {
    if (baseline_forced.load(std::memory_order_relaxed))
        return baseline_kernels();
#if defined(CONVOLVER_USE_AVX2_KERNELS)
    static const Kernels& chosen = cpu_has_avx2_fma() ? avx2::kernel_set() : baseline_kernels();
    return chosen;
#else
    return baseline_kernels();
#endif
}
void force_baseline_kernels(bool force) noexcept {
    baseline_forced.store(force, std::memory_order_relaxed);
}
} // namespace convolver::detail
