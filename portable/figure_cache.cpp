// SPDX-License-Identifier: GPL-2.0-or-later
#include "figure_cache.hpp"
#include <array>
#include <bit>
#include <chrono>
#include <mutex>
#include <random>

namespace convolver::detail {
namespace {
// A number chosen once per process for the keyed hash, so its collisions
// cannot be found ahead of time. std::random_device may throw (no entropy
// source); the clock and an address then stand in, which still differ from
// run to run.
std::uint64_t process_key() noexcept {
    static const std::uint64_t key = [] {
        std::uint64_t k = 0;
        try {
            std::random_device device;
            k = (static_cast<std::uint64_t>(device()) << 32) ^ device();
        } catch (...) {
        }
        static const int anchor = 0;
        k ^= static_cast<std::uint64_t>(
                 std::chrono::steady_clock::now().time_since_epoch().count()) ^
             std::bit_cast<std::uintptr_t>(&anchor);
        return k | 1; // Never zero.
    }();
    return key;
}
struct Entry {
    FilterKey key;
    FilterFigures figures;
    std::uint64_t used = 0; // When last found or stored; 0 for an empty slot.
};
struct Cache {
    std::mutex mutex;
    std::array<Entry, figure_cache_size> entries{};
    std::uint64_t clock = 0;
    FigureCacheCounts counts;
};
Cache& cache() noexcept {
    static Cache c;
    return c;
}
} // namespace

FilterKey filter_key(const Configuration& c) noexcept {
    // FNV-1a over 32-bit words (configuration_identity()), and beside it a
    // multiply-rotate hash seeded with the process key; one pass, the two
    // chains independent, so the second costs little beside the first.
    std::uint64_t fnv = 14695981039346656037ULL;
    const std::uint64_t seed = process_key();
    std::uint64_t keyed = seed;
    const auto add = [&](std::uint32_t word) {
        fnv ^= word;
        fnv *= 1099511628211ULL;
        keyed = std::rotl(keyed ^ (word + seed), 23) * 0x9E3779B97F4A7C15ULL;
    };
    const auto add64 = [&](std::uint64_t value) {
        add(static_cast<std::uint32_t>(value));
        add(static_cast<std::uint32_t>(value >> 32));
    };
    const auto add_float = [&](float value) { add(std::bit_cast<std::uint32_t>(value)); };
    add64(static_cast<std::uint64_t>(static_cast<std::int64_t>(c.sample_rate)));
    add64(c.input_channels);
    add64(c.output_channels);
    add(c.channel_mask);
    for (const auto* delays : {&c.input_delays, &c.output_delays}) {
        add64(delays->size());
        for (const auto d : *delays)
            add64(static_cast<std::uint64_t>(d));
    }
    add64(c.paths.size());
    for (const auto& path : c.paths) {
        add64(path.impulse.size());
        for (const auto sample : path.impulse)
            add_float(sample);
        for (const auto* routes : {&path.inputs, &path.outputs}) {
            add64(routes->size());
            for (const auto& route : *routes) {
                add64(route.channel);
                add_float(route.gain);
            }
        }
    }
    keyed ^= keyed >> 31;
    keyed *= 0xBF58476D1CE4E5B9ULL;
    keyed ^= keyed >> 29;
    return {fnv, keyed};
}

std::optional<FilterFigures> cached_figures(const FilterKey& key) noexcept {
    auto& c = cache();
    try {
        const std::lock_guard lock(c.mutex);
        for (auto& e : c.entries)
            if (e.used && e.key == key) {
                e.used = ++c.clock;
                ++c.counts.found;
                return e.figures;
            }
        ++c.counts.missed;
    } catch (...) { // The lock failed: find the figures again.
    }
    return std::nullopt;
}

void cache_figures(const FilterKey& key, const FilterFigures& figures) noexcept {
    auto& c = cache();
    try {
        const std::lock_guard lock(c.mutex);
        Entry* slot = nullptr;
        for (auto& e : c.entries)
            if (e.used && e.key == key) {
                slot = &e;
                break;
            }
        if (slot) {
            // The same filter: its figures are the same, so only an estimate
            // found since adds anything; one already cached stays.
            if (figures.estimated && !slot->figures.estimated) {
                slot->figures.gains.estimate_db = figures.gains.estimate_db;
                slot->figures.gains.estimate_failed = figures.gains.estimate_failed;
                slot->figures.estimated = true;
            }
        } else {
            slot = &c.entries.front();
            for (auto& e : c.entries)
                if (e.used < slot->used)
                    slot = &e;
            slot->key = key;
            slot->figures = figures;
        }
        slot->used = ++c.clock;
    } catch (...) { // The lock failed: nothing is kept, which costs only time.
    }
}

FigureCacheCounts figure_cache_counts() noexcept {
    auto& c = cache();
    try {
        const std::lock_guard lock(c.mutex);
        return c.counts;
    } catch (...) {
        return {};
    }
}

void clear_figure_cache() noexcept {
    auto& c = cache();
    try {
        const std::lock_guard lock(c.mutex);
        c.entries = {};
        c.clock = 0;
        c.counts = {};
    } catch (...) {
    }
}
} // namespace convolver::detail
