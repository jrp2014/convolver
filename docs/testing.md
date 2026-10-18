# Tests and their boundaries

## Native production tests

The [portable build](portable-core.md) now compiles production C++20 translation
units against the selected real FFT library and libsndfile. Build before running CTest:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Debug -DCONVOLVER_SANITIZERS=ON
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
```

With Clang on macOS/Linux this runs **18 suites**: eight native suites, one package-consumer suite and the
nine historical/documentation suites below. GCC and MSVC default to ten
native/package/documentation suites; the old extraction adapters require Clang.

- `native.fft`: independent direct DFT, canonical spectrum layout, DC/Nyquist,
  forward/inverse normalization and padded lengths.
- `native.engine`: independent double time-domain oracle, real FFT backend, seeded
  matrix routing/delays, awkward blocks and IR lengths, normalization, exact
  latency/tail, chunk invariance, reset, multiple instances, invalid spans,
  nonfinite data and a C++ heap-allocation guard during processing/reset.
- `native.io`: real libsndfile float/PCM16/24/32 round trips and clipping,
  legacy parser/config lists, Windows/POSIX/mixed relative paths, signed zero, malformed input,
  truncation and RAW alignment. POSIX-specific tests also exercise fragmented
  descriptors, borrowed descriptor ownership, broken pipes and injected WAV
  header-finalization errors.
- `native.cli`: subprocess file renders compared with independent expected
  output; empty, exact and partial last blocks, gain/matrix routing, both latency
  modes, real RAW pipelines, and protection against overwriting inputs/IRs/configs.
- `native.fft_planning`: measured/patient planning, saved wisdom reused in fresh
  CLI processes against direct convolution, missing-plan failure before output
  creation, malformed/oversized caches, Unicode filenames and source protection;
  Accelerate rejects FFTW-only options. FFT and engine oracle suites also exercise
  measured/patient plans and cached matrix processing with FFTW.

- `native.host`: planar/in-place float and double buffers, invalid samples,
  bypass/reset, concurrent and queued filter replacements, with a callback heap guard.
- `native.state`: fixed version-1 wire fixture, embedded configuration round trips, truncation, corrupt headers,
  invalid coefficients and bounded decoding.
- `native.utilities`: little-endian byte order and bounds, exclusive binary file
  creation, Unicode paths, link/overwrite protection and competing creators.

The optional [VST3 build](vst3.md) adds three tests: actual SDK-interface lifecycle
and audio tests, embedded preset generation/loading after deleting the source IR,
plus the SDK validator loading the built plug-in. These cover preactivation tail
queries, silent-input tails, automation offsets and failed state restore rollback.
They do not establish compatibility in a particular commercial DAW.

`packaging.consumer` installs and relocates the package, builds and runs an
external core/host/I/O consumer, and verifies components and source-tree use with and
without a parent CTest setup. Core-only configuration runs six C++/package
suites without Python or libsndfile. See [package tests](cmake-package.md#what-verifies-the-package).

`backend_equivalence.py` separately compares both actual CLIs over 16 matrix/delay
cases, including odd block sizes and both latency modes. See
[measurements and reproduction commands](fft-backends.md). The
[current validation record](review-2026-10/current-validation.md) pins fresh local
results and Linux/Windows/macOS CI evidence to source baseline `5df874e`.
Consult the [native workflow](../.github/workflows/regression.yml) and
[VST3 workflow](../.github/workflows/vst3.yml) for the configured matrix; a workflow
file alone is not evidence that a job passed.

## Run with CMake/CTest

Requirements: CMake 3.20+, Python 3.8+, Clang/AppleClang with AddressSanitizer and
UndefinedBehaviorSanitizer, and a native macOS or Linux environment. No Python
third-party packages, old Windows SDKs, `homes.zip`, or network access are needed
to run these tests once the toolchain is installed.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCONVOLVER_BUILD_NATIVE=OFF
ctest --test-dir build --output-on-failure --parallel 2
```

Alternatively, `cmake --build build --target check` runs the same suite.
Each C++ regression script compiles its own temporary harness during the test;
these extraction tests are separate from the native build targets. CMake checks
the compiler and sanitizer link support during configuration. CTest runs the
scripts using that selected compiler, overriding an inherited `CXX` variable.
Pass compiler options through the harnesses when changing them deliberately;
these scripts do not inherit arbitrary CMake build-type or compiler flags.

To run just the DSP tests or the documentation check:

