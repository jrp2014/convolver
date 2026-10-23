<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Release readiness

[Documentation index](README.md) · [TODO](../TODO.md) · [Release record](releases/developer-preview.md)

**In short:** status checked on 23 October 2026 against `main` at `1bea4cf`
(0.4.23). Nothing has been published yet: there are no GitHub releases, and
preview packages exist only as temporary CI artifacts. The command-line
renderer, the preset tool, the Equalizer APO export and the VST3 plug-in are
implemented and tested automatically; no named host has been tested and no
independent user has followed the instructions. The next milestone is an
independent user installing and using an exact preview package.

The **oracle** named below is a direct double-precision convolution computed
independently of the engine, the project's reference for correct output
([testing](develop/testing.md)).

This page is the short answer to "what is ready?". [TODO](../TODO.md) is the
backlog and owns the order of work; the linked guides own the behaviour. Update
this page when an item below changes level, and keep the date and commit above
current. Dated reviews stay as they were written.

## Evidence levels

Each feature is judged on four separate levels. A higher level does not follow
from a lower one.

| Level | Means | Evidence to cite |
| --- | --- | --- |
| **Implemented** | The code is on `main` | The guide that describes it |
| **Automatically tested** | A CTest test or CI workflow checks it, against an independent reference where one exists | The test name and workflow; a particular run for a particular commit |
| **Tested in a named host** | A person ran it in a named program (host name and version, OS, package SHA-256) and recorded the result | The record under HOST-1 or the format's host-matrix item |
| **Published** | It is in an asset of a GitHub pre-release or release that jrp published | The release page and its SHA-256 sums |

A user journey (below) adds a fifth: **demonstrated**, when someone unfamiliar
with the repository completed it from the exact preview files using the
documentation alone.

## What users can run now

