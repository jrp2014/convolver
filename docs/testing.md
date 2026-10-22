<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
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
[plug-in comparisons](#plug-in-comparisons-plug-2),
[historical tests](#historical-tests-run-with-cmakectest) and
[their coverage](#historical-coverage), [limits](#validation-limits-and-release-work),
[documentation checks](#documentation-checks), [fuzzing](#fuzz-smoke-tests-and-longer-campaigns)
and [when CI runs](#ci-triggers).

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
  bundled historical sample in `historical/configs/`.
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
- `native.state`: fixed version-1 wire fixture, the version-2 failed-load
  marker (and that every other state is still written as version 1), embedded configuration round trips, truncation, corrupt headers,
  invalid coefficients and bounded decoding.
- `native.utilities`: little-endian byte order and bounds, exclusive binary file
  creation, Unicode paths, link/overwrite protection and competing creators.
- `native.presets`: the [preset containers](presets.md) without any SDK: base64
  against RFC 4648's vectors, the `.vstpreset` layout field by field, reading
  presets as hosts save them (extra chunks, lower-case class IDs, padding),
  version-1 states with any channel mask, every truncation and damaged chunk
  list, another plug-in's preset, AU keys and XML escaping, one LV2 plug-in URI
  per fixed layout, Turtle escaping, and the explained refusals (channel limits,
  layouts without an LV2 plug-in, unusable preset names).
- `plugins.presets`: `convolver-preset` writes the 2→6 crossover fixture as all
  three formats in one run and reads each back without Convolver's code: the
  `.vstpreset` by its documented layout, the `.aupreset` with Python's
  `plistlib`, and the LV2 bundle's Turtle (also with `serdi`, a Turtle parser,
  when installed). All three must hold the same encoded state, which a separate
  version-1 decoder checks against the fixture's WAV samples, routes, gains and
  delays. It also converts a `.vstpreset` after its source files are deleted,
  and checks that refused requests write nothing.

- `export.equalizer_apo`: `convolver-equalizer-apo` exports the first-use
  examples and a configuration list, and a model of Equalizer APO 1.4.2 (written
  from its sources: line splitting, channel names, `Copy:` from the channels as
  they were before the line, created channels, `If:`/`Eval:`, `Preamp:`,
  `Delay:`, `Convolution:` and `Include:`; an IR at the wrong rate fails the
  model) runs the commands. The
  output must match the double-precision oracle within 1e-5 on devices with the
  filter's channels and with more (the extra ones silent); routes with gains and
  inversions, input and output delays, shared IRs, the choice between list
  entries (the largest that fits a larger device) and the export included from
  a `config.txt` are covered, and other sample rates and devices over 32
  channels must be silent. It also checks that existing folders and folder
  names too long for Equalizer APO are refused, that file names cannot add
  commands, and that failed exports leave nothing. The model is not Equalizer APO itself; running the export there
  is open as HOST-4. See the [guide](equalizer-apo.md#how-it-is-tested).

The optional [VST3 build](vst3.md) adds these tests: actual SDK-interface lifecycle
and audio tests, embedded preset generation/loading after deleting the source IR
(`vst3.preset`, which also checks that `convolver-preset` and
`convolver-vst3-preset` write the same bytes), `vst3.preset_format` (our
`.vstpreset` writer is byte-identical to Steinberg's `PresetFile::savePreset`,
the SDK reads our presets, and we read the SDK's, including controller and
metadata chunks),
plus the SDK validator loading the built plug-in in its default and extensive
modes (the preset test requires I/O). `vst3.fft_symbols` inspects the plug-in
binary with `nm`/`readelf`, `otool` or `dumpbin`: it must export no FFTW symbols
and, unless FFTW was chosen, import none (skipped where no tool is found).
`identities` (in every build with the CLI, and in pre-commit) checks the
[identity registry](plugin-identities.md#changing-the-registry): each format's
rules, its frozen values, the installer files, and that no file keeps its own
copy of an identity. `vst3.identities` checks that the bundle's `moduleinfo.json` (and, on macOS,
`Info.plist`) carries the class IDs, vendor and bundle identifier from
[`plugins/identities.json`](../plugins/identities.json), and that each VST3
speaker constant in the SDK is the same bit as its WAV channel-mask bit.
On macOS, `vst3.signature` checks the bundle's ad-hoc seal. These cover preactivation tail
queries, silent-input tails, automation offsets and failed state restore rollback.
`vst3.harness` runs the [PLUG-2 comparisons](#plug-in-comparisons-plug-2).
Given `-DCONVOLVER_PLUGINVAL=/path/to/pluginval`, two more load the bundle in
[pluginval](vst3.md#independent-host-tests-pluginval), a host that does not share
the SDK's hosting code. None of these establishes compatibility in a particular
commercial DAW.

`packaging.consumer` installs and relocates the package, builds and runs an
external core/host/I/O consumer, and verifies components and source-tree use with and
without a parent CTest setup. Core-only configuration runs seven C++/package
suites without Python or libsndfile. See [package tests](cmake-package.md#what-verifies-the-package).

`backend_equivalence.py` separately compares a pair of actual CLIs over 16 matrix/delay
cases, including odd block sizes and both latency modes. See
[measurements and reproduction commands](fft-backends.md). The
[18 October validation record](review-2026-10/current-validation.md) preserves
results for its older baseline `5df874e`; it is not current validation evidence.
Consult the [native workflow](../.github/workflows/regression.yml) and
[VST3 workflow](../.github/workflows/vst3.yml) for the configured matrix; a workflow
file alone is not evidence that a job passed. The [LV2](../.github/workflows/lv2.yml)
and [Audio Unit](../.github/workflows/au.yml) workflows install the
[pinned format tools](plugins/build.md#pinned-format-dependencies) and check
them on LV2's example plug-ins and Apple's AUDelay until the adapters add
their own bundles. The weekly [Dependency updates](../.github/workflows/updates.yml)
workflow reports newer releases of those pins; it tests nothing in Convolver.

## Plug-in comparisons (PLUG-2)

Every plug-in adapter is held to the same rules by one harness,
[`tests/plugins/harness.hpp`](../tests/plugins/harness.hpp). Each format's test
implements its small `Adapter` interface by driving the format's own host API
(today only VST3, in [`tests/vst3/harness_tests.cpp`](../tests/vst3/harness_tests.cpp));
the harness owns the cases and the criteria.

**Fixtures.** The [first-use examples](../examples/first-use/README.md) are the
shared fixtures: a mono IR file, a stereo `.cfg` with cross-feed, a fractional
route gain and an output delay, and a 2→6 crossover with level trims and
delays. Every route has a different response, so a swapped channel fails.
[`tests/plugins/fixtures.py`](../tests/plugins/fixtures.py) writes them from
integer arithmetic and a seeded generator, and holds the Python WAV, noise and
oracle helpers that the pluginval acceptance test and the release example
generator also use.

- `examples.fixtures`: the committed fixtures are byte for byte what the
  generator writes. After changing the generator, regenerate them with
  `python3 tests/plugins/fixtures.py write examples/first-use`.
- `examples.first_use`: runs the commands marked `<!-- first-use-example -->` in
  the README and the examples README exactly as written, with the CLI just
  built, and compares every render with the fixture's `expected.wav` (the
  oracle) within 1e-5. Every fixture must have a documented render. The renders
  stay in `BUILD/first-use/` as the CLI reference for the adapters.

**Two references.** The independent double-precision oracle
([`tests/native/oracle.hpp`](../tests/native/oracle.hpp), shared with
`native.engine`) is the authority: at most 1e-5 absolute. The CLI render shows
that the adapter is wired to the engine as the CLI is: a fresh instance must
reproduce it exactly, sample for sample, once the 256-frame latency is allowed
for. Both run the same engine, so the CLI alone could hide a shared defect.

**Cases**, for each fixture (case names as printed, for example
`bypass[crossover]`):

| Case | Requirement |
| --- | --- |
| `initial` | A filter loaded before activation: latency exactly 256, the exact tail, oracle and CLI agreement from the first frame; blocks of 1, 61, 300 and the host maximum (1024), each with separate and shared (in-place) buffers, and a variable block pattern |
| `restore-after-activation` | A filter loaded after activation but before audio is installed directly, without a crossfade |
| `restore-before-setup` | State restored before the host negotiates rate and layout (VST3, AU) is installed directly at activation; skipped for formats that always know their configuration first (LV2) |
| `restore-before-setup-mismatch` | Well-formed state for another rate, restored before setup, is kept as the saved state; activation plays exact silence; correcting the rate brings the filter back |
| `live-replacement` | A replacement starts with fresh history; the old filter plays on for 256 frames, then a 256-frame linear crossfade; two loads before the next callback coalesce to the newest |
| `tail-after-replacement` | Replacing long with short keeps reporting the long tail; reactivation reports the short tail exactly and plays the short filter afresh; replacing short with long raises it again |
| `failed-replacement` | A filter at the wrong rate, input count or output count is rejected while active; the running filter plays on uninterrupted and stays the saved state |
| `failed-replacement-inactive` | The same while inactive: rejected at once, and reactivation plays the last valid filter |
| `failed-initial-load` | A first filter that cannot be used is rejected, nothing is saved, and the instance plays exact silence, never its input; a valid filter loads afterwards |
| `empty-instance` | Nothing loaded: identity for equal counts, exact silence for unequal counts |
| `bypass` | A 64-sample ramp to the input delayed by 256 frames (input *c* to output *c*), and back. With unequal counts the ramp goes to silence on every output instead |
| `reset` | Exact silence after a reset, then the filter plays as if fresh |
| `silence-flags` | Input flagged silent does not cut the tail short; outputs flagged silent are silent |
| `nonfinite-input` | NaN and infinite input samples count as silence; output stays finite |

Every render continues two blocks past the end of the filter tail, where the
output must be within tolerance of zero.

These follow the [shared adapter contract](plans/2026-10-21-plugin-formats.md#shared-adapter-contract).
A format that does not yet meet part of it lists those cases as **known
deviations**, each with the text its failure must contain and the TODO item
that fixes it. The run prints them and still passes, but a listed case that
fails in any other way, or starts passing, fails the run, so the list can
neither hide a new defect nor go stale. A listed case stops at its first
failure, so its later checks run only once it is fixed. VST3 lists none: its
13 were removed by [VST3-7](../TODO.md#plug-ins-vst3-lv2-and-au), and all 66 cases pass. Its driver
counts an instance as configured once it has been activated with the setup,
because a VST3 host commits to its rate and buses at activation (JUCE-based
hosts call `setupProcessing` with placeholder values before restoring state).
The harness prints the worst error against the oracle; 1.8e-7 with Ooura on
Linux on 22 October 2026.

`vst3.harness` needs the VST3 build, the CLI and the native targets; CTest runs
`examples.first_use` first. A new format adds a driver beside its other tests
and a CTest entry with `FIXTURES_REQUIRED first_use_renders`, as in
[`plugins/vst3/CMakeLists.txt`](../plugins/vst3/CMakeLists.txt). Status ports
and host error messages are format-specific; LV2 and AU check them in their
own tests.

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

The tests below use the canonical `historical/` source tree. Earlier local
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

Run the pinned [pre-commit checks](../CONTRIBUTING.md#lint-checks)
for C++ formatting, ShellCheck, Markdown, YAML, workflow and maintained local-link validation.
The `documentation` CTest entry checks the archived website's hashes and links;
it is not a check of all maintained Markdown. Run changed CLI/build examples
against the current program as well as checking links.

## Fuzz smoke tests and longer campaigns

[The fuzz guide](../tests/fuzz/README.md) describes the four harnesses and corpus
replay. CI runs each libFuzzer target for 60 seconds on pull requests and
`main`, with ASan/UBSan and resource bounds. A manual workflow run defaults to
600 seconds per target; set its `seconds` input to `60` to reproduce the
smoke-test budget. Keep crashes
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

## CI triggers

The test workflows run for every pull request, for every push to `main` and
when started by hand. A commit pushed to a branch that has a pull request runs
once, as part of that pull request; a branch without one gets no automatic
run, so open the draft pull request early ([workflow](workflow.md#before-you-start))
or use Actions, then "Run workflow" on the branch. A new commit on a pull request
cancels the runs still going for the commit it replaces. Runs on `main` and
manual runs are never cancelled this way. Re-run only the latest commit's runs:
re-running an older commit's run cancels the current one.

This keeps the queue short. On the owner's Free plan GitHub runs at most 20
jobs at once across the account's repositories, and only 5 on macOS, and one
commit starts about 18 jobs, 5 of them on macOS. When the same commit also ran for the branch push, and older
commits kept running, jobs on 22 October 2026 waited a median of 10 minutes for
a macOS runner (up to an hour) while taking about 3 minutes to run.
