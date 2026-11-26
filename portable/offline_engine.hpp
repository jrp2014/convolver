// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/engine.hpp"
#include "partitions.hpp"
#include <cstddef>
#include <new>
#include <stdexcept>

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
//
// Larger stages need more storage: longer input and output rings and, for a
// filter that ends part-way through a block, up to one more large partition
// per path. A filter near the storage limit (or the machine's memory) that
// the real-time stages fit is then built with those stages instead, slower
// but the same figure up to float rounding, so whatever can play can also be estimated (found by
// the independent review).
struct OfflineEngine {
    static constexpr std::size_t growth = 8;
    static Engine make(const Configuration& c, std::size_t block_frames) {
        try {
            return Engine(c, block_frames, {}, growth * partition_layout().largest);
        } catch (const std::length_error&) {
        } catch (const std::bad_alloc&) {
        }
        return Engine(c, block_frames);
    }
};
// Tests only: the storage limit, in floats, that every Engine checks, lowered
// so that the fallback above can be tested without a filter of 1 GiB; 0
// restores the limit. Not thread-safe: call before constructing engines.
void force_storage_limit(std::size_t floats) noexcept;
} // namespace convolver::detail
