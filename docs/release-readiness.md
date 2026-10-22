<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Release readiness

[Documentation index](README.md) · [TODO](../TODO.md) · [Release record](releases/developer-preview.md)

**In short:** status checked on 22 October 2026 against `main` at `63562ff`
(0.4.19). Nothing has been published yet: there are no GitHub releases, and
preview packages exist only as temporary CI artifacts. The command-line
renderer, the preset tool, the Equalizer APO export and the VST3 plug-in are
implemented and tested automatically; no named host has been tested and no
independent user has followed the instructions. The next milestone is an
independent user installing and using an exact preview package.

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
with the repository completed it from a published package using the
documentation alone.

## What users can run now

| Feature | Implemented | Automatically tested | Named host | Published |
| --- | --- | --- | --- | --- |
| Command-line renderer `convolver` ([reference](portable-core.md)) | Yes | Yes: native workflow on Linux (Clang, GCC), macOS (FFTW, Accelerate) and Windows (MSVC); direct double-precision oracle | Not applicable | No; build from source |
| Configuration inspection and validation ([compatibility](configuration-compatibility.md)) | Yes | Yes: parser, corpus and fuzz replay | Other readers of `.cfg` not checked (CFG-1) | No |
| Preset tool `convolver-preset` ([guide](presets.md)) | Yes | Yes: `native.presets`, `plugins.presets`, `vst3.preset_format` | Not applicable | No; joins the preview package with PLUG-5 |
| Equalizer APO export ([guide](equalizer-apo.md)) | Yes | Against a model of Equalizer APO 1.4.2 only; a check in Equalizer APO itself is HOST-4 (in progress) | Not run in Equalizer APO yet | No |
| VST3 plug-in ([guide](vst3.md)) | Yes: 1–8 in, 1–8 out | Yes: SDK validator, pluginval at strictness 10, `vst3.harness` (66 cases against the oracle and the CLI) on Linux, macOS and Windows | None yet (HOST-1: REAPER and Carla suggested) | No; preview candidates are CI artifacts only |
| LV2 plug-in | In development (LV2-1) | Planned through the shared harness | None | No |
| AU plug-in | In development (AU-1, AU-2) | Planned through the shared harness and `auval` | None | No |
| C++ libraries ([CMake package](cmake-package.md)) | Yes | Yes: installed and source-tree consumer tests | Not applicable | Source only |
| FFmpeg pipe ([example](portable-core.md#with-ffmpeg)) | Documented | No; run by hand once (FFMPEG-1) | Not applicable | Not applicable |

Known defect: a reset during a filter transition delayed the next filter
(found in jrp's review of 0.4.19; the fix is the VST3-7 follow-up, not yet
merged: [PR #40](https://github.com/jrp2014/convolver/pull/40)). Lifecycle sequences that would catch defects like it are PLUG-7.

## User journeys for the first preview

"Ready" means these journeys work, not that a set of TODO items is ticked
(REL-2 defines them; REL-3 is the independent walkthrough). Each journey, on
each advertised platform: download, install, load a filter, get the expected
output, save and reopen, and recover from a wrong preset, using only the
documentation.

| Journey | Filter | Surfaces | Status |
| --- | --- | --- | --- |
| Stereo room correction | [`examples/first-use/stereo`](../examples/first-use/README.md) | VST3 in a named host; CLI | CLI commands run in CI (`examples.first_use`). The preview package has no preset made from this filter yet. Not demonstrated. |
| 2→6 crossover | [`examples/first-use/crossover`](../examples/first-use/README.md) | VST3 in a named host; CLI | CLI commands run in CI. No 2→6 preset in the package yet. Not demonstrated. |
| Offline file rendering | All three first-use filters | CLI from a preview package | Run from the source tree in CI; the preview package's CLI is not yet walked through. Not demonstrated. |

Recovering from a wrong preset is the weakest step today: VST3 keeps the
working filter but reports the rejection only as a refusal, so the host may
show nothing ([presets](presets.md#when-a-preset-does-not-fit)). PLUG-8 keeps
the reason; DIAG-1 decides where it is shown.

## Next milestone: an independent user runs a preview

From jrp's [guidance of 22 October 2026](review-2026-10/release-readiness-guidance-2026-10-22.md):
an independent user successfully installs and uses an exact preview package,
with the lifecycle regression fixed and the advertised workflows demonstrated.

| Blocker | Item | Status |
| --- | --- | --- |
| Reset during a transition plays the old filter | VST3-7 follow-up ([PR #40](https://github.com/jrp2014/convolver/pull/40)) | Fix in review, not merged |
| Lifecycle sequences checked against a reference model | PLUG-7 | Not started |
| Journeys written as steps for each advertised platform, with the presets they need in the package | REL-2 | Not started |
| Preview package contents (plug-in, CLI, preset tool, examples, guides) | PLUG-5 | Tools being added; not complete |
| Smoke test in at least one named host per advertised format | HOST-1 | Not run |
| An exact candidate published by jrp as an unsigned pre-release | PLUG-5 | Not done |
| Presets, a host project and expected audio kept from that release | COMPAT-1 | Not started; needs the publication |
| An independent user completes the journeys from that package | REL-3 | Not started; needs a tester |

Not blockers for this milestone, but tracked: signing and notarization (the
production gate, PLUG-5), resource budgets (PERF-3), showing failure messages
(PLUG-8, DIAG-1), other `.cfg` readers (CFG-1) and Equalizer APO itself (HOST-4).

## Compatibility guarantees

Four promises, kept separate because each breaks in a different way. Changing
any of them is an owner's decision.

| Guarantee | What is promised | Owning guide | Checked by |
| --- | --- | --- | --- |
| `.cfg` and list syntax | Existing files keep loading with the same routes, gains, delays, paths and list selection; syntax changes need a recorded decision | [Configuration](configuration.md), [compatibility](configuration-compatibility.md) | Compatibility corpus, parser and fuzz replay |
| Serialized plug-in state and presets | Every version-1 preset whose rate and counts fit keeps loading; version 2 only marks a failed first load | [Presets](presets.md#compatibility), [presets without a layout](plugin-identities.md#presets-without-a-layout) | `native.state` (literal version-1 and version-2 bytes), `vst3.adapter` (`version1_presets`), `plugins.presets`; files from releases are COMPAT-1 |
| Plug-in identity | VST3 class IDs, AU codes, LV2 URIs and bundle IDs never change after release | [Identities](plugin-identities.md) | `identities` (frozen values), `vst3.identities` |
| C++ API and CMake package | Before 1.0, incompatible changes only in a new minor version; consumers require a matching minor version | [Versioning](versioning.md#minor-and-major-versions), [package guide](cmake-package.md) | Installed and source-tree consumer tests |