```sh
ctest --test-dir build --output-on-failure -L dsp
ctest --test-dir build --output-on-failure -L docs
```

The direct runner remains available:

```sh
python3 tests/regression/run_all.py
```

It uses `clang++` by default; `CXX` may name a Clang executable (not a command
with flags). For documentation only, run
`python3 tests/regression/documentation_regressions.py`; no compiler is needed.

The GitHub Actions workflow runs the CMake/CTest entry point on macOS and Linux.
Adding a workflow file does not establish that its remote jobs have passed.

## Current coverage

The tests below use the canonical `convolver/` source tree. Earlier local
`tests/*.py` experiments aimed at a separate source snapshot are not the gate.

| Suite | What it establishes | Boundary |
| --- | --- | --- |
| Core | 444 checks against independent double time-domain convolution; partition padding, routing/delays, chunks/reset, complex arithmetic | Actual DSP bodies with portable DFT and platform/file stubs; not real FFTW |
| Host adapters | 44 debug/release cases; narrow PCM, aliases, additive/double output, disconnected inputs/delay history | Actual extracted sample adapters; Boost conversion boundary substituted; not a loaded plug-in |
| Gain | Two unequal route-count cases | Extracted gain routines |
| Branch regressions | Route signs, list display, dither constants, bundled Ooura full/tail round trips | Serial Ooura arithmetic, not the full alternate-backend engine |
| Historical diagnostics | Eight stream-state cases | Extracted diagnostic clauses, not complete parser fuzzing |
| Historical I/O | 32 debug/release cases plus legacy syntax/disabled-library checks | Actual wrapper/table code with mocked libsndfile; no codec or real file-descriptor integration |
| Pipe | 274 argument, failure and EOF accounting checks | Actual entry point/helpers with stream/engine stubs |
| Pipe DSP | 100 end-of-stream cases against direct convolution | Actual convolution bodies with DFT adapter; not real stdin/codecs |
| Documentation | 99 archived file sizes/hashes, 18-page inventory, local links/fragments, saved audit consistency | Offline preservation check; does not re-fetch the live website or external URLs |

All eight C++ suites enable ASan and UBSan with sanitizer failures fatal.
Some extraction adapters repair old compiler syntax or replace platform
boundaries. These are useful regression checks for reviewed behaviour, not proof
that unmodified production translation units compile on a modern toolchain.

The documentation checker also exercises seven failure cases on temporary
fixtures: changed bytes, changed size, missing file, missing link target, missing
fragment, and escaping manifest/link paths. Original archived bytes remain
unchanged. The independently enumerated pages and saved manifest detect loss;
the live-site comparison is a dated provenance record, not a network CI test.

## Gates for a native release

The initial native core now supplies normally compiled production tests with
the selected real FFT backend and libsndfile. Add every future backend to the same contract tests. Retain the
independent direct-convolution oracle rather than relying only on agreement
between FFT implementations.

Outstanding legacy cases include the original full-width PCM32 specialization,
nonzero noise shaping, old CMD EOF/tail handling, actual host format negotiation and latency/tail reporting,
malformed configuration/IR fuzzing, multi-instance/filter-switch behaviour, and
callback deadline measurements. Native PCM32 endpoints and CLI EOF are covered;
that does not fix the independent historical wrappers. The portable CTest gate does not
establish native Windows/ATL/DirectShow/VST compatibility or real-time safety.

The [current review](../REVIEW.md) separates implemented native behavior from
remaining legacy and release gaps. The [modernization plan](modernization.md)
prioritizes future compatibility and performance evidence. Only retire extraction
harnesses once their failure cases have equivalent production coverage.

## Plug-in candidate checks

The read-only candidate workflow builds native preview archives on Windows x64,
Linux x64 and both macOS architectures. It checks runtime dependency closure,
source inventories, final ZIP relocation, executable quickstart examples and
real SDK validation. Windows also compiles a fixture installer and exercises
changed-directory upgrades and locked-file uninstall recovery. Linux rebuilds
the supplied source with networking disabled. See the
[qualification record](releases/developer-preview.md) for external gates.

Local macOS builds may use `CONVOLVER_VST3_RUNTIME_VALIDATION=OFF` when system
policy rejects ad-hoc code. These runs retain signature-integrity and standalone
adapter checks but omit all dynamic SDK bundle loading; they are not release
qualification. Never bypass system security to turn those checks green.
