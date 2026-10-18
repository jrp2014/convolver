# Modernizing Convolver: status and remaining choices

Updated 18 October 2026 for portable **0.2.0**, source baseline `5df874e`.
The [original assessment](archive/reviews/2026-10-17-modernization.md) is archived.
The [current repository review](../REVIEW.md) is the correctness/status reference;
[this structure review](project-structure.md) maps the implementation boundaries.

## What is already implemented

| Original proposal | Current status | Guide/evidence |
| --- | --- | --- |
| Preserve and fix historical source | Correctness merge, selected 4.4/4.5 recovery, Pipe and website preservation | [Reconciliation](review-2026-10/reconciliation.md), [recovery](review-2026-10/archive-recovery.md) |
| Portable library and renderer | C++20 core, file/RAW CLI, historical configuration import and full EOF drain | [Portable guide](portable-core.md) |
| FFT abstraction and Apple backend | Real FFTW and Accelerate, independent numerical references and cross-backend tests | [FFT contract and measurements](fft-backends.md) |
| Planning and measured optimization | Offline FFTW planning/wisdom, split-spectrum vectorizable MACs | [Planning/performance evidence](fft-backends.md#fftw-tuning-on-x86-64-and-split-spectra) |
| Independent library packaging | Core/host/I/O targets, CMake exports/presets and relocated external consumers | [Package guide](cmake-package.md) |
| Current host adapter | Persistent host lifecycle, VST3 SDK 3.8.1, embedded presets and SDK validation | [VST3 guide](vst3.md) |
| Tooling and platform gates | Formatting/static analysis, real-library and historical tests, Linux/Windows/macOS CI | [Tests and limits](testing.md), [validation record](review-2026-10/current-validation.md) |

Compatibility remains the priority. Preserve uniform partitioning and explicit
latency/tail behavior while the remaining host and file compatibility work is
completed. There is no need to redesign the whole project or revive obsolete
platform dependencies in the portable core.

## FFT libraries and libsndfile

Keep **FFTW as the portable option** and **Accelerate as the Apple system-library
option**. Both have implementations and tests; backend selection is fixed for a
build. Native Apple Silicon FFTW 3.3.11 with NEON has been exercised, but its
performance depends on plans, block size, IR and routing. The recorded results do
not establish one universally fastest backend. Benchmark release builds on the
intended hardware before changing defaults. Planning/wisdom belongs off the
audio thread, and distributable builds must honor their supported CPU baseline.

Keep **libsndfile at the I/O boundary**. It already supplies IR decoding, real-file
and RAW handling under tests, and core/host consumers can omit it. Replacing it
would incur format and edge-case work without an identified current need. It is
not the device layer or resampler. Sample-rate mismatch remains an error; any
future offline resampling must be explicit and tested for timing, gain and length.

PFFFT, PocketFFT or an Intel-focused backend remain optional investigations,
not dependencies or implemented fallbacks. Add one only for a demonstrated
packaging requirement or measured workload gain, using the same independent
references. Preserve dependency notices and review the exact versions selected
for distribution; changing an FFT library does not relicense Convolver.

## Recommended next work

1. **Audio compatibility:** PCM16 dither is implemented (deterministic, seeded,
   documented in the portable guide). Optional noise shaping and speaker
   metadata remain separate work.
2. **VST3 delivery:** test named Windows/macOS DAWs, including scan/load, silence,
   seeks, variable buffers, bypass automation, state reload, filter changes and
   multiple instances. Define minimum OS versions and bundle dependencies; provide
   signing/notarization and installers. Start from the existing thin adapter and
   embedded presets. A custom editor, wider bus layouts or CLAP should follow user
   needs rather than precede this compatibility work.
3. **Interoperability:** add config inspection/validation, bounded fuzzing and a
   redistributable fixture corpus with per-consumer expectations. Preserve the
   original grammar and Windows relative-path support; version any new syntax.
4. **Efficiency:** measure transform/history reuse for identical ordered input
   mixes, then assess inverse-transform reuse separately. Split spectra are
   already implemented. Consider nonuniform partitioning only for demonstrated
   long-IR/low-latency needs. Record setup, memory, numerical error and callback
   distributions alongside average throughput; retain allocation/ownership gates.

A standalone audio-device application would add clocks, device changes, routing
UI and distribution work beyond the present library/CLI/plug-in scope. Revisit it
only for a specific unmet requirement. The current focused product is already
useful without that expansion.

## Documentation and downstream compatibility

The [4.4 SourceForge website](historical/convolver-4.4/README.md) remains preserved
as 99 original files including all 18 pages. The offline integrity gate checks
bytes and local references; the saved live-site comparison is dated evidence.
Current instructions belong in maintained Markdown, not edits to that archive.

The [downstream record](review-2026-10/downstream-evidence.json), classified on
17 October, supports Album Player derivative lineage through published binary
markers, hashes and attribution. It does not establish the derivative's bug
status or provide a source diff. Its recorded evidence also distinguishes DRC
Designer/cPlay distribution/hosting from Audiolense/JRiver, Roon, Hang Loose
Convolver and CamillaGUI/CamillaDSP configuration adoption. The latter is not
proof of shared DSP code; consumer-specific path/list semantics matter.

The [current review](../REVIEW.md#downstream-use-and-preserved-documentation)
retains those distinctions. Reuse downstream fixes only after obtaining and
comparing actual source, preserving licensing/provenance, and running focused
regressions. Do not infer that an old defect persists merely because a binary
retains an original class name.
