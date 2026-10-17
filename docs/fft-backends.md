# FFT backends and measurements

Measured 18 October 2026 on Apple M5 Max, 128 GiB RAM, macOS 27.0.1 (26A434).

## Selection and contract

`-DCONVOLVER_FFT_BACKEND=FFTW` is the default and portable choice.
`-DCONVOLVER_FFT_BACKEND=ACCELERATE` selects Apple's system vDSP framework and
requires an Apple target. Accelerate builds do not discover or link FFTW. Each
binary has one backend; there is no automatic or runtime backend switching.
`convolver --version` reports the backend. The library exposes
`fft_backend_name()` and `fft_backend_version()`.

The internal `portable/fft.hpp` interface owns prepared plans and buffers. Both
implementations expose interleaved complex bins 0..N/2 with separate DC/Nyquist,
an unscaled forward DFT, and an inverse scaled by N. The convolution engine
normalizes filter spectra once by 1/N. Accelerate's adapter handles the packed
DC/Nyquist and factor-of-two forward scaling described in
[Apple's real FFT guide](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/vDSP_Programming_Guide/UsingFourierTransforms/UsingFourierTransforms.html).

FFTW uses N=2B. Accelerate uses the next power of two at least 2B (minimum four),
while keeping the requested B-frame latency, partition payload, delays and tail.
It does not silently fall back to FFTW for awkward sizes. Extra padding consumes
more spectrum/history storage; validation accounts for that before allocation.
Planning/allocation remain outside processing. Neither backend changes routing,
configuration import, sample formats, or the overlap-add scheduling algorithm.

## Correctness evidence

Both implementations passed **14/14 CTest suites** in Debug with ASan/UBSan and
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

- [Release, FFTW NEON vs Accelerate](benchmarks/2026-10-18-release-equivalence.json):
  maximum absolute error **7.45e-7**, RMS error **8.19e-8**.
- [Debug sanitizer comparison](benchmarks/2026-10-18-debug-equivalence.json):
  maximum absolute error **8.34e-7**, RMS error **8.11e-8**. This used the original
  scalar FFTW test dependency; the optimized Release comparison used NEON.

These are numerically equivalent within the tested tolerance, not bit-identical.
The macOS CI workflow builds both and runs the paired comparison. The results
above are local measurements; platform CI results are recorded separately in
[GitHub Actions](https://github.com/jrp2014/convolver/actions/workflows/regression.yml).
The preceding FFTW implementation passed all platforms at `f78c22b`.

## Processing benchmark

This measures the full in-memory Engine processing loop, including routing,
partition accumulation and FFT layout conversion. It excludes file I/O, CLI
startup and filter preparation. It is not an isolated FFT microbenchmark.

Both Convolver builds use AppleClang 21.0.0, `-O3 -DNDEBUG`, no fast-math or
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

[Raw measurements](benchmarks/2026-10-18-m5-max.csv) preserve every repetition,
setup time, processed-frame count, checksum and separately sampled p99 block time.
The p99 measurements are descriptive samples of 512 calls, not deadline guarantees.

### FFTW implementation and planning comparisons

On the same M5 Max, seven alternating rounds of at least 0.2 seconds per case
compared scalar and NEON builds of FFTW 3.3.11 with the same configure-selected
compiler flags. NEON improved full-engine throughput by approximately 2–24%
across these eight workloads. [Scalar/NEON measurements](benchmarks/2026-10-18-fftw-scalar-neon.csv).
This supports keeping the native NEON build as the Mac baseline; it does not
establish an optimal FFT implementation for every machine or workload.

A separate five-round, 0.2-second comparison changed only FFTW planning from
ESTIMATE to MEASURE in a temporary backend source copy, using the same optimized
engine and benchmark. Several workloads gained about 2–6%, while the others
changed little; measured setup medians ranged approximately 4–26 ms.
[ESTIMATE/MEASURE measurements](benchmarks/2026-10-18-fftw-estimate-measure.csv).
These numbers precede the new public planning API; the temporary measured build
also passed the native engine oracle. They are a planning experiment, not a
benchmark of wisdom loading or patient planning. Saved wisdom support is now
tested independently through fresh CLI processes, and FFT/engine oracles run
estimate, measure and patient plans. See [the tuning workflow](portable-core.md#fftw-plan-tuning).

## Reproduce

### Building FFTW for this Apple Silicon Mac

NEON is not new in FFTW 3.3.11: earlier releases already support it. Version
3.3.11 adds Apple ARM cycle counters and SVE support, among other changes; NEON
is the SIMD backend enabled in this Mac's verified build. See the
[official release notes](https://fftw.org/release-notes.html). Rendering defaults to
`FFTW_ESTIMATE`; [offline measured planning and persisted wisdom](portable-core.md#fftw-plan-tuning)
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

### Comparing both backends

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
sources and dependency archive hashes are recorded in [the portable guide](portable-core.md).
