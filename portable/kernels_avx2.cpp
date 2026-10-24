// SPDX-License-Identifier: GPL-2.0-or-later
// The kernels of kernels.inc compiled for AVX2, used only on CPUs that report
// it (see kernels.cpp). CMakeLists.txt compiles this file with AVX2 enabled and
// floating-point contraction off, so results match the baseline set bit for bit.
// It includes nothing with inline functions (kernels.hpp only declares); see
// kernels.inc.
#include "kernels.hpp"
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(_M_ARM64EC)
#define CONVOLVER_KERNEL_NAMESPACE avx2
#define CONVOLVER_KERNEL_NAME "AVX2"
#include "kernels.inc"
#endif
