// SPDX-License-Identifier: GPL-2.0-or-later
// Partition multiply-accumulate layouts: interleaved (engine today) vs split re/im.
// Also times FFTW guru split-format r2c/c2r plans, which would feed a split MAC directly.
#include <fftw3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using Clock = std::chrono::steady_clock;
static double now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
template <class F> static double time_ns(F f, double seconds = 0.4) {
    for (int i = 0; i < 20; ++i) f();
    double best = 1e30;
    for (int r = 0; r < 5; ++r) {
        long iters = 0; double t0 = now(), t;
        do { for (int i = 0; i < 16; ++i) f(); iters += 16; t = now() - t0; } while (t < seconds / 5);
        best = std::min(best, t * 1e9 / iters);
    }
    return best;
}
static float rnd() { return float(std::rand() % 20001) / 20000.f - .5f; }

__attribute__((noinline)) void mac_interleaved(float* __restrict sum, const float* hist, const float* filt, std::size_t parts,
                                               std::size_t head, std::size_t stride) {
    std::fill_n(sum, stride, 0.f);
    for (std::size_t p = 0; p < parts; ++p) {
        const float* x = hist + ((head + parts - p) % parts) * stride;
        const float* h = filt + p * stride;
        for (std::size_t j = 0; j < stride; j += 2) {
            sum[j] += x[j] * h[j] - x[j + 1] * h[j + 1];
            sum[j + 1] += x[j] * h[j + 1] + x[j + 1] * h[j];
        }
    }
}
// Split layout: per partition, bins reals then bins imaginaries (padded to pad).
__attribute__((noinline)) void mac_split(float* __restrict sr, float* __restrict si, const float* hist, const float* filt,
                                         std::size_t parts, std::size_t head, std::size_t bins, std::size_t pad) {
    std::fill_n(sr, bins, 0.f); std::fill_n(si, bins, 0.f);
    for (std::size_t p = 0; p < parts; ++p) {
        const float* xr = hist + ((head + parts - p) % parts) * 2 * pad; const float* xi = xr + pad;
        const float* hr = filt + p * 2 * pad; const float* hi = hr + pad;
        for (std::size_t j = 0; j < bins; ++j) {
            sr[j] += xr[j] * hr[j] - xi[j] * hi[j];
            si[j] += xr[j] * hi[j] + xi[j] * hr[j];
        }
    }
}

int main(int argc, char** argv) {
    const std::size_t b = std::strtoul(argv[1], nullptr, 10), ir = std::strtoul(argv[2], nullptr, 10);
    const std::size_t n = 2 * b, bins = n / 2 + 1, stride = 2 * bins, parts = 1 + (ir - 1) / b, pad = (bins + 15) & ~std::size_t(15);
    std::vector<float> fi(parts * stride), hi(parts * stride), fs(parts * 2 * pad), hs(parts * 2 * pad);
    for (auto* v : {&fi, &hi}) for (auto& x : *v) x = rnd();
    for (std::size_t p = 0; p < parts; ++p) for (std::size_t j = 0; j < bins; ++j) {
        fs[p * 2 * pad + j] = fi[p * stride + 2 * j]; fs[p * 2 * pad + pad + j] = fi[p * stride + 2 * j + 1];
        hs[p * 2 * pad + j] = hi[p * stride + 2 * j]; hs[p * 2 * pad + pad + j] = hi[p * stride + 2 * j + 1];
    }
    std::vector<float> s(stride), sr(pad), si(pad);
    std::size_t head = 0;
    const double t_il = time_ns([&] { mac_interleaved(s.data(), hi.data(), fi.data(), parts, head, stride); head = (head + 1) % parts; });
    head = 0;
    const double t_sp = time_ns([&] { mac_split(sr.data(), si.data(), hs.data(), fs.data(), parts, head, bins, pad); head = (head + 1) % parts; });
    double err = 0;
    mac_interleaved(s.data(), hi.data(), fi.data(), parts, 0, stride);
    mac_split(sr.data(), si.data(), hs.data(), fs.data(), parts, 0, bins, pad);
    for (std::size_t j = 0; j < bins; ++j) err = std::max(err, double(std::abs(s[2 * j] - sr[j]) + std::abs(s[2 * j + 1] - si[j])));

    // FFTW interleaved vs split-format plans, FFTW_MEASURE, out of place.
    float* t = fftwf_alloc_real(n); float* c = fftwf_alloc_real(2 * pad); float* re = fftwf_alloc_real(pad); float* im = fftwf_alloc_real(pad);
    for (std::size_t i = 0; i < n; ++i) t[i] = rnd();
    fftwf_plan pi = fftwf_plan_dft_r2c_1d(int(n), t, (fftwf_complex*)c, FFTW_MEASURE);
    fftwf_iodim d{int(n), 1, 1};
    fftwf_plan ps = fftwf_plan_guru_split_dft_r2c(1, &d, 0, nullptr, t, re, im, FFTW_MEASURE);
    fftwf_plan ii = fftwf_plan_dft_c2r_1d(int(n), (fftwf_complex*)c, t, FFTW_MEASURE);
    fftwf_plan is = fftwf_plan_guru_split_dft_c2r(1, &d, 0, nullptr, re, im, t, FFTW_MEASURE);
    for (std::size_t i = 0; i < n; ++i) t[i] = rnd();
    const double f_il = time_ns([&] { fftwf_execute(pi); }, .2), f_sp = time_ns([&] { fftwf_execute(ps); }, .2);
    const double i_il = time_ns([&] { fftwf_execute(ii); }, .2), i_sp = time_ns([&] { fftwf_execute(is); }, .2);
    std::printf("%zu,%zu,%zu,%.1f,%.1f,%.2f,%.1e,%.1f,%.1f,%.1f,%.1f\n", b, ir, parts, t_il, t_sp, t_il / t_sp, err, f_il, f_sp, i_il, i_sp);
}
