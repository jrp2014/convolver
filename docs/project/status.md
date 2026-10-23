<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Status

[Documentation index](../README.md) · [TODO](../../TODO.md) · [Decisions](decisions.md) ·
[Releasing](../develop/releasing.md)

For jrp and anyone asking what is ready, what is not, and where the project is
going. [TODO](../../TODO.md) is the backlog and owns the order of work; the
linked guides own the behaviour.

**In short:** status checked on 23 October 2026 against `main` at `f593216`
(0.4.25). Nothing has been published yet: there are no GitHub releases, and
preview packages exist only as temporary CI artifacts. The command-line
renderer, the preset tool, the Equalizer APO export and the VST3, Audio Unit
and LV2 plug-ins are implemented and tested automatically; no named host has
been tested and no independent user has followed the instructions. The next
milestone is an independent user installing and using an exact preview
package.

The **oracle** named below is a direct double-precision convolution computed
independently of the engine, the project's reference for correct output
([testing](../develop/testing.md)).

Update this page when an item below changes level, and keep the date and
commit above current. Dated reviews stay as they were written.

Contents: [evidence levels](#evidence-levels) ·
[what users can run now](#what-users-can-run-now) ·
[user journeys](#user-journeys-for-the-first-preview) ·
[next milestone](#next-milestone-an-independent-user-runs-a-preview) ·
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
| VST3 plug-in ([guide](../use/plugins.md)) | Yes: 1–8 in, 1–8 out | Yes: SDK validator, pluginval at strictness 10, `vst3.harness` (against the oracle and the CLI) on Linux, macOS and Windows | None yet (HOST-1: REAPER and Carla suggested) | No; preview candidates are CI artifacts only |
| LV2 plug-in ([guide](../use/plugins.md)) | Yes: adapter and layouts (LV2-1); loading filters from a file is LV2-2 | Yes: `lv2.harness`, `lv2.adapter` and `lv2lint` on Linux | None | No; joins a later preview |
| Audio Unit ([guide](../use/plugins.md)) | Yes, built from source (AU-1, AU-2; PR #44) | Yes: `au.harness`, `au.adapter`, `au.bundle` and `auval -strict` on macOS | None (AU-4) | No; joins a later preview |
| C++ libraries ([CMake package](../develop/library.md)) | Yes | Yes: installed and source-tree consumer tests | Not applicable | Source only |
| FFmpeg pipe ([example](../use/command-line.md#with-ffmpeg)) | Documented | No; run by hand once (FFMPEG-1) | Not applicable | Not applicable |

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
| Stereo room correction | [`examples/first-use/stereo`](../../examples/first-use/README.md) | VST3 in a named host; CLI | CLI commands run in CI (`examples.first_use`). The preview package has no preset made from this filter yet. Not demonstrated. |
| 2→6 crossover | [`examples/first-use/crossover`](../../examples/first-use/README.md) | VST3 in a named host; CLI | CLI commands run in CI. No 2→6 preset in the package yet. Not demonstrated. |
| Offline file rendering | All three first-use filters | CLI from the preview package | Run from the source tree in CI. The package has no CLI yet; the first preview will ship it (PLUG-5). Not demonstrated. |

Recovering from a wrong preset is the weakest step today: VST3 keeps the
working filter but reports the rejection only as `kResultFalse`, so the host
may show nothing ([loading filters](../use/plugins.md#loading-a-filter)). PLUG-8 (done)
keeps the reason; DIAG-1 decides where it is shown.

## Next milestone: an independent user runs a preview

From jrp's [guidance of 22 October 2026](../review-2026-10/release-readiness-guidance-2026-10-22.md):
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
