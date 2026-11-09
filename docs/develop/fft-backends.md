<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# FFT backends and measurements

[Documentation index](../README.md) · [Concepts](../use/concepts.md) · [Open FFT work](../../TODO.md#fft-libraries-and-performance)

**In short:**

- Choose one FFT library per build with `-DCONVOLVER_FFT_BACKEND`: **FFTW**
  (every platform, supports tuned plans), **ACCELERATE** (Apple's system
  framework, no extra dependency) or **OOURA** (built in, no FFT library at all).
- The default, `AUTO`, prefers the platform's own library: **Accelerate on
  macOS** for every build, the command-line tools included. Elsewhere it is
  FFTW, or Ooura in a plug-in build. FFTW stays available on macOS with
  `-DCONVOLVER_FFT_BACKEND=FFTW`, for tuned plans
  ([which libraries each build uses](build.md#which-libraries-each-build-uses)).
- All three give the same results within a small tested tolerance. None is
  fastest everywhere: in the dated measurements below FFTW was faster in most
  cases, by up to about 1.8 times at an awkward (non-power-of-two) block size,
  while Accelerate won some Apple workloads by up to 7%.
- In the dated uniform-engine measurements, long filters spent most time in
  the per-partition multiply-accumulate. Recheck that balance after an engine
  change. The initial PERF-2 comparison used Accelerate for both engines;
  the separate [backend measurements](fft-measurements.md#perf-2-backend-and-wisdom-comparison)
  compare FFTW planning, and a [paired Ooura run](fft-measurements.md#ooura-with-the-nonuniform-engine)
  gives absolute one-core CPU estimates.
- An optimized (SIMD) FFTW build matters more than planner settings. Homebrew's
  FFTW on Apple Silicon and vcpkg's default port are built without SIMD.
- Compiler flags: keep `-O3` (GCC at `-O2` is 1.8 times slower). On x86-64 the
  multiply-accumulate also has an AVX2 version with fused multiply-add, chosen
  at run time, which makes room-correction filters 1.2–1.4 times faster. Its
  output differs from the baseline code by float rounding only (below
  -120 dBFS, no worse against the double-precision oracle). LTO, PGO,
  unrolling and `-ffast-math` gave nothing measurable.

Start with the selection table and contract. The figures quoted here are
**dated measurements**, not a ranking of current libraries on every machine;
each is summarised under [measurements](#measurements), and the full records,
with their machines and raw data, are on
[FFT and engine measurements](fft-measurements.md).

Contents: [selection and contract](#selection-and-contract),
[Ooura](#ooura-no-fft-library), [alternatives](#alternatives-for-linux-and-windows),
[correctness evidence](#correctness-evidence),
[Apple benchmark](#processing-benchmark),
[x86-64 FFTW tuning](#fftw-tuning-on-x86-64-and-split-spectra),
[shared forward transforms](#shared-forward-transforms),
[overlap-add versus overlap-save](#overlap-add-and-overlap-save),
[compiler flags and AVX2 kernels](#compiler-flags-and-avx2-kernels),
[startup, reset and filter loading](#startup-reset-and-filter-loading) and
[reproduction steps](#reproduce).

| Backend | Availability | Separate FFT dependency | Planning |
| --- | --- | --- | --- |
| FFTW (default on Linux and Windows, except plug-ins) | Supported macOS/Linux/Windows builds | Single-precision FFTW | Estimate, measured modes and saved wisdom |
| Accelerate (default on macOS) | Apple targets | System framework | Prepared setup; no FFTW wisdom/measured modes |
| Ooura | Built into portable source | None | Prepared tables; estimate mode only |

See [build commands](build.md#fft-backend) and [FFTW plan tuning](../use/command-line.md#fftw-plan-tuning). Primary references:
[FFTW manual](https://fftw.org/fftw3_doc/),
[Apple vDSP](https://developer.apple.com/documentation/accelerate/vdsp), and
[Ooura packages](https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html).
The [vendored-copy record](../../portable/third_party/ooura/README.md) describes
local Ooura modifications. Its freshness was checked on 21 October 2026 (FFT-1, complete).

## Selection and contract

`-DCONVOLVER_FFT_BACKEND=AUTO`, the default, selects Accelerate on Apple
platforms ([below](#accelerate-by-default-on-macos)) and FFTW elsewhere, except
in plug-in builds on Linux and Windows, which get Ooura ([below](#plug-in-builds)).
`-DCONVOLVER_FFT_BACKEND=FFTW` selects FFTW explicitly.
`-DCONVOLVER_FFT_BACKEND=ACCELERATE` selects Apple's system vDSP framework and
requires an Apple target. `-DCONVOLVER_FFT_BACKEND=OOURA` builds a
dependency-free FFT into the library (see below). Accelerate builds do not discover or link FFTW. Each
binary has one backend; there is no automatic or runtime backend switching.
`convolver --version` reports the backend. The library exposes
`fft_backend_name()` and `fft_backend_version()`.

The internal `portable/fft.hpp` interface owns prepared plans and buffers. All three
implementations expose interleaved complex bins 0..N/2 with separate DC/Nyquist,
an unscaled forward DFT, and an inverse scaled by N. The convolution engine
normalizes filter spectra once by 1/N. Accelerate's adapter handles the packed
DC/Nyquist and factor-of-two forward scaling described in
[Apple's real FFT guide](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/vDSP_Programming_Guide/UsingFourierTransforms/UsingFourierTransforms.html).

FFTW uses N=2B. Accelerate and Ooura use the next power of two at least 2B (minimum four),
while keeping the requested B-frame latency, partition payload, delays and tail.
Neither silently falls back to FFTW for awkward sizes. Extra padding consumes
more spectrum/history storage; validation accounts for that before allocation.
Planning/allocation remain outside processing. The backend does not change routing,
configuration import, sample formats, or the overlap-add scheduling algorithm.

### Accelerate by default on macOS

On Apple platforms `AUTO` selects **Accelerate** for every build: the
command-line tools and libraries as well as the plug-ins (FFT-3). Accelerate is
part of macOS, so a Mac build needs no FFT library installed and a Mac package
ships none, and the plug-ins avoid FFTW's shared planner (below). Its results
agree with FFTW's within the tested tolerance
([correctness evidence](#correctness-evidence)), and it prepares filters faster
([setup times](#processing-benchmark)).

FFTW remains the fallback: configure with `-DCONVOLVER_FFT_BACKEND=FFTW` after
`brew install fftw`. Choose it for the command-line tools when you want what
only FFTW offers, tuned plans and saved wisdom
([FFTW plan tuning](../use/command-line.md#fftw-plan-tuning)), or when you
measure it faster for your filters. In the dated
[Apple benchmark](#processing-benchmark), an FFTW built with NEON took 13–19%
less time than Accelerate for 65536-tap filters at B = 256 (they were equal at
B = 1024) and was 1.8 times faster at an awkward block size (B = 192), while
Accelerate was up to 7% faster in some cases with 4096-tap filters. Homebrew's FFTW is built without NEON, which cost FFTW 2–24% in the
same Mac's measurements
([FFTW comparisons](#fftw-implementation-and-planning-comparisons); to build
one with NEON, see [building FFTW](fft-measurements.md#building-fftw-for-this-apple-silicon-mac)). Use
power-of-two block sizes with Accelerate to avoid its padding.

### Plug-in builds

The distributed Mac plug-ins use Accelerate and need no FFTW tuning or
wisdom file. Custom FFTW plug-in builds use estimated plans; they expose no
measured-planning or wisdom-file control and do not automatically load a file
generated by the CLI. The [wisdom workflow](../use/command-line.md#fftw-plan-tuning)
applies to the FFTW command-line tool, not to normal plug-in operation.

In a plug-in build (`CONVOLVER_BUILD_VST3`, `CONVOLVER_BUILD_LV2` or
`CONVOLVER_BUILD_AU`), `AUTO` selects **Ooura** on Linux and Windows and
**Accelerate** on macOS (PLUG-1). A
plug-in runs inside a host process that may load other plug-ins, or the host
itself, using the same FFTW. FFTW's planner is not thread-safe, and Convolver's
planner lock only covers Convolver's own calls. Static FFTW is not enough on
Linux or macOS either, because its symbols are exported or can bind to an FFTW
already loaded in the process. Ooura took 0.9–1.45 times FFTW's time per frame in
the dated uniform-engine Linux measurements below. The
[new-engine Mac comparison](fft-measurements.md#ooura-with-the-nonuniform-engine)
found it close to Accelerate for a 30-second filter; neither result ranks
current Windows or Linux builds.

The choice stays a build option: pass `-DCONVOLVER_FFT_BACKEND=FFTW` (or `OOURA`,
`ACCELERATE`) to override it. CMake warns for FFTW in a plug-in build. The
`vst3.fft_symbols` test checks the built plug-in: it must never export FFTW
symbols, and unless FFTW was chosen it must not import them. One backend serves a
whole build, so build the CLI separately with FFTW if you want FFTW wisdom and
measured plans for offline rendering. The release candidates follow the same
defaults (`packaging/release-policy.json`).

### Ooura (no FFT library)

`-DCONVOLVER_FFT_BACKEND=OOURA` builds Takuya Ooura's split-radix real FFT
(`fftsg`, the package the historical engine also shipped) into the core, so the
library, CLI and VST3 plug-in need no FFT library at all. The configured CI exercises it with Clang, GCC and MSVC. The code is vendored in `portable/third_party/ooura/`
with its permissive licence ("use, copy, modify and distribute this code for any
purpose ... without fee"), which is compatible with GPL-2.0-or-later; see the
README there for the source and the small changes made. Like Accelerate it uses
the next power of two at least 2B (minimum four) and accepts only estimate
planning, without wisdom or time limits. Its tables are built when the engine is
prepared, not on the audio thread.

Measured 19 October 2026 on a cloud Linux x86-64 VM (Intel Xeon at 2.80 GHz,
Release, Clang 18, Ubuntu FFTW 3.3.10 with estimate planning), median of three
rounds of `benchmark_backends.py`:

| Case | FFTW ns/frame | Ooura ns/frame | FFTW/Ooura |
| --- | ---: | ---: | ---: |
| mono_short | 25.7 | 36.6 | 0.70× |
| mono_room | 34.7 | 45.0 | 0.77× |
| stereo_low_latency | 286.4 | 296.4 | 0.97× |
| stereo_long | 594.8 | 620.5 | 0.96× |
| matrix_short | 173.8 | 191.9 | 0.91× |
| matrix_long | 1066.9 | 1142.7 | 0.93× |
| stereo_large_block | 166.8 | 189.3 | 0.88× |
| stereo_odd_size | 120.1 | 167.7 | 0.72× |
| matrix6_short | 552.0 | 802.6 | 0.69× |
| matrix6_long | 2925.9 | 2653.7 | 1.10× |

Ooura takes 0.9–1.45 times FFTW's time per frame here; this uniform-engine
measurement found long filters dominated by the partition multiply-accumulate, which is the same for every
backend. A shared VM is noisy; treat these as indicative. On that machine the
16-case CLI comparison against FFTW gave a maximum absolute difference of
7.2e-7 (RMS 7.9e-8), and the engine's worst error against the double-precision
oracle was 2.6e-7 (FFTW 3.4e-7). CI builds and tests the Ooura backend on Linux
(Clang with sanitizers, GCC) and macOS, compares its CLI output with FFTW's, and
builds and tests the core with MSVC without FFTW.

## Alternatives for Linux and Windows

Reviewed on 8 November 2026 (FFT-2). **Keep the existing backends unless a
measured target-platform need justifies another dependency.** PFFFT is the
most promising alternative for a compact SIMD backend without FFTW's shared
planner, but the Mac trial is evidence, not a decision to adopt it. A modern or
actively maintained library is not automatically faster: compare complete
Convolver processing, including layout conversion, against Ooura and an
optimized FFTW build on the intended CPU. These are candidates, not new
supported backends or a change to package defaults.

| Library | Integration fit | Recommendation |
| --- | --- | --- |
| [Maintained PFFFT fork](https://github.com/marton78/pffft/tree/237ffa283b17e5ef88ef8198b465f41b0268011d) | Small C API, CMake, single-precision SSE on x86 and NEON on ARM, explicit workspace, read-only prepared setup. BSD-like FFTPACK licence; retain its notices. | If Windows/Linux measurements justify a change, start with ordered transforms behind `RealFft`; avoid a new spectrum layout for marginal gains. |
| [PocketFFT C++](https://github.com/mreineck/pocketfft/tree/4d865cba24359b05b37da4e75aee24498c3b78e7) | Header-only C++11, BSD-3-Clause, broad sizes and multidimensional support. Its normal execution interface allocates temporary storage; optional plan caching takes a mutex. | Not a drop-in real-time backend. Prefer it only for a separate offline need, or after an upstream workspace API makes it a small integration. |
| [KissFFT](https://github.com/mborgerding/kissfft) | Small C library with CMake/MSVC support and a real-transform half-spectrum close to ours. Choose float, static linkage, no OpenMP, separate input/output and prepared real-transform state. | A simplicity fallback if Ooura becomes unsuitable, not an evidenced speed upgrade. Audit generic-radix and in-place temporary allocation before adopting. |

The inspected PFFFT fork's head was `237ffa283b17` (5 November); PocketFFT's
C++ head was `4d865cba2435` (18 October). Both have older FFTPACK ancestry:
recent maintenance and usable interfaces matter more than the age of the
underlying algorithm. The PocketFFT concern is visible in its
[header](https://github.com/mreineck/pocketfft/blob/4d865cba24359b05b37da4e75aee24498c3b78e7/pocketfft_hdronly.h):
`general_r2c` allocates workspace, `rfftp::exec` allocates its own temporary
array even with a prepared plan, and `get_plan` locks when caching is enabled.
Turning off threading or enabling the cache does not remove those costs.
Its documented vectorization mainly targets multidimensional transforms,
so header-only deployment is not evidence of faster individual 1D audio FFTs.

PFFFT's [API contract](https://github.com/marton78/pffft/blob/237ffa283b17e5ef88ef8198b465f41b0268011d/include/pffft/pffft.h)
fits our forward/inverse scaling. Its ordered real spectrum packs Nyquist
into the imaginary DC slot; an adapter must unpack/repack those two values.
It also needs aligned buffers and explicit prepared workspace, and its SIMD
real transform requires supported factors and a multiple of 32. The PERF-2
production stage lengths (128 through 16,384) fit; tiny test partition
layouts need an existing Ooura fallback or a deliberately supported policy.

The [isolated Mac trial](fft-measurements.md#pffft-ordered-transform-experiment)
used 13–36% less processing time than Ooura across the measured cases, with
ordered transforms, so reordering is included in the measured cost. PFFFT's unordered `pffft_zconvolve_accumulate` could
avoid that cost, but using it would change the engine's spectrum-storage and
multiply-accumulate contract. Its separate PFFASTCONV implementation is not
a replacement for Convolver's routing and nonuniform scheduler. Only pursue
that broader integration if ordered transforms show a worthwhile target
platform gain and profiling identifies conversion as significant.

Before shipping PFFFT, test GCC/Clang on Linux and MSVC on Windows, including
x86-64 without AVX2 and ARM64 if offered. Keep a portable CPU baseline;
compiler-selected SIMD is not runtime dispatch. Check symbol visibility in
real plug-in bundles, licence notices, package exports, resource accounting
and the allocation-free process path, including C allocation calls that
the existing C++ `new` guard does not intercept. Compare callback tails, setup time
and the full engine oracle, not just an FFT microbenchmark. The Mac trial
cannot qualify SSE performance, Windows packaging or host behaviour.

Intel oneMKL remains a specialist option if an Intel workload demonstrates
a large benefit; its dependency and redistribution footprint is harder to
justify for these small, single-threaded transforms. GPU FFTs add transfer
and scheduling concerns and are outside this straightforward CPU-backend
comparison. Track outstanding measurements under [FFT-2](../../TODO.md#fft-libraries-and-performance).

## Correctness evidence

In the initial 18 October FFTW/Accelerate comparison, both implementations passed
**14/14 CTest suites** in Debug with ASan/UBSan and
optimized Release on this Mac. The new `native.fft` suite checks canonical bins
against a direct double DFT, DC-only/Nyquist-only signals, impulses, and inverse
normalization. Existing engine tests retain their independent double convolution
oracle, odd block sizes, one-frame blocks, matrix routes/delays, chunk invariance,
reset, tails and processing-allocation guard. Existing real libsndfile/CLI tests
run unchanged except for backend-neutral version identification.

Paired CLI comparisons run both binaries on the same seeded three-input,
four-output configurations with two filter paths, delays, signed routes, and both
latency modes. They cover B=1,3,17,64,128,192,256,1024: 16 cases and 213,324 samples.
No tolerance was relaxed. The acceptance bound is
`abs(a-b) <= 2e-5*(1+max(abs(a),abs(b)))`.

- [Release, FFTW NEON vs Accelerate](../benchmarks/2026-10-18-release-equivalence.json):
  maximum absolute error **7.45e-7**, RMS error **8.19e-8**.
- [Debug sanitizer comparison](../benchmarks/2026-10-18-debug-equivalence.json):
  maximum absolute error **8.34e-7**, RMS error **8.11e-8**. This used the original
  scalar FFTW test dependency; the optimized Release comparison used NEON.

These are numerically equivalent within the tested tolerance, not bit-identical.
The macOS CI workflow builds both and runs the paired comparison. The results
above are local measurements; platform CI results are recorded separately in
[GitHub Actions](https://github.com/jrp2014/convolver/actions/workflows/regression.yml).
The preceding FFTW implementation passed all platforms at `f78c22b`.

## Measurements

The dated measurements behind these choices, with their machines, builds and
raw data, and the steps to reproduce them, are in
[FFT and engine measurements](fft-measurements.md). The sections below keep
each conclusion and its old link. Benchmark the intended routing, filter
lengths and latency on the target machine before choosing a backend or flags
for speed.

### Processing benchmark

On an M5 Max on 18 October 2026, FFTW built with NEON and Accelerate were
close for most workloads: FFTW took 13–19% less time for 65,536-tap filters
at B = 256 and was 1.8 times faster at B = 192, where Accelerate pads to a
power of two, while Accelerate was up to 7% faster in some 4096-tap cases and
prepared filters faster (9–10 times for the shortest filters, 1.2–2.7 times
for 65,536-tap ones)
([measurements](fft-measurements.md#processing-benchmark)).

#### FFTW implementation and planning comparisons

<a id="building-fftw-for-this-apple-silicon-mac"></a>

On the same Mac a NEON build of FFTW 3.3.11 was 2–24% faster than a scalar
one (Homebrew's is scalar), and MEASURE planning gained about 2–6% over
ESTIMATE in several workloads
([measurements](fft-measurements.md#fftw-implementation-and-planning-comparisons);
[building FFTW with NEON](fft-measurements.md#building-fftw-for-this-apple-silicon-mac)).

### FFTW tuning on x86-64 and split spectra

<a id="fftw-build-and-planner-findings"></a>
<a id="split-spectrum-storage"></a>

On x86-64 (18 October 2026), FFTW's SIMD codelets matter most: an AVX2 build
is 2–4 times faster than scalar FFTW, and FFTW picks the instruction set at
run time. MEASURE plans add 1.2–1.8 times on the FFT. PATIENT and
`FFTW_DESTROY_INPUT` add nothing, AVX-512 about 10% (not worth a custom
build), and FFTW's split-format plans were about 2 times slower. vcpkg's
`fftw3` has no SIMD unless features are requested, so Windows CI asks for
`fftw3[core,sse2,avx,avx2]`. The engine stores each partition's spectra
split (all real parts, then all imaginary parts), which made the
multiply-accumulate 1.4–1.8 times faster on x86 with identical results
([measurements](fft-measurements.md#fftw-tuning-on-x86-64-and-split-spectra)).

#### Where the time goes

For filters of 16,384 frames or more the two FFTs are under 4% of the block
time; nearly all of it is the per-partition multiply-accumulate, so tuning
the FFT library speeds long filters up very little
([measurements](fft-measurements.md#where-the-time-goes)).

#### Shared forward transforms

Paths whose ordered input routes are identical (same channels, same order,
bit-identical gains) mix the same block, so the engine runs one forward FFT
per distinct mix and keeps one spectrum history ring for it, sized for the
longest member path. Output is bit-identical to running each path alone.
Dense matrices gain 1.2–1.4 times; mono and diagonal stereo are unchanged.
Inverse-transform sharing would change summation order and is open as
PERF-2 ([measurements](fft-measurements.md#shared-forward-transforms)).

### Overlap-add and overlap-save

<a id="overlap-add-and-overlap-save-1"></a>

The portable engine uses overlap-add over a ring of past input spectra; the
historical engine used overlap-save. On x86-64 overlap-save was 7–15% faster
at B = 64 and up to 1.8 times faster with a million taps, and within noise at
larger blocks; the engine has not been switched
([measurements](fft-measurements.md#overlap-add-and-overlap-save)).

### Compiler flags and AVX2 kernels

<a id="compiler-flags"></a>
<a id="where-the-time-goes-1"></a>
<a id="compiler-flags-1"></a>

Measured on x86-64 in October 2026 (PERF-4,
[measurements](fft-measurements.md#compiler-flags-and-avx2-kernels)):

- Keep `-O3`: GCC at `-O2` (the `RelWithDebInfo` default) is 1.8 times
  slower. Release builds and packages must stay `Release`.
- Only the vector width matters: AVX2 makes the multiply-accumulate 1.3–1.4
  times faster. AVX-512 adds nothing with Clang and loses the gain with GCC. LTO, PGO, unrolling and
  `-fno-math-errno` are within noise.
- `-ffast-math` is unsafe: it lets the compiler remove `std::isfinite`, so
  non-finite input, filters and state are accepted.
- `-march=native` and `-march=x86-64-v3` cannot be used for distributed
  binaries; the release check rejects `-march=native` and `/arch:AVX`.
  The kernels below give portable binaries the AVX2 gain instead.

#### AVX2 kernels chosen at run time

<a id="divergence"></a>
<a id="speed"></a>

So that portable binaries get the AVX2 gain, the five functions of the
spectrum processing (`portable/kernels.inc`: split, join, multiply,
multiply-accumulate and the per-path sum over partitions) are compiled twice on
x86-64: once for the baseline and once with `-mavx2 -mfma -ffp-contract=off`
(`/arch:AVX2` with MSVC, which allows FMA). In the AVX2 copy the multiply and
multiply-accumulate use explicit FMA intrinsics, per bin
`re += fma(xr, hr, -(xi·hi))` and `im += fma(xr, hi, xi·hr)`, in the vector
loop and the scalar remainder alike. In x86-64 builds with the AVX2 kernels,
neither copy is contracted implicitly, even if `-march` flags are added
(`-ffp-contract=off`, and pragmas for clang, clang-cl and MSVC), so the
baseline stays unfused and each set's results depend only on the CPU's set,
not on the compiler (other targets, such as arm64, and builds with
`-DCONVOLVER_AVX2_KERNELS=OFF` use the compiler's default contraction, as
before PERF-4): GCC 13 and Clang 18 gave
identical divergence figures below. `kernels()` checks once that the CPU has
AVX2 and FMA and that the operating system saves the 256-bit registers;
otherwise the baseline set is used. Each engine takes the chosen set when it is
built. `-DCONVOLVER_AVX2_KERNELS=OFF` leaves the AVX2 copy out;
`convolver_benchmark` prints which set ran (`AVX2+FMA` or `baseline`).

Before PERF-4 adopted FMA, the AVX2 set had no FMA and gave bit-identical
output to the baseline ([speed](fft-measurements.md#speed)). With FMA ("add FMA, but keep an
eye out for significant output divergences";
[decisions log](../project/decisions.md#configuration-and-sound)), **output
differs between CPUs with and without AVX2+FMA at float-rounding level**. The accumulation stays a separate add: fusing both
products into the running sum (`re = fma(-xi, hi, fma(xr, hr, re))`) rounds the
sum twice per partition, and on a 64k-tap sine render was 1.31 times the
baseline's RMS error against the double oracle (geometric mean of 15
frequencies, worst 1.67); the form used is 1.00.

The two sets differ by 1.8e-7 to 7.5e-7 of the output's peak, at most
-122.6 dBFS, which is not audible; `native.kernel_divergence` fails if they
differ by more than 1e-6 of the peak or -120 dBFS
([divergence](fft-measurements.md#divergence)). With FMA the kernels are
1.32 (Clang) and 1.42 (GCC) times faster over the six room-correction
workloads ([speed](fft-measurements.md#speed)).

### Startup, reset and filter loading

<a id="startup-reset-and-loading"></a>

Since PERF-5 a new, reset or replaced filter skips the partitions that would
meet the silence before its first block, so the average callback cost while
the history refills about halves, and `reset()` takes about 4–13 µs instead of about 150 µs. Steady
processing is unchanged. Since PERF-6 a filter whose paths name channels of
one file opens and decodes it once
([measurements](fft-measurements.md#startup-reset-and-filter-loading)).

### Reproduce

<a id="comparing-backends"></a>

The benchmark programs and scripts are in
[`tests/native/benchmarks/`](../../tests/native/benchmarks/), the dated
results in [`docs/benchmarks/`](../benchmarks/), and the commands on the
[measurements page](fft-measurements.md#reproduce).
