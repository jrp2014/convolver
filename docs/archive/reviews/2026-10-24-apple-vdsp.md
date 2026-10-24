<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Apple Silicon vDSP accumulation experiment — 24 October 2026

PERF-1 experiment requested by jrp. Base revision:
`884c27e4db9f1630841fc96b9cd1e6c1ed5daadc`.

## Finding

Keep the current compiler-vectorised kernels. This experiment establishes no
substantial whole-engine speed improvement from vDSP, and the candidate fails
existing numerical-divergence gates. The experiment is opt-in, off by default,
and is not a proposal to relax tolerances or change shipped behaviour.

The candidate uses `vDSP_zvmul` without conjugation for the first partition and
`vDSP_zvma` for later partitions, accumulating in place. It uses the existing
split real/imaginary arrays directly. Spectrum conversions, FFTs, filter scaling,
partition order and the rest of the engine are unchanged.

## Environment and method

- Apple M5 Max, native arm64; macOS 27.0.1 (26A434).
- AppleClang 21.0.0 (clang-2100.3.34.2); CMake 3.31.6.
- Release: `-O3 -DNDEBUG -std=c++20`, no fast-math, LTO or architecture override.
- Accelerate FFT in both variants. The baseline already uses compiler-generated
  NEON/FMA; this is not a comparison against scalar arithmetic.
- One executable contains both sets. `--baseline` selects the original set
  before constructing the engine; the runner checks the reported kernel name.
- Seven pairs per case, alternating baseline/vDSP and vDSP/baseline by round;
  workload order is deterministically shuffled each round. Each throughput
  measurement lasts at least 0.4 seconds. All samples are retained.
- Engine: all 19 existing workloads at 48 kHz, including shared input mixes,
  matrices, crossovers, odd blocks, and 4k–512k room filters. The existing
  benchmark warms the full history, then measures throughput and separately
  samples 4,096 block durations. Setup is excluded from throughput.
- Kernel: seven bin/partition sizes using the actual production `accumulate`
  function, rotating history heads, with 128 warm-up calls. Allocation is
  outside timing. No FFT or layout conversion occurs in this measurement.
- No concurrent builds, tests or linters during measurements. This is an
  ordinary desktop session, not a controlled real-time qualification. CPU
  affinity was not pinned. `pmset` reported AC power, but thermal/performance
  warning queries were unavailable. Small differences should not be
  generalised to other Apple processors or OS versions.

Tables give median times. Change is the median of the seven *paired*
`100 * (vDSP / baseline - 1)` percentages; positive means vDSP takes longer.
The range is the minimum/maximum paired change, not a confidence interval.
The CSVs also retain preparation time, callback p50/p99/max and RSS for engine
runs; these desktop callback samples do not establish a worst-case deadline.

## Whole-engine measurements

[All raw engine samples](../../benchmarks/2026-10-24-apple-vdsp-engine.csv).

| Workload | Current ns/frame | vDSP ns/frame | Paired change | Paired range |
| --- | ---: | ---: | ---: | ---: |
| stereo_odd_size | 49.68 | 49.67 | -0.0% | -2.4% to +0.6% |
| room_reverb_512k | 909.26 | 933.99 | +2.5% | -2.4% to +3.7% |
| room_stereo_32k | 82.38 | 83.21 | +0.7% | -0.7% to +1.7% |
| stereo_low_latency | 63.95 | 63.33 | -1.1% | -1.4% to -0.5% |
| stereo_long | 147.61 | 152.97 | +3.6% | +0.8% to +4.4% |
| room_stereo_128k | 268.88 | 275.40 | +2.3% | +1.9% to +3.5% |
| mono_short | 5.53 | 5.66 | +2.5% | +1.2% to +3.3% |
| matrix6_long | 745.51 | 751.85 | +1.0% | +0.2% to +1.2% |
| mono_room | 10.91 | 11.05 | +1.5% | +1.0% to +4.3% |
| matrix6_short | 119.38 | 123.34 | +3.3% | +1.6% to +4.0% |
| room_true_stereo_64k | 279.52 | 283.92 | +1.6% | +0.9% to +2.3% |
| room_crossover6_32k | 231.28 | 233.47 | +0.8% | +0.5% to +1.1% |
| room_crossover6_16k | 133.93 | 134.65 | +0.7% | +0.2% to +1.2% |
| stereo_large_block | 44.01 | 44.41 | +0.9% | -0.0% to +1.4% |
| room_stereo_64k | 147.17 | 152.14 | +3.2% | +2.1% to +4.5% |
| matrix_long | 278.17 | 283.83 | +1.8% | +0.9% to +3.1% |
| room_stereo_4k | 23.38 | 23.39 | -0.3% | -1.7% to +0.9% |
| room_crossover6_64k | 413.33 | 420.69 | +1.9% | +0.2% to +5.9% |
| matrix_short | 35.77 | 36.67 | +3.0% | -2.5% to +3.2% |

## Partition-kernel measurements

