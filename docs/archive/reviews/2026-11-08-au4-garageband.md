<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# GarageBand Audio Unit checks (AU-4), 8 November 2026

> **Dated record.** jrp's GarageBand 10.4.14 checks of the Audio Unit with
> the 0.12.5 candidate from `main`
> ([run 42437291088](https://github.com/jrp2014/convolver/actions/runs/42437291088),
> source `4117a6d`, the commit tagged `v0.12.5`), installed from the
> package, posted in the project on 8 November 2026 at 13:19 UTC and copied
> here verbatim below the line (two `->` and `>` signs restored from the
> copy's HTML escapes). It covers most of the
> [GarageBand smoke test](../../develop/host-testing.md#garageband-audio-unit)
> on the current window; it is partial AU-4 evidence, not a gate pass. The
> files it names are on jrp's Mac and are not in this repository. PLUG-31
> ([PR #148](https://github.com/jrp2014/convolver/pull/148)), merging before
> the preview, changes the candidate after this run; see
> [status](../../project/status.md#what-still-blocks-the-first-preview).

---

# GarageBand 0.12.5 preview checks — 8 November 2026

GarageBand 10.4.14 (6648), macOS 27.2 arm64. Installed user-local candidate
from run 42437291088; exact source 4117a6dd099fcd6d4475f129372793a5a1612653.
Created garageband-preview.band as a new project. Track 1 is empty; track 2
has the 20 s stereo WAV, stereo input, no monitoring, unity volume/pan,
Compressor and Channel EQ bypassed, sends at zero. Metronome off.

- Empty instance: No filter, audio passes unchanged, 44100 Hz, 2 in/out.
- User-local identity, echo and repeat presets load and show Active.
  Repeat reports 250.0 ms; echo 5.9 ms. Listening not yet verified.
- Echo project saved, GarageBand quit normally and relaunched: retained
  preset name, Active, 0 dB, 100% wet, 256 frames.
- First 24-bit WAV export was normalized by GarageBand (its Advanced
  setting Export projects at full volume was on). Not a DSP comparison pass.
  After disabling this single setting, export matches packaged CLI echo:
  maximum sample error 1.42e-7, -112.99 dB relative to expected peak.
- While Play and Cycle were on, selected 48000 Hz echo preset. UI and log
  report refusal and previous filter kept. Original 44100 Hz echo name and
  dimensions remain. Export after stopping playback matches CLI with the
  same 1.42e-7 maximum error. Diagnostic report saved separately.
- GarageBand exports 882000 frames; CLI includes 257 tail frames. The CLI
  comparison passes, including its treatment of the omitted silent tail.

## File load, settings and latency follow-up

- User completed Choose… with the packaged repeat-44100-stereo/filter.cfg.
  Verified Active, filter.cfg, stereo 44100 Hz, 250 ms; expanded File detail
  shows that exact path. This establishes successful manual file selection;
  automation's intermittent focus loss is not evidence of a user-facing failure.
- Gain numeric entry -12 moved its slider to -12 dB. Mix numeric entries 0
  then 50 moved its slider accordingly. Expanded details fitted the window
  and collapsed again. Reload when the file changes is visibly ellipsized;
  the full label/help is exposed in accessibility. Cosmetic observation only.
- Saved, quit GarageBand normally and reopened: repeat filter, filter.cfg,
  Gain -12, Mix 50 and latency 256 restored. Log independently records
  the restored values. Reload is disabled for embedded restored filters,
  as documented.
- Started looped playback (Play=1 and moving playhead verified). Selected
  1024 frames. Before stopping, window showed adds 23.2 ms and Active; log
  recorded 256 -> 1024 at 13:14:23. This is application of the latency
  change, not proof of live compensation against another track.
- Stopped, disabled Cycle, saved, quit and reopened once more: repeat
  filter, -12 dB, 50%, 1024 frames and adds 23.2 ms restored.
- Copy Diagnostics worked after the live change and after restoration;
  reports saved as latency-playing-diagnostics.txt and
  restored-settings-diagnostics.txt.
- Restored the original GarageBand Advanced > Export projects at full volume
  preference to on, and verified its checkbox. Future numerical comparisons
  must disable it again. Project is saved and stopped, Convolver window open.

## Qualification boundary

Passed observed controls/state persistence, preset and file loading, refusal
with retained filter, and the two unnormalized echo export comparisons.
This is partial AU-4 evidence, not a complete gate pass. No crash, lost filter
or incorrect export was observed in these exercised paths.

Unverified: listening to identity/echo/repeat and Gain/Mix changes, bypass
click/pop behavior, live latency gap and compensation against another track,
and VoiceOver operation. User chose the first response (not sure / wasn't
listening) when asked about the live latency change. Nothing in the UI or log
is treated as proof of these audible outcomes. AU-3 TSan is separate and
remains without qualifying evidence in this session.
