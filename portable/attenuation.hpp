// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The filter analysis behind worst_case_peak() and estimate_attenuation(),
// shared with HostFilter so a newly accepted filter is analysed once
// (PERF-27, jrp's review of 7bd024f). Private to Convolver.
#include "convolver/engine.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>

namespace convolver::detail {
// Both figures of one pass over each output's kernels (for each input, the
// sum over paths of out_gain * in_gain * h, as worst_case_peak() documents):
// `peak` is worst_case_peak(), the largest output's sum of absolute taps,
// and `noise_gain` the largest output's root sum of squared taps (PLUG-52),
// each accumulated in the order its own pass used, so both are the same
// doubles as before. Allocates the longest impulse's length in doubles
// only when two paths join the same output and input (PERF-31).
struct KernelSums {
    double peak = 0;
    double noise_gain = 0;
};
KernelSums kernel_sums(const Configuration&);
// ROBUST-11: whether every tap of every path is zero or below the smallest
// normal float, which processing flushes to zero (PERF-15), so the filter
// plays silence whatever its routes. One pass over the taps, cheap beside
// reading them. kernel_sums() and the noise estimate read such taps as zero
// too, so such a filter's figures are those of a silent one.
bool plays_silence(const Configuration&) noexcept;
// Thrown by estimate_attenuation() when `stop` was set while it ran.
struct EstimateStopped : std::exception {
    const char* what() const noexcept override {
        return "the estimate was stopped";
    }
};
// estimate_attenuation() with worst_case_peak(c) already found, as
// HostFilter has it, so the bound is not computed again. Validates `c` as
// estimate_attenuation() does; `worst_case_peak` must be worst_case_peak(c).
// PLUG-74: `stop`, when given, is read between blocks of noise (4,096
// frames), and set ends the estimate with EstimateStopped; it changes
// nothing else, so an estimate that runs to its end is the same.
AttenuationEstimate estimate_attenuation(const Configuration& c, double worst_case_peak,
                                         std::uint64_t seed = 0, std::size_t lengths = 10,
                                         const std::atomic<bool>* stop = nullptr);
} // namespace convolver::detail