[All raw kernel samples](../../benchmarks/2026-10-24-apple-vdsp-kernel.csv).
These times are for one complete partition sum, not one complex multiplication.

| Bins | Partitions | Current ns/sum | vDSP ns/sum | Paired change | Paired range |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 65 | 4 | 36.4 | 46.7 | +27.1% | +25.8% to +43.3% |
| 257 | 512 | 27446.7 | 27384.1 | -0.3% | -1.8% to +0.1% |
| 257 | 16 | 572.1 | 622.7 | +9.2% | +4.8% to +33.4% |
| 257 | 2048 | 110303.7 | 111307.3 | +1.9% | +0.5% to +3.8% |
| 1025 | 64 | 13477.9 | 13453.4 | -0.2% | -2.2% to +0.2% |
| 257 | 256 | 13925.6 | 13930.2 | -0.3% | -0.5% to +3.0% |
| 129 | 128 | 3516.6 | 3501.7 | -0.3% | -1.0% to +1.9% |

## Correctness and validation boundary

- Unmodified baseline, Debug ASan/UBSan: **36/36 CTest tests passed**.
- Candidate, Debug ASan/UBSan: **35/36 passed**.
- Candidate, Release: **35/36 passed**.
- The failure in both configurations is `native.kernel_divergence`. The
  registered test stops at the 64k stereo sine: maximum baseline/candidate
  difference `1.31e-6`, about `1.33e-6` relative to peak and -117.6 dBFS,
  exceeding the existing `1e-6` relative/absolute limits.
- `native.engine`, including the direct oracle and processing-allocation
  checks, passes in both configurations. Its reported worst absolute oracle
  error across the selected and baseline sets is `2.54118e-7`.
- A **separate diagnostic continuation**, not a passing registered test,
  collects all failures using the same computations and thresholds. It finds
  eight failed gates across nine workloads. At 512k taps, the sine difference
  reaches `4.08e-6` (-107.8 dBFS). The largest sampled vDSP-oracle error is
  `2.25e-6` (baseline `2.06e-6` on that case). The 128k and 256k noise cases
  also exceed the permitted 1.5x baseline-oracle error ratio. Quiet and
  near-denormal cases pass their existing gates. No tolerances were changed.
- These are small numerical differences, but the candidate does not meet the
  project's current acceptance limits. Timing it does not qualify it for use.
- Final `pre-commit run --all-files` passed. AppleClang's static analyzer
  reported no findings in `portable/kernels_vdsp.cpp`. clang-tidy was not
  installed locally; this experiment has not completed an independent review
  and remains a draft, not an implementation ready to merge.
- No plug-in SDK, DAW, other FFT backend or other machine was tested here.
  The default CI configuration does not enable this experiment and cannot
  establish its numerical acceptance.

Evidence: [baseline CTest](../../benchmarks/2026-10-24-apple-vdsp-baseline-tests.txt),
[candidate sanitizer CTest](../../benchmarks/2026-10-24-apple-vdsp-candidate-tests.txt),
[candidate Release CTest](../../benchmarks/2026-10-24-apple-vdsp-release-tests.txt),
[engine oracle](../../benchmarks/2026-10-24-apple-vdsp-engine-oracle.txt),
[diagnostic continuation](../../benchmarks/2026-10-24-apple-vdsp-divergence-all.txt).
The inherited divergence output labels say FMA; the selected set here is vDSP.

## Reproduce

Use this experiment branch, a native Apple compiler and the usual libsndfile
development dependency. Add `CMAKE_PREFIX_PATH` if dependencies are outside
standard search locations. The recorded local dependency prefix was
`/private/tmp/convolver-native-deps`.

```sh
cmake -S . -B build/vdsp-release -DCMAKE_BUILD_TYPE=Release \
  -DCONVOLVER_FFT_BACKEND=ACCELERATE -DCONVOLVER_BUILD_BENCHMARKS=ON \
  -DCONVOLVER_EXPERIMENT_VDSP_MAC=ON
cmake --build build/vdsp-release --parallel 6
ctest --test-dir build/vdsp-release --output-on-failure --parallel 4
```

Expect the numerical-divergence failure described above. Repeat correctness
checks in a separate Debug build with `CONVOLVER_SANITIZERS=ON`. To inspect
all divergence cases without changing the registered test:

```sh
python3 docs/benchmarks/src/report_vdsp_divergence.py build/vdsp-release
```

That diagnostic exits nonzero when any gate fails. It compiles a temporary copy
of the existing test that collects failures instead of stopping at the first;
it does not change numerical formulas or limits. To measure, after all other
build/test processes have finished:

```sh
python3 docs/benchmarks/src/run_vdsp.py build/vdsp-release build/vdsp-results \
  --repeats 7 --seconds 0.4
```

The runner checks each process's exit status and selected kernel. Raw files are
`engine.csv` and `kernel.csv`. The default-off build remains the existing
implementation; no FFT-boundary conversion or PFFFT experiment was included.
