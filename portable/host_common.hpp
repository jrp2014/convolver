// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Small rules that HostFilter (host.cpp) and HostProcessor
// (host_processor.cpp) both apply, kept in one place since PLUG-24 split
// the two. Private to Convolver::host.
#include "convolver/host.hpp"
#include <algorithm>
#include <cmath>

namespace convolver::detail {
// A playing filter's own answer: a dry signal only with equal counts (PLUG-23).
inline bool own_dry(const Configuration& c) noexcept {
    return c.input_channels == c.output_channels;
}
inline float gain_from_db(double db) noexcept {
    return static_cast<float>(std::pow(10.0, db / 20));
}
inline double clamp_gain_db(double db) noexcept {
    return std::isnan(db) ? 0 : std::clamp(db, min_gain_db, max_gain_db);
}
inline double clamp_mix(double mix) noexcept {
    return std::isnan(mix) ? 1 : std::clamp(mix, 0.0, 1.0);
}
} // namespace convolver::detail
