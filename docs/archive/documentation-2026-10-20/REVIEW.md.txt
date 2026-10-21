# Convolver: current repository review

Reviewed 18 October 2026 against `5df874e` (portable **0.2.0**). This is a
source-and-test reassessment of the current repository, not a new exhaustive
audit of every historical Windows application. The [superseded reviews](docs/archive/reviews/README.md)
are preserved separately; their 4.3 findings and proposed milestones must not be
read as the status of the portable implementation.

## Repository baseline

There are two implementations with separate version histories:

- [`convolver/`](convolver) preserves the imported 4.3 engine, merged correctness
  fixes and selected archive recovery, including the 4.4 `convolverPipe`.
  Recovery does not establish a complete stable 4.5 release. The
  [reconciliation](docs/review-2026-10/reconciliation.md) and
  [recovery report](docs/review-2026-10/archive-recovery.md) record provenance.
- [`portable/`](portable), [`include/convolver/`](include/convolver) and
  [`plugins/vst3/`](plugins/vst3) implement the current C++20 library, file/RAW CLI,
  host lifecycle and optional VST3 adapter. The CMake package version is 0.2.0,
  independently of the old Windows resource versions.

The current implementation has independently consumable `Convolver::core`,
`Convolver::host` and optional `Convolver::io` static targets, package exports,
Ninja presets and external-consumer tests. Core/host need neither libsndfile nor
a VST SDK. The CLI and configuration/IR loader use libsndfile. CMake 3.20 is the
core minimum; optional VST3 builds need 3.25 and the pinned SDK 3.8.1 build 84.
See [structure](docs/project-structure.md), [package usage](docs/cmake-package.md)
and [VST3 behavior](docs/vst3.md).

## Correctness and compatibility

### Current convolution algorithm

The portable engine uses uniformly partitioned **overlap-add**, rather than the
historical engine's overlap-save/future-output accumulation. For a block size B,
it splits each IR at `p * B`, keeps a circular history of input spectra, multiplies
IR partition p by input history `head - p`, sums the products and overlap-adds the
inverse transform. FFTW uses N = 2B; Accelerate rounds N up to a supported power
of two (at least four). Extra transform padding does not change the IR stride,
B-frame scheduling or latency.

The FFT adapters expose the same unnormalized transform convention. Filters
are scaled by 1/N once during preparation. Real/imaginary partition spectra are
stored separately for vectorizable direct complex multiply/accumulate; adapter
packing and DC/Nyquist handling remain private. Input and output delays have
separate rings. Reset clears convolution, staging, overlap and delay history.

This construction and the tests support correct convolution for the exercised
routing, delays, gains, IR lengths and caller chunk boundaries. Agreement between
backends alone is insufficient: independent direct DFT and double time-domain
convolution are the numerical references. The tests are finite coverage, not a
proof for every possible filter or floating-point input.

### Earlier findings and their current disposition

| Earlier finding | Historical merged source | Current portable implementation/evidence |
| --- | --- | --- |
| Padded partitions shift IR taps | Fixed rounded partition stride | B-spaced IR partitioning; awkward lengths/blocks checked against direct convolution |
| Input delays overwrite overlap history | Fixed with a separate small delay ring | Separate delay state; matrix/delay, chunk and reset tests |
| FastArray assignment overreads | Bounded copy; the original overread was undefined behavior | New owned buffers; independent bounds and sanitizer tests, no FastArray dependency |
| Empty IR missing throw; per-path gain bounds | Fixed | Configuration validation and per-route processing; invalid input and matrix tests |
| Three-multiply complex cancellation | Direct complex products restored | Direct real/imaginary MACs, with cancellation and numerical-oracle coverage |
| PCM24 sign/reduced valid-bit packing | Historical conversion fixes retained | Real libsndfile PCM16/24/32 round trips and endpoint/clipping checks; not a claim about every legacy wrapper |
| VST2 aliasing, additive/double transfer, disconnected inputs | Extracted adapter fixes tested | Separate planar/in-place float/double HostProcessor and SDK adapter tests; internal DSP remains float |
| Negative-zero routes, dither constants, list display, Ooura tail scaling | Branch fixes retained and extraction-tested | Legacy format semantics tested; Ooura and dither/noise shaping on all integer formats added to the portable core after this review |
| Old CMD EOF and old host tail/lifecycle behavior | Original CMD/DMO/VST2 release gaps remain | New CLI explicitly drains; HostProcessor preserves stream history and handles bypass/filter changes; these do not repair the old applications |

The merged historical fixes are exercised through extracted production routines
with platform/FFT/I/O substitutions. That does **not** demonstrate a complete
native build of the historical ATL, DirectShow, WMP or VST2 applications.

### Streaming and host contract

The core accepts arbitrary caller chunk sizes with B-frame processing latency.
The CLI supports trimming that latency or retaining it and drains the connected
FIR tail at EOF. File and RAW-stream behavior, error handling and overwrite
protection are tested with real libsndfile and subprocesses.

`HostProcessor` maintains stream state across calls, adapts planar/in-place host
buffers, prepares replacements off the audio thread, warms/crossfades them and
retires engines on the control thread. Its processing boundary is nonthrowing,
with invalid sample containment and callback heap guards in tests. Control-side
prepare/replace/collect/destruction calls must be serialized, and destruction
requires stopped audio. These contracts and tests do not establish hard realtime
deadlines or absence of every possible race.

