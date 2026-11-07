<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REAPER crossover retest, 7 November 2026

> **Dated record.** jrp's results from retesting HOST-1 part D (the 2→6
> crossover) with the candidate of PR #131, posted in the project thread on
> 7 November 2026 at 18:17 UTC and copied here verbatim below the line. The
> file names in it are files on jrp's Mac (renders, projects, logs and
> scripts); they are not in this repository. The save-and-reopen failure it
> reports was fixed in the same pull request (eeaa843 and c3dbf0d), and jrp's
> second retest of that fix, posted at 19:24 UTC the same day, is copied
> verbatim [below](#second-retest-c3dbf0d).

---

# PR #131: REAPER crossover retest

7 November 2026. **Six of the seven requested checks passed. Save/reopen failed.**
The same restoration failure occurs after FX Offline/Online. The tested candidate
should not be signed off for the crossover walkthrough until this is fixed.

## Candidate and environment

- PR head: `5d188a7ec8597676940e5ff415599a0473c6058e`.
- Candidate workflow: [42370383795](https://github.com/jrp2014/convolver/actions/runs/42370383795).
- Version: Convolver 0.10.13, macOS arm64, Accelerate, baseline CPU kernels.
- Package source commit: `199ca8489b10e2d23afdcf1b65614c5b698cd3f7`
  (the workflow's PR merge commit, distinct from the PR head).
- Package SHA-256: `83f6e2020e41c56e9047342ed2f1953cd3f0694f24ebca8121f50739ce980c1a`.
- All four candidate asset hashes matched. The installed VST3's signature
  verified. The previous 0.10.9 bundle is preserved in `previous-Convolver.vst3`.
- REAPER 7.85/macOS-arm64; macOS 27.2, build 26B123; device block size 512.
- The working project is `d-crossover-retest.rpp`, copied from the earlier
  walkthrough. The original project and earlier results were not overwritten.

## Method and boundaries

These checks ran in the installed, running REAPER application through its
[built-in ReaScript API](https://www.reaper.fm/sdk/reascript/reascripthelp.html).
They exercise the real host, installed plug-in, preset import, rendering,
parameters, audio-device rate changes and process restart. They are not a
standalone adapter harness. They do not qualify mouse interaction, the editor's
appearance, listening quality, VoiceOver or an independent user's experience.
The updated script explicitly excludes the window and Latency retests.

Presets were imported with `TrackFX_SetPreset` using the absolute `.vstpreset`
path. Status was read from REAPER's formatted Status parameter and corroborated
with the Convolver log. A true quit/relaunch used different REAPER processes
(23901 before, 24345 after), after saving the only open project.

Rendering used a six-channel track and six-channel master, WAV float32,
48000 Hz (44100 for the mismatch checks), no fades, normalization or dither,
and a 1000 ms tail. This differs from the guide's selected-track stem route:
REAPER's scripted render action reverted the requested stem settings to a
master render. The initial stereo master dropped channels 3-6; setting the
master to six channels preserved all outputs and the reference comparison
confirmed their routing and delay compensation.

Some initial scripted renders were entirely silent, including with Convolver
bypassed. Those are not accepted as evidence of correct silence or a DSP defect.
The final sequence explicitly brought media online, briefly played, then rendered.
Positive controls with the filter enabled and REAPER bypassed proved that the
input was present. The underlying cause of the initial scripted-render behaviour
was not isolated further. Initial diagnostic files are preserved but superseded
by the named final evidence below.

## Results, following section 3 of the supplied plan

| Check | Result | Evidence |
| --- | --- | --- |
| 1. Load 2-in/6-out crossover on 6-in/6-out track | **Pass** | Status: `Filter loaded: 48000 Hz, 2 in, 6 out; plug-in inputs 3-6 ignored.` Log confirms inputs 1-2 are read. |
| 2. Render all six outputs | **Pass** | `online-baseline.wav` matches shipped `expected.wav`; worst error 1.19e-07, -128.47 dB relative to expected peak. Channel 5, frame 1748. No alignment correction was applied. |
| 3. Mix 0% remains fully filtered | **Pass** | `played-mix-zero.wav` matches expected and is sample-for-sample identical to `online-baseline.wav` across all six channels. |
| 4. Convolver bypass; separate host bypass/offline | **Pass for audio behaviour** | `online-plugin-bypass.wav` is exactly zero on all six outputs. `online-reaper-bypass.wav` and `online-fx-offline.wav` reproduce the source exactly on channels 1-2; channels 3-6 are zero. Returning FX online loses the filter: see the restoration failure below. |
| 5. Save, quit and reopen | **FAIL** | `save-reopen.txt`, `Convolver-retest.log` and `reopened-crossover.wav`: saved filter rejected on temporary stereo layout; all six rendered outputs remain zero after REAPER switches to 6-in/6-out. |
| 6. Six-input preset on stereo track | **Pass** | A separate temporary instance refused a 6-in/2-out preset on 2-in/2-out buses, isolating the input-count failure. Status gives the correct counts. Preset, configuration and track state preserved under `six-input-refusal/`. |
| 7. Wrong rate, bypass, then recovery | **Pass** | Device confirmed 44100 Hz; Status says filter is for 48000 Hz. `rate44100-filter.wav` and `rate44100-plugin-bypass.wav` are exactly zero on all six outputs. Returning the project/device to 48000 Hz, without reloading the filter, gives `rate48000-recovered.wav`, again matching expected at -128.47 dB. |

Final render lengths are 52096 frames at 48000 Hz and 47863 frames at 44100 Hz.
The reference has 4249 frames; the comparison accepts the additional silent tail.
See `audio-verification.json`, `*-compare.txt` and `scripts/verify-results.py`.

## Restoration failure: reproducible sequence

1. Load the crossover on the six-channel track. Its render matches expected.
2. Save the project with Mix 100%, Gain 0 dB and Convolver bypass off.
3. Quit REAPER completely and reopen that project.
4. The log shows a temporary `Started at 48000 Hz, 2 in, 2 out`.
5. The saved filter is then refused because it has 2 inputs and 6 outputs while
   the plug-in currently has 2 inputs and 2 outputs.
6. REAPER subsequently starts the plug-in with 6 inputs and 6 outputs. It remains
   silent; the rejected configuration is not recovered.
7. Playback and rendering do not repair this. Manually importing the same
   crossover preset again restores correct output.

The same refusal sequence was observed twice when FX was taken offline and
brought online. The saved working state is preserved as
`crossover-saved-working.rpp`; `save-reopen.txt` records the status before quitting
and after reopening. The first initial project had recorded an earlier failed
load, but this reproduction used a newly saved, successfully loaded crossover
whose render had passed.

There is also misleading diagnostic text after the final layout is installed:

> Silent: the filter has 2 in, 6 out, the plug-in 6 in, 6 out. Details in the Convolver log.

Those counts are compatible under PLUG-23. The log retains the more informative
reason: rejection happened while the host offered only two outputs.

### Likely implementation cause and next regression test

The candidate's `plugins/vst3/processor.cpp` accepts an activated layout as
known in `known_layout()`. `install()` passes the active/known layout to
`filter_.load()`, even when restoring state. The observed REAPER startup
sequence therefore treats its temporary stereo activation as definitive and
rejects the saved crossover before the final bus layout arrives.

The fix needs to preserve restored configuration through host startup/layout
negotiation and revalidate it when the final layout is activated. It must still
refuse an explicit incompatible preset on an established track; simply removing
channel checks would break checks 6 and the existing contract. Add a regression
with REAPER's observed order: activate stereo, restore a saved 2x6 configuration,
change buses to 6x6, activate/process, and verify all outputs. Cover offline/online
restoration as well. Whether the adapter can distinguish project restoration
from an explicit preset import needs to be checked against the VST3 host context.

This retest does not establish that PR #131 introduced the restoration problem;
it establishes that the nominated candidate cannot complete its save/reopen gate.

## Final state and evidence

REAPER is left open and stopped at the start, on the copied test project, with
the crossover manually reloaded, project/device at 48000 Hz, Gain 0 dB, Mix 100%,
256-frame latency and both bypasses off. The temporary refusal track was removed.
The test project is saved, but reopening it will still encounter the known bug.
See `final-state.txt`.

`Convolver-retest.log` contains this candidate's log lines for the session.
`Convolver-before-retest.log` preserves the earlier log. `scripts/` preserves the
ReaScripts used, including intermediate diagnostic attempts; these are a record
of the session, not a clean unattended end-to-end runner. No repository source
was changed and no GitHub comment was posted.

---

## Second retest, c3dbf0d

> Copied verbatim from jrp's message of 7 November 2026, 19:24 UTC.

# PR #131: restoration retest on c3dbf0d

7 November 2026. **The previously failing REAPER restoration checks now pass.**

### Candidate

- Requested run: [42404299284](https://github.com/jrp2014/convolver/actions/runs/42404299284), Plug-in distribution candidates; completed successfully.
- PR head: `c3dbf0db5faf383e33295c27f1b226bb838068bd`.
- Artifact: `candidate-macos-arm64`, ID `13587465282`.
- Package: Convolver 0.10.13; source commit `1b0d873729f65cfba768e2dada1675cccec953fa` (PR merge commit).
- Package SHA-256: `64196c430cf9e7c9c9eab53bb8c71367d311983bb440bb75543cfd6775addc75`.
- All four asset hashes matched the candidate manifest. The extracted and installed VST3 bundles passed strict/deep signature verification.
- Previous installed bundle preserved in `previous-Convolver.vst3`; original project and earlier evidence retained in the preceding retest folder.
- Host: REAPER 7.85/macOS-arm64, macOS 27.2 build 26B123. Project/device 48000 Hz; 512-frame device blocks.

### Method

Focused retest of the previous save/reopen and FX Offline/Online failures, with Mix and bypass controls. Native ReaScripts drove the installed REAPER application. No filter preset was imported during this run: the filter had to survive exclusively through saved state.

First, reopened a copy of the working project saved by the previous candidate. Then took FX offline and online, rendered the restored output, checked Mix 0% and plug-in bypass, and saved again with normal settings. Quit REAPER completely and reopened the new build's saved project for a second restoration render.

The six-channel track feeds a six-channel master, with float32 WAV output, unity levels, no fades/normalization/dither, and 1000 ms tail. Explicitly brought media online and briefly played before rendering, using the working procedure established in the previous retest.

### Results

| Check | Result | Evidence |
| --- | --- | --- |
| Open the old candidate's saved crossover | **Pass** | `reopened.wav`; correct Status before playback; all six outputs match expected. |
| FX Offline | **Observed expected host bypass** | `offline.wav`: exact source on channels 1-2, channels 3-6 zero. |
| FX Online again | **Pass** | `online-restored.wav`; crossover returns automatically, with no preset import. |
| Mix 0% | **Pass** | `mix-zero.wav`; sample-identical to the normal six-channel render. |
| Convolver bypass | **Pass** | `plugin-bypass.wav`; every sample on all six outputs exactly zero. |
| Restore normal settings | **Pass** | `normal-restored.wav`; same audio as baseline. |
| Save with new build, quit and reopen | **Pass** | `roundtrip.wav`; same audio as baseline, without reimporting the preset. |

All five filtered renders match the packaged reference. Worst error is
**1.19e-07**, **-128.47 dB relative to expected peak**, at channel 5, frame 1748.
They are also sample-for-sample identical to each other. Each has 52096 frames;
the reference has 4249 frames and the additional rendered tail is silent.
No timing alignment or level correction was applied during comparison.

Status after each restoration, including before playback:

> Filter loaded: 48000 Hz, 2 in, 6 out; plug-in inputs 3-6 ignored.

The log now records the previously destructive sequence correctly: it retains
the crossover silently during the temporary stereo activation, explaining that
the host has not played audio yet, then starts it successfully on 6-in/6-out
buses. This happened both on reopening and when bringing FX online. The log uses
the untagged-state fallback reason; it does not say REAPER identified the state
as project restoration.

### Scope and final state

This closes the specific REAPER restoration failure observed with candidate
`5d188a7`. It does not qualify every host or lifecycle order. Wrong-rate recovery,
insufficient-input rejection, editor interactions, listening quality and Latency
changes were not repeated in this focused run; earlier results remain dated to
their tested candidate.

REAPER is left open and stopped at the start of `crossover.rpp`, with the
crossover loaded, 48000 Hz, Gain 0 dB, Mix 100%, 256-frame latency and both
bypasses off. The new build's saved project has now survived a full restart.

Evidence: `restore-checks.txt`, `roundtrip.txt`, `Convolver-retest.log`,
`audio-verification.json`, `*-compare.txt`, the WAV renders and `scripts/`.
No repository source was changed and no report was posted to GitHub.
