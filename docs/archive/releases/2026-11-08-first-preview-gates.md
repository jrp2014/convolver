<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# First preview: gates, journeys and defects

> **Archived 20 November 2026.** These sections of the
> [status page](../../project/status.md) recorded how the first preview was
> qualified: the defects jrp's reviews found before it, the user journeys
> REL-2 wrote, the milestone conditions met and the seven gates as they
> stood when jrp published 0.12.12 on 8 November 2026. They are kept as they
> stood at `549ec9c` (0.16.8), when DOC-19 rewrote the status page for
> 0.14.0; only relative links are adjusted. The checks still open are in
> [checks left](../../project/status.md#checks-left-before-publication).

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
| Stereo room correction | [`examples/first-use/stereo`](../../../examples/first-use/README.md) | VST3 and the Audio Unit in named hosts; CLI | CLI commands run in CI (`examples.first_use`). The package holds the example and a preset made from it, and the journey is written ([check a stereo filter in a host](../../use/check-a-stereo-filter.md)), checked with `convolver --compare`; the release tests render it with the packaged CLI. On 1 November its package and command-line steps passed on a macOS arm64 candidate (REL-5). On 6 November its host steps passed in REAPER 7.85 on macOS with the VST3 plug-in: render, save and reopen, and presets that do not fit, each compared as a Match ([record](../reviews/2026-11-06-host1-reaper.md#part-b-stereo-numerical-journey), HOST-1). |
| 2→6 crossover | [`examples/first-use/crossover`](../../../examples/first-use/README.md) | VST3 and the Audio Unit in named hosts; CLI | CLI commands run in CI, and the release tests run the packaged CLI on it. The package holds the example and a 2→6 preset, and the journey is written for REAPER, with the VST3 plug-in and the Audio Unit ([check a crossover in a host](../../use/check-a-crossover-in-a-host.md)), checked with `convolver --compare`. GarageBand and Audacity offer only mono and stereo effects. The VST3 crossover passed in REAPER 7.85 on macOS on 7 November (HOST-1), with [PR #131](https://github.com/jrp2014/convolver/pull/131)'s candidate, which lets a filter read the first inputs of a wider bus (REAPER gives the plug-in 6 inputs and 6 outputs, so the first run, on 6 November, refused the preset): its render matched (worst error −128.47 dB), Mix 0% stayed fully filtered, bypass was silent, a six-input preset was refused on a stereo track, and the filter came back after the wrong rate and after saving, quitting and reopening, and FX Offline then Online ([record](../reviews/2026-11-07-reaper-crossover-retest.md)). The Audio Unit in REAPER not yet run; no recorded decision makes it a first-preview gate, and REL-3 covers it only if the tester uses it. |
| Offline file rendering | All three first-use filters | CLI from the preview package | The journey is written ([check file rendering with the command line](../../use/check-files-with-the-command-line.md)): it renders and compares all three examples and shows the messages for a filter at another rate or channel count. The release tests run every command in it from the packaged `bin/convolver`, including the two refusals and their messages. Not demonstrated by a user. |

Recovering from a wrong preset is the step to watch. VST3 keeps the working
filter but can report the rejection to the host only as `kResultFalse`, which
the host may not show; the read-only Status parameter says why, and the
Convolver log explains in full
([status and log](../../use/plugins.md#status-and-the-convolver-log)). Audacity
showed the Status text (HOST-1), and GarageBand's generic window showed the
Status row with no value, which is why the Audio Unit has its own window
(AU-4, AU-6); REAPER's display of it remains (check 1 in [checks left](../../project/status.md#checks-left-before-publication)).

## Next milestone: an independent user runs a preview

From jrp's [guidance of 22 October 2026](../../review-2026-10/release-readiness-guidance-2026-10-22.md):
an independent user successfully installs and uses an exact preview package,
with the lifecycle regression fixed and the advertised workflows demonstrated.
The first preview is macOS-only ([decisions](../../project/decisions.md#plug-in-behaviour)):
the VST3 plug-in and the Audio Unit with the existing shared window and their
Gain, Mix and Latency settings, the `convolver` CLI and the preset tool.
Later previews added the Windows and Linux packages, after an automated run
on clean machines (REL-9), the Windows window (VST3-12), LV2 in the Linux
package (LV2-5, LV2-7) and the other gain modes (PR #188)
([decisions](../../project/decisions.md#releases)); the Linux window and the meters come
later. Since 10 November 2026 significant features come before corner and
edge cases, optional features included, and the items jrp parked by name
wait for the walkthrough ([decisions](../../project/decisions.md#releases),
[TODO](../../../TODO.md#execution-priorities)); crashes, lost state, wrong
audio, ordinary compatibility failures and demonstrated dropouts are still
fixed at once.

The first preview is cut from a 0.12.x tag. The milestone's conditions
already met are below; what still needs running is in
[checks left](../../project/status.md#checks-left-before-publication).

| Condition met | Source | Item | Done |
| --- | --- | --- | --- |
| Reset during a transition no longer plays the old filter | jrp's milestone | VST3-7 follow-up | 23 October 2026, [PR #40](https://github.com/jrp2014/convolver/pull/40) |
| Lifecycle sequences checked against a reference model | Proposed with the milestone, so the next defect of this kind is caught | PLUG-7 | [PR #58](https://github.com/jrp2014/convolver/pull/58); `au.lifecycle` runs in macOS CI only |
| The edge cases from jrp's review of 0.4.43 fixed | jrp's review | DIAG-1, ROBUST-4 | [PR #72](https://github.com/jrp2014/convolver/pull/72), [PR #73](https://github.com/jrp2014/convolver/pull/73), [PR #96](https://github.com/jrp2014/convolver/pull/96) |
| The defects from jrp's later reviews fixed | jrp's reviews of `6920a45` and `276c7dc` | PLUG-12, ROBUST-3, PLUG-13 | [PR #91](https://github.com/jrp2014/convolver/pull/91), [PR #105](https://github.com/jrp2014/convolver/pull/105) |
| Settings (Gain, Mix, Latency) and the shared macOS window implemented in VST3 and the Audio Unit, and checked in CI | jrp | PLUG-9, PLUG-10, VST3-3, VST3-4, AU-7 | Implemented and checked in CI (`vst3.editor`, `au.editor`, pluginval's editor tests); the window last redesigned in [PR #128](https://github.com/jrp2014/convolver/pull/128) (PLUG-22, 7 November 2026). Its host checks (VST3-11, AU-6) are checks 1, 4 and 5 in [checks left](../../project/status.md#checks-left-before-publication). The Windows window followed in 0.13.12 (VST3-12); later previews: the Linux window (VST3-13) and the meters (PLUG-10); the other gain modes are in 0.13.20 (PR #188) |
| The Audio Unit validated by `auval` and pluginval, and in the package | jrp | AU-3, AU-5 | `auval -strict` and `au.editor` in CI; pluginval on the installed component in the Audio Unit workflow; the component in both macOS candidates since PR #115, with `auval -strict` after the `.pkg` installs it (first passed in candidates [run 42108659224](https://github.com/jrp2014/convolver/actions/runs/42108659224)). AU-3's ThreadSanitizer build is gate 4 below (done) |
| Journeys written as steps, with the presets they need in the package | jrp's milestone | REL-2 | 6 November 2026: the stereo, 2→6 crossover and CLI journeys (table above) |

At publication, and before the next preview: COMPAT-1 keeps presets, a host
project and expected audio from the published release, so later previews are
tested against them. The presets and audio of 0.13.2, 0.13.12, 0.13.20, 0.13.24 and 0.14.0 are kept
(the same bytes in all five), and a REAPER
project, to be saved with 0.14.0, the baseline, is not kept yet. Not part of this milestone, but tracked: signing and
notarization (the production gate, REL-1), resource budgets and
the wider supported workloads (PERF-3; the loading gate uses its own finite
preview workloads), other `.cfg` readers
(CFG-1) and Equalizer APO on a real device (HOST-4).

## What still blocks the first preview

This section keeps the first preview's gates as they stood when jrp
published it on 8 November 2026, so the gate numbers hold; it is a record,
not a list of open work. What remains, on 0.14.0, is in
[checks left](../../project/status.md#checks-left-before-publication).

jrp [decided](../../project/decisions.md#releases) on 8 November 2026 to publish the
first preview from the next tag without further testing, so that people
have something to try. Gates 2, 3 and 7 are waived for that preview only:
what they still need stays open below and in [TODO](../../../TODO.md), and
the release notes say it was not checked. Later previews keep the gates.

[Releasing: what blocks a preview](../../develop/releasing.md#what-blocks-a-preview)
gives the stop rule, how a finding is classified and when to retest. The
gate results below remain authoritative; those rules do not pass or waive
any gate.

These are the gates on 8 November 2026, each listed once (gates 1 and 4 are
done and stay listed so the numbers hold), in the
order of [TODO's priorities](../../../TODO.md#execution-priorities), except that
the exact candidate (gate 6) comes just before the walkthrough that uses it.
Each row names only
what remains and where the evidence is; the items hold the detail. Gates 2
and 3 finish on the v0.12.8 candidate (apart from the two guide steps run on
the final candidate), gate 7 runs on the final candidate, and gate 5's result
is for the 0.12.5 source ([checks left](../../project/status.md#checks-left-before-publication)).

| Gate | Item | Status | Evidence |
| --- | --- | --- | --- |
| 1. A new VST3 Latency comes into use in REAPER | VST3-16 | **Done** (8 November 2026). In REAPER 7.85 a new Latency had stayed unused until the project was reopened; since [PR #134](https://github.com/jrp2014/convolver/pull/134) it plays at once. jrp's check with its candidate passed: stopped and while playing, 256 to 1024 frames and back, the window and REAPER's delay compensation showed the new latency at once, and every impulse in the later renders lined up | [Acceptance record](../reviews/2026-11-08-vst3-16-reaper-acceptance.md); [REAPER record, step C5](../reviews/2026-11-06-host1-reaper.md#part-c-window-and-settings) |
| 2. VST3 smoke tests in named hosts, with the current window | HOST-1, VST3-11 | REAPER passed on 6 November apart from the Latency (gate 1), and its crossover on 7 November; Audacity's exports all matched on 7 November; both ran the window from before PLUG-22. On 8 November REAPER's part C passed its controls and state on the redesigned window with the 0.12.5 candidate, and loading packaged presets with save and reopen passed with the candidate from `f11a3dd` (PLUG-31 merged); jrp [decided](../../project/decisions.md#plug-in-behaviour) that both stand. Listening was not checked. What remains, REAPER's other steps and every Audacity step, is in [checks left](../../project/status.md#checks-left-before-publication) | [REAPER](../reviews/2026-11-06-host1-reaper.md), [crossover](../reviews/2026-11-07-reaper-crossover-retest.md), [Audacity](../reviews/2026-11-07-host1-audacity.md), [REAPER window](../reviews/2026-11-08-host1-reaper-window.md) and [PLUG-31 REAPER](../reviews/2026-11-08-plug31-reaper.md) records; [smoke tests](../../develop/host-testing.md#smoke-test-in-each-first-host) |
| 3. The Audio Unit smoke test in GarageBand | AU-4, AU-6 | jrp's checks of the settings (25 October) and of the window up to `bbf94c5` (1 November) passed. On 8 November, with the 0.12.5 candidate, GarageBand's controls, state, refusal and exports passed on the current window, and those results stand ([decision](../../project/decisions.md#plug-in-behaviour)); listening was not checked. What remains is in [checks left](../../project/status.md#checks-left-before-publication) | [GarageBand record](../reviews/2026-11-08-au4-garageband.md); AU-4 and AU-6 in [TODO](../../../TODO.md) |
| 4. A ThreadSanitizer build of the Audio Unit | AU-3 | **Done** (8 November 2026, [PR #150](https://github.com/jrp2014/convolver/pull/150)). The Audio Unit workflow runs `au.adapter` under ThreadSanitizer, with bypass switched while rendering, the Latency set from audio threads and its work held while bypass changes; the first runs' four reports were all in the test's own setup (none in the component), and the test now orders that setup where ThreadSanitizer can see it. Passed in Audio Unit [run 42491345430](https://github.com/jrp2014/convolver/actions/runs/42491345430) | [Audio Unit tests](../../develop/testing.md#audio-unit-tests), [decisions](../../project/decisions.md#plug-in-behaviour) |
| 5. Loading responsiveness qualified on a named machine | REL-6, [loading responsiveness](../../../TODO.md#loading-responsiveness) | **Qualified for the 0.12.5 source** on jrp's MacBook Pro (M5 Max, macOS 27.2) on 8 November 2026: all 24 workload and entry-point combinations (VST3 and the Audio Unit, cold and warm, 1, 10 and 30 s stereo filters) passed on the first run, with no late callback in a judged phase; the longest hold of the host's thread was 43.2 ms (limits 100 ms up to 10 s, 250 ms at 30 s) and the longest judged callback 2.95 ms (limit 5.33 ms). At jrp's direction his usual applications stayed open, which the record states. This qualifies that source under the recorded conditions, not every later build. No engine or loading code has changed since 0.12.5: PLUG-31 changed the window's heading for a restored filter with no saved name (both formats) and the VST3 presets `convolver-preset` writes. jrp [decided](../../project/decisions.md#releases) that the result stands for the final candidate while no engine, loading or plug-in code changes after v0.12.8. The release notes' responsiveness sentence names the 0.12.5 source and says his usual applications were open; his `rel6-evidence.zip` (the timing files) goes with the release evidence | [REL-6 record](../reviews/2026-11-08-rel6-qualification.md); [responsiveness qualification](../../develop/releasing.md#responsiveness-qualification) |
| 6. One exact candidate from a 0.12.x tag | PLUG-5, REL-7 | The candidates hold everything the preview ships ([what a package holds](../../develop/releasing.md#what-a-package-holds)). jrp [decided](../../project/decisions.md#releases) that the final candidate is a hand-run of "Plug-in distribution candidates" on the first tag after DOC-10 (the shorter first-use route) merges, so the tester and the release use that guide. The host checks run before then on a v0.12.8 candidate ([run 42506723627](https://github.com/jrp2014/convolver/actions/runs/42506723627)) and stand for the final one while no engine, loading or plug-in code changes after v0.12.8, checked with `git diff v0.12.8 <final tag> -- portable plugins include` (comment-only changes, such as PR #153's, do not count) and recorded with the final candidate. Each record gives its candidate's package SHA-256. Done: the final tag's candidate run passed | PLUG-5 and REL-7 in [TODO](../../../TODO.md) |
| 7. An independent user completes the journeys from those exact files | REL-3 | Not started; **waived for the first preview** by jrp, so the walkthrough uses the published files afterwards. jrp chooses the tester | [Releasing](../../develop/releasing.md#publishing-an-unsigned-preview) |

The LV2 version mapping is decided (LV2-5,
[decisions](../../project/decisions.md#identities-and-file-formats)).

jrp's review of `276c7dc` (1 November 2026): qualify this fixed feature set
against explicit acceptance criteria before adding further conveniences or
DSP optimisation. His review of 0.10.8 (6 November 2026) narrowed the first
preview to macOS and held optional features and further hardening until the
first independent walkthrough ([decisions](../../project/decisions.md#plug-in-behaviour)):
the strongest next check is someone using Convolver without help. The
long-reverb experiment and the non-uniform zero-latency engine (PERF-2,
draft [PR #135](https://github.com/jrp2014/convolver/pull/135) and
[PR #139](https://github.com/jrp2014/convolver/pull/139)) stay separate
until after the first preview.

