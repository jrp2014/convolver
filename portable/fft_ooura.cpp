// SPDX-License-Identifier: GPL-2.0-or-later
// Dependency-free FFT backend using Ooura's split-radix real DFT.
#include "convolver/engine.hpp"
#include "fft.hpp"
#include "fft_ooura_transform.hpp"
#include <stdexcept>
#include <vector>

namespace convolver {
const char* fft_backend_name() noexcept {
    return "Ooura";
}
const char* fft_backend_version() noexcept {
    return "fftsg (built in)";
}
bool fft_supports_wisdom() noexcept {
    return false;
}
void import_fft_wisdom(std::string_view) {
    throw std::invalid_argument("wisdom requires the FFTW backend");
}
std::string export_fft_wisdom() {
    throw std::invalid_argument("wisdom requires the FFTW backend");
}
namespace detail {
void check_fft_options(const FftOptions& options) {
    if (options.planning != FftPlanning::estimate || options.wisdom_only ||
        options.time_limit_seconds != 0)
        throw std::invalid_argument("measured planning and wisdom require the FFTW backend");
}
std::size_t fft_storage(std::size_t n) noexcept {
    // time, frequency, sum and the transform's own work and tables.
    return n + 2 * (n + 2) + OouraTransform::storage(n);
}
struct RealFft::Impl {
    std::vector<float> time, frequency, sum;
    OouraTransform transform;
    explicit Impl(std::size_t length, FftOptions options)
        : time(length), frequency(length + 2), sum(length + 2), transform(length) {
        check_fft_options(options);
    }
};
RealFft::RealFft(std::size_t n, FftOptions options) : impl_(std::make_unique<Impl>(n, options)) {}
RealFft::~RealFft() = default;
float* RealFft::time() noexcept {
    return impl_->time.data();
}
float* RealFft::frequency() noexcept {
    return impl_->frequency.data();
}
float* RealFft::sum() noexcept {
    return impl_->sum.data();
}
void RealFft::forward() noexcept {
    impl_->transform.forward(impl_->time.data(), impl_->frequency.data());
}
void RealFft::inverse() noexcept {
    impl_->transform.inverse(impl_->sum.data(), impl_->time.data());
}
} // namespace detail
} // namespace convolver
