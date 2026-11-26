// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/engine.hpp"
#include "partitions.hpp"
#include <cstddef>

namespace convolver::detail {
// PERF-23: an Engine for work that never plays in real time, such as the noise
// estimate (estimate_attenuation()). Its stages grow to eight times the
// largest block processing in real time uses (65,536 frames with the default
// layout instead of 8,192), so a long filter is summed in far fewer
// partitions, and the whole filter spectrum is read far less often: the
// estimate of a 30 s filter took a third of the time (measured 26 November
// 2026, docs/develop/fft-measurements.md#offline-blocks). Larger blocks need
// a large latency to pay off, which costs nothing offline. The output differs
// from Engine's only by float rounding.
struct OfflineEngine {
    static constexpr std::size_t growth = 8;
    static Engine make(const Configuration& c, std::size_t block_frames) {
        return Engine(c, block_frames, {}, growth * partition_layout().largest);
    }
};
} // namespace convolver::detail
