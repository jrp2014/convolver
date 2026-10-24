<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# FFT backends and measurements

[Documentation index](../README.md) · [Concepts](../use/concepts.md) · [Open FFT work](../../TODO.md#fft-libraries-and-performance)

**In short:**

- Choose one FFT library per build with `-DCONVOLVER_FFT_BACKEND`: **FFTW**
  (default, every platform, supports tuned plans), **ACCELERATE** (Apple's system
  framework, no extra dependency) or **OOURA** (built in, no FFT library at all).
- All three give the same results within a small tested tolerance. None is
  fastest everywhere: in the dated measurements below FFTW was faster in most
  cases, by up to about 1.8 times at an awkward (non-power-of-two) block size,
  while Accelerate won some Apple workloads by up to 7%.
- For long filters most time goes into the per-partition multiply-accumulate,
  not the FFT, so changing or tuning the FFT library helps them little.
- An optimized (SIMD) FFTW build matters more than planner settings. Homebrew's
  FFTW on Apple Silicon and vcpkg's default port are built without SIMD.
- Compiler flags: keep `-O3` (GCC at `-O2` is 1.8 times slower). On x86-64 the
  multiply-accumulate also has an AVX2 version with fused multiply-add, chosen
  at run time, which makes room-correction filters 1.2–1.4 times faster. Its
  output differs from the baseline code by float rounding only (below
  -120 dBFS, no worse against the double-precision oracle). LTO, PGO,
  unrolling and `-ffast-math` gave nothing measurable.

Start with the selection table and contract. Later sections contain **dated
measurements**, not a ranking of current libraries on every machine. The Apple
results were measured on 18 October 2026 on an M5 Max, 128 GiB RAM,
macOS 27.0.1 (26A434); later x86/Ooura sections name their own setup.

Contents: [selection and contract](#selection-and-contract),
[Ooura](#ooura-no-fft-library), [correctness evidence](#correctness-evidence),
[Apple benchmark](#processing-benchmark),
[x86-64 FFTW tuning](#fftw-tuning-on-x86-64-and-split-spectra),
[shared forward transforms](#shared-forward-transforms),
[overlap-add versus overlap-save](#overlap-add-and-overlap-save),
[compiler flags and AVX2 kernels](#compiler-flags-and-avx2-kernels) and
[reproduction steps](#reproduce).

| Backend | Availability | Separate FFT dependency | Planning |
| --- | --- | --- | --- |
| FFTW (default) | Supported macOS/Linux/Windows builds | Single-precision FFTW | Estimate, measured modes and saved wisdom |
| Accelerate | Apple targets | System framework | Prepared setup; no FFTW wisdom/measured modes |
| Ooura | Built into portable source | None | Prepared tables; estimate mode only |

See [build commands](build.md#fft-backend) and [FFTW plan tuning](../use/command-line.md#fftw-plan-tuning). Primary references:
[FFTW manual](https://fftw.org/fftw3_doc/),
[Apple vDSP](https://developer.apple.com/documentation/accelerate/vdsp), and
[Ooura packages](https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html).
The [vendored-copy record](../../portable/third_party/ooura/README.md) describes
local Ooura modifications. Its freshness was checked on 21 October 2026 (FFT-1, complete).

## Selection and contract

`-DCONVOLVER_FFT_BACKEND=AUTO`, the default, selects FFTW, except in plug-in
builds (below). `-DCONVOLVER_FFT_BACKEND=FFTW` selects FFTW explicitly.
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

### Plug-in builds

With `CONVOLVER_BUILD_VST3=ON`, `AUTO` selects **Ooura** on Linux and Windows and
**Accelerate** on macOS (owner's decision, 21 October 2026; TODO PLUG-1). A
plug-in runs inside a host process that may load other plug-ins, or the host
itself, using the same FFTW. FFTW's planner is not thread-safe, and Convolver's
planner lock only covers Convolver's own calls. Static FFTW is not enough on
Linux or macOS either, because its symbols are exported or can bind to an FFTW
already loaded in the process. Ooura took 0.9–1.45 times FFTW's time per frame in
the measurements below, so the cost is small.

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

Ooura takes 0.9–1.45 times FFTW's time per frame here; long filters are
dominated by the partition multiply-accumulate, which is the same for every
backend. A shared VM is noisy; treat these as indicative. On that machine the
16-case CLI comparison against FFTW gave a maximum absolute difference of
7.2e-7 (RMS 7.9e-8), and the engine's worst error against the double-precision
oracle was 2.6e-7 (FFTW 3.4e-7). CI builds and tests the Ooura backend on Linux
(Clang with sanitizers, GCC) and macOS, compares its CLI output with FFTW's, and
builds and tests the core with MSVC without FFTW.

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

## Processing benchmark

The following is the 18 October Apple FFTW/Accelerate measurement record.
Later engine changes and other machines require new measurements.

This measures the full in-memory Engine processing loop, including routing,
partition accumulation and FFT layout conversion. It excludes file I/O, CLI
startup and filter preparation. It is not an isolated FFT microbenchmark.

Those two Convolver builds used AppleClang 21.0.0, `-O3 -DNDEBUG`, no fast-math or
sanitizers. FFTW is official 3.3.11, built with `--enable-float --enable-neon
--enable-shared --disable-fortran`; configure chose `-O3 -fomit-frame-pointer
-mtune=native -fstrict-aliasing`. It reports `fftw-3.3.11-neon`. Plans use
`FFTW_ESTIMATE`, without persisted wisdom. Accelerate is the macOS system
framework (reported framework version 4.0.0). Both CLI tests use libsndfile 1.2.2.

Each workload warms the complete partition history before timing. Seven separate
process repetitions per backend/case run for at least 0.35 seconds each, in
64-block batches. Backend order alternates by repetition and case. No builds,
tests or other benchmarks ran concurrently. The figures below are medians; lower
ns/frame is better. A frame includes all configured channels. The final column
is FFTW time divided by Accelerate time; above 1 favours Accelerate.

| Case | FFTW ns/frame | Accelerate ns/frame | FFTW/Accelerate |
| --- | ---: | ---: | ---: |
| mono_short | 5.6 | 6.0 | 0.94× |
| mono_room | 9.4 | 8.8 | 1.07× |
| stereo_low_latency | 78.2 | 76.2 | 1.03× |
| stereo_long | 145.4 | 167.2 | 0.87× |
| matrix_short | 51.5 | 48.1 | 1.07× |
| matrix_long | 293.5 | 360.4 | 0.81× |
| stereo_large_block | 44.1 | 44.1 | 1.00× |
| stereo_odd_size | 31.9 | 56.3 | 0.57× |

For example, matrix_long takes about 1.41% of one core's time at 48 kHz with FFTW
and 1.73% with Accelerate in this benchmark. This is average throughput, not a
real-time deadline guarantee. Threads are not pinned and this was an interactive
Mac, so OS scheduling and background work can affect results.

Accelerate's long-filter measurements varied: stereo_long ranged 144.3–182.6
ns/frame (FFTW 145.4–150.4); matrix_long ranged 291.8–365.9 (FFTW 290.9–294.4).
Do not read these medians as a universal library ranking. The awkward B=192 case
uses N=384 for FFTW versus N=512 for Accelerate, increasing both FFT and partition
accumulation work. Power-of-two blocks avoid that padding penalty.

Keep **FFTW as the default**. Accelerate is a useful optional Apple backend: it
removes the separate FFT dependency, prepares filters faster in these cases, and
is competitive in several processing workloads. Benchmark the intended routing,
IR lengths and latency on the target machine before selecting it for speed.

Setup medians include plan construction and all filter spectra, separately timed:

| Case | B | IR frames | Paths | FFTW setup ms | Accelerate setup ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| mono_short | 64 | 256 | 1 | 0.260 | 0.026 |
| mono_room | 256 | 4096 | 1 | 0.486 | 0.054 |
| stereo_low_latency | 128 | 16384 | 2 | 0.495 | 0.093 |
| stereo_long | 256 | 65536 | 2 | 0.788 | 0.477 |
| matrix_short | 128 | 4096 | 4 | 0.471 | 0.069 |
| matrix_long | 256 | 65536 | 4 | 1.056 | 0.917 |
| stereo_large_block | 1024 | 65536 | 2 | 1.088 | 0.396 |
| stereo_odd_size | 192 | 8192 | 2 | 0.879 | 0.116 |

[Raw measurements](../benchmarks/2026-10-18-m5-max.csv) preserve every repetition,
setup time, processed-frame count, checksum and separately sampled p99 block time.
The p99 measurements are descriptive samples of 512 calls, not deadline guarantees.

### FFTW implementation and planning comparisons

On the same M5 Max, seven alternating rounds of at least 0.2 seconds per case
compared scalar and NEON builds of FFTW 3.3.11 with the same configure-selected
compiler flags. NEON improved full-engine throughput by approximately 2–24%
across these eight workloads. [Scalar/NEON measurements](../benchmarks/2026-10-18-fftw-scalar-neon.csv).
This supports keeping the native NEON build as the Mac baseline; it does not
establish an optimal FFT implementation for every machine or workload.

A separate five-round, 0.2-second comparison changed only FFTW planning from
ESTIMATE to MEASURE in a temporary backend source copy, using the same optimized
engine and benchmark. Several workloads gained about 2–6%, while the others
changed little; measured setup medians ranged approximately 4–26 ms.
[ESTIMATE/MEASURE measurements](../benchmarks/2026-10-18-fftw-estimate-measure.csv).
These numbers precede the new public planning API; the temporary measured build
also passed the native engine oracle. They are a planning experiment, not a
benchmark of wisdom loading or patient planning. Saved wisdom support is now
tested independently through fresh CLI processes, and FFT/engine oracles run
estimate, measure and patient plans. See [the tuning workflow](../use/command-line.md#fftw-plan-tuning).

## FFTW tuning on x86-64 and split spectra

Measured 18 October 2026 on a 4-vCPU Intel Xeon cloud VM at 2.1 GHz (AVX-512),
Ubuntu 24.04, Clang 18, `-O3 -DNDEBUG`. It is a shared virtual machine, so
differences under about 5% are noise. FFTW 3.3.11 was built from the
[FFTW/fftw3](https://github.com/FFTW/fftw3) `fftw-3.3.11` tag in three variants:
scalar, `--enable-sse2 --enable-avx --enable-avx2`, and the same plus
`--enable-avx512`. Ubuntu's packaged library (`fftw-3.3.10-sse2-avx`) was also
measured. The M5 Max figures above predate the split-spectrum change below.

### Where the time goes

[`src/fft_breakdown.cpp`](../benchmarks/src/fft_breakdown.cpp) times one path's
forward FFT, partition multiply-accumulate and inverse FFT separately
([raw data](../benchmarks/2026-10-18-x86-fftw-builds.csv)). With the AVX2 build and
MEASURE plans, the two FFTs are about 50% of per-block work for a 256-frame IR
at B=64, 20% for 4096 frames at B=256, and under 4% for 16384 frames or more at
B=128–256. Long
filters spend almost all their time in the multiply-accumulate, so FFTW tuning
alone cannot speed them up much.

### FFTW build and planner findings

| FFT pair time (forward + inverse), N=512 | ESTIMATE | MEASURE |
| --- | ---: | ---: |
| FFTW 3.3.11 scalar | 2.39 µs | 2.22 µs |
| Ubuntu 3.3.10 SSE2/AVX | 1.26 µs | 0.94 µs |
| FFTW 3.3.11 SSE2/AVX/AVX2 | 1.03 µs | 0.59 µs |
| FFTW 3.3.11 + AVX-512 | 0.86 µs | 0.52 µs |

- **SIMD codelets matter most**: AVX2 is 2–4× faster than scalar FFTW. FFTW
  detects CPU features at run time, so enabling SSE2/AVX/AVX2 is safe on older
  x86-64 CPUs.
- **MEASURE plans** are another 1.2–1.8× on the FFT, at 0.1–0.5 s planning per
  block size. Use `--tune-fft` once per machine and load the wisdom.
- **PATIENT** matched MEASURE within noise while taking 2–5× longer to plan.
- **AVX-512** added about 10% over AVX2 on the FFT. Not worth a custom build.
- **`FFTW_DESTROY_INPUT`** on the out-of-place forward plan made no difference.
- **FFTW's split-format guru plans** (`fftwf_plan_guru_split_dft_r2c`) were
  about 2× slower than interleaved plans, so the engine keeps interleaved FFTs
  and converts layout itself.

Packaging, from the FFTW 3.3.11 sources, the
[Homebrew formula](https://github.com/Homebrew/homebrew-core/blob/master/Formula/f/fftw.rb)
and the [vcpkg port](https://github.com/microsoft/vcpkg/tree/master/ports/fftw3):

- Homebrew on Apple Silicon does **not** pass `--enable-neon`, so `brew install
  fftw` there is scalar (`--version` reports plain `fftw-3.3.11`). Homebrew on
  Intel enables SSE2/AVX/AVX2. Build FFTW yourself with `--enable-neon` on an ARM
  Mac, as in the reproduction steps below.
- vcpkg's `fftw3` has no SIMD unless features are requested. Windows CI now
  installs `fftw3[core,sse2,avx,avx2]`. vcpkg's `neon` feature is limited to
  Linux/Android ARM, so an arm64 macOS vcpkg build is scalar.
- FFTW's own CMake build (used by vcpkg) only knows SSE/SSE2/AVX/AVX2 and NEON
  (by vcpkg patch). AVX-512, SVE and generic SIMD need the autotools build.
- FFTW 3.3.11 has cycle counters for Apple ARM and Linux AArch64 by default, so
  MEASURE planning works there without extra configure flags.

### Split spectrum storage

The engine previously stored filter and history spectra as interleaved complex
pairs. Each partition's multiply-accumulate then needed shuffles on x86. Spectra
are now stored per partition as all real parts followed by all imaginary parts;
the forward spectrum is split once per block and the sum is re-interleaved once
before the inverse FFT. Arithmetic per bin is unchanged.
[`src/spectrum_layout.cpp`](../benchmarks/src/spectrum_layout.cpp) measured the
multiply-accumulate alone at 1.4–1.8× faster with identical results
([raw data](../benchmarks/2026-10-18-x86-mac-layout.csv)).

Full engine benchmark, seven alternating rounds of 0.3 s, median ns/frame, FFTW
ESTIMATE plans ([raw data](../benchmarks/2026-10-18-x86-split-spectra.csv)):

| Case | Scalar FFTW, before | Scalar FFTW, after | AVX2 FFTW, before | AVX2 FFTW, after | Layout gain (AVX2) |
| --- | ---: | ---: | ---: | ---: | ---: |
| mono_short | 18.7 | 18.2 | 12.8 | 12.4 | 1.04× |
| mono_room | 26.9 | 24.4 | 20.6 | 21.2 | 0.97× |
| stereo_low_latency | 195.1 | 147.9 | 183.6 | 138.6 | 1.32× |
| stereo_long | 456.3 | 381.0 | 411.2 | 335.2 | 1.23× |
| matrix_short | 137.3 | 118.0 | 124.0 | 96.9 | 1.28× |
| matrix_long | 901.6 | 838.6 | 952.9 | 814.1 | 1.17× |
| stereo_large_block | 121.3 | 105.8 | 106.4 | 92.2 | 1.15× |
| stereo_odd_size | 93.2 | 80.0 | 71.4 | 57.4 | 1.24× |

On ARM, NEON's `ld2` de-interleaving loads make the old layout cheaper than on
x86, so the gain there may be smaller; rerun `benchmark_backends.py` on the Mac
to confirm. Larger gains for long filters need fewer multiply-accumulates rather
than faster FFTs. Sharing forward FFTs between paths with the same input mix is
now implemented ([below](#shared-forward-transforms)); inverse-transform sharing
and non-uniform partitioning remain open as **PERF-2** in
[TODO](../../TODO.md#fft-libraries-and-performance).

### Shared forward transforms

Paths whose ordered input routes are identical (same channels, same order,
bit-identical gains) mix the same block, so the engine now runs one forward FFT
per distinct mix and keeps one spectrum history ring for it, sized for the
longest member path. Paths still multiply, inverse-transform and accumulate in
their configured order, so output is bit-identical to the previous engine; this
was checked on 20 randomized 3×3 configurations with delays and mixed inputs,
comparing the old and new libraries byte for byte. Memory falls by one history
ring per shared path.

Release builds, FFTW 3.3.10 (Ubuntu, AVX2), GCC 13.3, Intel Xeon @ 2.10 GHz on a
shared cloud VM, so differences under about 7% are noise (`stereo_long` shares
nothing yet reads 0.93×). Five alternating rounds of 0.4 s per case, medians;
p99 is over 4096 single-block calls after the throughput run; RSS is the whole
process peak. `matrix6_short` and `matrix6_long` are new 6×6 matrix cases (36
paths, 6 distinct mixes):

| Case | Paths | Before ns/frame | After ns/frame | Speed-up | Before p99 µs | After p99 µs | Before RSS KiB | After RSS KiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| mono_short | 1 | 15.5 | 15.1 | 1.03× | 1.8 | 1.4 | 12448 | 12448 |
| mono_room | 1 | 21.1 | 21.3 | 0.99× | 9.3 | 10.1 | 12448 | 12448 |
| stereo_low_latency | 2 | 159.2 | 154.1 | 1.03× | 42.8 | 33.6 | 12448 | 12448 |
| stereo_long | 2 | 362.1 | 388.2 | 0.93× | 159.7 | 169.5 | 12448 | 12448 |
| matrix_short | 4 | 100.7 | 100.7 | 1.00× | 26.5 | 25.2 | 12448 | 12448 |
| matrix_long | 4 | 940.2 | 789.3 | 1.19× | 329.6 | 315.5 | 12448 | 12448 |
| stereo_large_block | 2 | 115.8 | 106.6 | 1.09× | 181.7 | 173.6 | 12448 | 12448 |
| stereo_odd_size | 2 | 70.5 | 68.9 | 1.02× | 26.8 | 24.5 | 12448 | 12448 |
| matrix6_short | 36 | 451.6 | 329.2 | 1.37× | 98.9 | 72.9 | 12448 | 12448 |
| matrix6_long | 36 | 2345.6 | 1767.3 | 1.33× | 866.6 | 673.9 | 18196 | 14232 |

Gains appear only where several paths read one mix: dense matrices (1.2–1.4×,
with p99 callback time falling by about the same factor). Diagonal stereo and
mono have one path per mix and are unchanged.

Inverse-transform sharing was evaluated but not implemented. Paths with identical
ordered output routes could sum their spectra and run one inverse FFT and one
overlap buffer, which would save the inverse FFT for every extra path into the
same outputs (30 of 36 inverses in the 6×6 cases). Unlike forward sharing it
changes summation order (frequency domain instead of time domain after the
inverse), so output would no longer be bit-identical, only within the oracle
tolerance. It also needs care with output delays and the per-path output gains.
It is a reasonable next step for dense matrices, as a separate, separately
measured change.

## Overlap-add and overlap-save

The historical engine uses uniformly partitioned overlap-save: each block's input
spectrum is multiplied by every filter partition and accumulated into a ring of
future output spectra (`historical/convolution/convolution.cpp`,
`doPartitionedConvolution`). The portable engine uses overlap-add with a ring of
past input spectra (`portable/engine.cpp`). The portable engine was written this
way from its first commit (`fa8ca77`); no reason for the change is recorded. It
was not made for speed.

Per block and path both schemes do one forward FFT of size 2B, P
multiply-accumulates and one inverse FFT. The only differences are memory traffic
and bookkeeping. Overlap-save reads and writes back P output spectra. Overlap-add
reads P history spectra into a single accumulator, and pads its input with zeros
and adds the overlap afterwards.

[`src/overlap_schemes.cpp`](../benchmarks/src/overlap_schemes.cpp) times both on one
path with the same FFTW MEASURE plans and the same split-spectrum MAC loop, so
only the scheme differs. Both agree with a direct double-precision convolution
to within 1e-5. Measured 19 October 2026 on a 4-vCPU Intel Xeon cloud VM at
2.8 GHz, Ubuntu 24.04, GCC 13.3 `-O3`, Ubuntu's FFTW 3.3.10. Each case ran seven
alternating rounds of 0.2 s, and the whole benchmark ran three times. The table
shows medians across the three runs and each run's ratio
([raw data](../benchmarks/2026-10-19-x86-overlap-schemes.csv)). A ratio below 1
means overlap-save was faster.

| IR frames | B | Partitions | Overlap-add µs/block | Overlap-save µs/block | Save/add (each run) |
| ---: | ---: | ---: | ---: | ---: | --- |
| 4096 | 64 | 64 | 3.2 | 3.0 | 0.92, 0.93, 1.00 |
| 4096 | 256 | 16 | 3.9 | 3.8 | 1.00, 0.96, 0.95 |
| 4096 | 1024 | 4 | 9.4 | 10.6 | 1.28, 1.20, 1.01 |
| 4096 | 4096 | 1 | 35.2 | 35.5 | 1.02, 0.97, 1.01 |
| 65536 | 64 | 1024 | 46.9 | 42.7 | 0.94, 0.89, 0.90 |
| 65536 | 256 | 256 | 45.2 | 46.7 | 1.03, 1.05, 0.92 |
| 65536 | 1024 | 64 | 48.5 | 52.2 | 1.04, 1.16, 1.08 |
| 65536 | 4096 | 16 | 89.5 | 94.8 | 1.10, 1.01, 1.06 |
| 262144 | 64 | 4096 | 215.0 | 184.7 | 0.85, 0.86, 0.88 |
| 262144 | 256 | 1024 | 246.2 | 234.1 | 0.95, 0.91, 0.88 |
| 262144 | 1024 | 256 | 217.1 | 206.3 | 1.03, 0.93, 1.05 |
| 262144 | 4096 | 64 | 234.8 | 242.3 | 1.02, 1.03, 1.05 |
| 1048576 | 64 | 16384 | 1213.1 | 833.2 | 0.86, 0.69, 0.86 |
| 1048576 | 256 | 4096 | 1872.8 | 1045.2 | 0.66, 0.56, 0.70 |
| 1048576 | 1024 | 1024 | 1407.9 | 1337.8 | 0.95, 1.07, 0.89 |
| 1048576 | 4096 | 256 | 1250.3 | 1169.3 | 0.85, 1.00, 0.91 |

Overlap-save was consistently faster at B = 64, by 7–15% up to 256k taps. With
1M taps at B = 64–256 it was 1.2–1.8× faster, where the spectra no longer fit in
cache. At B = 1024–4096 the two were within shared-VM noise up to 256k taps, with
overlap-add sometimes ahead. The cause has not been profiled; ARM is unmeasured.

The layout also decides which transform sharing is natural. Input-spectrum
history lets paths with the same input mix share one forward FFT and history
ring (implemented above). Output-spectrum accumulation would instead let paths
with the same output routes share one inverse FFT. Any switch to overlap-save
must keep the engine's latency, tail, oracle and allocation tests.

## Compiler flags and AVX2 kernels

Measured 23–24 October 2026 (PERF-4) on a 4-vCPU Intel Xeon cloud VM at
2.80 GHz (AVX2 and AVX-512), Ubuntu 24.04, Clang 18.1.3 and GCC 13.3.0, Ubuntu
FFTW 3.3.10 (SSE2/AVX, estimate plans) and the built-in Ooura FFT. It is a
shared virtual machine: treat differences under about 5% as noise. Each variant
was a separate Release build of the core with `CMAKE_CXX_FLAGS_RELEASE`
replaced; the benchmark was pinned to one core, variants ran in a shuffled order
within each case, and the tables give medians of 5 rounds of 0.3 s each. All
workloads use B = 256 at 48 kHz, the plug-in and CLI default. Raw data:
[flags](../benchmarks/2026-10-24-x86-compiler-flags.csv),
[kernels without FMA](../benchmarks/2026-10-24-x86-avx2-kernels.csv),
[kernels with FMA](../benchmarks/2026-10-24-x86-avx2-fma-kernels.csv) (a later,
different VM; see [AVX2 kernels](#avx2-kernels-chosen-at-run-time)).

Both kernel tables were made with
[`compiler_flags.py`](../benchmarks/src/compiler_flags.py) and
`convolver_benchmark` as committed, and their CSVs are the script's output (the
commands are under [Reproduce](#compiler-flags-1)). The flag table, the FFT shares
and the PGO and per-file builds came first, from a one-off harness that ran the
same nine workload sizes and layouts and also timed the FFTs on their own; it was
not kept. Its CSV has the columns `variant` (compiler, backend and flags),
`round` (from 0), `case`, `taps`, `paths`, `ns_per_frame`, the block-time
percentiles, `fft_pair_ns` (one forward plus one inverse transform) and
`fft_share` (the FFTs' share of the block time). `compiler_flags.py` can rerun
the flag rows other than PGO and the per-file builds with `--variant`, but does
not time the FFTs.

The primary workloads are room-correction filters. REW exports filters of
[131,072 samples](https://www.roomeqwizard.com/help/help/html/file.html), a
[Roon community guide](https://community.roonlabs.com/t/a-guide-to-advanced-room-correction-with-rew-and-rephase-using-convolution-filters/90990)
recommends 65,536 taps at 44.1 and 48 kHz, and Audiolense users report 32k–65k
filters, 130k for long ones. So: stereo at 32k, 64k and 128k taps, true stereo
(a 2×2 matrix) at 64k, and a stereo 3-way crossover (2 inputs, 6 outputs, one
path each) at 32k and 64k. A 16k crossover, 4k stereo and a 512k-tap stereo
reverb are secondary checks. They are cases 10 to 18 of `convolver_benchmark`.

### Where the time goes

At these lengths almost all of the time is the per-partition multiply-accumulate.
The FFTs (one forward per input, one inverse per path, timed separately at
N = 512) are this share of the -O3 block time:

| | 32k stereo | 64k stereo | 128k stereo | 64k true stereo | 64k crossover | 4k stereo |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| FFTW | 5–7% | 2–3% | 1% | 2% | 2% | 25–27% |
| Ooura | 14–15% | 6% | 3% | 4–5% | 4% | 41–48% |

That is why the flags used to build the FFT library matter little here: building
Ooura's code alone for AVX2 changed nothing measurable (0.96–1.03×), while
building only `engine.cpp` for AVX2 gave the whole AVX2 gain. For FFTW, a SIMD
build is still worth having for short filters ([above](#fftw-build-and-planner-findings)).
The pinned preview recipe builds FFTW without SIMD, but no preview package uses
FFTW; enable FFTW's SSE2/AVX/AVX2 (chosen at run time by FFTW) if one ever does.

### Compiler flags

Speedup over `-O3 -DNDEBUG` (CMake's Release default), geometric mean of the six
primary workloads; above 1 is faster. "v2", "v3" and "v4" are
`-march=x86-64-v2` (SSE4.2), `-march=x86-64-v3` (AVX2, FMA) and
`-march=x86-64-v4` (AVX-512).

| Flags | Clang, FFTW | Clang, Ooura | GCC, FFTW | GCC, Ooura |
| --- | ---: | ---: | ---: | ---: |
| `-O2` | 0.97 | 0.99 | **0.54** | **0.55** |
| `-O3` (current) | 1.00 | 1.00 | 1.00 | 1.00 |
| LTO (`CMAKE_INTERPROCEDURAL_OPTIMIZATION`) | 1.00 | 1.03 | 0.99 | 0.97 |
| PGO, trained on the benchmark | 0.99 | 0.98 | 1.03 | 1.02 |
| `-funroll-loops` | 0.98 | 1.02 | 0.95 | 0.96 |
| `-fno-math-errno` | 0.97 | 1.01 | 0.98 | 1.00 |
| `-ffast-math` | 0.97 | 1.01 | 0.97 | 0.96 |
| v2 | 0.98 | 1.03 | 0.96 | 0.99 |
| v3 | **1.39** | **1.34** | **1.39** | **1.34** |
| v3, `-ffp-contract=off` (no FMA) | 1.35 | 1.29 | 1.37 | 1.27 |
| v3, `-ffp-contract=fast` | 1.39 | 1.33 | 1.44 | 1.33 |
| v3 + LTO | 1.40 | 1.36 | 1.32 | 1.34 |
| v3 + `-ffast-math` | 1.44 | 1.38 | 1.35 | 1.32 |
| v4 | 1.36 | 1.35 | 1.05 | 0.97 |
| v3 for `engine.cpp` only | 1.40 | 1.32 | 1.41 | 1.32 |
| v3 for Ooura's files only | | 0.98 | | 0.99 |

- **Keep `-O3`.** With GCC, `-O2` (the `RelWithDebInfo` default) is 1.8 times
  slower: GCC 13 at `-O2` emitted no packed multiplies in `engine.cpp`, so the
  loops run one value at a time. Clang vectorizes them at both levels. Release builds and packages must stay `Release`.
- **Only the vector width matters.** AVX2 makes the multiply-accumulate about
  1.3–1.4 times faster for every primary workload. AVX-512 adds nothing with
  Clang and loses the gain with GCC. FMA adds about 2–7% and changes the output
  in the last bits (up to 1.5e-5 absolute on a 51-peak render, 3e-7 relative).
- **The rest is noise.** LTO, PGO, unrolling, `-fno-math-errno`,
  `-ffast-math` and v2 stay within ±5%. The hot loops call no library
  functions and cross no file boundaries that LTO could remove.
- **`-ffast-math` is unsafe here.** Its `-ffinite-math-only` lets the compiler
  remove `std::isfinite`, so the Clang build failed six test suites (engine,
  host, state, I/O, CLI and inspection): non-finite input, filters and state
  were accepted. With `-fno-finite-math-only` added (GCC) the tests pass, but it
  gives no speedup.
- **Clang was up to 20% faster than GCC** on these workloads (median 1.1× at
  `-O3` and with the AVX2 kernels below, for both FFT libraries).
- `-march=native` and plain v3 cannot be used for distributed binaries: they
  would fail on older or budget x86-64 CPUs and, for the x86-64 macOS package,
  under Rosetta 2 before macOS 15, which has
  [no AVX2](https://www.macg.co/macos/2024/06/rosetta-2-va-etre-plus-efficace-avec-macos-sequoia-et-cest-important-pour-les-jeux-144245).
  The release check rejects `-march=native` and `/arch:AVX`.

### AVX2 kernels chosen at run time

So that portable binaries get the AVX2 gain, the five functions of the
spectrum processing (`portable/kernels.inc`: split, join, multiply,
multiply-accumulate and the per-path sum over partitions) are compiled twice on
x86-64: once for the baseline and once with `-mavx2 -mfma -ffp-contract=off`
(`/arch:AVX2` with MSVC, which allows FMA). In the AVX2 copy the multiply and
multiply-accumulate use explicit FMA intrinsics, per bin
`re += fma(xr, hr, -(xi·hi))` and `im += fma(xr, hi, xi·hr)`, in the vector
loop and the scalar remainder alike. Neither copy is contracted implicitly,
even if `-march` flags are added (`-ffp-contract=off`, and pragmas for clang,
clang-cl and MSVC), so the baseline stays unfused and each set's results
depend only on the CPU's set, not on the compiler: GCC 13 and Clang 18 gave
identical divergence figures below. `kernels()` checks once that the CPU has
AVX2 and FMA and that the operating system saves the 256-bit registers;
otherwise the baseline set is used. Each engine takes the chosen set when it is
built. `-DCONVOLVER_AVX2_KERNELS=OFF` leaves the AVX2 copy out;
`convolver_benchmark` prints which set ran (`AVX2+FMA` or `baseline`).

Until 24 October 2026 the AVX2 set had no FMA and gave bit-identical output to
the baseline (the first table below). jrp then chose FMA ("add FMA, but keep
an eye out for significant output divergences"; owner's decision, 24 October
2026), so **output now differs between CPUs with and without AVX2+FMA at
float-rounding level**. The accumulation stays a separate add: fusing both
products into the running sum (`re = fma(-xi, hi, fma(xr, hr, re))`) rounds the
sum twice per partition, and on a 64k-tap sine render was 1.31 times the
baseline's RMS error against the double oracle (geometric mean of 15
frequencies, worst 1.67); the form used is 1.00.

#### Divergence

`native.kernel_divergence` renders each workload with both sets at B = 256 and
compares them sample by sample, and both with a direct double-precision
convolution at 256 frames per output where the whole filter is engaged. The
filters are synthetic room corrections (a central peak in decaying noise) or,
for the crossover, Blackman-windowed bands at 10–250–2500–24k Hz. The input is
scaled so that the full-level signal's output peaks at -0.1 dBFS; the quiet
inputs are that level less 120 dB, or 1e-36 of it (output near `FLT_MIN`).
Measured 24 October 2026 with the Ooura FFT, Release builds, GCC 13.3 and
Clang 18.1.3 giving identical figures; "relative" is to the output's peak, dBFS
to full scale (1.0):

| Workload | FMA vs baseline, max abs | relative | dBFS | baseline vs oracle | FMA vs oracle |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64k stereo, white noise | 1.8e-7 | 1.8e-7 | -135.0 | 2.8e-7 | 2.6e-7 |
| 64k stereo, full-scale 997 Hz sine | 4.8e-7 | 4.8e-7 | -126.4 | 8.1e-7 | 8.7e-7 |
| 64k stereo, noise at -120 dB | 1.5e-13 | 1.5e-7 | -256.5 | 2.2e-13 | 2.2e-13 |
| 64k stereo, noise near `FLT_MIN` | 0 | 0 | | 1.2e-42 | 1.2e-42 |
| 64k 2→6 crossover, white noise | 2.4e-7 | 2.4e-7 | -132.5 | 3.6e-7 | 3.6e-7 |
| 128k stereo, white noise | 1.8e-7 | 1.8e-7 | -135.0 | 3.0e-7 | 3.5e-7 |
| 256k stereo, white noise | 1.8e-7 | 1.8e-7 | -135.0 | 3.6e-7 | 3.9e-7 |
| 512k stereo, white noise | 2.1e-7 | 2.1e-7 | -133.6 | 4.4e-7 | 4.6e-7 |
| 512k stereo, full-scale sine | 7.5e-7 | 7.5e-7 | -122.6 | 2.4e-6 | 2.4e-6 |

The two sets differ by 1.8e-7 to 7.5e-7 of the output's peak (3 to 12 float
rounding steps of the largest samples), the same size as each set's own error
against the oracle, and at most -122.6 dBFS. That is 1.5 to 6 steps of 24-bit output
(a 24-bit step is 2^-23 ≈ 1.2e-7 of full scale, -138.5 dBFS), so 24-bit
renders can differ in their lowest bits between CPUs with and without
AVX2+FMA; 16-bit output (step 3.1e-5) differs only where a sample lies that
close to a rounding boundary. It is below the noise of any converter, so not
audible. For scale, changing the block size from 256 to 512 with the baseline
set changes a 64k sine render by as much (up to 1.5e-6 at some frequencies,
-116.5 dBFS). The test fails if the sets differ by more than 1e-6 of the peak
or -120 dBFS, if either leaves the oracle tolerance, or if the FMA set's
oracle error exceeds 1.5 times the baseline's; those limits are not to be
widened without investigating the cause. In Debug builds it runs the 64k
workloads only. `native.engine` also checks every kernel against the standard
floating-point error bound of a double-precision sum, on random values of
every magnitude.

#### Speed

Speedup of the AVX2 kernels **without FMA** (the set before 24 October) over
the same `-O3` build without them (`-DCONVOLVER_AVX2_KERNELS=OFF`), median of
5 rounds, on the 4-vCPU VM described above; above 1 is faster:

| | 32k stereo | 64k stereo | 128k stereo | 64k true stereo | 64k crossover | 32k crossover | 16k crossover | 4k stereo | 512k reverb |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Clang, FFTW | 1.35 | 1.25 | 1.33 | 1.44 | 1.55 | 1.37 | 1.30 | 1.11 | 1.37 |
| Clang, Ooura | 1.31 | 1.11 | 1.20 | 1.15 | 1.18 | 1.26 | 1.16 | 1.08 | 1.33 |
| GCC, FFTW | 1.43 | 1.38 | 1.43 | 1.41 | 1.44 | 1.47 | 1.36 | 1.14 | 1.39 |
| GCC, Ooura | 1.20 | 1.25 | 1.32 | 1.30 | 1.23 | 1.18 | 1.19 | 0.98 | 1.32 |

Over the six primary workloads (the first six columns) that is 1.38 (Clang,
FFTW), 1.20 (Clang, Ooura), 1.43 (GCC, FFTW) and 1.25 (GCC, Ooura) as a
geometric mean. Without the kernels a 64k stereo filter took 410–475 ns per
frame (about 2.0–2.3% of one core at 48 kHz) and a 64k 2→6 crossover
1090–1380 ns (about 5–7%). The 99th percentile block time fell by similar
amounts (Clang, Ooura: 184 to 150 µs for 64k stereo, 419 to 360 µs for the 64k
crossover). The same runs built the whole engine for v3 with FMA and without
the kernels: 1.42, 1.32, 1.42 and 1.34 times the baseline (same order), no
faster than the kernels with FFTW and 7–10% faster with Ooura.

Speedup **with FMA** (the kernels as now), measured on 24 October 2026 on a
different, smaller VM: 2 vCPUs of an Intel Xeon at 2.70 GHz (AVX2, FMA and
AVX-512), 7 GiB, Ubuntu 24.04, the same compilers, the Ooura FFT only (FFTW was
not installed there), another session sharing the machine. Three builds ran in
a shuffled order per case, pinned to one core, 15 rounds of 0.5 s: without the
kernels, with the earlier AVX2 kernels without FMA (commit 356a707) and with
the AVX2+FMA kernels. Speedup over the build without the kernels, median of
the 15 rounds:

| | 32k stereo | 64k stereo | 128k stereo | 64k true stereo | 64k crossover | 32k crossover | 16k crossover | 4k stereo | 512k reverb |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Clang, Ooura, AVX2 | 1.08 | 1.36 | 1.33 | 1.35 | 1.28 | 1.31 | 1.08 | 1.02 | 1.19 |
| Clang, Ooura, AVX2+FMA | 1.10 | 1.47 | 1.27 | 1.44 | 1.34 | 1.32 | 1.09 | 1.03 | 1.39 |
| GCC, Ooura, AVX2 | 1.10 | 1.33 | 1.47 | 1.44 | 1.44 | 1.26 | 1.08 | 1.01 | 1.36 |
| GCC, Ooura, AVX2+FMA | 1.16 | 1.43 | 1.62 | 1.52 | 1.51 | 1.33 | 1.13 | 1.04 | 1.43 |

Over the six primary workloads that is 1.32 (Clang) and 1.42 (GCC) for
AVX2+FMA, against 1.28 and 1.33 without FMA on the same machine: FMA itself is
worth 1.03 (Clang) and 1.06 (GCC), consistent with the 2–7% the whole-engine v3
builds showed on the first VM. The 99th percentile block time with Clang went
from 139 µs (no kernels) through 107 (AVX2) to 103 µs (AVX2+FMA) for 64k
stereo, and from 396 through 317 to 302 µs for the 64k crossover. FMA with FFTW
was not re-measured; on the first VM the whole-engine v3 build with FMA was no
faster than the kernels without it there. The two VMs' absolute times and
per-case speedups differ (the second machine's baseline was faster for 32k,
slower for 64k), so compare ratios within one table only.

Not measured here: Apple Silicon (AppleClang, Accelerate), where Clang's
default target is already the M1 and NEON is always used, and MSVC on Windows.
Rerun cases 10 to 18 there before choosing other flags for those builds (PERF-1).

## Reproduce

### Overlap-add and overlap-save

Needs a C++20 compiler and single-precision FFTW (`brew install fftw` on macOS,
`apt-get install libfftw3-dev` on Ubuntu); no CMake build is required:

```sh
sh docs/benchmarks/src/run_overlap_schemes.sh > overlap.csv   # about 1 minute
sh docs/benchmarks/src/run_overlap_schemes.sh --quick         # about 10 s, no 1M-tap cases
```

The script finds FFTW with `pkg-config` or Homebrew, compiles with `-O3 -DNDEBUG`
(override with `CXX` and `CXXFLAGS`), prints the accuracy check and compiler to
stderr and CSV to stdout.

### Building FFTW for this Apple Silicon Mac

NEON is not new in FFTW 3.3.11: earlier releases already support it. Version
3.3.11 adds Apple ARM cycle counters and SVE support, among other changes; NEON
is the SIMD backend enabled in this Mac's verified build. See the
[official release notes](https://fftw.org/release-notes.html). Rendering defaults to
`FFTW_ESTIMATE`; [offline measured planning and persisted wisdom](../use/command-line.md#fftw-plan-tuning)
are now available explicitly.

The benchmark dependency was built from the official source archive with the
following options. To reproduce in an ignored local dependency directory, start
at the Convolver repository root (Xcode command-line tools must be available):

```sh
convolver_root="$PWD"
mkdir -p build-deps
curl -fL https://fftw.org/fftw-3.3.11.tar.gz -o build-deps/fftw-3.3.11.tar.gz
shasum -a 256 build-deps/fftw-3.3.11.tar.gz
# Expected: 5630c24cdeb33b131612f7eb4b1a9934234754f9f388ff8617458d0be6f239a1
tar -xzf build-deps/fftw-3.3.11.tar.gz -C build-deps
mkdir -p build-deps/fftw-neon-build
cd build-deps/fftw-neon-build
../fftw-3.3.11/configure --prefix="$convolver_root/build-deps/fftw-neon" \
  --enable-float --enable-neon --enable-shared --disable-fortran
make -j8
make check
make install
cd "$convolver_root"
cmake -S . -B build-local-neon -DCMAKE_BUILD_TYPE=Release \
  -DCONVOLVER_FFT_BACKEND=FFTW -DCMAKE_DISABLE_FIND_PACKAGE_FFTW3f=ON \
  -DCMAKE_PREFIX_PATH="$convolver_root/build-deps/fftw-neon"
cmake --build build-local-neon --parallel
ctest --test-dir build-local-neon --output-on-failure
./build-local-neon/convolver --version
```

The FFTW package-config lookup is disabled here so an unrelated system CMake
package cannot override the intended configure/make installation. Use a fresh
Convolver build directory; existing cached library paths can select an older
installation. libsndfile must also be installed, or its prefix supplied in the
semicolon-separated `CMAKE_PREFIX_PATH` list.

The tested configure defaults selected `-O3 -mtune=native` (plus the flags listed
above). No `-ffast-math`, SVE or x86 SIMD flags were added. `--enable-float` is
essential: Convolver uses `fftwf_*`, not the double-precision API. This installs
under the repository's ignored `build-deps/` directory without changing system
libraries. The benchmark's existing installation is temporary, under
`/private/tmp/convolver-native-deps-fftw-neon`; use the instructions above for a
durable installation. Convolver's tests passed against that temporary build;
`make check` above additionally runs FFTW's own tests when reproducing.

### Compiler flags

[`src/compiler_flags.py`](../benchmarks/src/compiler_flags.py) builds the core
and benchmark once per flag set and alternates them over the room-correction
cases. The first kernel table came from this, once per compiler and FFT
library (`|baseline` builds without the AVX2 kernels):

```sh
python3 docs/benchmarks/src/compiler_flags.py --compiler clang++ --backend OOURA --cpu 2 \
    --variant 'O3=-O3 -DNDEBUG' --variant 'O3-baseline=-O3 -DNDEBUG|baseline' \
    --variant 'O3-v3-fma=-O3 -march=x86-64-v3 -DNDEBUG|baseline' \
    --work /tmp/convolver-flags --output kernels.csv
```

The FMA table came from this, once per compiler, with the commit before FMA
exported to a folder (`|source=` builds another source tree):

```sh
git archive 356a707 | (mkdir -p /tmp/nofma && tar -x -C /tmp/nofma)
python3 docs/benchmarks/src/compiler_flags.py --compiler clang++ --backend OOURA --cpu 1 \
    --rounds 15 --seconds 0.5 \
    --variant 'O3-baseline=-O3 -DNDEBUG|baseline' \
    --variant 'O3-avx2-nofma=-O3 -DNDEBUG|source=/tmp/nofma' --variant 'O3=-O3 -DNDEBUG' \
    --work /tmp/convolver-fma --output fma-kernels.csv
```

The divergence table is the output of `native.kernel_divergence` in a Release
build (`ctest -R kernel_divergence -V`, or run `convolver_kernel_divergence_tests`).

Without `--variant` it builds `-O2`, `-O3`, LTO, v3 and v3 without FMA.

### Comparing backends

Install CMake, a C++20 compiler, optimized single-precision FFTW and libsndfile.
Use `CMAKE_PREFIX_PATH` for libraries installed outside standard locations.

```sh
cmake -S . -B build-fftw -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_FFT_BACKEND=FFTW -DCONVOLVER_BUILD_BENCHMARKS=ON
cmake -S . -B build-accelerate -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_FFT_BACKEND=ACCELERATE -DCONVOLVER_BUILD_BENCHMARKS=ON
cmake --build build-fftw --parallel
cmake --build build-accelerate --parallel
ctest --test-dir build-fftw --output-on-failure
ctest --test-dir build-accelerate --output-on-failure
python3 tests/native/backend_equivalence.py build-fftw/convolver build-accelerate/convolver
python3 tests/native/benchmark_backends.py build-fftw/convolver_benchmark build-accelerate/convolver_benchmark --rounds 7 --seconds 0.35 --output benchmark.csv
```

Repeat correctness tests in separate Debug build directories with
`-DCONVOLVER_SANITIZERS=ON`. Never benchmark sanitizer builds. Native baseline
sources and dependency archive hashes are recorded in [the portable guide](../use/command-line.md).

For Ooura, configure another directory with `-DCONVOLVER_FFT_BACKEND=OOURA`
and pass that CLI/benchmark as one side of the same pairwise comparison.
Compare each backend with an independent oracle as well as with its peers.
