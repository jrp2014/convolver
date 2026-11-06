// SPDX-License-Identifier: GPL-2.0-or-later
// PERF-15: subnormal (denormal) floating-point numbers are flushed to zero
// while Convolver processes audio and while it prepares engines. Arithmetic on numbers below the
// smallest normal float (about 1.2e-38) takes many times longer on most processors, and a long
// filter or a tail fading to silence produces them; on the Linux cloud VM the longest callbacks
// with a 30 s filter took 37 to 43 ms with them and 8 to 12 ms without, and preparing the filter
// took about three times as long. Most hosts set flush-to-zero on their audio threads, but not all,
// and the CLI's thread never has it.
//
// ScopedFlushDenormals sets the current thread's flush mode for its lifetime
// and then restores the caller's setting, on every way out of the scope
// (return or exception), as JUCE's ScopedNoDenormals does (jrp's decision, 5
// November 2026). Standard C++ has no portable switch, so each architecture
// has its own control:
//   - x86-64: MXCSR flush-to-zero (FTZ, bit 15: results below the normal range
//     become zero) and denormals-are-zero (DAZ, bit 6: subnormal inputs are
//     read as zero), for SSE and AVX arithmetic.
//   - AArch64 (Apple Silicon, Arm Linux): FPCR.FZ (bit 24), which flushes
//     subnormal inputs and results of single and double precision.
// Elsewhere the scope changes nothing and processing keeps IEEE gradual
// underflow, as before. Only the flush bits are touched: the rounding mode,
// exception masks and status flags stay as the caller has them. A scope
// entered when flushing is already on writes nothing, so nesting costs one
// register read.
#pragma once
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#define CONVOLVER_FLUSH_DENORMALS_X86 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define CONVOLVER_FLUSH_DENORMALS_AARCH64 1
#endif

namespace convolver::detail {
#if defined(CONVOLVER_FLUSH_DENORMALS_X86)
inline constexpr bool flush_denormals_supported = true;
inline constexpr std::uint64_t flush_denormal_bits = 0x8040; // FTZ | DAZ
inline std::uint64_t floating_point_mode() noexcept {
    return _mm_getcsr();
}
inline void set_floating_point_mode(std::uint64_t mode) noexcept {
    _mm_setcsr(static_cast<unsigned int>(mode));
}
#elif defined(CONVOLVER_FLUSH_DENORMALS_AARCH64)
inline constexpr bool flush_denormals_supported = true;
inline constexpr std::uint64_t flush_denormal_bits = std::uint64_t(1) << 24; // FZ
inline std::uint64_t floating_point_mode() noexcept {
    std::uint64_t mode;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(mode) : : "memory");
    return mode;
}
inline void set_floating_point_mode(std::uint64_t mode) noexcept {
    __asm__ __volatile__("msr fpcr, %0" : : "r"(mode) : "memory");
}
#else
inline constexpr bool flush_denormals_supported = false;
inline constexpr std::uint64_t flush_denormal_bits = 0;
inline std::uint64_t floating_point_mode() noexcept {
    return 0;
}
inline void set_floating_point_mode(std::uint64_t) noexcept {}
#endif

// Whether the current thread flushes subnormals (false where unsupported).
inline bool denormals_flushed() noexcept {
    return flush_denormals_supported &&
           (floating_point_mode() & flush_denormal_bits) == flush_denormal_bits;
}
// Sets or clears only the flush bits; tests and benchmarks use it to stand in
// for a host's audio thread.
inline void set_denormals_flushed(bool flushed) noexcept {
    const auto mode = floating_point_mode();
    set_floating_point_mode(flushed ? mode | flush_denormal_bits : mode & ~flush_denormal_bits);
}

class ScopedFlushDenormals {
  public:
    ScopedFlushDenormals() noexcept : saved_(floating_point_mode() & flush_denormal_bits) {
        if (saved_ != flush_denormal_bits)
            set_floating_point_mode(floating_point_mode() | flush_denormal_bits);
    }
    ~ScopedFlushDenormals() {
        if (saved_ != flush_denormal_bits)
            set_floating_point_mode((floating_point_mode() & ~flush_denormal_bits) | saved_);
    }
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

  private:
    std::uint64_t saved_; // The caller's flush bits.
};
} // namespace convolver::detail
