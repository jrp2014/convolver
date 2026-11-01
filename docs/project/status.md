<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Status

[Documentation index](../README.md) · [TODO](../../TODO.md) · [Decisions](decisions.md) ·
[Releasing](../develop/releasing.md)

For jrp and anyone asking what is ready, what is not, and where the project is
going. [TODO](../../TODO.md) is the backlog and owns the order of work; the
linked guides own the behaviour.

**In short:** status checked on 1 November 2026 against `main` at `276c7dc`
(0.8.6), whose last source change is `e3336e9`
([PR #103](https://github.com/jrp2014/convolver/pull/103)). Every workflow
that runs on `main` passed on `e3336e9`, including
[Native and historical tests](https://github.com/jrp2014/convolver/actions/runs/41360889002),
[VST3 compatibility](https://github.com/jrp2014/convolver/actions/runs/41360888968),
[Audio Unit](https://github.com/jrp2014/convolver/actions/runs/41360888965)
and the
[Plug-in distribution candidates](https://github.com/jrp2014/convolver/actions/runs/41360888979).
Nothing has been published yet: there are no GitHub releases, and
preview packages exist only as temporary CI artifacts. The command-line
renderer, the preset tool, the Equalizer APO export and the VST3, Audio Unit
and LV2 plug-ins are implemented and tested automatically; VST3 and the
Audio Unit have Gain, Mix and Latency and share one macOS window. jrp has
run the Audio Unit and its window in GarageBand; the VST3 hosts (REAPER,
Audacity) have not been run, and no independent user has followed the
instructions. The next milestone is end-to-end testing of a fixed feature
set: an independent user installing and using an exact preview package, cut
from the 0.8 series that `main` is now in.
[What still blocks it](#what-still-blocks-the-first-preview) lists the
remaining work by TODO item.

The **oracle** named below is a direct double-precision convolution computed
independently of the engine, the project's reference for correct output
([testing](../develop/testing.md)).

Update this page when an item below changes level, and keep the date and
commit above current. Dated reviews stay as they were written. Detailed
progress has one home, the item's `Progress` lines in [TODO](../../TODO.md)
([recording progress](../develop/workflow.md#recording-progress-in-todo)):
this page gives each feature's level and each condition's status in a
sentence, names what remains, and links the item rather than retelling it.

Contents: [evidence levels](#evidence-levels) ·
[what users can run now](#what-users-can-run-now) ·
[defects found in reviews](#defects-found-in-reviews-and-lifecycle-tests) ·
[user journeys](#user-journeys-for-the-first-preview) ·
[next milestone](#next-milestone-an-independent-user-runs-a-preview) ·
[what still blocks it](#what-still-blocks-the-first-preview) ·
[compatibility guarantees](#compatibility-guarantees) ·
[what the tests do not establish](#what-the-tests-do-not-establish) ·
[direction](#direction)

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
| Command-line renderer `convolver` ([reference](../use/command-line.md)) | Yes | Yes: "Native and historical tests" workflow on Linux (Clang, GCC), macOS (FFTW, Accelerate) and Windows (MSVC); direct double-precision oracle | Not applicable | No; in the preview package since PR #101, not yet published |
| Configuration inspection and validation ([compatibility](../use/other-programs.md)) | Yes | Yes: parser tests, a corpus of example configurations, and replay of inputs found by fuzzing | Other readers of `.cfg` not checked (CFG-1) | No |
| Preset tool `convolver-preset` ([guide](../use/presets.md)) | Yes | Yes: `native.presets`, `plugins.presets`, `vst3.preset_format` | Not applicable | No; in the preview package since PR #38, not yet published |
| Equalizer APO export ([guide](../use/equalizer-apo.md)) | Yes | Yes: against a model of Equalizer APO 1.4.2, and on Windows CI in Equalizer APO's own `Benchmark.exe` (`export.equalizer_apo.runtime`) | Not yet checked on a real Windows device (HOST-4) | No; in the preview package since PR #38, not yet published |
| VST3 plug-in ([guide](../use/plugins.md)) | Yes: 1–8 in, 1–8 out; Gain, Mix and Latency (VST3-4); the window on macOS (VST3-11), not yet on Windows or Linux | Yes: SDK validator, pluginval at strictness 10, `vst3.harness` (against the oracle and the CLI) and `vst3.adapter` on Linux, macOS and Windows; `vst3.editor` and pluginval's editor tests on macOS | None yet (HOST-1: REAPER and Audacity chosen, not yet run) | No; preview candidates are CI artifacts only |
| LV2 plug-in ([guide](../use/plugins.md)) | Yes: adapter and layouts (LV2-1); filter files through the worker and saved paths (LV2-2) | Yes: `lv2.harness`, `lv2.adapter` and `lv2lint` on Linux | None | No; joins a later preview |
| Audio Unit ([guide](../use/plugins.md)) | Yes, built from source: adapter, state and `.aupreset` loading (AU-1, AU-2; a named host's preset menu is checked under AU-4); Gain, Mix and Latency (AU-7); Convolver's window with Choose… and file dropping (AU-6) | Yes: `au.harness`, `au.adapter`, `au.lifecycle`, `au.editor`, `au.bundle` and `auval -strict` on macOS | GarageBand: jrp's checks of the settings (25 October 2026) and of the window up to its redesign (`bbf94c5`, 1 November) passed; the full smoke test, including Latency, is not yet recorded (AU-4; what was checked is under AU-6) | No; in the first preview (AU-5), not yet in the package |
| C++ libraries ([CMake package](../develop/library.md)) | Yes | Yes: installed and source-tree consumer tests | Not applicable | Source only |
| FFmpeg pipe ([example](../use/command-line.md#with-ffmpeg)) | Documented | No; run by hand once (FFMPEG-1) | Not applicable | Not applicable |

## Defects found in reviews and lifecycle tests

jrp's reviews of `main` versions and his host runs, and the PLUG-7 lifecycle
sequences written after them, found defects that the earlier tests had missed. Since the 0.4.30
review, tests take their expectations from the shared contract and each
format's specification rather than from the implementation.

| Found by | What was wrong | Fixed in |
| --- | --- | --- |
| jrp, 0.4.19 | A reset during a filter transition delayed the next filter | [PR #40](https://github.com/jrp2014/convolver/pull/40), 23 October 2026 |
| jrp, 0.4.30 | An LV2 first load refused without a worker played unfiltered audio until the next control call; restoring an empty LV2 state map did not reset the plug-in; running out of memory in `HostFilter::load` could leave one filter playing while another was saved | [PR #55](https://github.com/jrp2014/convolver/pull/55) |
| The PLUG-7 sequences | A filter refused in the same LV2 cycle as an empty-state restore let the input play; VST3 and AU reported a load made while inactive as failed after keeping it, when memory ran out while working out its tail | [PR #58](https://github.com/jrp2014/convolver/pull/58) |
| jrp, 0.4.37 | A sound file with an unlisted header (Sun/NeXT AU) named `.pcm` or `.raw` was read as raw samples; syntax-only inspection opened filters, so a named pipe blocked it; a native test was missing from the `check` target and the sanitizer flags. Processing faults were not shown, and the log did not say what ran | [PR #66](https://github.com/jrp2014/convolver/pull/66), [PR #68](https://github.com/jrp2014/convolver/pull/68) |
| jrp, 0.4.43 | Kept debugging symbols were checked by name only; `Quantizer` could run out of memory where the caller could not see it; a non-finite attenuation estimate was accepted | [PR #72](https://github.com/jrp2014/convolver/pull/72) |
| jrp, 0.4.43 | Faults counted while a replacement was prepared were shown as Fault against the new filter; Fault could overwrite Load failed; the user guides did not yet explain Status and the log | [PR #73](https://github.com/jrp2014/convolver/pull/73), 25 October 2026 |
| jrp, `6920a45` (0.6.6) | A filter of 16,777,216 samples loaded (a `.cfg` allows 64 Mi samples) and then could not be saved (saved state holds 64 MiB), reproduced through PR #84's Audio Unit file loading and present in LV2's filter files (PLUG-12); after a failed first load, VST3, the Audio Unit and LV2 allocated a silent filter, so running out of memory there let the unfiltered input play while the status said the load failed (ROBUST-3) | [PR #91](https://github.com/jrp2014/convolver/pull/91), 31 October 2026 |
| jrp, GarageBand run of PR #97 (`bbf94c5`) and his questions after it | In the Audio Unit's window, Option-click on a slider during an unfinished typed edit was undone on leaving the box (found before merging); leaving the Gain or Mix box without typing set its rounded text, or a value older than a host change made while it had focus (on `main` in 0.8.1) | [PR #97](https://github.com/jrp2014/convolver/pull/97) (`ddfc64c`, re-checked by jrp in GarageBand) and [PR #98](https://github.com/jrp2014/convolver/pull/98), 1 November 2026 |
| jrp, `276c7dc` (0.8.6) | A VST3 filter that filled the saved state at default settings loaded and saved, and then could not be saved once Gain changed, since settings add 20 bytes (PLUG-13) | [PR #105](https://github.com/jrp2014/convolver/pull/105), 1 November 2026 |

## User journeys for the first preview

"Ready" means these journeys work, not that a set of TODO items is ticked
(REL-2 defines them; REL-3 is the independent walkthrough). Each plug-in
journey, on each platform the preview offers: download, install, load a
filter, check the expected output, save and reopen, and recover from a wrong
preset, using only the documentation. The CLI journey covers a filter that does
not fit its input instead of saving and presets.

| Journey | Filter | Surfaces | Status |
| --- | --- | --- | --- |
| Stereo room correction | [`examples/first-use/stereo`](../../examples/first-use/README.md) | VST3 and the Audio Unit in named hosts; CLI | CLI commands run in CI (`examples.first_use`). The package holds the example and a preset made from it, and the journey is written ([check a stereo filter in a host](../use/check-a-stereo-filter.md)), checked with `convolver --compare`; the release tests render it with the packaged CLI. On 1 November its package and command-line steps passed on a macOS arm64 candidate (REL-5). Not demonstrated in a host: that is part of the REAPER smoke test (HOST-1). |
| 2→6 crossover | [`examples/first-use/crossover`](../../examples/first-use/README.md) | VST3 and the Audio Unit in named hosts; CLI | CLI commands run in CI. The package holds the example and a 2→6 preset; the journey is not written. GarageBand offers only mono and stereo effects, so the Audio Unit's 2→6 journey needs a second host, still to be chosen in AU-4. Not demonstrated. |
| Offline file rendering | All three first-use filters | CLI from the preview package | Run from the source tree in CI; the release tests also run the packaged `bin/convolver` on all three examples. The journey is not written. Not demonstrated. |

Recovering from a wrong preset is the step to watch. VST3 keeps the working
filter but can report the rejection to the host only as `kResultFalse`, which
the host may not show; the read-only Status parameter says why, and the
Convolver log explains in full
([status and log](../use/plugins.md#status-and-the-convolver-log)). Whether
REAPER, Audacity and GarageBand display the Status parameter is for HOST-1 to
record.

## Next milestone: an independent user runs a preview

From jrp's [guidance of 22 October 2026](../review-2026-10/release-readiness-guidance-2026-10-22.md):
an independent user successfully installs and uses an exact preview package,
with the lifecycle regression fixed and the advertised workflows demonstrated.
The first preview holds VST3, the Audio Unit (macOS), the `convolver` CLI and
the preset tool, with the editor's settings and window in both plug-in
formats (until 25 October 2026 the Audio Unit was to join a later
preview). LV2 joins a later preview once its own journeys are demonstrated.

The first preview is cut from a 0.8.x tag.

| Condition | Source | Item | Status |
| --- | --- | --- | --- |
| Reset during a transition no longer plays the old filter | jrp's milestone | VST3-7 follow-up ([PR #40](https://github.com/jrp2014/convolver/pull/40)) | Done, 23 October 2026 |
| Lifecycle sequences checked against a reference model | Proposed with the milestone, so the next defect of this kind is caught | PLUG-7 | Done ([PR #58](https://github.com/jrp2014/convolver/pull/58)); `au.lifecycle` runs in macOS CI only |
| The edge cases from jrp's review of 0.4.43 fixed | jrp's review | DIAG-1, ROBUST-4 | Done ([PR #72](https://github.com/jrp2014/convolver/pull/72), [PR #73](https://github.com/jrp2014/convolver/pull/73), [PR #96](https://github.com/jrp2014/convolver/pull/96)) |
| The defects from jrp's later reviews fixed | jrp's reviews of `6920a45` and `276c7dc` | PLUG-12, ROBUST-3, PLUG-13 | Done ([PR #91](https://github.com/jrp2014/convolver/pull/91), [PR #105](https://github.com/jrp2014/convolver/pull/105)) |
| Smoke test of VST3 in at least one named host | Proposed with the milestone; the preview gate in [releasing](../develop/releasing.md#publishing-an-unsigned-preview) | HOST-1 | Not run. Steps written for REAPER (with the window) and Audacity on jrp's Mac ([smoke tests](../develop/host-testing.md#smoke-test-in-each-first-host)) |
| The Audio Unit validated, smoke-tested in a named host and in the package | jrp | AU-3, AU-4, AU-5 | `auval -strict` and `au.editor` run in CI. Remaining: pluginval for the component (AU-3), GarageBand's [smoke test](../develop/host-testing.md#garageband-audio-unit) (AU-4) and the component in the package (AU-5) |
| An exact preview package: VST3, the CLI, the preset tool, examples and guides | jrp's milestone | PLUG-5 | In progress: the package holds the preset tool, the Equalizer APO export, the CLI and the first-use examples with their presets. Remaining: the VST trademark notice |
| Settings (gain, mix, latency) and a window in each VST3 build and the Audio Unit | jrp | PLUG-9, PLUG-10, VST3-3, VST3-4, VST3-11 to VST3-13, AU-6, AU-7 | In progress: both formats have Gain, Mix and Latency and share the macOS window (the Audio Unit's checked by jrp in GarageBand, VST3's not yet in a host). Remaining: the Windows and Linux VST3 windows (VST3-12, VST3-13); the gain modes, computed gains and meters (PLUG-9, PLUG-10, AU-7); the host checks listed under AU-6 and VST3-11 |
| Journeys written as steps, with the presets they need in the package | jrp's milestone | REL-2 | In progress: the stereo journey is written ([check a stereo filter in a host](../use/check-a-stereo-filter.md)). Remaining: the 2→6 crossover and CLI journeys |
| An independent user completes the journeys from those exact files | jrp's milestone | REL-3 | Not started; uses the candidate's files before upload |

At publication, and before the next preview: COMPAT-1 keeps presets, a host
project and expected audio from the published release, so later previews are
tested against them. Not part of this milestone, but tracked: signing and
notarization (the production gate, REL-1 and PLUG-5), resource budgets
(PERF-3), other `.cfg` readers
(CFG-1) and Equalizer APO on a real device (HOST-4).

## What still blocks the first preview

In the order of [TODO's priorities](../../TODO.md#execution-priorities), on
1 November 2026. Each line names only what remains; the items hold the
detail.

1. **Open fixes:** none. The defects from jrp's reviews are fixed (table
   above).
2. **jrp's host smoke tests** on his Mac, from the latest candidate:
   HOST-1 (VST3 in REAPER, with the window, and Audacity) and AU-4 (the
   Audio Unit in GarageBand, with Latency). They can start now.
3. **The journeys and the package:** the 2→6 crossover and CLI journeys
   (REL-2), the VST trademark notice (PLUG-5).
4. **Settings, windows and the Audio Unit:** the VST3 windows on Windows and
   Linux (VST3-12, VST3-13); the gain modes, computed gains and meters
   (PLUG-9, PLUG-10, AU-7); the Audio Unit's pluginval run and component in
   the package (AU-3, AU-5); the remaining checks under AU-6 and VST3-11; and
   the [loading responsiveness](../../TODO.md#loading-responsiveness) gate.
5. **jrp's decisions:** who the REL-3 tester is, and the wording of the VST
   trademark notice (PLUG-5). The LV2 version mapping (LV2-5) is needed only
   before LV2 joins a preview.

jrp's review of `276c7dc` (1 November 2026): qualify this fixed feature set
against explicit acceptance criteria before adding further conveniences or
DSP optimisation. A smaller, macOS-first preview would need jrp to revise
the agreed scope.

## Compatibility guarantees

Four promises, kept separate because each breaks in a different way. Changing
any of them is an owner's decision.

| Guarantee | What is promised | Owning guide | Checked by |
| --- | --- | --- | --- |
| `.cfg` and list syntax | Existing files keep loading with the same routes, gains, delays, paths and list selection; syntax changes need a recorded decision | [Configuration](../use/configuration.md), [compatibility](../use/other-programs.md) | Compatibility corpus, parser and fuzz replay |
| Serialized plug-in state and presets | Every version-1 preset whose rate and counts fit keeps loading; version 2 only marks a failed first load | [Presets](../use/presets.md#old-and-new-versions), [presets without a layout](../develop/plugin-identities.md#presets-without-a-layout) | `native.state` (literal version-1 and version-2 bytes), `vst3.adapter` (`version1_presets`), `plugins.presets`; files from releases are COMPAT-1 |
| Plug-in identity | VST3 class IDs, AU codes, LV2 URIs and bundle IDs never change after release | [Identities](../develop/plugin-identities.md) | `identities` (frozen values), `vst3.identities` |
| C++ API and CMake package | Before 1.0, incompatible changes only in a new minor version; consumers require a matching minor version | [Versioning](../develop/versioning.md#minor-and-major-versions), [package guide](../develop/library.md) | Installed and source-tree consumer tests |

## What the tests do not establish

[Testing](../develop/testing.md) maps each test to the behaviour it checks.
The workflows in `.github/workflows/` describe the configured gates; check
[Actions](https://github.com/jrp2014/convolver/actions) for a particular
revision's results, since neither a workflow file nor a source tag proves a
pass. Automated tests do not establish compatibility in a named commercial
DAW, hard real-time deadlines, signed or notarized distribution, or native
builds of the historical Windows applications. The
[18 October validation record](../review-2026-10/current-validation.md) belongs
to its 0.2.0 baseline, and [performance measurements](../develop/fft-backends.md)
apply to their stated source, hardware and builds; no FFT backend is
established as fastest for every workload. Adoption of the `.cfg` format by
another program does not establish identical selection, path or routing
behaviour ([other programs](../use/other-programs.md)).

## Direction

1. Finish a dependable product: someone unfamiliar with the repository can
   download, install and use an exact preview package by following the
   documentation (the [next milestone](#next-milestone-an-independent-user-runs-a-preview)).
   Make the plug-ins straightforward to build, install and use in named hosts,
   with reproducible packages and documented platform support.
2. Preserve configuration and stream behaviour. Record consumer-specific
   differences and test changes against independent references, and against
   files kept from released builds.
3. Improve speed only for measured workloads, keeping latency, memory and
   callback behaviour visible alongside throughput. Resource budgets come
   first.
4. Keep maintenance cost down: move code into shared layers only when two
   formats need the same behaviour (as `HostFilter` was), and do not generalise
   host lifecycle or release machinery beyond the requirements in hand. The
   architecture is sound enough; the next decisions matter more than another
   substantial refactor (jrp's
   [guidance](../review-2026-10/release-readiness-guidance-2026-10-22.md),
   22 October 2026).

The plug-in direction is the [plug-in formats plan](plans/2026-10-21-plugin-formats.md):
VST3, LV2 and AU in parallel, CLAP on request. The dependency choices behind
the build (FFTW, Accelerate and Ooura, libsndfile) are explained in the
[implementation map](../develop/architecture.md#dependency-choices). Recorded
owner's decisions are in the [decisions log](decisions.md); where the code came
from is in [history](history.md).
