> **Archived plan — completed.** Moved on 21 October 2026 from `docs/superpowers/plans/2026-10-18-configuration-interoperability.md`. Its "proposal" status predates implementation: all six tasks were delivered in PR #5 (`c1304c5`, package 0.3.0) with fixes in 0.3.1. Named Roon/JRiver/Audiolense profiles, which the plan made conditional on evidence, remain open as CFG-1 in [TODO](../../../TODO.md). Current behavior: [configuration compatibility](../../configuration-compatibility.md) and [core and CLI reference](../../portable-core.md). Two links to sections that no longer exist now point to the page. The text below records intent and evidence at that time; its status lines, checkboxes and test counts were not updated. For current behavior read the guides, not this plan.

# Configuration Interoperability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Status:** Proposal for review; no implementation is included. Prepared against
`8b03377` on 18 October 2026. The values and interfaces below are proposed decisions,
not descriptions of features that already exist.

**Goal:** Let users discover what a configuration will do, diagnose why it will
not load, and move configurations between platforms with reproducible evidence
that routing and audio behavior are preserved.

**Architecture:** Retain one legacy-format parser and the existing loader API.
Separate source-aware parsing from asset resolution/selection and coefficient
loading, then build inspection and validation on the same path used by rendering.
Keep metadata emission and fuzz infrastructure as separately testable additions.

**Tech Stack:** Existing C++20, CMake/CTest, FFTW/Accelerate and libsndfile;
C++20 standard-library parsing, files and report serialization; Python standard-library
fixture/CLI tests; optional Clang libFuzzer targets. POSIX calls are permitted only
behind narrow platform adapters when standard C++ cannot meet the requirement.

