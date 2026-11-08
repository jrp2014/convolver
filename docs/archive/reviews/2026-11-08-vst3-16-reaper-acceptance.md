<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# VST3-16 REAPER acceptance, 8 November 2026

> **Dated record.** jrp's acceptance check of the VST3-16 fix in REAPER 7.85
> with the candidate of PR #134 at `7d6a8b4`, following the steps written
> for it, posted in the project thread on 8 November 2026 at 01:28 UTC and
> copied here verbatim below the line. The files listed under Evidence are
> on jrp's Mac and are not in this repository. The filter-name observation
> became PLUG-31 in [TODO](../../../TODO.md).

---

# VST3-16 REAPER acceptance results — 8 November 2026

The three required latency changes passed the immediate-display and subsequent
render-alignment checks in REAPER 7.85 on this Apple Silicon Mac. Optional
listening on normal material remains unverified.

## Candidate and setup

- Run [42431674090](https://github.com/jrp2014/convolver/actions/runs/42431674090),
  PR #134 head `7d6a8b46025c0a6bdc19cfc84b6e68238ecd2092`.
- Artifact `candidate-macos-arm64`, version 0.12.1, source merge commit
  `3cce4f9c8bfb52add265f1381607ae1f21aac1c4`.
- All four downloaded asset hashes matched the candidate manifest. The installed
  user-local VST3 bundle passed strict/deep code-signature verification. This is
  an unsigned developer preview, not Developer ID/notarization qualification.
- REAPER was restarted after replacing the bundle. The editor and session log
  both identify 0.12.1. No explicit rescan or system-wide installation was done.
- macOS 27.2 (26B123); REAPER 7.85/macOS-arm64; 48,000 Hz, stereo,
  512-frame host callbacks, Accelerate backend; identity filter restored from
  the baseline project, Gain 0 dB, Mix 100% wet, bypass off.
- The baseline was copied to `latency-fix.rpp`. The original remains unchanged.
  Test instance: VST3 #2, REAPER process 89471. Instance #1 belongs to the
  previously opened project and is excluded from the phase results.

## Results

The Convolver row and REAPER delay-compensation display were observed **before**
the subsequent render, without manually toggling Offline/Online.

| Change | Convolver row | REAPER PDC | All four impulses, both channels |
| --- | --- | --- | --- |
| Stopped, 256 → 1024 | adds 21.3 ms | 1024/1024 spls | +0 frames |
| Playing, 1024 → 256 | adds 5.3 ms | 256/512 spls | +0 frames |
| Playing, 256 → 1024 | adds 21.3 ms | 1024/1024 spls | +0 frames |

No row said “not in use yet”. The log records each latency change before the
later render restart. Playback was confirmed by REAPER's display and the local
transport helper, including `playState=1` before each stop.

Exports were selected-track stems, 48,000 Hz, stereo, 32-bit float WAV, entire
project with a 100 ms tail: 196,800 frames per file. Dither and postprocessing
were disabled. The supplied `impulse.py` checked the first channel, and
`scripts/verify_fix_renders.py` independently checked both channels, finite
samples, expected impulse positions and the sample differences without shifting.
All 24 impulse comparisons aligned. Maximum absolute sample error was
1.1920928955078125e-7 for 1024 frames and 5.960464477539063e-8 for 256 frames.
The reference peaks are at frames 24000, 72000, 120000 and 168000.

## Limits and automation notes

- Optional listening on normal material was not performed. These exports do
  not measure the live transition gap or establish the absence of audible
  clicks/flams during that gap.
- REAPER's main window was intermittently unavailable to accessibility actions.
  A local ReaScript, run through REAPER's Actions menu, controlled transport,
  selected the two test tracks and saved the project. All latency changes were
  made in Convolver's actual editor; the helper never set latency.
- One helper invocation failed with a Lua newline syntax error. It was fixed
  before rerunning; this was a test-automation error, not a plug-in error.
- Playback is stopped, and the test project is saved with latency 1024.
  REAPER remains open. No code was changed and nothing was posted to GitHub.

## Filter-name observation

The editor still displays “From the saved project or preset”. Saving and
restoring the filter's display name is possible and is already implemented
for files chosen inside the VST3 editor: the controller stores the name and
filter identity, and the processor displays that name when the identity matches.

The candidate's preset generator explicitly omits the name for VST3 outputs
(`portable/preset_main.cpp`, around line 170). The packaged identity preset has
only a `Comp` chunk, with no controller-state chunk. Generated presets therefore
need to carry the same name/identity metadata. This is a concrete gap, not a
fundamental limitation of embedded filter audio.

Use the filename including its extension for the label; keep it independent
of the original folder. For older states without a name, “Restored filter” is
a suitable fallback. A name does not prove whether the host restored a project
or a preset; origin wording should use only information the host supplies.
No change to the processor's audio-state format is implied by this finding.

## Evidence

- `provenance-fix.json` and `run-fix.json`: candidate provenance.
- `notes-fix.txt`: observations and timeline.
- `log-fix.txt`: relevant lines for test instance #2 only.
- `Convolver-fix-session.log`: complete session lines for process 89471.
- `fix-play-state.txt` and `fix-transport.log`: transport observations.
- `f1024stopped-measure.txt`, `f256playing-measure.txt`,
  `f1024playing-measure.txt`: supplied measurement outputs.
- `measurements-fix.json`: independent stereo measurements.
- Six matching `f*.wav` exports, `latency-fix.rpp`, and `scripts/` helpers.

The “commit 3cdf9ca5d1f5” in the software log is the VST3 SDK revision,
not the Convolver source commit. The artifact manifest records the latter.
