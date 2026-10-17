# Portable Convolver: structure and next milestones

Review of the portable implementation, 18 October 2026, accompanying the FFT
abstraction, Accelerate, measurements and wisdom support. This is the next development
plan, not a claim that the proposed features have been implemented.

## Current boundaries

| Component | Files | Responsibility |
| --- | --- | --- |
| Public DSP API | `include/convolver/engine.hpp` | In-memory routes, coefficients, latency/tail, reset and processing; FFT preparation options |
| DSP implementation | `portable/engine.cpp` | Validation, partitioned overlap-add, history, delays and routing |
| FFT adapters | `portable/fft.hpp`, `fft_fftw.cpp`, `fft_accelerate.cpp` | Prepared transforms, buffers and normalization contract; build selects one backend |
| File/configuration adapter | `include/convolver/io.hpp`, `portable/io.cpp`, `portable/config.cpp` | libsndfile, historical config/list import and IR loading |
| CLI | `portable/main.cpp` | Arguments, safe file selection, render/drain, RAW streams and offline FFT tuning |
| Tests/measurements | `tests/native/`, `tests/regression/`, `docs/benchmarks/` | Real libraries and independent oracles; historical checks; performance evidence |
| Preserved implementation | `convolver/`, `docs/historical/` | Historical hosts, sources and documentation with provenance |

Keep these boundaries. The implementation is small enough that a wholesale file
move would add churn without resolving its main problems. Make further splits
when responsibilities grow: CLI argument parsing, preparation, and new host
adapters are natural seams. The canonical spectrum layout is currently private;
retain that freedom to change it without breaking callers.

FFTW 3.3.11 with NEON is already a working native Apple Silicon baseline.
Accelerate provides an independent Apple implementation. Keep libsndfile in the
I/O adapter. Additional FFT libraries, SIMD variants and device backends should
have a specific deployment or measured performance reason.

## 1. Make the core independently buildable and consumable

**Observed gap:** target dependencies separate DSP from libsndfile, but the single
`CONVOLVER_BUILD_NATIVE` option still configures core, I/O and CLI together. A core
consumer must discover libsndfile even when it supplies coefficients in memory.
The install copies headers/libraries but supplies no `find_package(Convolver)`
package. There is no portable API version, shared-library export policy, preset
file or clean downstream-consumer test.

Next change:

- Separate core, I/O, CLI, tests and benchmarks as build choices with explicit
  prerequisites. Core-only builds must not search for libsndfile or require Python.
- Export namespaced CMake targets, initially `Convolver::core` and
  `Convolver::io`. Support both `add_subdirectory` and installed consumption.
  Recreate dependency targets in the package config; do not bake developer paths
  into exported interfaces. Follow the [CMake package/export guide](https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html).
- Define the portable library's version/API policy separately from recovered
  historical version numbers. Start with an explicitly evolving C++ API; defer a
  stable C ABI until an actual consumer needs it. Test static linking first and
  explicitly handle Windows symbol exports before claiming shared-library support.
