// SPDX-License-Identifier: GPL-2.0-or-later
// Splits one Convolver path's per-block work into forward FFT, partition MAC
// and inverse FFT, mirroring portable/engine.cpp compute().
#include <fftw3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
static double now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

template <class F> static double time_ns(F f, double seconds = 0.3) {
    for (int i = 0; i < 50; ++i) f();
    double best = 1e30;
    for (int r = 0; r < 5; ++r) {
        long iters = 0; double t0 = now(), t;
        do { for (int i = 0; i < 64; ++i) f(); iters += 64; t = now() - t0; } while (t < seconds / 5);
        best = std::min(best, t * 1e9 / iters);
    }
    return best;
}

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: breakdown BLOCK IR FLAGS(estimate|measure|patient|exhaustive)[+destroy][+inplace]\n"); return 1; }
    const std::size_t b = std::strtoul(argv[1], nullptr, 10), ir = std::strtoul(argv[2], nullptr, 10);
    const std::string mode = argv[3];
    unsigned flags = FFTW_ESTIMATE;
    if (mode.find("measure") != std::string::npos) flags = FFTW_MEASURE;
    if (mode.find("patient") != std::string::npos) flags = FFTW_PATIENT;
    if (mode.find("exhaustive") != std::string::npos) flags = FFTW_EXHAUSTIVE;
    if (mode.find("destroy") != std::string::npos) flags |= FFTW_DESTROY_INPUT;
    const bool inplace = mode.find("inplace") != std::string::npos;
    const std::size_t n = 2 * b, bins = n / 2 + 1, stride = 2 * bins, parts = 1 + (ir - 1) / b;
    float* time = fftwf_alloc_real(inplace ? stride : n);
    float* freq = inplace ? time : fftwf_alloc_real(stride);
    float* sum = fftwf_alloc_real(stride);
    double plan_t0 = now();
    fftwf_plan fwd = fftwf_plan_dft_r2c_1d(int(n), time, reinterpret_cast<fftwf_complex*>(freq), flags);
    fftwf_plan inv = fftwf_plan_dft_c2r_1d(int(n), reinterpret_cast<fftwf_complex*>(sum), time, flags);
    double plan_ms = (now() - plan_t0) * 1e3;
    std::vector<float> filters(parts * stride), history(parts * stride);
    for (auto& x : filters) x = float(std::rand()) / float(RAND_MAX) - .5f;
    for (auto& x : history) x = float(std::rand()) / float(RAND_MAX) - .5f;
    for (std::size_t i = 0; i < n; ++i) time[i] = float(std::rand()) / float(RAND_MAX) - .5f;
    std::size_t head = 0;
    const double f_ns = time_ns([&] { fftwf_execute(fwd); });
    const double i_ns = time_ns([&] { fftwf_execute(inv); });
    const double m_ns = time_ns([&] {
        std::fill_n(sum, stride, 0.f);
        for (std::size_t p = 0; p < parts; ++p) {
            const auto h0 = (head + parts - p) % parts;
            const float* x = history.data() + h0 * stride;
            const float* h = filters.data() + p * stride;
            for (std::size_t j = 0; j < stride; j += 2) {
                sum[j] += x[j] * h[j] - x[j + 1] * h[j + 1];
                sum[j + 1] += x[j] * h[j + 1] + x[j + 1] * h[j];
            }
        }
        if (++head == parts) head = 0;
    });
    const double total = f_ns + i_ns + m_ns;
    std::printf("%s,%zu,%zu,%zu,%s,%.3f,%.1f,%.1f,%.1f,%.1f,%.3f\n", fftwf_version, b, ir, parts, mode.c_str(), plan_ms,
                f_ns, i_ns, m_ns, 100.0 * (f_ns + i_ns) / total, total / b);
    char* w = fftwf_sprint_plan(fwd);
    if (std::getenv("SHOW_PLAN")) std::fprintf(stderr, "%s\n", w);
    std::free(w);
    fftwf_destroy_plan(fwd); fftwf_destroy_plan(inv);
}
