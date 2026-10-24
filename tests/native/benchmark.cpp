// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include "kernels.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/resource.h>
#endif

using Clock = std::chrono::steady_clock;
// Peak resident set in KiB where the platform reports it, otherwise -1.
long peak_rss_kib() {
#ifndef _WIN32
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
#ifdef __APPLE__
        return usage.ru_maxrss / 1024; // Bytes on macOS.
#else
        return usage.ru_maxrss; // KiB on Linux.
#endif
#endif
    return -1;
}
enum class Layout { diagonal, matrix, crossover }; // crossover: output o fed by input o % inputs
struct Case {
    const char* name;
    std::size_t block, ir, inputs, outputs;
    Layout layout;
};
// Cases 10-18 are room-correction workloads (PERF-4): 32k-128k taps at B=256,
// true stereo and a 2-way stereo crossover to 6 outputs, then short and very long checks.
constexpr Case cases[] = {{"mono_short", 64, 256, 1, 1, Layout::diagonal},
                          {"mono_room", 256, 4096, 1, 1, Layout::diagonal},
                          {"stereo_low_latency", 128, 16384, 2, 2, Layout::diagonal},
                          {"stereo_long", 256, 65536, 2, 2, Layout::diagonal},
                          {"matrix_short", 128, 4096, 2, 2, Layout::matrix},
                          {"matrix_long", 256, 65536, 2, 2, Layout::matrix},
                          {"stereo_large_block", 1024, 65536, 2, 2, Layout::diagonal},
                          {"stereo_odd_size", 192, 8192, 2, 2, Layout::diagonal},
                          {"matrix6_short", 128, 1024, 6, 6, Layout::matrix},
                          {"matrix6_long", 256, 16384, 6, 6, Layout::matrix},
                          {"room_stereo_32k", 256, 32768, 2, 2, Layout::diagonal},
                          {"room_stereo_64k", 256, 65536, 2, 2, Layout::diagonal},
                          {"room_stereo_128k", 256, 131072, 2, 2, Layout::diagonal},
                          {"room_true_stereo_64k", 256, 65536, 2, 2, Layout::matrix},
                          {"room_crossover6_64k", 256, 65536, 2, 6, Layout::crossover},
                          {"room_crossover6_32k", 256, 32768, 2, 6, Layout::crossover},
                          {"room_crossover6_16k", 256, 16384, 2, 6, Layout::crossover},
                          {"room_stereo_4k", 256, 4096, 2, 2, Layout::diagonal},
                          {"room_reverb_512k", 256, 524288, 2, 2, Layout::diagonal}};
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--count") {
            std::cout << std::size(cases) << '\n';
            return 0;
        }
        if (argc == 4 && std::string(argv[3]) == "--baseline")
            convolver::detail::force_baseline_kernels(true);
        else if (argc != 3)
            throw std::invalid_argument(
                "usage: convolver_benchmark CASE SECONDS [--baseline] (CASE=0.." +
                std::to_string(std::size(cases) - 1) + "), or convolver_benchmark --count");
        const auto index = std::stoul(argv[1]);
        const auto seconds = std::stod(argv[2]);
        if (index >= std::size(cases) || !std::isfinite(seconds) || seconds <= 0 || seconds > 60)
            throw std::invalid_argument("invalid case or duration");
        const auto c = cases[index];
        convolver::Configuration config{48000, c.inputs, c.outputs, 0, {}, {}, {}};
        std::uint32_t state = 12345;
        auto random = [&] {
            state = 1664525 * state + 1013904223;
            return (float(state >> 8) / 16777216.f - .5f);
        };
        for (std::size_t in = 0; in < c.inputs; ++in)
            for (std::size_t out = 0; out < c.outputs; ++out) {
                if ((c.layout == Layout::diagonal && in != out) ||
                    (c.layout == Layout::crossover && out % c.inputs != in))
                    continue;
                convolver::FilterPath path;
                path.inputs = {{in, 1}};
                path.outputs = {{out, 1}};
                path.impulse.resize(c.ir);
                for (auto& x : path.impulse)
                    x = random() * .02f;
                config.paths.push_back(std::move(path));
            }
        std::vector<float> input(c.block * c.inputs), output(c.block * c.outputs);
        for (auto& x : input)
            x = random();
        const auto setup_start = Clock::now();
        convolver::Engine engine(config, c.block);
        const double setup_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - setup_start).count();
        for (std::size_t i = 0; i < c.ir / c.block + 16; ++i)
            engine.process(input, output, c.block);
        std::size_t blocks = 0;
        double checksum = 0;
        const auto start = Clock::now();
        double elapsed;
        do {
            for (int batch = 0; batch < 64; ++batch)
                engine.process(input, output, c.block);
            blocks += 64;
            checksum += output[0];
            elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        } while (elapsed < seconds);
        // Individual-block timing is separate from throughput measurement.
        std::vector<double> latency(4096);
        for (auto& us : latency) {
            const auto before = Clock::now();
            engine.process(input, output, c.block);
            us = std::chrono::duration<double, std::micro>(Clock::now() - before).count();
        }
        std::sort(latency.begin(), latency.end());
        if (!std::isfinite(checksum))
            throw std::runtime_error("nonfinite benchmark output");
        const auto percentile = [&](double q) {
            return latency[static_cast<std::size_t>(q * double(latency.size() - 1))];
        };
        std::cout
            << "backend,kernels,case,block,ir,channels,paths,setup_ms,ns_per_frame,p50_block_us,"
               "p99_block_us,max_block_us,peak_rss_kib,frames,checksum\n"
            << std::setprecision(10) << convolver::fft_backend_name() << ','
            << convolver::detail::kernels().name << ',' << c.name << ',' << c.block << ',' << c.ir
            << ',' << c.inputs << "->" << c.outputs << ',' << config.paths.size() << ',' << setup_ms
            << ',' << elapsed * 1e9 / double(blocks * c.block) << ',' << percentile(.5) << ','
            << percentile(.99) << ',' << latency.back() << ',' << peak_rss_kib() << ','
            << blocks * c.block << ',' << checksum << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
