# Tests and their boundaries

[Documentation index](README.md) · [Implementation map](project-structure.md#change-and-test-map)

**In short:**

- `ctest --test-dir BUILD --output-on-failure` runs everything your build
  configured; `ctest --test-dir BUILD -N` lists it. The total depends on the
  enabled options, so do not quote a fixed count.
- The `native.*` tests check the real portable code against independent
  references (a direct double-precision convolution and a direct DFT), real
  libsndfile files and real CLI processes.
- With Clang on macOS or Linux, `regression.*` tests also compile slices of the
  historical Windows source under ASan/UBSan. They preserve old behaviour; they
  are not tests of the portable code.
- Tests and CI workflows describe coverage. Only an actual run on a stated
  revision shows a pass, and no test here proves compatibility with a named DAW
  or hard real-time deadlines.

Use the native tests for changes to the active implementation. Historical
extraction tests are additional preservation checks and are described separately
below. A test list is coverage information; a pass claim needs an actual run.

Contents: [native tests](#native-production-tests),
[historical tests](#historical-tests-run-with-cmakectest) and
[their coverage](#historical-coverage), [limits](#validation-limits-and-release-work),
[documentation checks](#documentation-checks) and [fuzzing](#fuzz-smoke-tests-and-longer-campaigns).

## Native production tests

The [portable build](portable-core.md) compiles production C++20 translation
units against the selected real FFT library and libsndfile. Build before running CTest:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Debug -DCONVOLVER_SANITIZERS=ON
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
```

The default Clang build on macOS/Linux also enables historical extraction tests.
GCC/MSVC builds omit those extraction tests. The core-only configuration omits
file I/O, CLI, Python and historical tests. Optional components change the total;
list the tests in your actual build rather than relying on a copied count:

```sh
ctest --test-dir build-native -N
```

- `native.fft`: independent direct DFT, canonical spectrum layout, DC/Nyquist,
  forward/inverse normalization and padded lengths.
- `native.engine`: independent double time-domain oracle, real FFT backend, seeded
  matrix routing/delays, awkward blocks and IR lengths, normalization, exact
  latency/tail, chunk invariance, reset, multiple instances, invalid spans,
  nonfinite data and a C++ heap-allocation guard during processing/reset.
- `native.io`: real libsndfile float/PCM8/16/24/32 round trips, valid-bit headers, clipping, seeded dither
  and noise-shaping behavior,
  legacy parser/config lists, Windows/POSIX/mixed relative paths, signed zero, malformed input,
  truncation and RAW alignment. POSIX-specific tests also exercise fragmented
  descriptors, borrowed descriptor ownership, broken pipes and injected WAV
  header-finalization errors.
- `native.config_parse`: the private configuration parser on its own: physical
  line/column locations after BOM, comments, blank lines and CRLF, original
  tokens with effective signed-zero/fractional routes and truncated delays, list
  detection and include-chain formatting.
- `native.cli`: subprocess file renders compared with independent expected
  output; empty, exact and partial last blocks, gain/matrix routing, both latency
  modes, real RAW pipelines, and protection against overwriting inputs/IRs/configs.
- `native.inspection`: the inspection API's selection traversal, located and
  chained diagnostics, syntax-only null fields, all-entry validation after
  failures, and agreement of `analyze_configuration` with Engine latency/tail at
  odd block sizes; it also checks that inspection changes no FFTW wisdom and
  creates no files.
- `native.inspection_cli`: `--inspect`/`--validate` modes, exit codes 0/1/2,
  rejected render options, deterministic JSON, escaping of quotes, backslashes,
  tabs and Unicode in names, invalid UTF-8 filenames on Linux, the
  `portable-paths` profile with `--warnings-as-errors`, and diagnosis of every
  bundled historical sample in `convolver/configs/`.
- `compatibility.corpus`: [generated configuration fixtures](../tests/compatibility/README.md)
  rendered at block sizes 1, 3, 17 and 256 in both latency modes against an
  independent direct-convolution reference, then inspected to check the reported
  selection, routes, delays and tail, and the `portable-paths` warnings, against
  the same expectations: routes, signed zero, delays, BOM/CRLF,
  relative/Windows/mixed/UTF-8 paths, list selection and diagnostics.
- `native.limits`: every resource limit accepted at its value and rejected just
  beyond it, using the production constants: 1 MiB per text file, 16 nesting
  levels, 4096 list visits, 16 MiB of configuration text per resolution (shared
  sublists counted per read), 16 Mi frames per delay and 64 Mi delay frames in
  total, 64 Mi decoded IR samples (an oversized file is rejected from its header
  without decoding), 256 diagnostics plus a truncation marker, and 64 MiB of host
  state.
- `fuzz.replay.config_text`, `fuzz.replay.config_list`, `fuzz.replay.host_state`,
  `fuzz.replay.ir_file`: every committed [fuzz seed and regression input](../tests/fuzz/README.md)
  run once through its harness on every compiler, without libFuzzer.
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

The optional [VST3 build](vst3.md) adds these tests: actual SDK-interface lifecycle
and audio tests, embedded preset generation/loading after deleting the source IR,
plus the SDK validator loading the built plug-in in its default and extensive
modes (the preset test requires I/O). `vst3.fft_symbols` inspects the plug-in
binary with `nm`/`readelf`, `otool` or `dumpbin`: it must export no FFTW symbols
and, unless FFTW was chosen, import none (skipped where no tool is found).
On macOS, `vst3.signature` checks the bundle's ad-hoc seal. These cover preactivation tail
queries, silent-input tails, automation offsets and failed state restore rollback.
Given `-DCONVOLVER_PLUGINVAL=/path/to/pluginval`, two more load the bundle in
[pluginval](vst3.md#independent-host-tests-pluginval), a host that does not share
the SDK's hosting code. None of these establishes compatibility in a particular
commercial DAW.

`packaging.consumer` installs and relocates the package, builds and runs an
external core/host/I/O consumer, and verifies components and source-tree use with and
without a parent CTest setup. Core-only configuration runs six C++/package
suites without Python or libsndfile. See [package tests](cmake-package.md#what-verifies-the-package).

`backend_equivalence.py` separately compares a pair of actual CLIs over 16 matrix/delay
cases, including odd block sizes and both latency modes. See
[measurements and reproduction commands](fft-backends.md). The
[18 October validation record](review-2026-10/current-validation.md) preserves
results for its older baseline `5df874e`; it is not current validation evidence.
Consult the [native workflow](../.github/workflows/regression.yml) and
[VST3 workflow](../.github/workflows/vst3.yml) for the configured matrix; a workflow
file alone is not evidence that a job passed.

## Historical tests: run with CMake/CTest

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

## Historical coverage

The tests below use the canonical `convolver/` source tree. Earlier local
`tests/*.py` experiments aimed at a separate source snapshot are not the gate.

| Suite | What it establishes | Boundary |
| --- | --- | --- |
| Core | 444 checks against independent double time-domain convolution; partition padding, routing/delays, chunks/reset, complex arithmetic | Actual DSP bodies with portable DFT and platform/file stubs; not real FFTW |
| Host adapters | 44 debug/release cases; narrow PCM, aliases, additive/double output, disconnected inputs/delay history | Actual extracted sample adapters; Boost conversion boundary substituted; not a loaded plug-in |
| Gain | Two unequal route-count cases | Extracted gain routines |
| Branch regressions | Route signs, list display, dither constants, bundled Ooura full/tail round trips | Serial Ooura arithmetic, not the full alternate-backend engine |
| Historical diagnostics | Eight stream-state cases | Extracted diagnostic clauses; the portable parser is fuzzed separately |
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

## Validation limits and release work

Every FFT backend must pass the same independent DFT and convolution references.
Agreement between two implementations is useful but cannot replace an oracle.
Statistical dither/shaping tests assess quantization separately from convolution.

Native tests do not establish a full build of the old Windows applications,
universal host compatibility or hard real-time deadlines. The historical
extraction tests also leave old full-width PCM32, nonzero noise shaping, CMD
tail handling and full host negotiation outside their coverage. Preserve these
limits when quoting a result. [TODO](../TODO.md#documented-limitations) gathers
open validation gaps and intentional constraints.

## Documentation checks

Run the pinned [pre-commit checks](../CONTRIBUTING.md#shell-markdown-and-yaml-checks)
for ShellCheck, Markdown, YAML, workflow and maintained local-link validation.
The `documentation` CTest entry checks the archived website's hashes and links;
it is not a check of all maintained Markdown. Run changed CLI/build examples
against the current program as well as checking links.

## Fuzz smoke tests and longer campaigns

[The fuzz guide](../tests/fuzz/README.md) describes the four harnesses and corpus
replay. Push CI runs each libFuzzer target for 60 seconds with ASan/UBSan and
resource bounds. A manual workflow run defaults to 600 seconds per target;
set its `seconds` input to `60` to reproduce the smoke-test budget. Keep crashes
as committed regression inputs after investigating them.

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
