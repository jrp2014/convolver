// SPDX-License-Identifier: GPL-2.0-or-later
#include "fft.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

void near(float actual, double expected) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 2e-5 * (1 + std::abs(expected)))
        throw std::runtime_error("FFT oracle mismatch");
}
int main() {
    try {
        const auto modes =
            convolver::fft_supports_wisdom()
                ? std::vector{convolver::FftPlanning::estimate, convolver::FftPlanning::measure,
                              convolver::FftPlanning::patient, convolver::FftPlanning::exhaustive}
                : std::vector{convolver::FftPlanning::estimate};
        for (auto mode : modes)
            for (std::size_t b : {1, 2, 3, 7, 8, 17, 64, 127}) {
                const auto n = convolver::detail::fft_size(b);
                if (n < 2 * b || n % 2)
                    throw std::runtime_error("FFT length contract");
                // A time limit must still yield a correct plan.
                convolver::detail::RealFft fft(n, {mode, false, b % 2 ? 0.05 : 0.0});
                for (int kind = 0; kind < 4; ++kind) {
                    std::vector<float> input(n);
                    for (std::size_t i = 0; i < n; ++i)
                        input[i] = kind == 0   ? 1.f
                                   : kind == 1 ? (i % 2 ? -1.f : 1.f)
                                   : kind == 2 ? (i == 0 ? 1.f : 0.f)
                                               : float(std::sin(i * .37) + .25 * std::cos(i * .21));
                    std::copy(input.begin(), input.end(), fft.time());
                    fft.forward();
                    for (std::size_t k = 0; k <= n / 2; ++k) {
                        double re = 0, im = 0;
                        for (std::size_t i = 0; i < n; ++i) {
                            const double angle =
                                -2 * std::numbers::pi * double(k) * double(i) / double(n);
                            re += input[i] * std::cos(angle);
                            im += input[i] * std::sin(angle);
                        }
                        near(fft.frequency()[2 * k], re);
                        near(fft.frequency()[2 * k + 1], im);
                    }
                    std::copy_n(fft.frequency(), n + 2, fft.sum());
                    fft.inverse();
                    for (std::size_t i = 0; i < n; ++i)
                        near(fft.time()[i] / float(n), input[i]);
                }
            }
        std::cout << "FFT layout, DC/Nyquist, direct DFT and normalization passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
