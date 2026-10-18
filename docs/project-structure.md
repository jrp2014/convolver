# Portable Convolver: current structure and next milestones

Reviewed 18 October 2026 at `5df874e`, portable **0.2.0**. This replaces the
[early portable structure review](archive/reviews/early-portable-structure.md).
See the [repository review](../REVIEW.md) for correctness findings and evidence.

## Current boundaries

| Component | Files | Responsibility |
| --- | --- | --- |
| Core API | `include/convolver/engine.hpp` | In-memory routes/coefficients, preparation options, processing, reset, latency/tail |
| DSP | `portable/engine.cpp` | Validated uniform overlap-add, split-spectrum MACs, per-path history, delay rings and routing |
| FFT | `portable/fft.hpp`, `portable/fft_fftw.cpp`, `portable/fft_accelerate.cpp` | Prepared real transforms, packing/scaling, private buffers; one backend selected at build time |
| Host and state | `include/convolver/host.hpp`, `portable/host.cpp`, `portable/state.cpp` | Planar/in-place buffers, persistent stream state, bypass, prepared replacement/crossfade, deferred destruction, bounded serialization |
| File/configuration | `include/convolver/io.hpp`, `portable/io.cpp`, `portable/config.cpp` | libsndfile, historical config/list import, portable path handling and IR loading |
| CLI | `portable/main.cpp` | Safe file/RAW rendering, latency policy, full drain and offline FFTW planning/wisdom |
| VST3 | `plugins/vst3/` | SDK processor/controller, bus/state/automation adaptation and embedded-preset tool |
| Build/package | `CMakeLists.txt`, `CMakePresets.json`, `cmake/` | Optional components, dependency discovery, install/export and consumer verification |
| Verification | `tests/native/`, `tests/consumer/`, `tests/vst3/`, `tests/regression/` | Independent references, real libraries, external consumers, SDK and historical extraction tests |
| Preserved implementation | `convolver/`, `docs/historical/`, `docs/review-2026-10/` | Historical hosts, recovered documentation and dated provenance |

Keep platform, SDK and decoding dependencies at their boundaries. A wholesale
file move adds little value now. Split CLI parsing/preparation or configuration
resolution when new features justify it; keep private FFT storage free to evolve.

## Implemented milestones

- **Independent package:** static C++20 `Convolver::core`, `Convolver::host` and
  optional `Convolver::io`; installed and source-tree consumers; relocatable
  package paths; release/sanitizer/core-only/Accelerate presets. Core-only has no
  libsndfile/Python requirement. Version matching is within minor 0.2, not a stable
  C++ ABI promise. See [package contracts](cmake-package.md).
- **Compatibility/tooling:** Windows and mixed relative configuration paths,
  signed-zero routing semantics, real PCM/file tests, `.clang-format`,
  `.clang-tidy` and CI checks. PCM16 output uses a deterministic, seeded
  quantizer with triangular (default in the CLI), historical high-pass
  triangular, rectangular or no dither.
- **FFT selection and optimization:** FFTW and Accelerate behind one private
  contract; offline FFTW planning/wisdom; split real/imaginary spectra and
  vectorizable MAC loops. The [recorded measurements](fft-backends.md) distinguish
  Apple backend/planning experiments from the later x86 split-spectrum results.
- **Host lifecycle and VST3:** persistent processing history, prepared filter
  replacement, warm-up/crossfade, delayed dry bypass and control-thread retirement;
  optional SDK 3.8.1 adapter, embedded presets and actual validator CI. These are
  implemented, with [documented limits](vst3.md), rather than future architecture.

The core's checked interleaved API and the nonthrowing host boundary serve
different callers. Retain that separation. The host layer does not grant freedom
to prepare filters, destroy active processors or invoke control methods from
arbitrary threads; its ownership/thread contract remains part of the API.

## Next compatibility work

PCM16 dither is implemented with tests of quantization error, clipping and
reproducibility that are independent of convolution accuracy. Noise shaping and
PCM24 dither remain separate, explicit choices if they are wanted.

Configuration import still combines parsing, selection and asset decoding; no
inspection/validation command exposes the resolved graph. Add one grammar-backed
inspection path reporting files, sample rate, routes, delays, latency, tail and
estimated storage. Build a small redistributable compatibility corpus with
consumer-specific profiles. Preserve route order and list-selection semantics.
Speaker masks, RF64, optional resampling and wider VST3 layouts each need their
own acceptance tests. Parser/state/IR fuzzing should have bounded resource use.

For VST3, prioritize named Windows/macOS DAW tests and repeatable release bundles
before adding a custom UI or another host API. SDK validation is necessary but
does not exercise every host's scan, state reload, silence, seek and bus behavior.

## Next efficiency work

`Engine::Impl::compute()` still performs forward FFT, spectrum history and inverse
FFT per path. Identical ordered input mixes can share forward transforms/history;
that optimization is not implemented. Preserve mixing order to avoid gratuitous
rounding changes, and measure simple stereo as well as dense matrices.

Inverse-transform sharing needs a separate routing/overlap/delay design. The MAC
loop has already moved to split spectra; do not describe it as an unprofiled
scalar baseline. Further packing, alignment, backend-native MACs or nonuniform
partitioning should follow workload measurements. Saved FFTW wisdom chooses FFT
plans, not Convolver block size or partition scheduling.

Acceptance for each DSP optimization: unchanged public behavior, latency and
finite tail; direct-double oracle and cross-backend tests; cancellation/long-decay
cases; no new processing allocations; setup, storage and callback timing as well
as throughput measurements. Avoid blanket fast-math or machine-specific release
binaries without a deliberate compatibility policy.

## Release and maintenance gates

Keep native production, package-consumer, historical and documentation checks.
Add race/transition stress and deadline-distribution evidence without equating
heap guards or average throughput with hard realtime guarantees. Development
packages are relocatable but still require compatible dependencies; installers,
minimum OS/toolchain policy, dependency pinning/bundling and signing remain work.

The [modernization plan](modernization.md) records the remaining choices;
[testing](testing.md) defines the evidence actually provided today.
