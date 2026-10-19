// SPDX-License-Identifier: GPL-2.0-or-later
// Compares the two uniformly partitioned FFT convolution schemes on one path:
//   OLA: overlap-add with a history of input spectra (portable/engine.cpp)
//   OLS: overlap-save accumulating into future output spectra
//        (convolver/convolution/convolution.cpp, doPartitionedConvolution)
// Both use the same FFTW plans (N = 2B, FFTW_MEASURE) and the same split
// real/imaginary multiply-accumulate loop, so only the scheme differs.
// Run with docs/benchmarks/src/run_overlap_schemes.sh.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fftw3.h>
#include <random>
#include <vector>

#if defined(_MSC_VER)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

namespace {
using Clock = std::chrono::steady_clock;

void mac(float* RESTRICT re, float* RESTRICT im, const float* RESTRICT x, const float* RESTRICT h,
         std::size_t bins) {
    const float *xr = x, *xi = x + bins, *hr = h, *hi = h + bins;
    for (std::size_t j = 0; j < bins; ++j) {
        re[j] += xr[j] * hr[j] - xi[j] * hi[j];
        im[j] += xr[j] * hi[j] + xi[j] * hr[j];
    }
}

struct Common {
    std::size_t B, N, bins, P;
    float* time;
    fftwf_complex* freq;
    fftwf_plan fwd, inv;
    std::vector<float> H; // P partitions, each bins real parts then bins imaginary parts.
    Common(const std::vector<float>& ir, std::size_t b)
        : B(b), N(2 * b), bins(b + 1), P((ir.size() + b - 1) / b) {
        time = fftwf_alloc_real(N);
        freq = fftwf_alloc_complex(bins);
        fwd = fftwf_plan_dft_r2c_1d(static_cast<int>(N), time, freq, FFTW_MEASURE);
        inv = fftwf_plan_dft_c2r_1d(static_cast<int>(N), freq, time, FFTW_MEASURE);
        H.assign(P * 2 * bins, 0.f);
        for (std::size_t p = 0; p < P; ++p) {
            std::fill_n(time, N, 0.f);
            for (std::size_t t = 0; t < B && p * B + t < ir.size(); ++t)
                time[t] = ir[p * B + t];
            fftwf_execute(fwd);
            split(H.data() + p * 2 * bins, 1.f / static_cast<float>(N));
        }
    }
    ~Common() {
        fftwf_destroy_plan(fwd);
        fftwf_destroy_plan(inv);
        fftwf_free(time);
        fftwf_free(freq);
    }
    Common(const Common&) = delete;
    Common& operator=(const Common&) = delete;
    void split(float* to, float scale) const {
        for (std::size_t j = 0; j < bins; ++j) {
            to[j] = freq[j][0] * scale;
            to[bins + j] = freq[j][1] * scale;
        }
    }
    void join(const float* from) const {
        for (std::size_t j = 0; j < bins; ++j) {
            freq[j][0] = from[j];
            freq[j][1] = from[bins + j];
        }
    }
};

// Portable: zero-padded input block, ring of input spectra, one accumulator, overlap-add.
struct OverlapAdd : Common {
    std::vector<float> history, sum, overlap;
    std::size_t head = 0;
    OverlapAdd(const std::vector<float>& ir, std::size_t b)
        : Common(ir, b), history(P * 2 * bins), sum(2 * bins), overlap(b) {}
    void block(const float* in, float* out) {
        std::fill_n(time, N, 0.f);
        std::copy_n(in, B, time);
        fftwf_execute(fwd);
        split(history.data() + head * 2 * bins, 1.f);
        std::fill(sum.begin(), sum.end(), 0.f);
        for (std::size_t p = 0; p < P; ++p)
            mac(sum.data(), sum.data() + bins, history.data() + ((head + P - p) % P) * 2 * bins,
                H.data() + p * 2 * bins, bins);
        join(sum.data());
        fftwf_execute(inv);
        for (std::size_t t = 0; t < B; ++t) {
            out[t] = time[t] + overlap[t];
            overlap[t] = time[B + t];
        }
        if (++head == P)
            head = 0;
    }
};

// Historical: last 2B inputs, spectrum accumulated into P future output slots, overlap-save.
struct OverlapSave : Common {
    std::vector<float> previous, spectrum, future;
    std::size_t slot = 0;
    OverlapSave(const std::vector<float>& ir, std::size_t b)
        : Common(ir, b), previous(b), spectrum(2 * bins), future(P * 2 * bins) {}
    void block(const float* in, float* out) {
        std::copy_n(previous.data(), B, time);
        std::copy_n(in, B, time + B);
        std::copy_n(in, B, previous.data());
        fftwf_execute(fwd);
        split(spectrum.data(), 1.f);
        for (std::size_t p = 0; p < P; ++p) {
            float* y = future.data() + ((slot + p) % P) * 2 * bins;
            mac(y, y + bins, spectrum.data(), H.data() + p * 2 * bins, bins);
        }
        float* y = future.data() + slot * 2 * bins;
        join(y);
        std::fill_n(y, 2 * bins, 0.f);
        fftwf_execute(inv);
        std::copy_n(time + B, B, out);
        if (++slot == P)
            slot = 0;
    }
};

constexpr std::size_t input_blocks = 64;

// Microseconds per block, timing whole batches for at least `seconds`.
template <class Engine>
double time_block(Engine& e, const std::vector<float>& x, std::vector<float>& y, double seconds) {
    std::size_t blocks = 0;
    const auto start = Clock::now();
    double elapsed = 0;
    do {
        for (std::size_t k = 0; k < 16; ++k, ++blocks)
            e.block(x.data() + (blocks % input_blocks) * e.B,
                    y.data() + (blocks % input_blocks) * e.B);
        elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    } while (elapsed < seconds);
    return elapsed / static_cast<double>(blocks) * 1e6;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Both schemes against direct double-precision convolution.
bool check(std::mt19937& g) {
    std::uniform_real_distribution<float> u(-1, 1);
    constexpr std::size_t b = 128, blocks = 64;
    std::vector<float> ir(1000), x(blocks * b), ya(x.size()), yb(x.size());
    for (auto& v : ir)
        v = u(g);
    for (auto& v : x)
        v = u(g);
    OverlapAdd a(ir, b);
    OverlapSave s(ir, b);
    for (std::size_t k = 0; k < blocks; ++k) {
        a.block(&x[k * b], &ya[k * b]);
        s.block(&x[k * b], &yb[k * b]);
    }
    double ea = 0, es = 0;
    for (std::size_t n = 0; n < x.size(); ++n) {
        double ref = 0;
        for (std::size_t m = 0; m < ir.size() && m <= n; ++m)
            ref += static_cast<double>(ir[m]) * x[n - m];
        ea = std::max(ea, std::fabs(ya[n] - ref));
        es = std::max(es, std::fabs(yb[n] - ref));
    }
    std::fprintf(stderr, "# max error vs direct double convolution: OLA %.3g, OLS %.3g\n", ea, es);
    return ea < 1e-4 && es < 1e-4;
}
} // namespace

int main(int argc, char** argv) {
    // --quick: fewer, shorter rounds and no 1M-tap cases.
    const bool quick = argc > 1 && std::strcmp(argv[1], "--quick") == 0;
    const int rounds = quick ? 3 : 7;
    const double seconds = quick ? 0.05 : 0.2;
    std::mt19937 g(1);
    if (!check(g)) {
        std::fprintf(stderr, "error: a scheme disagrees with direct convolution\n");
        return 1;
    }
    std::uniform_real_distribution<float> u(-1, 1);
    std::vector<std::size_t> lengths{4096, 65536, 262144};
    if (!quick)
        lengths.push_back(1048576);
    std::printf("ir_frames,block,partitions,ola_us_per_block,ols_us_per_block,ols_over_ola\n");
    for (const std::size_t L : lengths)
        for (const std::size_t B : {64, 256, 1024, 4096}) {
            std::vector<float> ir(L);
            for (std::size_t i = 0; i < L; ++i)
                ir[i] = u(g) * std::exp(-5.f * static_cast<float>(i) / static_cast<float>(L));
            std::vector<float> x(input_blocks * B), y(input_blocks * B);
            for (auto& v : x)
                v = u(g);
            OverlapAdd a(ir, B);
            OverlapSave s(ir, B);
            time_block(a, x, y, seconds / 2); // Warm caches and page in storage.
            time_block(s, x, y, seconds / 2);
            std::vector<double> ta, ts;
            for (int r = 0; r < rounds; ++r) { // Alternate order to spread drift evenly.
                if (r % 2) {
                    ts.push_back(time_block(s, x, y, seconds));
                    ta.push_back(time_block(a, x, y, seconds));
                } else {
                    ta.push_back(time_block(a, x, y, seconds));
                    ts.push_back(time_block(s, x, y, seconds));
                }
            }
            const double ma = median(ta), ms = median(ts);
            std::printf("%zu,%zu,%zu,%.2f,%.2f,%.2f\n", L, B, a.P, ma, ms, ms / ma);
            std::fflush(stdout);
        }
    return 0;
}