**Spec:** Item 3 of [the current review](../../../REVIEW.md),
[the modernization plan](../../modernization.md), and the
[existing compatibility contract](../../portable-core.md#compatibility-and-library-api).
The detailed proposed behavior below elaborates that scope.

## Global constraints

- Compatibility takes precedence over speed. Do not change the configuration grammar.
- Preserve route order, signed-zero semantics, fractional weights, float delay parsing
  and frame truncation, unequal IR lengths, and first matching list selection.
- Preserve `load_configuration(path, expected_rate, expected_inputs, source_paths)`.
  Render and preset generation must continue to use the same semantics.
- Keep C++20 and CMake 3.20 for core/CLI; no new runtime dependency or VST SDK requirement.
- Prefer `std::filesystem`, standard streams, `std::span`, `std::string_view`,
  standard containers and RAII. Use `std::from_chars` where it preserves the existing
  grammar; retain classic-locale float parsing where changing it would change results.
- Do not add a platform framework or third-party parser, JSON, CLI or filesystem
  library. Implement only the fixed report schema's JSON serialization with standard
  C++ strings/streams, tested with Python's standard `json` reader.
- Use POSIX only for capabilities absent from standard C++, isolated behind private
  adapters with an explicit Windows equivalent. Do not expose file descriptors or
  platform headers in the inspection API, or assume POSIX is available on Windows.
- Avoid new dependencies on shell utilities, `/tmp`, locale-specific behavior,
  symlinks, case-sensitive filesystems or process-global working-directory changes.
  Use platform-neutral paths and temporary-directory discovery; keep Unicode rules explicit.
- Core-only consumers remain independent of Python, libsndfile and inspection code.
- Keep configuration/IR reading, reporting and allocation outside audio callbacks.
- Keep the 99-file historical website archive and its manifests unchanged.
- No implicit resampling, automatic route reordering, guessed drive mappings or
  automatic edits to user configurations. Dither and DSP optimization remain separate.

## Approach and scope

A reporting wrapper around today's loader would be cheap but could not retain
source-line diagnostics or inspect configurations without decoding IRs. A new
parser would create two interpretations of the format. The recommended approach
is a small refactor of the existing parser, followed by reporting on its results.

Deliver inspection and the compatibility corpus first (tasks 1–4). Add bounded
fuzzing next, then speaker metadata as a distinct opt-in feature. General format
conversion, ZIP import/export, RF64 policy, resampling and additional plug-in bus
layouts are outside this milestone. A future converter should consume the same
parsed representation rather than introduce another grammar.

## Proposed user interface

```sh
convolver --inspect room.cfg
convolver --inspect filters.txt --sample-rate 48000 --input-channels 2 --json
convolver --validate room.cfg --block-size 256
convolver --validate filters.txt --all-configurations --json
convolver --inspect room.cfg --syntax-only --json
convolver --validate room.cfg --profile portable-paths
```

`--inspect` and `--validate` are mutually exclusive operation modes, also exclusive
with rendering/tuning options. Both perform full selected-configuration validation
by default. Inspect prints the report; validate prints diagnostics and a brief
success summary. Neither creates an output audio file or FFT plan. `--json` emits
one UTF-8 JSON document on stdout, including on validation failure. Human-readable
errors go to stderr; existing rendering/RAW stdout behavior stays unchanged.

Use exit codes 0 for valid, 1 for validation/I/O/internal failure, and 2 for invalid
CLI usage in these new modes. Keep existing render-mode exit behavior unchanged.
Warnings do not fail by default; `--warnings-as-errors` promotes them to exit 1.
Invalid UTF-8 in reportable names receives an actionable diagnostic instead of
malformed JSON. Never interpolate unescaped filenames into JSON.

Selection rules:

- Default list handling is identical to rendering: first matching rate/input count,
  or first entry when neither selector is supplied. Report why that entry was chosen.
- Do not open assets of nonmatching entries merely to validate a selected render.
  Preserve the existing validation of text encountered before selection.
- Only explicit `--all-configurations` traverses every list entry and validates every
  leaf. It rejects simultaneous rate/input selectors. Stop a leaf at its first fatal
  error, continue with independent siblings and return failure if any leaf fails.
- `--syntax-only` reads bounded text, resolves references lexically and checks grammar,
  routes and delays without opening IR assets. It is initially restricted to one
  configuration text file; lists and direct audio receive a mode-specific diagnostic.
  The report explicitly says coefficients and renderability are unverified; tail and
  storage fields are null. This mode never produces a misleading full-validation pass.

Report schema version 1 contains:

| Section | Required contents |
| --- | --- |
| Identity | Schema version, validation mode, profile, backend/build identity, validity |
| Selection | Supplied selectors, selected file, list traversal/selection reason |
| Graph | Rate, input/output counts, raw speaker mask, ordered routes and effective gains |
| Timing | Declared delay tokens and effective frame delays; B-frame latency; finite tail separately |
| IRs | Referenced spelling, resolved path, selected channel, sample rate, decoded frame count |
| Resources | Partitions per path, FFT length, accounted float-buffer bytes and explicit exclusions |
| Diagnostics | Stable code, severity, message, file, physical line/column where known, include chain |

Do not serialize coefficients. Relative paths remain relative in the source fields;
resolved paths are machine-specific and must not appear in golden snapshots. For
syntax-only or failed loads, unavailable values are null rather than fabricated zeros.
Storage is an estimate of accounted buffers, excluding backend plan internals,
allocator overhead and decoder buffers; never label it total process RAM.

## File and interface boundaries

| File | Change/responsibility |
| --- | --- |
| `portable/config.cpp` | Keep loader orchestration, path resolution, list selection and IR decoding |
| `portable/config_parse.hpp`, `portable/config_parse.cpp` (new, private) | Parse text with physical source locations; no filesystem or decoder access |
| `include/convolver/inspection.hpp`, `portable/inspection.cpp` (new) | Public I/O-component inspection options/report and shared report construction |
| `include/convolver/engine.hpp`, `portable/engine.cpp` | Expose analysis from the existing validator/storage accounting; no DSP scheduling change |
| `portable/inspection_cli.hpp`, `portable/inspection_cli.cpp` (new, private) | Mode-specific arguments, human/JSON serialization; keep renderer in `main.cpp` |
| `portable/io.cpp`, `include/convolver/io.hpp` | Later opt-in channel-map writing, with unchanged defaults |
| `tests/compatibility/` (new) | Small source-owned fixtures, generation recipe, manifests and independent oracle |
| `tests/native/inspection_tests.cpp`, `inspection_cli_tests.py` (new) | Inspection API, diagnostics and CLI contracts |
| `tests/fuzz/` (new) | Optional bounded harnesses, seed corpus and replay instructions |

Private parser entry point:
`ParsedDocument parse_configuration_text(std::string_view text, const std::filesystem::path& source)`.
`ParsedDocument` distinguishes a configuration from a list and retains physical
source locations, original reference/delay tokens, header values, ordered routes
and selected IR channels. It contains no decoded coefficients. Reuse the existing
number/delay/route helpers; do not replace float semantics with double parsing.

Public inspection entry point:
`InspectionReport inspect_configuration(const std::filesystem::path&, const InspectionOptions& = {})`.
Options contain `expected_rate=0`, `expected_inputs=0`, `block_frames=256`,
`syntax_only=false`, `all_configurations=false` and an `InspectionProfile` enum
(`convolver`, `portable_paths`). Reports own their strings/vectors, preserve ordered
entries and contain diagnostics even when a leaf fails. Expected parse/load errors
are reported; resource exhaustion/unexpected internal exceptions remain exceptions
caught at the CLI boundary. The old loader still throws on failed loading.

Core analysis entry point:
`ConfigurationAnalysis analyze_configuration(const Configuration&, std::size_t block_frames=256)`.
Fields: `latency_frames`, `tail_frames`, `fft_frames`, `accounted_buffer_bytes` and
`partitions_per_path`. Refactor existing `validate_configuration` and
`configuration_tail_frames` to share its validation/accounting, not copy formulas.
No FFT planning or prepared spectral-buffer allocation is allowed here.

The JSON emitter is schema-specific, not a general-purpose JSON parser or DOM.
Test escaping and number formatting explicitly and avoid introducing a reusable
framework. Dependency-specific audio handling remains in the existing libsndfile
adapter. Any necessary POSIX/Windows divergence belongs there or in one small
private helper, with equivalent tests on both platforms.

## Review focus

1. A list's nonmatching entry references a missing IR: selected-mode loading must still succeed; task 2 pins it.
2. A filename contains Unicode, spaces or JSON control characters: reports remain valid and diagnostics identify the source; task 3 pins it.
3. `-0`, repeated routes or fractional delays survive reporting/refactoring without changed audio; tasks 1–3 pin it.
4. A cyclic list, hostile length field or huge declared delay exhausts resources: rejection occurs before unbounded allocation; task 5 pins it.
5. A valid speaker mask is attached to the wrong channel order: preserve sample order and independently inspect written metadata; task 6 pins it.

## Task 1: Establish the compatibility corpus

**Files:** Create `tests/compatibility/README.md`, `cases.json`, `generate_fixtures.py`,
`run_corpus.py` and small text fixtures. Modify `CMakeLists.txt` to register
`compatibility.corpus` only when CLI/Python tests are available.

**Interfaces:** Manifest schema 1 describes generated integer/float IR samples,
inputs, ordered expected routes/delays, expected selection or diagnostic category,
and expected output length. `run_corpus.py CONVOLVER_EXE` generates a temporary
fixture tree, renders cases and checks an independent double direct-convolution
reference. It must not call Convolver to generate expected output.

- [ ] Add fixtures for identity, inversion, fractional/repeated routes, cross-channel
  mixing, unequal IR lengths, zero/nonzero/truncated delays and last-frame impulses.
- [ ] Add relative Windows/POSIX/mixed paths, changed working directories, UTF-8 and
  spaces, BOM/CRLF/comments, first-match lists, missing nonmatching assets and cycles.
- [ ] Include block sizes 1, 3, 17 and 256, split input writes and both latency modes.
  Assert frame counts exactly; use the existing numerical bound
  `2e-5 * (1 + max(abs(actual), abs(expected)))` without relaxing older tests.
- [ ] Label fixtures `spec-derived`, `current-behavior`, or `consumer-verified`.
  Use generated IRs and repository-owned text; do not copy third-party filters.
  Every consumer claim needs a source/revision, date and evidence type.
- [ ] Run the new corpus against the unmodified current loader first. Characterize
  unexpected differences explicitly; do not silently rewrite expected data or change
  parsing to match an assumption. Commit the passing baseline corpus.

**Acceptance:** Same fixture expectations pass on FFTW and Accelerate and all three
OSes; path-only platform differences are explicit, and expected audio is independently computed.

## Task 2: Separate parsing while preserving loader behavior

**Files:** `portable/config.cpp`, new private parser files, `tests/native/io_tests.cpp`,
new `tests/native/config_parse_tests.cpp`, `CMakeLists.txt`.

**Interfaces:** Produce `parse_configuration_text` and `ParsedDocument` as above;
keep the existing public `load_configuration` signature and selected-source list.
A shared internal resolver consumes parsed documents for loader and later inspector.

- [ ] Add tests requiring actual physical line/column diagnostics after blank lines,
  comments and BOM; require an include chain for nested-list failures. Demonstrate
  that these assertions fail on current diagnostics before refactoring.
- [ ] Extract parser helpers and preserve their semantics and existing input limits.
  Keep audio-file recognition in the resolver; arbitrary filenames/extensions must
  continue to work. Do not introduce a second extension-based interpretation.
- [ ] Preserve visited-source tracking used by CLI overwrite protection, first-match
  early exit, and skipped nonmatching IR loading. Test all three explicitly.
- [ ] Exercise route-order/sign/delay edge cases from task 1. Existing exceptions may
  gain context, but successful configurations and selected coefficient data must match.
- [ ] Run `native.io`, the parser test and `compatibility.corpus`, then full existing
  CTest gates. Commit only after unchanged renders and improved diagnostics pass.

**Acceptance:** One parser powers loading and inspection; existing valid renders,
selection and overwrite protections remain unchanged.

## Task 3: Add shared analysis and inspection/validation commands

**Files:** New inspection API/implementation/CLI files above; engine validator,
`portable/main.cpp`, CMake install/targets, new inspection tests and consumer checks.

**Interfaces:** Produce `ConfigurationAnalysis`, `inspect_configuration` and schema-1
reports. Inspection lives in `Convolver::io`; the analysis function stays in core.
No new public dependency or report code is linked into core-only consumers.

- [ ] Add failing API/CLI tests for the commands and modes above, null fields in
  syntax-only results, deterministic JSON, escaping, invalid flag combinations and exit codes.
- [ ] Share core validation/accounting and assert analysis latency/tail agrees with
  actual Engine values for both backends and odd block sizes. Assert inspection
  cannot alter FFTW wisdom or open output files.
- [ ] Implement single-configuration and selected-list inspection, followed by explicit
  all-entry validation with per-leaf diagnostics. Test that missing unrelated assets
  affect only all-entry mode, and that failed entries cannot produce a valid report.
- [ ] Serialize effective signed-zero routing as channel 0/gain -1 while retaining
  the source token; distinguish declared milliseconds from effective frame delays.
- [ ] Add an installed external I/O consumer for the new API. Run new tests, core-only
  consumer checks and all existing CLI/planning tests. Document examples and commit.

**Acceptance:** A user can understand selection, routes, latency, tail and asset
failures without rendering; renderability is distinguished from syntax-only validity.

## Task 4: Add evidence-backed compatibility reporting

**Files:** `portable/inspection.cpp`, corpus manifest/tests, new
`docs/configuration-compatibility.md`; link from portable guide and modernization plan.

**Interfaces:** `InspectionProfile::convolver` reports native validity.
`InspectionProfile::portable_paths` adds warnings for absolute/platform-bound
references, case-dependent spelling and other demonstrable portability issues.
Profile checks do not rewrite paths or alter graph/selection semantics.

- [ ] Add failing profile tests: an otherwise valid native absolute path is a warning;
  a missing required IR is an error under either profile; a relative mixed-separator
  path is accepted when the referenced file exists. Test warnings-as-errors separately.
- [ ] Preserve legitimate `..` references in native usage; flag distribution outside
  the configuration tree as a packaging warning, not an unconditional parser rejection.
  On case-insensitive systems compare stored directory-entry spelling where available;
  never claim cross-platform portability merely because the local open succeeded.
- [ ] Publish a compatibility matrix separating grammar accepted, reference render
  passed and independently tested in a particular consumer/version. Unknown stays unknown.
- [ ] Use checked-in downstream evidence to choose future fixtures. Before introducing
  a named Roon/JRiver/Audiolense profile, verify the applicable specification/version
  and add its fixtures. Ship no inferred "fully compatible" badge or code-reuse claim.
- [ ] Run profile/CLI/corpus tests and documentation link checks; commit the guide
  and the two implemented profiles together.

**Acceptance:** Profiles provide actionable, bounded claims backed by fixtures;
choosing a profile never silently changes rendering.

## Task 5: Bound and fuzz configuration, state and IR inputs

**Files:** New optional `tests/fuzz/` harnesses/README/seeds, `CMakeLists.txt`,
`.github/workflows/fuzz.yml`; targeted parser/resolver/state/I/O fixes only if reproduced.

**Interfaces:** `CONVOLVER_BUILD_FUZZERS=OFF` by default. Clang-only targets
`fuzz_config_text`, `fuzz_config_list`, `fuzz_host_state`, `fuzz_ir_file`, each with
`LLVMFuzzerTestOneInput(const uint8_t*, size_t)`. Use production parser/decoder code.
List fuzzing uses a bounded in-memory asset map through a private resolver seam;
IR fuzzing reads only an isolated harness-owned file created using portable paths
and standard streams. Generated paths cannot read arbitrary machine files or networks.

- [ ] Add deterministic tests for each current limit and just-over-limit values:
  1 MiB per text file, 16 active nesting levels, 4096 visits, 64 Mi decoded samples,
  16 Mi delay frames in text import, and 64 MiB encoded host state. Reuse existing
  limits rather than copying divergent constants; assert overflow rejection before allocation.
- [ ] Add an aggregate text budget of 16 MiB per resolution operation and cap
  diagnostic output at 256 records plus a truncation marker. These are proposed
  new rejection/report limits: document them and test boundaries, including shared
  sublists and all-entry mode. Do not claim unchanged acceptance for extreme inputs.
- [ ] Seed fuzzers from the corpus and existing malformed-state/audio tests. Exercise
  cycles, malformed numbers, signed zeros, Unicode, oversized dimensions, truncated
  headers, nonfinite coefficients and damaged RIFF lengths. Catch only expected
  validation exceptions; unexpected failures must fail the harness.
- [ ] Add Linux Clang ASan/UBSan fuzz smoke tests: 60 seconds per target, input cap
  64 KiB, per-input timeout 2 seconds and RSS cap 512 MiB. Keep near-production-limit
  cases in deterministic tests with deliberately bounded allocations.
- [ ] Archive crash inputs/logs on failure; minimize each actionable case into a
  deterministic regression before fixing it. Replay regressions on Linux/macOS/Windows.
  Longer campaigns remain manually invoked CI work, not a claim of exhaustive safety.
- [ ] Run the optional smoke gate and ordinary suites, confirm default/core-only
  builds need no fuzz toolchain, then commit.

**Acceptance:** Normal CI replays all known failures; optional fuzzing uses actual
production boundaries, bounded resources and no uncontrolled filesystem access.

## Task 6: Carry speaker masks into opt-in WAV output

**Files:** `include/convolver/io.hpp`, `portable/io.cpp`, `portable/main.cpp`,
`tests/native/io_tests.cpp`, CLI/compatibility tests and current output documentation.

**Interfaces:** Add `OutputMetadata { uint32_t channel_mask = 0; }` as a defaulted
fourth `AudioWriter` argument. Add `--speaker-map config` for WAV render modes;
absence retains existing output behavior. RAW plus this flag is a usage error.
A zero mask means unspecified; nonzero masks require known speaker bits with a
population count equal to output channels. Preserve output sample order.

- [ ] Add failing round-trip tests for stereo mask 0x3, 5.1 mask 0x3f, zero,
  population mismatch and unknown bits, using distinguishable impulses per channel.
- [ ] Verify the installed libsndfile channel-map/WAVEX API with real output before
  wiring it into the CLI. Use explicit mapping from WAVE speaker bits to libsndfile
  channel-map values; the enum values are not interchangeable.
- [ ] Write WAVE_FORMAT_EXTENSIBLE for an explicit valid nonzero map. Reject invalid
  explicit maps before opening output. Check command/close results; never silently
  claim a requested map was written when the backend cannot represent it.
- [ ] Inspect the RIFF `fmt` chunk independently of libsndfile to assert the exact
  channel mask, then reopen and verify samples/channel order. Test float and PCM
  formats and keep existing clipping/dither behavior unchanged.
- [ ] Run all real-file/CLI tests and three-platform CI; document the opt-in flag and
  limitations. Commit separately from parser changes.

**Acceptance:** Correct speaker labels survive file round trips without reordering
samples; unsupported or inconsistent explicit requests fail clearly.

## Final gate and delivery

Each task follows focused failing test → implementation → focused pass → relevant
existing regressions → review → commit. Task 1 characterizes existing behavior
rather than inventing failing baseline expectations. Do not rerun unrelated long
checks after every prose edit; run the full matrix on the completed feature commit.

```sh
cmake --preset debug-sanitizers
cmake --build --preset debug-sanitizers
ctest --preset debug-sanitizers
cmake --preset accelerate-debug
cmake --build --preset accelerate-debug
ctest --preset accelerate-debug
python3 tests/native/backend_equivalence.py \
  build/presets/debug-sanitizers/convolver build/presets/accelerate-debug/convolver
```

Also require Linux GCC Release, Windows MSVC, installed/core-only consumers,
existing VST3 validation (state/API regressions), historical preservation and the
new fuzz smoke gate. Inspect exported targets and runtime dependencies to confirm
that the feature adds no new runtime library, platform framework or interpreter
requirement. CI scripts must use CMake or Python's standard library instead of
assuming a Unix shell/toolset on Windows. A passing profile test is not a
DAW/consumer certification.
Update the current review only for completed behavior with commit-specific evidence.

Recommended release split: tasks 1–4 deliver useful inspection/validation; task 5
adds robustness evidence; task 6 adds opt-in metadata. Treat new public APIs as a
portable 0.3.0 milestone under the existing same-minor package policy, update the
package consumer expectations and changelog together, and retain source-level
compatibility for existing loader/writer calls. Native implementation in this
session is the simplest execution approach because the parser/report interfaces
are tightly coupled; request an independent review before merging.
