// SPDX-License-Identifier: GPL-2.0-or-later
// Declarations for Ooura's fftsg routines used by the portable core (see
// fftsg.cpp for the copyright and permission notice).
#pragma once

namespace convolver::detail::ooura {
// Real DFT of n (a power of two) points in place. isgn = 1: forward, with
// a[2k] = R[k] = sum a[j] cos(2 pi j k / n), a[2k+1] = I[k] = sum a[j] sin(...)
// for 0 < k < n/2, a[0] = R[0] and a[1] = R[n/2]. isgn = -1: the inverse of that
// layout without scaling, giving n/2 times the signal. ip needs 2 + sqrt(n/2)
// ints with ip[0] = 0 before first use; w needs n/2 floats.
void rdft(int n, int isgn, float* a, int* ip, float* w);
} // namespace convolver::detail::ooura
