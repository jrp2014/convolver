// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// PERF-47: the figures HostFilter finds for each accepted filter, kept for the
// whole process, so a filter accepted again finds them at once: Audacity's
// fresh plug-in pair at every Play, a second instance of the same filter, a
// project reopened. Above all the noise estimate behind Gain mode Estimated,
// which filters ten filter lengths of noise (seconds for a long reverb), but
// also the peak and noise gain, the tail and whether it plays silence. The
// figures depend on the filter alone, so a cached figure is the one finding it
// again would give, bit for bit. Control side only: the audio thread never
// reaches it. Private to Convolver::host.
#include "convolver/host.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace convolver::detail {
// Which filter a set of figures belongs to: configuration_identity() and a
// second hash of the same values, keyed by a number chosen at random once per
// process, so no file can be made to collide with another's figures on
// purpose (the identity alone is FNV-1a, which can be).
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

// Tests only: lookups that found figures, and that did not, since the last
// clear; clear_figure_cache() empties the cache and zeroes both.
struct FigureCacheCounts {
    std::uint64_t found = 0, missed = 0;
};
FigureCacheCounts figure_cache_counts() noexcept;
void clear_figure_cache() noexcept;
} // namespace convolver::detail