VST3 supports one input and one output bus of 1 to 8 channels each (counts may
differ), 32/64-bit host buffers,
256-sample latency, latency-aligned bypass, embedded-filter presets and bounded
state restoration. Tail reporting works before activation and conservatively
through replacements. Sample-rate/layout mismatches are rejected; there is no
implicit resampling, custom file-picker editor or VST2 state migration promise.
Windows bundles use static FFTW to avoid reliance on a host's DLL search path.

Windows-style relative paths in `.cfg` and list files now work on all platforms,
including mixed separators. Absolute Windows drive paths are not automatically
mapped onto a Mac filesystem; they fail with a diagnostic naming the line and,
where one exists, a relative replacement. The historical channel mask is
retained in the configuration and written as WAV speaker labels only on request
(`--speaker-map config`; header bytes checked independently of libsndfile by
`native.io`, `native.cli` and the compatibility corpus).

## Verification evidence and limits

The refresh uses the following checks; detailed suite boundaries and reproduction
commands are in [testing](docs/testing.md):

| Layer | Gate | What it does not establish |
| --- | --- | --- |
| Native FFT/core/host/state/I/O/CLI/planning | Seven production suites using real FFT libraries and libsndfile; independent numerical references, callback heap checks and invalid-input cases | Exhaustive numeric/race analysis or realtime deadline guarantees |
| Package | Relocated install plus built/running external core/host/I/O consumers, subproject and core-only checks | Self-contained distributable binaries or stable C++ ABI |
| Historical code | Eight Clang extraction suites with ASan/UBSan | Native old Windows host integration or codecs behind mocked boundaries |
| Historical documentation | 99 files, 18 pages, hashes and local references | Current availability of external websites |
| VST3 | SDK-interface tests, embedded preset tests and actual bundle validator in three-platform CI | Compatibility in a named commercial DAW, signed/notarized distribution |

Local refresh results and exact baseline CI links are recorded in
[the review validation record](docs/review-2026-10/current-validation.md).
The ordinary full Clang configuration contains 17 CTest entries; native-only
GCC/MSVC configurations contain nine including documentation and consumer tests.
Core-only runs five without Python/libsndfile. VST3 adds three tests (12 when
legacy extraction is disabled). Cross-backend CLI equivalence is an additional
16-case comparison, not one of those CTest entries.

Performance figures in [FFT measurements](docs/fft-backends.md) are dated results
for particular code, CPUs and builds. The 0.2.0 split-spectrum change has separate
x86 measurements; earlier Apple figures must not be relabelled as measurements
of that change. No new speed claim is made by this documentation refresh.

## Downstream use and preserved documentation

The [17 October downstream evidence](docs/review-2026-10/downstream-evidence.json)
includes hashes, binary markers and references supporting Album Player's Windows
ConvolverVST adaptation and Linux core lineage. These binaries were inspected,
not executed; neither their bug status nor source-level changes were established.
The [inspector](docs/review-2026-10/inspect_downstream.py) can recheck the recorded
public artifacts, but changing upstream downloads are not an ordinary CI gate.

Keep these distinct classifications:

- **Album Player:** derivative implementation evidence, beyond mere format support.
- **DRC Designer/cPlay:** documented distribution/integration or external plug-in
  hosting; not proof of another embedded engine fork.
- **Audiolense/JRiver, Roon, Hang Loose Convolver and CamillaGUI/CamillaDSP:**
  configuration-format adoption/import evidence, with consumer-specific limits;
  original DSP source reuse is not established by that evidence.
- **Equalizer APO:** the recorded evidence identifies a different convolution
  library lineage.

These are the checked-in investigation's dated conclusions, not a fresh survey
of every project's latest release. Future reuse of downstream fixes needs actual
source comparison and relevant tests.

All 18 SourceForge documentation pages and required assets remain in the
[4.4 website archive](docs/historical/convolver-4.4/README.md). Its hashes and
saved live-site comparison remain intact. Old review reports have moved to
[the review archive](docs/archive/reviews/README.md); dated reconciliation and
recovery records remain available as provenance rather than current instructions.

## Remaining priorities

1. **Dither:** implemented after this review. Every integer output format uses a
   documented, seeded quantizer with opt-in TPDF, the old engine's high-pass
   triangular and rectangular dither, and its five noise shapers; see
   [the portable guide](docs/portable-core.md).
2. **Host compatibility and delivery:** exercise named Windows/macOS DAWs,
   silence/seek/reload/bypass and multiple instances; define minimum OS versions,
   dependency bundling, signing and installers. SDK validation is the starting gate.
3. **Configuration interoperability:** inspection/validation output, redistributable
   fixtures for downstream profiles, bounded parser/state/IR fuzzing and speaker
   metadata. Add resampling only as an explicit, separately tested feature.
4. **Measured efficiency:** forward transform/history reuse for identical ordered
   input mixes was implemented after this review, with bit-identical output and
   1.2–1.4× faster dense matrices. The benchmark now reports p50/p99/max callback
   times and peak memory. Inverse-transform sharing was evaluated and left as a
   separate change because it alters summation order; see
   [the measurements](docs/fft-backends.md#shared-forward-transforms).

The [updated modernization plan](docs/modernization.md) separates these remaining
choices from the core, package, FFT and VST3 work already implemented.
