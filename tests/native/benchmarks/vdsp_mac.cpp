// SPDX-License-Identifier: GPL-2.0-or-later
// Time the actual production partition kernel, including history-ring traversal.
#include "kernels.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string(argv[4]) == "--baseline")
            convolver::detail::force_baseline_kernels(true);
        else if (argc != 4)
            throw std::invalid_argument("usage: mac BINS PARTITIONS SECONDS [--baseline]");
        const auto bins = std::stoul(argv[1]), parts = std::stoul(argv[2]);
        const double seconds = std::stod(argv[3]);
        if (!bins || bins > 65537 || !parts || parts > 4096 || !std::isfinite(seconds) ||
            seconds <= 0 || seconds > 60)
            throw std::invalid_argument("invalid benchmark dimensions or duration");
        const auto& k = convolver::detail::kernels();
        const auto stride = 2 * bins;
        std::vector<float> history(parts * stride), filters(parts * stride), sum(stride);
        std::uint32_t seed = 1;
        for (auto* v : {&history, &filters})
            for (auto& f : *v) {
                seed = 1664525 * seed + 1013904223;
                f = float(seed >> 8) / 16777216.f - .5f;
            }
        std::size_t head = 0;
        const auto step = [&] {
            k.accumulate(sum.data(), sum.data() + bins, history.data(), head, parts, filters.data(),
                         parts, bins);
            if (++head == parts)
                head = 0;
        };
        for (int i = 0; i < 128; ++i)
            step();
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        std::size_t count = 0;
        double elapsed = 0, checksum = 0;
        do {
            for (int i = 0; i < 64; ++i)
                step();
            count += 64;
            checksum += sum[0];
            elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        } while (elapsed < seconds);
        if (!std::isfinite(checksum))
            throw std::runtime_error("nonfinite output");
        std::cout << "kernels,bins,partitions,ns_per_accumulation,iterations,checksum\n"
                  << std::setprecision(10) << k.name << ',' << bins << ',' << parts << ','
                  << elapsed * 1e9 / double(count) << ',' << count << ',' << checksum << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