| Feature | Implemented | Automatically tested | Named host | Published |
| --- | --- | --- | --- | --- |
| Command-line renderer `convolver` ([reference](portable-core.md)) | Yes | Yes: "Native and historical tests" workflow on Linux (Clang, GCC), macOS (FFTW, Accelerate) and Windows (MSVC); direct double-precision oracle | Not applicable | No; build from source. The first preview will ship it (PLUG-5) |
| Configuration inspection and validation ([compatibility](configuration-compatibility.md)) | Yes | Yes: parser tests, a corpus of example configurations, and replay of inputs found by fuzzing | Other readers of `.cfg` not checked (CFG-1) | No |
| Preset tool `convolver-preset` ([guide](presets.md)) | Yes | Yes: `native.presets`, `plugins.presets`, `vst3.preset_format` | Not applicable | No; in the preview package since PR #38, not yet published |
| Equalizer APO export ([guide](equalizer-apo.md)) | Yes | Yes: against a model of Equalizer APO 1.4.2, and on Windows CI in Equalizer APO's own `Benchmark.exe` (`export.equalizer_apo.runtime`) | Not yet checked on a real Windows device (HOST-4) | No; in the preview package since PR #38, not yet published |
| VST3 plug-in ([guide](vst3.md)) | Yes: 1–8 in, 1–8 out | Yes: SDK validator, pluginval at strictness 10, `vst3.harness` (against the oracle and the CLI) on Linux, macOS and Windows | None yet (HOST-1: REAPER and Carla suggested) | No; preview candidates are CI artifacts only |
| LV2 plug-in ([guide](lv2.md)) | Yes: adapter and layouts (LV2-1); loading filters from a file is LV2-2 | Yes: `lv2.harness`, `lv2.adapter` and `lv2lint` on Linux | None | No; joins a later preview |
| AU plug-in | In development (AU-1, AU-2) | Planned through the shared harness and `auval` | None | No; joins a later preview |
| C++ libraries ([CMake package](develop/library.md)) | Yes | Yes: installed and source-tree consumer tests | Not applicable | Source only |
| FFmpeg pipe ([example](portable-core.md#with-ffmpeg)) | Documented | No; run by hand once (FFMPEG-1) | Not applicable | Not applicable |

Fixed defect: a reset during a filter transition delayed the next filter
(found in jrp's review of 0.4.19; fixed on 23 October 2026 by the VST3-7
follow-up, [PR #40](https://github.com/jrp2014/convolver/pull/40)). Lifecycle
sequences that would catch defects like it are PLUG-7.

## User journeys for the first preview

"Ready" means these journeys work, not that a set of TODO items is ticked
(REL-2 defines them; REL-3 is the independent walkthrough). Each plug-in
journey, on each platform the preview offers: download, install, load a
filter, check the expected output, save and reopen, and recover from a wrong
preset, using only the documentation. The CLI journey covers a filter that does
not fit its input instead of saving and presets.

| Journey | Filter | Surfaces | Status |
| --- | --- | --- | --- |
| Stereo room correction | [`examples/first-use/stereo`](../examples/first-use/README.md) | VST3 in a named host; CLI | CLI commands run in CI (`examples.first_use`). The preview package has no preset made from this filter yet. Not demonstrated. |
| 2→6 crossover | [`examples/first-use/crossover`](../examples/first-use/README.md) | VST3 in a named host; CLI | CLI commands run in CI. No 2→6 preset in the package yet. Not demonstrated. |
| Offline file rendering | All three first-use filters | CLI from the preview package | Run from the source tree in CI. The package has no CLI yet; the first preview will ship it (PLUG-5). Not demonstrated. |

Recovering from a wrong preset is the weakest step today: VST3 keeps the
working filter but reports the rejection only as `kResultFalse`, so the host
may show nothing ([loading filters](vst3.md#loading-filters)). PLUG-8 (done)
keeps the reason; DIAG-1 decides where it is shown.

## Next milestone: an independent user runs a preview

From jrp's [guidance of 22 October 2026](review-2026-10/release-readiness-guidance-2026-10-22.md):
an independent user successfully installs and uses an exact preview package,
with the lifecycle regression fixed and the advertised workflows demonstrated.
The first preview holds VST3, the `convolver` CLI and the preset tool; LV2
and AU join later previews once their own journeys are demonstrated
(owner's decision, 23 October 2026).

| Condition | Source | Item | Status |
| --- | --- | --- | --- |
| Reset during a transition no longer plays the old filter | jrp's milestone | VST3-7 follow-up ([PR #40](https://github.com/jrp2014/convolver/pull/40)) | Done, merged 23 October 2026 |
| Journeys written as steps, with the presets they need in the package | jrp's milestone | REL-2 | Not started |
| An exact preview package: VST3, the CLI, the preset tool, examples and guides | jrp's milestone | PLUG-5 | Preset tool and Equalizer APO export added ([PR #38](https://github.com/jrp2014/convolver/pull/38)); the CLI and the journey presets remain |
| An independent user completes the journeys from those exact files | jrp's milestone | REL-3 | Not started; uses the candidate's files before upload (owner's decision, 23 October 2026); the tester is still to be chosen by jrp |
| Lifecycle sequences checked against a reference model | Proposed here, so the next defect of this kind is caught | PLUG-7 | Not started |
| Smoke test of VST3 in at least one named host | Proposed here, already the preview gate in the plug-in plan | HOST-1 | Not run |

At publication, and before the next preview: COMPAT-1 keeps presets, a host
project and expected audio from the published release, so later previews are
tested against them. Not part of this milestone, but tracked: signing and
notarization (the production gate, PLUG-5), resource budgets (PERF-3), showing
failure messages (DIAG-1; PLUG-8 already keeps the reasons), other `.cfg` readers (CFG-1) and Equalizer
APO itself (HOST-4).

## Compatibility guarantees

Four promises, kept separate because each breaks in a different way. Changing
any of them is an owner's decision.

| Guarantee | What is promised | Owning guide | Checked by |
| --- | --- | --- | --- |
| `.cfg` and list syntax | Existing files keep loading with the same routes, gains, delays, paths and list selection; syntax changes need a recorded decision | [Configuration](configuration.md), [compatibility](configuration-compatibility.md) | Compatibility corpus, parser and fuzz replay |
| Serialized plug-in state and presets | Every version-1 preset whose rate and counts fit keeps loading; version 2 only marks a failed first load | [Presets](presets.md#compatibility), [presets without a layout](develop/plugin-identities.md#presets-without-a-layout) | `native.state` (literal version-1 and version-2 bytes), `vst3.adapter` (`version1_presets`), `plugins.presets`; files from releases are COMPAT-1 |
| Plug-in identity | VST3 class IDs, AU codes, LV2 URIs and bundle IDs never change after release | [Identities](develop/plugin-identities.md) | `identities` (frozen values), `vst3.identities` |
| C++ API and CMake package | Before 1.0, incompatible changes only in a new minor version; consumers require a matching minor version | [Versioning](develop/versioning.md#minor-and-major-versions), [package guide](develop/library.md) | Installed and source-tree consumer tests |
