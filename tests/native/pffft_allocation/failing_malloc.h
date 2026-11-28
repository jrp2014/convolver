/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compiles PFFFT's sources with their malloc calls sent to
 * convolver_test_malloc (pffft_allocation_tests.cpp), which can fail on
 * demand. The system headers that declare malloc come first, so only
 * PFFFT's own calls are renamed. */
#ifndef CONVOLVER_PFFFT_FAILING_MALLOC_H
#define CONVOLVER_PFFFT_FAILING_MALLOC_H
#include <stddef.h>
#include <stdlib.h>
#if defined(_MSC_VER)
#include <malloc.h>
#endif
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif
void* convolver_test_malloc(size_t size);
#define malloc convolver_test_malloc
#endif
