// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// PERF-47: the figures HostFilter finds for each accepted filter, kept for the
// whole process, so a filter accepted again finds them at once: Audacity's
// fresh plug-in pair at every Play, a second instance of the same filter, a
// project reopened. Above all the noise estimate behind Gain mode Estimated,
// which filters ten filter lengths of noise (seconds for a long reverb), but
// also the peak and noise gain, the tail and whether it plays silence. The
// figures depend on the filter alone, so a cached figure is the one finding it
// again gives, bit for bit, in every usual case. Two rare ones could differ
// by float rounding, as finding them twice could before: an estimate made
// with the real-time stages when memory is short (OfflineEngine), and a
// thread whose flush-to-zero mode differs for subnormal sums. Control side
// only: the audio thread never reaches it. Private to Convolver::host.
#include "convolver/host.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace convolver::detail {
// Which filter a set of figures belongs to: configuration_identity() and a
// second hash of the same values, keyed by a number chosen at random once per
// process. The identity alone is FNV-1a, whose collisions can be made on
// purpose; the keyed hash makes a file made to take another's figures
// impractical, as its key cannot be known ahead of time. It is not a
// cryptographic hash. Unlike HostFilter's same-filter test (PLUG-38), which
// compares every value, the cache keeps no copy of the filter to compare.
struct FilterKey {
    std::uint64_t identity = 0, keyed = 0;
    bool operator==(const FilterKey&) const = default;
};
// Both hashes in one pass over the filter; `identity` equals
// configuration_identity(c).
FilterKey filter_key(const Configuration& c) noexcept;

struct FilterFigures {
    double peak = 0, noise = 0; // worst_case_peak and the noise gain (kernel_sums)
    std::size_t tail = 0;       // configuration_tail_frames
    bool silent = false;        // plays_silence
    // filter_gains()'s figures; the estimate's (estimate_db, or
    // estimate_failed) only once `estimated`.
    FilterGains gains;
    bool estimated = false;
};
// The figures cached for `key`, if any. Does not throw.
std::optional<FilterFigures> cached_figures(const FilterKey& key) noexcept;
// Keeps `figures` for `key`, merging an estimate into figures cached without
// one. The cache holds the last few filters (figure_cache_size); a new one
// replaces the one used longest ago. Allocates nothing and does not throw.
void cache_figures(const FilterKey& key, const FilterFigures& figures) noexcept;
inline constexpr std::size_t figure_cache_size = 8;

// PLUG-74: finds the estimate a HostEstimator searches for and caches it
// with the figures `search` carries; `stop` set ends it early with
// EstimateStopped. Throws what finding it throws (std::bad_alloc); an
// estimate that overflows is cached as failed, as HostFilter keeps it.
void search_estimate(const HostEstimateSearch& search, const std::atomic<bool>* stop);
// Counts an estimate actually found (filtering noise), for the tests.
void count_estimate() noexcept;
// Tests only: lookups that found figures and that did not, and estimates
// found, since the last clear; clear_figure_cache() empties the cache and
// zeroes all three.
struct FigureCacheCounts {
    std::uint64_t found = 0, missed = 0, estimates = 0;
};
FigureCacheCounts figure_cache_counts() noexcept;
void clear_figure_cache() noexcept;
} // namespace convolver::detail
