// SPDX-License-Identifier: GPL-2.0-or-later
// Running out of memory while PFFFT prepares a transform must fail the
// preparation with std::bad_alloc, never crash the host (FFT-2, the local
// change in portable/third_party/pffft/README.md). This test links its own
// copy of PFFFT whose malloc calls come here (pffft_allocation/), fails the
// first, second, ... allocation in turn while a RealFft is made, and checks
// that each attempt throws until one has nothing left to fail.
#include "fft.hpp"
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
int calls = 0;
int fail_at = 0; // 0: never fail.
} // namespace

extern "C" void* convolver_test_malloc(std::size_t size) {
    if (++calls == fail_at)
        return nullptr;
    return std::malloc(size);
}

int main() {
    // 1024 points: PFFFT's own transform, not the Ooura one for short lengths.
    constexpr std::size_t n = 1024;
    int failed = 0;
    for (fail_at = 1;; ++fail_at) {
        calls = 0;
        try {
            convolver::detail::RealFft fft(n);
            if (calls >= fail_at) {
                std::cerr << "allocation " << fail_at << " failed but RealFft was made\n";
                return 1;
            }
            // Every allocation succeeded: the transform must work.
            fft.time()[1] = 1.f;
            fft.forward();
            if (fft.frequency()[0] != 1.f) {
                std::cerr << "the transform made after the failures is wrong\n";
                return 1;
            }
            break;
        } catch (const std::bad_alloc&) {
            ++failed;
        }
    }
    // Four aligned buffers, then PFFFT's setup and its twiddles.
    if (failed < 6) {
        std::cerr << "only " << failed << " allocations were failed; expected PFFFT's setup too\n";
        return 1;
    }
    std::cout << "each of " << failed << " allocation failures threw std::bad_alloc\n";
    return 0;
}
