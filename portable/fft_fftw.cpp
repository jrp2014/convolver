// SPDX-License-Identifier: GPL-2.0-or-later
#include "fft.hpp"
#include "convolver/engine.hpp"
#include <fftw3.h>
#include <cstdlib>
#include <mutex>
#include <stdexcept>

namespace convolver {
namespace {
std::mutex& planner_mutex() {static std::mutex mutex;return mutex;}
struct Free {void operator()(void* p) const noexcept {fftwf_free(p);}};
unsigned planning_flags(FftOptions options) {
    unsigned flags=0;
    switch(options.planning) {
    case FftPlanning::estimate: flags=FFTW_ESTIMATE; break;
    case FftPlanning::measure: flags=FFTW_MEASURE; break;
    case FftPlanning::patient: flags=FFTW_PATIENT; break;
    default: throw std::invalid_argument("invalid FFT planning mode");
    }
    return flags | (options.wisdom_only?FFTW_WISDOM_ONLY:0u);
}
}
const char* fft_backend_name() noexcept {return "FFTW";}
const char* fft_backend_version() noexcept {return fftwf_version;}
bool fft_supports_wisdom() noexcept {return true;}
void import_fft_wisdom(std::string_view wisdom) {
    if(wisdom.empty() || wisdom.find('\0')!=std::string_view::npos)
        throw std::invalid_argument("invalid FFTW wisdom");
    const std::string text(wisdom);
    std::lock_guard lock(planner_mutex());
    if(!fftwf_import_wisdom_from_string(text.c_str()))
        throw std::invalid_argument("invalid or incompatible FFTW wisdom");
}
std::string export_fft_wisdom() {
    std::lock_guard lock(planner_mutex());
    // FFTW allocates exported text with malloc, not its aligned FFT allocator.
    const std::unique_ptr<char,decltype(&std::free)> text(fftwf_export_wisdom_to_string(),&std::free);
    if(!text) throw std::bad_alloc();
    return text.get();
}
namespace detail {
namespace {
using Buffer=std::unique_ptr<float,Free>;
Buffer allocate(std::size_t count) {
    Buffer p(static_cast<float*>(fftwf_malloc(count*sizeof(float))));
    if(!p) throw std::bad_alloc();
    return p;
}
struct Plan {
    fftwf_plan value=nullptr;
    ~Plan() {if(value) {std::lock_guard lock(planner_mutex());fftwf_destroy_plan(value);}}
};
}
std::size_t fft_size(std::size_t b) noexcept {return 2*b;}
std::size_t fft_storage(std::size_t n) noexcept {return n+2*(n+2);}
struct RealFft::Impl {
    Buffer time,frequency,sum; // Buffers outlive plans, including on exceptions.
    Plan forward,inverse;
    explicit Impl(std::size_t n,FftOptions options):time(allocate(n)),frequency(allocate(n+2)),sum(allocate(n+2)) {
        const auto flags=planning_flags(options);
        std::lock_guard lock(planner_mutex());
        forward.value=fftwf_plan_dft_r2c_1d(static_cast<int>(n),time.get(),reinterpret_cast<fftwf_complex*>(frequency.get()),flags);
        inverse.value=fftwf_plan_dft_c2r_1d(static_cast<int>(n),reinterpret_cast<fftwf_complex*>(sum.get()),time.get(),flags);
        if(!forward.value || !inverse.value) throw std::runtime_error(options.wisdom_only?
            "FFTW wisdom has no matching plans for this block size and planning mode; run --tune-fft":
            "FFTW plan creation failed");
    }
};
RealFft::RealFft(std::size_t n,FftOptions options):impl_(std::make_unique<Impl>(n,options)) {}
RealFft::~RealFft()=default;
float* RealFft::time() noexcept {return impl_->time.get();}
float* RealFft::frequency() noexcept {return impl_->frequency.get();}
float* RealFft::sum() noexcept {return impl_->sum.get();}
void RealFft::forward() noexcept {fftwf_execute(impl_->forward.value);}
void RealFft::inverse() noexcept {fftwf_execute(impl_->inverse.value);}
}
}
