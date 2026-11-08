<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Status

[Documentation index](../README.md) · [TODO](../../TODO.md) · [Decisions](decisions.md) ·
[Releasing](../develop/releasing.md)

For jrp and anyone asking what is ready, what is not, and where the project is
going. [TODO](../../TODO.md) is the backlog and owns the order of work; the
linked guides own the behaviour.

**In short:** status checked on 8 November 2026 against `main` at
`5cc95cd` (0.12.1), whose last source change is `b50e50e`
([PR #136](https://github.com/jrp2014/convolver/pull/136): one set of words
for filters, presets and projects, DOC-9). The 0.11 series began with
[PR #131](https://github.com/jrp2014/convolver/pull/131), which lets a filter
read the first inputs of a wider host bus (PLUG-23), and the 0.12 series with
[PR #128](https://github.com/jrp2014/convolver/pull/128), which redesigned the
shared macOS window to name the filter and say each fact once (PLUG-22).
Every workflow that runs on `main` passed on `b50e50e`, including
[Native and historical tests](https://github.com/jrp2014/convolver/actions/runs/42427162872)
and the
[Plug-in distribution candidates](https://github.com/jrp2014/convolver/actions/runs/42427162803).
Since then, [PR #141](https://github.com/jrp2014/convolver/pull/141) named the
packaged example presets after their folder (PLUG-28, 0.12.2).
Nothing has been published yet: there are no GitHub releases, and
preview packages exist only as temporary CI artifacts. The command-line
renderer, the preset tool, the Equalizer APO export and the VST3, Audio Unit
and LV2 plug-ins are implemented and tested automatically; VST3 and the
Audio Unit have Gain, Mix and Latency and share one macOS window. jrp has
run the VST3 plug-in in REAPER (6 and 7 November) and Audacity (7
November), and the Audio Unit in GarageBand (up to 1 November), all with
the window from before PLUG-22; on 8 November, with the 0.12.5 candidate,
he ran the current window in REAPER and GarageBand, where controls, saved
state and exports passed and listening was not checked. The one open fix, a new VST3 Latency that
REAPER used only after the project was reopened, is done: VST3-16
([PR #134](https://github.com/jrp2014/convolver/pull/134)) passed jrp's
REAPER check on 8 November
([record](../archive/reviews/2026-11-08-vst3-16-reaper-acceptance.md)). No independent
user has followed the instructions. The next milestone is end-to-end
testing of a fixed feature set: an independent user installing and using
an exact preview package, cut from the 0.12 series.
[What still blocks it](#what-still-blocks-the-first-preview) lists each
remaining gate once, with its status and evidence.

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
[performance](#performance-what-is-measured) ·
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
| VST3 plug-in ([guide](../use/plugins.md)) | Yes: 1–8 in, 1–8 out; Gain, Mix and Latency (VST3-4); the window on macOS (VST3-11), not yet on Windows or Linux | Yes: SDK validator, pluginval at strictness 10, `vst3.harness` (against the oracle and the CLI) and `vst3.adapter` on Linux, macOS and Windows; `vst3.editor` and pluginval's editor tests on macOS | REAPER 7.85 on macOS (HOST-1): on 6 November (candidate 0.10.9) the smoke steps, the stereo journey and the window's settings passed, except that a new Latency was used only after reopening the project ([record](../archive/reviews/2026-11-06-host1-reaper.md)), which VST3-16 fixed and jrp checked in REAPER on 8 November ([record](../archive/reviews/2026-11-08-vst3-16-reaper-acceptance.md)); on 7 November the 2→6 crossover passed with PR #131's candidate ([record](../archive/reviews/2026-11-07-reaper-crossover-retest.md)). Audacity 4.0.2 on macOS (7 November 2026, candidate 0.10.13): every stereo export with a filter or bypass matched the CLI, including after a refused preset and after restarting Audacity; listening, mono and a Latency change not yet checked there ([record](../archive/reviews/2026-11-07-host1-audacity.md)). Both hosts ran the window from before PLUG-22 (0.12.0); REAPER ran the current window on 8 November with the 0.12.5 candidate (part C: controls and state passed, listening not checked; [record](../archive/reviews/2026-11-08-host1-reaper-window.md)) | No; preview candidates are CI artifacts only |
| LV2 plug-in ([guide](../use/plugins.md)) | Yes: adapter and layouts (LV2-1); filter files through the worker and saved paths (LV2-2) | Yes: `lv2.harness`, `lv2.adapter`, `lv2lint` and pluginval at strictness 10 on Linux; a headless Ardour session records, restores and exports the first-use examples against the oracle (HOST-5) | None | No; joins a later preview |
| Audio Unit ([guide](../use/plugins.md)) | Yes: adapter, state and `.aupreset` loading (AU-1, AU-2; a named host's preset menu is checked under AU-4); Gain, Mix and Latency (AU-7); Convolver's window with Choose… and file dropping (AU-6) | Yes: `au.harness`, `au.adapter`, `au.lifecycle`, `au.editor`, `au.bundle`, `auval -strict`, pluginval at strictness 10 (with its editor tests) and an AVAudioEngine render of the first-use examples against the oracle (AU-15) on macOS; in each macOS candidate, `au.bundle`'s checks on the packaged component and `auval -strict` after installing the `.pkg` | GarageBand: jrp's checks of the settings (25 October 2026) and of the window up to its redesign (`bbf94c5`, 1 November) passed (DIAG-2 has since reworded its messages and moved Bypass and the diagnostics buttons, and PLUG-22 redesigned the window; jrp saw both on 8 November with the 0.12.5 candidate, when the controls, state and exports passed and listening, bypass clicks, live latency compensation and VoiceOver were not checked, [record](../archive/reviews/2026-11-08-au4-garageband.md)); the full smoke test, including Latency, is not yet recorded (AU-4; what was checked is under AU-6) | No; in the macOS preview packages since PR #115 (AU-5), not yet published |
| C++ libraries ([CMake package](../develop/library.md)) | Yes | Yes: installed and source-tree consumer tests | Not applicable | Source only |
| FFmpeg pipe ([example](../use/command-line.md#with-ffmpeg)) | Documented | Yes: `native.ffmpeg_pipe` runs the example with Ubuntu's FFmpeg in the Linux g++ Release CI job (FFMPEG-1) | Not applicable | Not applicable |

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
| Stereo room correction | [`examples/first-use/stereo`](../../examples/first-use/README.md) | VST3 and the Audio Unit in named hosts; CLI | CLI commands run in CI (`examples.first_use`). The package holds the example and a preset made from it, and the journey is written ([check a stereo filter in a host](../use/check-a-stereo-filter.md)), checked with `convolver --compare`; the release tests render it with the packaged CLI. On 1 November its package and command-line steps passed on a macOS arm64 candidate (REL-5). On 6 November its host steps passed in REAPER 7.85 on macOS with the VST3 plug-in: render, save and reopen, and presets that do not fit, each compared as a Match ([record](../archive/reviews/2026-11-06-host1-reaper.md#part-b-stereo-numerical-journey), HOST-1). |
| 2→6 crossover | [`examples/first-use/crossover`](../../examples/first-use/README.md) | VST3 and the Audio Unit in named hosts; CLI | CLI commands run in CI, and the release tests run the packaged CLI on it. The package holds the example and a 2→6 preset, and the journey is written for REAPER, with the VST3 plug-in and the Audio Unit ([check a crossover in a host](../use/check-a-crossover-in-a-host.md)), checked with `convolver --compare`. GarageBand and Audacity offer only mono and stereo effects. The VST3 crossover passed in REAPER 7.85 on macOS on 7 November (HOST-1), with [PR #131](https://github.com/jrp2014/convolver/pull/131)'s candidate, which lets a filter read the first inputs of a wider bus (REAPER gives the plug-in 6 inputs and 6 outputs, so the first run, on 6 November, refused the preset): its render matched (worst error −128.47 dB), Mix 0% stayed fully filtered, bypass was silent, a six-input preset was refused on a stereo track, and the filter came back after the wrong rate and after saving, quitting and reopening, and FX Offline then Online ([record](../archive/reviews/2026-11-07-reaper-crossover-retest.md)). The Audio Unit in REAPER not yet run; no recorded decision makes it a first-preview gate, and REL-3 covers it only if the tester uses it. |
| Offline file rendering | All three first-use filters | CLI from the preview package | The journey is written ([check file rendering with the command line](../use/check-files-with-the-command-line.md)): it renders and compares all three examples and shows the messages for a filter at another rate or channel count. The release tests run every command in it from the packaged `bin/convolver`, including the two refusals and their messages. Not demonstrated by a user. |

Recovering from a wrong preset is the step to watch. VST3 keeps the working
filter but can report the rejection to the host only as `kResultFalse`, which
the host may not show; the read-only Status parameter says why, and the
Convolver log explains in full
([status and log](../use/plugins.md#status-and-the-convolver-log)). Audacity
showed the Status text (HOST-1), and GarageBand's generic window showed the
Status row with no value, which is why the Audio Unit has its own window
(AU-4, AU-6); REAPER's display of it remains (gate 2 below).

## Next milestone: an independent user runs a preview

From jrp's [guidance of 22 October 2026](../review-2026-10/release-readiness-guidance-2026-10-22.md):
an independent user successfully installs and uses an exact preview package,
with the lifecycle regression fixed and the advertised workflows demonstrated.
The first preview is macOS-only ([decisions](decisions.md#plug-in-behaviour)):
the VST3 plug-in and the Audio Unit with the existing shared window and their
Gain, Mix and Latency settings, the `convolver` CLI and the preset tool. The
Windows and Linux windows, the other gain modes and the meters come in later
previews, and LV2 joins one once its own journeys are demonstrated. Until the
first independent walkthrough, optional features and further hardening wait;
crashes, lost state, wrong audio, ordinary compatibility failures and
demonstrated dropouts are still fixed at once.

The first preview is cut from a 0.12.x tag. The milestone's conditions
already met are below; those still open are the gates in
[what still blocks the first preview](#what-still-blocks-the-first-preview).

| Condition met | Source | Item | Done |
| --- | --- | --- | --- |
| Reset during a transition no longer plays the old filter | jrp's milestone | VST3-7 follow-up | 23 October 2026, [PR #40](https://github.com/jrp2014/convolver/pull/40) |
| Lifecycle sequences checked against a reference model | Proposed with the milestone, so the next defect of this kind is caught | PLUG-7 | [PR #58](https://github.com/jrp2014/convolver/pull/58); `au.lifecycle` runs in macOS CI only |
| The edge cases from jrp's review of 0.4.43 fixed | jrp's review | DIAG-1, ROBUST-4 | [PR #72](https://github.com/jrp2014/convolver/pull/72), [PR #73](https://github.com/jrp2014/convolver/pull/73), [PR #96](https://github.com/jrp2014/convolver/pull/96) |
| The defects from jrp's later reviews fixed | jrp's reviews of `6920a45` and `276c7dc` | PLUG-12, ROBUST-3, PLUG-13 | [PR #91](https://github.com/jrp2014/convolver/pull/91), [PR #105](https://github.com/jrp2014/convolver/pull/105) |
| Settings (Gain, Mix, Latency) and the shared macOS window implemented in VST3 and the Audio Unit, and checked in CI | jrp | PLUG-9, PLUG-10, VST3-3, VST3-4, AU-7 | Implemented and checked in CI (`vst3.editor`, `au.editor`, pluginval's editor tests); the window last redesigned in [PR #128](https://github.com/jrp2014/convolver/pull/128) (PLUG-22, 7 November 2026). Its host checks (VST3-11, AU-6) are gates 2 and 3 below. Later previews: the Windows and Linux windows (VST3-12, VST3-13), the other gain modes and the meters (PLUG-9, PLUG-10, AU-7) |
| The Audio Unit validated by `auval` and pluginval, and in the package | jrp | AU-3, AU-5 | `auval -strict` and `au.editor` in CI; pluginval on the installed component in the Audio Unit workflow; the component in both macOS candidates since PR #115, with `auval -strict` after the `.pkg` installs it (first passed in candidates [run 42108659224](https://github.com/jrp2014/convolver/actions/runs/42108659224)). AU-3's ThreadSanitizer build is gate 4 below (done) |
| Journeys written as steps, with the presets they need in the package | jrp's milestone | REL-2 | 6 November 2026: the stereo, 2→6 crossover and CLI journeys (table above) |

At publication, and before the next preview: COMPAT-1 keeps presets, a host
project and expected audio from the published release, so later previews are
tested against them. Not part of this milestone, but tracked: signing and
notarization (the production gate, REL-1 and PLUG-5), resource budgets and
the wider supported workloads (PERF-3; the loading gate uses its own finite
preview workloads), other `.cfg` readers
(CFG-1) and Equalizer APO on a real device (HOST-4).

## What still blocks the first preview

The [acceptance plan](plans/2026-11-08-preview-acceptance.md) defines the stop
rule, blocker classification and retest boundaries. The gate results below
remain authoritative; writing the plan does not pass or waive any gate.

These are the gates on 8 November 2026, each listed once (gates 1 and 4 are
done and stay listed so the numbers hold), in the
order of [TODO's priorities](../../TODO.md#execution-priorities), except that
the exact candidate (gate 6) comes just before the walkthrough that uses it.
Each row names only
what remains and where the evidence is; the items hold the detail. Gates 2,
3, 5 and 7 are run on one exact candidate.

| Gate | Item | Status | Evidence |
| --- | --- | --- | --- |
| 1. A new VST3 Latency comes into use in REAPER | VST3-16 | **Done** (8 November 2026). In REAPER 7.85 a new Latency had stayed unused until the project was reopened; since [PR #134](https://github.com/jrp2014/convolver/pull/134) it plays at once. jrp's check with its candidate passed: stopped and while playing, 256 to 1024 frames and back, the window and REAPER's delay compensation showed the new latency at once, and every impulse in the later renders lined up | [Acceptance record](../archive/reviews/2026-11-08-vst3-16-reaper-acceptance.md); [REAPER record, step C5](../archive/reviews/2026-11-06-host1-reaper.md#part-c-window-and-settings) |
| 2. VST3 smoke tests in named hosts, with the current window | HOST-1, VST3-11 | REAPER passed on 6 November apart from the Latency (gate 1), and its crossover on 7 November; Audacity's exports all matched on 7 November; both ran the window from before PLUG-22. On 8 November REAPER's part C passed its controls and state on the redesigned window with the 0.12.5 candidate; listening was not checked. PLUG-31 ([PR #148](https://github.com/jrp2014/convolver/pull/148)), which jrp chose to merge before the preview, changes the candidate (the window's heading for a restored filter, and a controller-state chunk in generated presets; audio state unchanged), but jrp [decided](decisions.md#plug-in-behaviour) that the 0.12.5 part C results stand; on 8 November, with the candidate from `f11a3dd` (PLUG-31 merged), loading packaged presets and save and reopen passed in REAPER, the heading naming each preset's file. On the final candidate, with the package SHA-256 recorded, part C needs only listening and REAPER's display of the Status parameter. Remaining there: the other REAPER steps (parts A, B and D), and every Audacity step, including those HOST-1 lists as not yet run: listening, **Choose…**, a mono track, a Latency change, a long tail and VoiceOver | [REAPER](../archive/reviews/2026-11-06-host1-reaper.md), [crossover](../archive/reviews/2026-11-07-reaper-crossover-retest.md), [Audacity](../archive/reviews/2026-11-07-host1-audacity.md) [REAPER window](../archive/reviews/2026-11-08-host1-reaper-window.md) and [PLUG-31 REAPER](../archive/reviews/2026-11-08-plug31-reaper.md) records; [smoke tests](../develop/host-testing.md#smoke-test-in-each-first-host) |
| 3. The Audio Unit smoke test in GarageBand | AU-4, AU-6 | jrp's checks of the settings (25 October) and of the window up to `bbf94c5` (1 November) passed. On 8 November, with the 0.12.5 candidate, GarageBand's controls, state, refusal and exports passed on the current window; listening was not checked. Those results stand for the final candidate ([decision](decisions.md#plug-in-behaviour)). Remaining, on that candidate (after PLUG-31, with the package SHA-256): loading a packaged preset and save and reopen, which PLUG-31 touches, and the unchecked steps: listening to the presets and to Gain and Mix, a click when bypassing, whether GarageBand keeps the track in time after a Latency change, VoiceOver, and the Recent list (AU-6) | [GarageBand record](../archive/reviews/2026-11-08-au4-garageband.md); AU-4 and AU-6 in [TODO](../../TODO.md) |
| 4. A ThreadSanitizer build of the Audio Unit | AU-3 | **Done** (8 November 2026, [PR #150](https://github.com/jrp2014/convolver/pull/150)). The Audio Unit workflow runs `au.adapter` under ThreadSanitizer, with bypass switched while rendering, the Latency set from audio threads and its work held while bypass changes; the first runs' four reports were all in the test's own setup (none in the component), and the test now orders that setup where ThreadSanitizer can see it. Passed in Audio Unit [run 42491345430](https://github.com/jrp2014/convolver/actions/runs/42491345430) | [Audio Unit tests](../develop/testing.md#audio-unit-tests), [decisions](decisions.md#plug-in-behaviour) |
| 5. Loading responsiveness qualified on a named machine | REL-6 (the procedure is written; the run remains), [loading responsiveness](../../TODO.md#loading-responsiveness) | The timing tests and jrp's limits are in place (PERF-9 to PERF-11), but CI's shared runners judge the callbacks only on Linux ([performance](#performance-what-is-measured) below). Not yet run on jrp's MacBook Pro from the candidate's source, cold and from a full history. An entry that fails or is not qualified means the release is not qualified on that machine; if the 30 s filter holds the host's thread over 250 ms, jrp chooses between background preparation (VST3-5) and a higher, documented limit. Kept as a first-preview gate by jrp | [Responsiveness qualification](../develop/releasing.md#responsiveness-qualification), [decisions](decisions.md#plug-in-behaviour) |
| 6. One exact candidate from a 0.12.x tag | PLUG-5 | The candidates hold everything the preview ships ([what a package holds](../develop/releasing.md#what-a-package-holds)), with the example presets named after their folder since [PR #141](https://github.com/jrp2014/convolver/pull/141) (PLUG-28). Remaining: a candidate cut after gate 1, for gates 2, 3, 5 and 7 | PLUG-5 in [TODO](../../TODO.md) |
| 7. An independent user completes the journeys from those exact files | REL-3 | Not started. jrp chooses the tester; the walkthrough uses the candidate's files before upload. Then jrp publishes | [Releasing](../develop/releasing.md#publishing-an-unsigned-preview) |

The LV2 version mapping (LV2-5) is needed only before LV2 joins a preview.

jrp's review of `276c7dc` (1 November 2026): qualify this fixed feature set
against explicit acceptance criteria before adding further conveniences or
DSP optimisation. His review of 0.10.8 (6 November 2026) narrowed the first
preview to macOS and held optional features and further hardening until the
first independent walkthrough ([decisions](decisions.md#plug-in-behaviour)):
the strongest next check is someone using Convolver without help. The
long-reverb experiment and the non-uniform zero-latency engine (PERF-2,
draft [PR #135](https://github.com/jrp2014/convolver/pull/135) and
[PR #139](https://github.com/jrp2014/convolver/pull/139)) stay separate
until after the first preview.

## Performance: what is measured

Two kinds of measurement, of different things; neither qualifies a
workload on its own ([release qualification](../develop/releasing.md) runs
them on the intended machine, REL-6). The figures and their sources are in
[loading responsiveness](../../TODO.md#loading-responsiveness).

- **Loading, from a cold start** (PERF-9 to PERF-11, 5 November 2026): how
  long a load holds the host's thread, with a newly loaded filter that has
  heard little input. With the 30 s filter, CI held it 28 to 154 ms and the
  Linux cloud VM 227 to 254 ms, against jrp's 250 ms limit. A run whose
  callbacks cannot be judged (late baselines, as on the macOS and Windows
  runners) counts as not qualified, never as passed.
- **Playing at full history** (PERF-14, 5 November 2026, PR #116): the
  same filters after their whole length of audio, which is a long filter's
  sustained cost. On the cloud VM (512-frame callbacks, 10.7 ms apart) the
  10 s filter took about 3 ms a callback and the 30 s filter 25 to 43 ms,
  every callback late, because its tail decays into subnormal numbers.
  Convolver now flushes them itself, whatever the host thread's setting
  (PERF-15, 6 November 2026,
  [PR #117](https://github.com/jrp2014/convolver/pull/117)): the longest
  callbacks fell to 7 to 15 ms, still over half the period there (jrp's callback limit while a load
  runs; no limit for steady playing is decided). So the 30 s workload is not
  qualified on that machine, which neither condemns the engine nor shows
  that loading needs to move to the background.

## Compatibility guarantees

Four promises, kept separate because each breaks in a different way. Changing
any of them is an owner's decision.

| Guarantee | What is promised | Owning guide | Checked by |
| --- | --- | --- | --- |
| `.cfg` and list syntax | Existing files keep loading with the same routes, gains, delays, paths and list selection; syntax changes need a recorded decision | [Configuration](../use/configuration.md), [compatibility](../use/other-programs.md) | Compatibility corpus, parser and fuzz replay |
| Serialized plug-in state and presets | Every version-1 preset whose rate and counts fit keeps loading (except one made before PLUG-13 within 256 bytes of 64 MiB; none was published); version 2 only marks a failed first load | [Presets](../use/presets.md#old-and-new-versions), [presets without a layout](../develop/plugin-identities.md#presets-without-a-layout) | `native.state` (literal version-1 and version-2 bytes), `vst3.adapter` (`version1_presets`), `plugins.presets`; files from releases are COMPAT-1 |
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
5. Be better than the most popular reverb plug-ins where it can be measured
   and checked: correct and checkable output, failing safely, explaining
   itself and free source on every platform, and then the ranked edges of the
   [edges plan](plans/2026-11-06-edges-over-other-convolvers.md) (the
   limitations users cite first: sample rates, CPU spikes with long filters,
   true-stereo files and a filter's own delay; then filters over the
   saved-state limit, finer delays, showing a filter's response, comparison
   and published evidence; then reverb features), after the walkthroughs,
   with its open questions
   for jrp. Precision beyond what can be heard is not a goal.

The plug-in direction is the [plug-in formats plan](plans/2026-10-21-plugin-formats.md):
VST3, LV2 and AU in parallel, CLAP on request. The dependency choices behind
the build (FFTW, Accelerate and Ooura, libsndfile) are explained in the
[implementation map](../develop/architecture.md#dependency-choices). Recorded
owner's decisions are in the [decisions log](decisions.md); where the code came
from is in [history](history.md).
