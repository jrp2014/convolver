// SPDX-License-Identifier: GPL-2.0-or-later
// The independent reference for every convolution test: direct double-precision
// time-domain convolution, route by route, with no FFT and no partitioning.
// Shared by the engine tests and the plug-in comparison harness (PLUG-2).
#pragma once
#include "convolver/engine.hpp"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace convolver::test {
// FIR and routing support after the last input frame, excluding latency.
inline std::size_t tail(const Configuration& c) {
    std::size_t result = 0;
    for (const auto& p : c.paths)
        for (const auto& i : p.inputs)
            for (const auto& o : p.outputs)
                if (i.gain != 0 && o.gain != 0)
                    result = std::max(
                        result, p.impulse.size() - 1 +
                                    (c.input_delays.empty() ? 0 : c.input_delays[i.channel]) +
                                    (c.output_delays.empty() ? 0 : c.output_delays[o.channel]));
    return result;
}
// Interleaved output for interleaved input, delayed by b frames of latency, with
// (count + tail + 3b) frames so callers can compare past the end of the tail.
inline std::vector<double> oracle(const Configuration& c, const std::vector<float>& input,
                                  std::size_t b) {
    const auto count = input.size() / c.input_channels;
    std::vector<double> output((count + tail(c) + b + 2 * b) * c.output_channels);
    // Deliberately direct double-precision route-by-route convolution, independent of FFT
    // partitioning.
    for (const auto& p : c.paths)
        for (const auto& i : p.inputs)
            for (const auto& o : p.outputs)
                for (std::size_t t = 0; t < count; ++t)
                    for (std::size_t k = 0; k < p.impulse.size(); ++k) {
                        const auto when =
                            b + t + k + (c.input_delays.empty() ? 0 : c.input_delays[i.channel]) +
                            (c.output_delays.empty() ? 0 : c.output_delays[o.channel]);
                        output[when * c.output_channels + o.channel] +=
                            static_cast<double>(input[t * c.input_channels + i.channel]) * i.gain *
                            p.impulse[k] * o.gain;
                    }
    return output;
}
} // namespace convolver::test
