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
| [Actionable diagnostics](2026-11-05-actionable-diagnostics.md) | Every message a user can meet as problem, cause, fix and note: labelled lines in the command-line tools and the log, and in the macOS window the problem in bold, Cause and Fix rows, a button that carries out the fix (Choose Filter… or Copy Diagnostics), a Note row, and Bypass and the diagnostics buttons moved off the bottom edge | PR #110 | [Troubleshooting](../../use/troubleshooting.md), [plug-ins](../../use/plugins.md#convolvers-window) |
| [Plug-in distribution](2026-10-18-plugin-distribution.md), with its [design](2026-10-18-plugin-distribution-design.md) (superseded) | Pinned dependencies and corresponding source, staging, unsigned installers, artifact verification, promotion gates and the candidate workflow for unsigned previews; signing, clean installation, host qualification and publication moved to the guides and TODO (REL-1, PLUG-5, HOST-1) | PR #19 (`378d215`), 21 October 2026; extended to the preset tool and Equalizer APO export in PR #38 | [Releasing](../../develop/releasing.md), [host testing](../../develop/host-testing.md) |
| [Plug-in formats: VST3, LV2 and AU](2026-10-21-plugin-formats.md) (superseded) | Who loads which format, framework choice and the parallel VST3, LV2 and Audio Unit streams: the shared groundwork (PLUG-1 to PLUG-4, PLUG-6 to PLUG-8), the VST3 adapter's layouts and the LV2 and Audio Unit adapters; LV2-3 to LV2-5, PLUG-5 and REL-1 stay open in TODO | From PR #16 and PR #17 (21 October 2026) to PR #44 (AU-1, AU-2) and PR #150 (AU-3) | [Plug-in adapter contract](../../develop/plugin-contract.md), [how the plug-ins behave](../../use/plugins.md) |
| [Plug-in editor](2026-10-24-plugin-editor.md) (superseded) | The window and its settings: PLUG-9's report, Gain, Mix and Latency (PLUG-10), VST3-3, VST3-4, and the macOS window VST3 and the Audio Unit share (VST3-11, AU-6, AU-7); the other gain modes and the meters, PLUG-11, VST3-12, VST3-13, LV2-6 and LV2-7 stay open in TODO | PR #78 (the plan), then PRs #80, #84, #95 and #103, among others | [How the plug-ins behave: the window](../../use/plugins.md#convolvers-window), [plug-in adapter contract: settings](../../develop/plugin-contract.md#settings) |
| [Finish and qualify the first preview](2026-11-08-preview-acceptance.md) (superseded) | The bounded qualification of the first (0.12) preview (REL-7): one fixed candidate, acceptance criteria per gate, how a finding is classified, retesting in proportion, the stop rule and deferred work. Previews 0.12.12, 0.13.2 and 0.13.12 were published with the checks left open that jrp decided; the lasting rules moved to the releasing guide, and its contract-table work is PLUG-36 | PR #145 (the plan), applied 8 November 2026 | [Releasing: what blocks a preview](../../develop/releasing.md#what-blocks-a-preview), [status](../../project/status.md) |

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

The plug-in formats and editor plans were archived as superseded on
8 November 2026 (DOC-13): their implementation-state paragraphs dated from
24 October, and the rules, behaviour and choices they planned are now in the
[plug-in adapter contract](../../develop/plugin-contract.md),
[how the plug-ins behave](../../use/plugins.md) and the
[decisions log](../../project/decisions.md). Their open work is in TODO
(LV2-3 to LV2-5, PLUG-5, REL-1, and the editor's remaining phases under the
items each archive notice names); risks, a recommendation and the PipeWire
comparison not yet owned by an item were added to HOST-2, VST3-6, AU-6, LV2-4
and LV2-5. Each file gained an
archive notice, and its relative links to `docs/project/` were adjusted for
the new folder; the distribution plans' and the archived TODO entries' links
to these two plans now point here. Nothing else changed. The parked FFmpeg
and Media Foundation plans are still open.
