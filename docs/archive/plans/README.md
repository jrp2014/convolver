<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Archived plans

These design and implementation plans are finished: the work they describe is on
`main`, or, for a plan marked superseded, what it left open is now owned by a
maintained guide and TODO. The first ones were moved here from `docs/superpowers/` (later `docs/plans/`, now `docs/project/plans/`) on 21 October 2026;
later plans move here when their work is merged or they are superseded. All are kept
as records of what was intended, decided and measured at the time. Use the
[current documentation](../../README.md) for how Convolver behaves now.

Plans for work that is still open are in [`docs/project/plans/`](../../project/plans/). Open work is
tracked in [TODO](../../../TODO.md).

| Plan | What it delivered | Implemented in | Current guide |
| --- | --- | --- | --- |
| [Portable core and CLI](2026-10-17-portable-core.md) | C++20 engine with uniform partitioned FFT convolution, libsndfile I/O, `.cfg` loader and the `convolver` CLI | `fa8ca77` | [Command-line reference](../../use/command-line.md) |
| [FFT backends](2026-10-18-fft-backends.md) | Accelerate backend beside FFTW, FFTW planning modes and wisdom, benchmarks | `2571990` | [FFT guide](../../develop/fft-backends.md) |
| [Independent core and CMake package](2026-10-18-cmake-package-core.md) | Exported `Convolver::core` and `Convolver::io`, presets, relocatable package consumer tests | `82cf94b` | [Library guide](../../develop/library.md) |
| [Host lifecycle and VST3](2026-10-18-vst3-host-lifecycle.md), with its [design](2026-10-18-vst3-host-design.md) | `HostProcessor`, versioned state codec, VST3 adapter and preset tool, SDK validator in CI | `2f44142`, `5bcbef3` | [How the plug-ins behave](../../use/plugins.md) |
| [Configuration interoperability](2026-10-18-configuration-interoperability.md) | Compatibility corpus, shared parser, `--inspect`/`--validate`, `portable-paths` profile, limit tests and fuzzing, `--speaker-map` | PR #5 (`c1304c5`, 0.3.0), fixes in 0.3.1 | [Other programs](../../use/other-programs.md) |
| [Documentation by reader](2026-10-23-documentation-by-reader.md) | Maintained pages sorted into `docs/use/`, `docs/develop/` and `docs/project/`; status page, decisions log, shorter front pages and the reader-folder check | PRs #47, #51, #52, #53 (last merged in `41d1d3a`) | [Documentation guide](../../develop/documentation.md) |
| [Diagnostics by surface](2026-10-24-diagnostics-by-surface.md) | Where each failure is explained on every surface (24 October 2026), and the gaps closed from jrp's choices: CLI sample faults and incomplete outputs, next steps for damaged files, `convolver-preset` wording and option order, logged plug-in refusals and failed saves, a test per message | PR #96 | [Command line](../../use/command-line.md), [plug-ins](../../use/plugins.md#status-and-the-convolver-log) |
| [Plug-in distribution](2026-10-18-plugin-distribution.md), with its [design](2026-10-18-plugin-distribution-design.md) (superseded) | Pinned dependencies and corresponding source, staging, unsigned installers, artifact verification, promotion gates and the candidate workflow for unsigned previews; signing, clean installation, host qualification and publication moved to the guides and TODO (REL-1, PLUG-5, HOST-1) | PR #19 (`378d215`), 21 October 2026; extended to the preset tool and Equalizer APO export in PR #38 | [Releasing](../../develop/releasing.md), [host testing](../../develop/host-testing.md) |

## How completion was checked

Each plan was compared with the source on `main` at `3d867a2` (package 0.4.3),
not only with its own checkboxes. For example, the configuration plan's files,
public functions (`analyze_configuration`, `inspect_configuration`,
`OutputMetadata`), CLI options, limits (`max_total_text_bytes`, 256 diagnostics),
fuzz targets, `tests/compatibility/` and `fuzz.yml` all exist. Two plans read as
unfinished on their own:

- The configuration plan still says "proposal" and has unticked boxes because it
  was written before PR #5 implemented it. Consumer-specific Roon, JRiver or
  Audiolense profiles were conditional on evidence in the plan and remain open
  as CFG-1.
- The CMake package plan's last box (green CI before updating `main`) was a
  delivery step. The change is on `main`, and CI runs the core-only preset and
  package consumers.

## What changed when archiving

Each file gained a one-paragraph archive notice. The VST3 plan's plain-text spec
path became a link, and two links in the configuration plan to sections that no
longer exist now point to the page instead. Nothing else in the plans was edited.
Relative links still resolve because `docs/archive/plans/` is at the same depth
as `docs/superpowers/plans/`, where they were written.

The plug-in distribution plan and its design were archived as superseded on
24 October 2026: the preview pipeline they planned is merged, and their open
tasks (signing, clean installation, named-host qualification, publication) are
owned by [releasing](../../develop/releasing.md), [host testing](../../develop/host-testing.md)
and REL-1, PLUG-5 and HOST-1 in TODO. Each file gained an archive notice, and
its links to the plug-in formats plan, which stays open, now point to
`docs/project/plans/`; nothing else changed.
The plug-in formats plan (LV2-3 to LV2-5, AU-3 to AU-5, PLUG-5) and the parked
FFmpeg and Media Foundation plans are still open.
