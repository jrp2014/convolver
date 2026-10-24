// SPDX-License-Identifier: GPL-2.0-or-later
// The kernels of kernels.inc compiled for AVX2 with fused multiply-add, used
// only on CPUs that report both (see kernels.cpp). CMakeLists.txt compiles this
// file with AVX2 and FMA enabled and implicit contraction off; the multiplies use
// explicit FMA intrinsics, so the output differs from the baseline set only by
// float rounding (owner's decision, 24 October 2026; native.engine bounds it).
// It includes nothing with inline functions that could be emitted out of line
// (kernels.hpp only declares; the intrinsics are compiler built-ins); see
// kernels.inc.
#include "kernels.hpp"
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(_M_ARM64EC)
#define CONVOLVER_KERNEL_NAMESPACE avx2
#define CONVOLVER_KERNEL_NAME "AVX2+FMA"
#define CONVOLVER_KERNEL_FMA 1
#include "kernels.inc"
#endif
