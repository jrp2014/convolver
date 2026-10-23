// SPDX-License-Identifier: GPL-2.0-or-later
// Port numbers and status values of the LV2 plug-ins, shared by the adapter
// and its tests. generate_ttl.py writes the same numbers into convolver.ttl;
// lv2.adapter checks that the two agree.
#pragma once
#include <cstdint>

namespace convolver::lv2 {
// Every layout has the three control ports first, then its audio inputs
// (symbols in_1 ... in_N), then its audio outputs (out_1 ... out_M).
inline constexpr std::uint32_t enabled_port = 0; // lv2:enabled: 1 processes, 0 bypasses.
inline constexpr std::uint32_t latency_port = 1; // Output: the reported latency in frames.
inline constexpr std::uint32_t status_port = 2;  // Output: one of the Status values.
inline constexpr std::uint32_t first_audio_port = 3;

// What the status port shows. Loading is reserved for worker loads (LV2-2).
enum class Status : int { running = 0, no_filter = 1, load_failed = 2, loading = 3 };

// Callback capacity when the host gives no bufsz:maxBlockLength; longer
// run() calls are processed in pieces of at most this many frames.
inline constexpr std::uint32_t default_max_block = 4096;
inline constexpr std::uint32_t largest_max_block = 65536;
} // namespace convolver::lv2
