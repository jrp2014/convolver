// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The filter analysis behind worst_case_peak() and estimate_attenuation(),
// shared with HostFilter so a newly accepted filter is analysed once
// (PERF-27, jrp's review of 7bd024f). Private to Convolver.
#include "convolver/engine.hpp"
#include <cstddef>
#include <cstdint>

namespace convolver::detail {
// Both figures of one pass over each output's kernels (for each input, the
// sum over paths of out_gain * in_gain * h, as worst_case_peak() documents):
// `peak` is worst_case_peak(), the largest output's sum of absolute taps,
// and `noise_gain` the largest output's root sum of squared taps (PLUG-52),
// each accumulated in the order its own pass used, so both are the same
// doubles as before. Allocates the longest impulse's length in doubles.
struct KernelSums {
    double peak = 0;
    double noise_gain = 0;
};
KernelSums kernel_sums(const Configuration&);
// estimate_attenuation() with worst_case_peak(c) already found, as
// HostFilter has it, so the bound is not computed again. Validates `c` as
// estimate_attenuation() does; `worst_case_peak` must be worst_case_peak(c).
AttenuationEstimate estimate_attenuation(const Configuration& c, double worst_case_peak,
                                         std::uint64_t seed = 0, std::size_t lengths = 10);
} // namespace convolver::detail
