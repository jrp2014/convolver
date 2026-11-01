// SPDX-License-Identifier: GPL-2.0-or-later
// Private: --compare, which checks a rendered file against the file it should
// match (for example a host's render against a first-use example's expected.wav).
#pragma once
#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>

namespace convolver::cli {
// The largest difference that still matches: 0.0001 of the expected file's
// peak (-80 dB), and never less than two 16-bit steps (2^-14), so rounding
// and 16-bit dither pass even for a quiet expected file. A wrong filter,
// route, gain or alignment differs by far more.
inline double compare_limit(double expected_peak) {
    return std::max(1e-4 * expected_peak, 1.0 / 16384);
}

struct Difference {
    double largest = 0;      // Largest absolute sample difference.
    std::size_t channel = 0; // Where it is, counted from 0.
    std::size_t frame = 0;
};
// Largest difference between two interleaved signals with the same channel
// count, output moved `shift` frames earlier (negative: later) and expected
// scaled by `gain`. Frames that one signal lacks count as silence, so a
// missing tail shows as a difference.
Difference difference(std::span<const float> output, std::span<const float> expected,
                      std::size_t channels, std::ptrdiff_t shift = 0, double gain = 1);
// First frame where any channel exceeds `level`, if any.
std::optional<std::size_t> onset(std::span<const float> samples, std::size_t channels,
                                 double level);

// True when argv[1] is --compare, which run_comparison then handles.
bool is_comparison(int argc, char** argv);
// Exit status: 0 the files match, 1 they differ or cannot be read, 2 invalid usage.
int run_comparison(int argc, char** argv);
} // namespace convolver::cli