- Add [CMake presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
  compatible with the project's stated minimum, for release, sanitizer and backend
  configurations. Keep local dependency paths in ignored user presets.
- Pin dependency baselines for release artifacts, record compiler/architecture
  and linked versions, and add a short contribution guide. Keep historical source
  and documentation inventories unchanged.

**Acceptance:** a tiny external application builds and runs an impulse test using
only the installed core on Linux, Windows and macOS; also test subproject use,
core-only configuration, both Apple backends, and a relocated installation with
its dependencies available. Test the installed CLI and runtime dependency lookup
separately. Passing source-tree tests does not establish package usability.

## 2. Make configuration compatibility an explicit product feature

**Observed gap:** `load_configuration` parses text, selects list entries and
decodes IRs in one operation. There is no inspection/validation command or public
description of the selected graph. The historical channel mask is retained but
not written as WAV speaker metadata. Existing parser tests do not constitute a
downstream compatibility corpus.

Separate parsed configuration and resolved assets internally as inspection needs
grow; do not introduce a second grammar. Add a validation/inspection CLI that
reports selected files, rate, routes, channel count, B-frame algorithmic latency,
tail and estimated prepared storage. Preserve relative paths, signed zero,
fractional route weights, delay truncation and list-selection behavior.

Publish small generated IRs/configurations with direct-convolution reference
outputs. Label downstream profiles individually: format support in Roon/JRiver
does not prove shared DSP code or identical list/path semantics. Record sources
and verified limitations in [the downstream assessment](modernization.md#documentation-and-downstream-compatibility).
Treat RF64 output, speaker masks, dither and optional offline resampling as
separate compatibility features with focused file tests.

**Acceptance:** preserved historical examples and negative cases run offline;
validation gives useful file/line errors; changing syntax requires a versioned
format decision. Decoder/parser fuzzing has bounded time/memory and sanitizer
coverage. The archival documentation stays byte-identical.

## 3. Optimize redundant work before expanding the backend list

**Observed opportunity:** `Engine::Impl::compute()` performs a forward FFT,
stores a spectrum history and performs an inverse FFT for every path. Matrix
paths can share identical input mixes. Identical mixes currently repeat both
transform and history work. The complex multiply/accumulate loop is scalar C++;
its generated vectorization and memory cost have not been profiled here.

First benchmark and implement forward-transform/history reuse for *identical*
ordered input mixes, retaining per-path filters and output behavior. Compare
mono, diagonal and dense matrices: an optimization that only benefits dense
matrices must not impose a large cost on simple stereo. Avoid canonicalizing
route order in the first change because it changes floating-point summation.

Then profile partition MACs and memory traffic. Consider aligned spectra, loop
layout, and backend-native packing/MAC only when measurements support them.
Output-spectrum accumulation can reduce inverse transforms, but requires a
separate review of routing, overlap, delay and summation semantics. Introduce
nonuniform partitioning later, for demonstrated long-IR/low-latency workloads.

**Acceptance for each optimization:** unchanged API/latency/tail, direct-double
oracle and cross-backend tests, cancellation-heavy and long-decay cases, no new
processing allocations, and published setup/memory/throughput/callback-tail
measurements. Benchmark optimized binaries on a quiet machine. Keep FFTW
single-threaded initially and avoid blanket fast-math. Saved FFTW wisdom tunes
plans; it does not select Convolver's block size or partition schedule.

## 4. Establish a host-ready lifecycle before adding plug-ins

**Observed gap:** the engine prepares data in its constructor and has reset and
arbitrary-chunk processing, but has no filter replacement, crossfade, separate
immutable prepared-filter object, or plug-in host adapter. Its checked process
entry point can throw on invalid spans/nonfinite input. C++ heap checks and
average benchmark throughput do not establish real-time safety.

When a host requires it, separate prepared filter data from mutable stream state;
prepare replacements off-thread, align latency, warm/crossfade deliberately and
retire old objects off-thread. Choose planar/in-place adaptation from the host's
needs while retaining the tested interleaved CLI boundary. Define invalid-input,
bypass, reset, seek, bus-change, denormal and concurrent-instance behavior.

**Acceptance:** allocation/lock and lifetime checks on the callback path,
transition stress tests, deadline distributions and host integration tests.
Then add a VST3 or CLAP adapter according to intended hosts. Keep device management
and UI outside the DSP library.

## Recommended delivery order

Deliver milestone 1 next: independent build choices, package exports, presets,
consumer tests and contribution instructions. Follow with configuration
inspection/compatibility fixtures, then one measured transform-reuse change.
Keep the native numerical, real-file, historical and documentation gates through
each step. Add a modern host only after its lifecycle contract is exercised.

The broader alternatives remain in [the modernization assessment](modernization.md);
current build and test behavior is documented in [the portable guide](portable-core.md)
and [test coverage](testing.md).
