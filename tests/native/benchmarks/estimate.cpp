// SPDX-License-Identifier: GPL-2.0-or-later
// Time the noise estimate (estimate_attenuation(), PERF-23) for stereo or true
// stereo filters of decaying noise, as Estimated mode and every load in that
// mode wait for it. Only the public API, so any revision can run it.
#include "convolver/engine.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 4)
            throw std::invalid_argument(
                "usage: convolver_estimate_benchmark TAPS stereo|true-stereo [ROUNDS]");
        const auto taps = std::stoul(argv[1]);
        const std::string layout = argv[2];
        const auto rounds = argc == 4 ? std::stoul(argv[3]) : 3UL;
        if (!taps || taps > convolver::max_impulse_frames || !rounds || rounds > 100 ||
            (layout != "stereo" && layout != "true-stereo"))
            throw std::invalid_argument("invalid taps, layout or rounds");
        convolver::Configuration c;
        c.sample_rate = 48000;
        c.input_channels = c.output_channels = 2;
        std::uint32_t seed = 1;
        for (std::size_t i = 0; i < 2; ++i)
            for (std::size_t o = 0; o < 2; ++o) {
                if (layout == "stereo" && i != o)
                    continue;
                convolver::FilterPath p;
                p.impulse.resize(taps);
                // Uniform noise decaying by about 70 dB over the filter.
                for (std::size_t n = 0; n < taps; ++n) {
                    seed = 1664525 * seed + 1013904223;
                    const float noise = static_cast<float>(seed >> 8) / 16777216.f - .5f;
                    p.impulse[n] =
                        0.1f * noise *
                        std::exp(-8.0f * static_cast<float>(n) / static_cast<float>(taps));
                }
                p.inputs = {{i, 1}};
                p.outputs = {{o, 1}};
                c.paths.push_back(std::move(p));
            }
        std::cout << "backend,kernels,taps,layout,round,ms,noise_attenuation_db\n";
        for (std::size_t round = 1; round <= rounds; ++round) {
            const auto start = std::chrono::steady_clock::now();
            const auto e = convolver::estimate_attenuation(c);
            const std::chrono::duration<double, std::milli> ms =
                std::chrono::steady_clock::now() - start;
            std::cout << convolver::fft_backend_name() << ',' << convolver::cpu_kernels_name()
                      << ',' << taps << ',' << layout << ',' << round << ',' << std::setprecision(6)
                      << ms.count() << ',' << std::setprecision(12) << e.noise_attenuation_db
                      << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
