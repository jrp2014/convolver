<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Status

[Documentation index](../README.md) · [TODO](../../TODO.md) · [Decisions](decisions.md) ·
[Releasing](../develop/releasing.md)

For jrp and anyone asking what is ready, what is not, and where the project is
going. [TODO](../../TODO.md) is the backlog and owns the order of work; the
linked guides own the behaviour.

**In short:** status checked on 24 October 2026 against `main` at `9fa13f7`
(0.4.44). Nothing has been published yet: there are no GitHub releases, and
preview packages exist only as temporary CI artifacts. The command-line
renderer, the preset tool, the Equalizer APO export and the VST3, Audio Unit
and LV2 plug-ins are implemented and tested automatically; no named host has
been tested and no independent user has followed the instructions. The next
milestone is end-to-end testing: an independent user installing and using an
exact preview package, cut from the 0.5 series. Four things
[still block it](#what-still-blocks-the-first-preview): jrp's host smoke
tests (VST3, and the Audio Unit in GarageBand), the CLI and journey presets in
the package with the journeys written, the plug-in settings and windows with
the Audio Unit in the package, and a tester for the walkthrough.

The **oracle** named below is a direct double-precision convolution computed
independently of the engine, the project's reference for correct output
([testing](../develop/testing.md)).

Update this page when an item below changes level, and keep the date and
commit above current. Dated reviews stay as they were written.

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
| Command-line renderer `convolver` ([reference](../use/command-line.md)) | Yes | Yes: "Native and historical tests" workflow on Linux (Clang, GCC), macOS (FFTW, Accelerate) and Windows (MSVC); direct double-precision oracle | Not applicable | No; build from source. The first preview will ship it (PLUG-5) |
| Configuration inspection and validation ([compatibility](../use/other-programs.md)) | Yes | Yes: parser tests, a corpus of example configurations, and replay of inputs found by fuzzing | Other readers of `.cfg` not checked (CFG-1) | No |
| Preset tool `convolver-preset` ([guide](../use/presets.md)) | Yes | Yes: `native.presets`, `plugins.presets`, `vst3.preset_format` | Not applicable | No; in the preview package since PR #38, not yet published |
| Equalizer APO export ([guide](../use/equalizer-apo.md)) | Yes | Yes: against a model of Equalizer APO 1.4.2, and on Windows CI in Equalizer APO's own `Benchmark.exe` (`export.equalizer_apo.runtime`) | Not yet checked on a real Windows device (HOST-4) | No; in the preview package since PR #38, not yet published |
| VST3 plug-in ([guide](../use/plugins.md)) | Yes: 1–8 in, 1–8 out | Yes: SDK validator, pluginval at strictness 10, `vst3.harness` (against the oracle and the CLI) on Linux, macOS and Windows | None yet (HOST-1: REAPER and Audacity chosen, not yet run) | No; preview candidates are CI artifacts only |
| LV2 plug-in ([guide](../use/plugins.md)) | Yes: adapter and layouts (LV2-1); filter files through the worker and saved paths (LV2-2) | Yes: `lv2.harness`, `lv2.adapter` and `lv2lint` on Linux | None | No; joins a later preview |
| Audio Unit ([guide](../use/plugins.md)) | Yes, built from source: adapter, state and `.aupreset` loading (AU-1, AU-2; a named host's preset menu is checked under AU-4) | Yes: `au.harness`, `au.adapter`, `au.bundle` and `auval -strict` on macOS | None yet (AU-4: GarageBand chosen, not yet run) | No; in the first preview (AU-5), not yet in the package |
| C++ libraries ([CMake package](../develop/library.md)) | Yes | Yes: installed and source-tree consumer tests | Not applicable | Source only |
| FFmpeg pipe ([example](../use/command-line.md#with-ffmpeg)) | Documented | No; run by hand once (FFMPEG-1) | Not applicable | Not applicable |

## Defects found in reviews and lifecycle tests

jrp's reviews of `main` versions, and the PLUG-7 lifecycle sequences written
after them, found defects that the earlier tests had missed. Since the 0.4.30
review, tests take their expectations from the shared contract and each
format's specification rather than from the implementation.

| Found by | What was wrong | Fixed in |
| --- | --- | --- |
| jrp, 0.4.19 | A reset during a filter transition delayed the next filter | [PR #40](https://github.com/jrp2014/convolver/pull/40), 23 October 2026 |
| jrp, 0.4.30 | An LV2 first load refused without a worker played unfiltered audio until the next control call; restoring an empty LV2 state map did not reset the plug-in; running out of memory in `HostFilter::load` could leave one filter playing while another was saved | [PR #55](https://github.com/jrp2014/convolver/pull/55) |
| The PLUG-7 sequences | A filter refused in the same LV2 cycle as an empty-state restore let the input play; VST3 and AU reported a load made while inactive as failed after keeping it, when memory ran out while working out its tail | [PR #58](https://github.com/jrp2014/convolver/pull/58) |
| jrp, 0.4.37 | A sound file with an unlisted header (Sun/NeXT AU) named `.pcm` or `.raw` was read as raw samples; syntax-only inspection opened filters, so a named pipe blocked it; a native test was missing from the `check` target and the sanitizer flags. Processing faults were not shown, and the log did not say what ran | [PR #66](https://github.com/jrp2014/convolver/pull/66), [PR #68](https://github.com/jrp2014/convolver/pull/68) |
| jrp, 0.4.43 | Kept debugging symbols were checked by name only; `Quantizer` could run out of memory where the caller could not see it; a non-finite attenuation estimate was accepted | [PR #72](https://github.com/jrp2014/convolver/pull/72) |
| jrp, 0.4.43 | Faults counted while a replacement was prepared were shown as Fault against the new filter; Fault could overwrite Load failed; the user guides did not yet explain Status and the log | [PR #73](https://github.com/jrp2014/convolver/pull/73), open |

## User journeys for the first preview

"Ready" means these journeys work, not that a set of TODO items is ticked
(REL-2 defines them; REL-3 is the independent walkthrough). Each plug-in
journey, on each platform the preview offers: download, install, load a
filter, check the expected output, save and reopen, and recover from a wrong
preset, using only the documentation. The CLI journey covers a filter that does
not fit its input instead of saving and presets.

| Journey | Filter | Surfaces | Status |
| --- | --- | --- | --- |
| Stereo room correction | [`examples/first-use/stereo`](../../examples/first-use/README.md) | VST3 and the Audio Unit in named hosts; CLI | CLI commands run in CI (`examples.first_use`). The preview package has no preset made from this filter yet. Not demonstrated. |
| 2→6 crossover | [`examples/first-use/crossover`](../../examples/first-use/README.md) | VST3 and the Audio Unit in named hosts; CLI | CLI commands run in CI. No 2→6 preset in the package yet. GarageBand offers only stereo effects, so the Audio Unit's 2→6 journey needs a second host, still to be chosen in AU-4. Not demonstrated. |
| Offline file rendering | All three first-use filters | CLI from the preview package | Run from the source tree in CI. The package has no CLI yet; the first preview will ship it (PLUG-5). Not demonstrated. |

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

The first preview is cut from a 0.5.x tag.

| Condition | Source | Item | Status |
| --- | --- | --- | --- |
| Reset during a transition no longer plays the old filter | jrp's milestone | VST3-7 follow-up ([PR #40](https://github.com/jrp2014/convolver/pull/40)) | Done, 23 October 2026 |
| Lifecycle sequences checked against a reference model | Proposed with the milestone, so the next defect of this kind is caught | PLUG-7 | Done ([PR #58](https://github.com/jrp2014/convolver/pull/58)); `au.lifecycle` runs in macOS CI only |
| The edge cases from jrp's review of 0.4.43 fixed | jrp's review | DIAG-1, ROBUST-4 | Symbol check and low-memory fixes merged ([PR #72](https://github.com/jrp2014/convolver/pull/72)); fault state after reloads, Load failed before Fault and the guides in [PR #73](https://github.com/jrp2014/convolver/pull/73), open |
| Smoke test of VST3 in at least one named host | Proposed with the milestone; the preview gate in [releasing](../develop/releasing.md#publishing-an-unsigned-preview) | HOST-1 | Not run. Steps written for REAPER and Audacity on jrp's Mac ([smoke tests](../develop/host-testing.md#smoke-test-in-each-first-host)) |
| The Audio Unit validated, smoke-tested in a named host and in the package | jrp | AU-3, AU-4, AU-5 | `auval -strict` runs in CI; pluginval for the component, GarageBand's [smoke test](../develop/host-testing.md#garageband-audio-unit) and the component in the package remain |
| An exact preview package: VST3, the CLI, the preset tool, examples and guides | jrp's milestone | PLUG-5 | Preset tool and Equalizer APO export in the package ([PR #38](https://github.com/jrp2014/convolver/pull/38)); the CLI, the journey presets and the VST trademark notice remain |
| Settings (gain, mix, latency) and a window in each VST3 build and the Audio Unit | jrp | PLUG-9, PLUG-10, VST3-3, VST3-4, VST3-11 to VST3-13, AU-6, AU-7 | Planned ([editor plan](plans/2026-10-24-plugin-editor.md#phases-and-todo-items), [PR #78](https://github.com/jrp2014/convolver/pull/78)); not started |
| Journeys written as steps, with the presets they need in the package | jrp's milestone | REL-2 | Not started |
| An independent user completes the journeys from those exact files | jrp's milestone | REL-3 | Not started; uses the candidate's files before upload |

At publication, and before the next preview: COMPAT-1 keeps presets, a host
project and expected audio from the published release, so later previews are
tested against them. Not part of this milestone, but tracked: signing and
notarization (the production gate, REL-1 and PLUG-5), resource budgets
(PERF-3), the rest of the diagnostics review (DIAG-1), other `.cfg` readers
(CFG-1) and Equalizer APO on a real device (HOST-4).

## What still blocks the first preview

In the order of [TODO's priorities](../../TODO.md#execution-priorities), on
25 October 2026:

1. **Open fixes: done.** [PR #73](https://github.com/jrp2014/convolver/pull/73)
   (DIAG-1) was merged on 25 October 2026 (`7ed7e38`), and the "Plug-in
   distribution candidates" run on that commit passed
   ([run](https://github.com/jrp2014/convolver/actions/runs/40145929436)).
2. **jrp's time on his Mac** for the HOST-1 smoke tests in REAPER and
   Audacity. They need only the latest candidate artifact, not the CLI, so they
   can start once the fix is merged. GarageBand's Audio Unit test (AU-4), at
   the same sitting, now gates this preview too.
3. **Unclaimed work:** PLUG-5's first-preview contents (the `convolver` CLI and
   presets made from the stereo and 2→6 first-use filters, in the package) and
   the REL-2 journeys. No pull request is open for either.
4. **The editor's settings and windows, and the Audio Unit:** PLUG-9 and PLUG-10, then VST3-3, VST3-4, AU-7 and AU-6's
   properties, then the macOS window shared by VST3 and the Audio Unit, and
   the Windows and Linux windows (VST3-11 to VST3-13, AU-6), as planned in
   the [editor plan](plans/2026-10-24-plugin-editor.md#phases-and-todo-items);
   and the Audio Unit's validation, guide and package (AU-3 to AU-5).
   Unclaimed; the largest remaining part of the preview.
5. **jrp's decisions:** who the REL-3 tester is, and the wording of the VST
   trademark notice (PLUG-5). The LV2 version mapping (LV2-5) is needed only
   before LV2 joins a preview.

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
