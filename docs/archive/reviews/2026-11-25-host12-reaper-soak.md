<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-12 first REAPER soak, 25 November 2026

> **Dated record.** jrp's first run of `scripts/reaper_check.py --soak`
> ([a long REAPER soak](../../develop/host-testing.md#a-long-reaper-soak)),
> for 30 minutes from 22:07 UTC on 25 November 2026, in REAPER 7.86 on
> macOS 27.2.1 (Apple silicon) through CoreAudio's default device at
> 48 kHz with a 128-frame block. The package was the 0.18.23
> `candidate-macos-arm64` from the candidates run on `main` at `17a9666`
> ([run 45707665165](https://github.com/jrp2014/convolver/actions/runs/45707665165)),
> which holds both soak pull requests (#288 and #294); the script was the
> one on `main` at that commit. jrp posted the run's `result.md` in the
> project at 22:40 UTC and it is copied below the line (its top heading is
> one level lower here).
>
> The result is **no new underrun**, with all six checks passed: the four
> returns played their reverbs at Latency 0 for the whole 1800 s, all 724
> Status readings while playing were the Active text, and the 20 s
> replacement loaded into return 1 at 900 s (its saved state, 20,480,348
> base64 characters, is 15.36 MB, the replacement preset's size).
>
> What the result rests on: REAPER's last-underrun times were 0 for audio
> and for media before, while and after playing, while its clock advanced
> by 1800 s. REAPER gives the audio underrun time only where the driver
> reports one, and this run cannot tell "none happened" from "CoreAudio's
> default device does not report them", as `result.md` says. No run on this
> Mac has yet shown REAPER reporting an audio underrun through this
> device, so the result is not evidence of zero dropouts. jrp added no
> notes to the report.
>
> Two things in the Convolver log that are not the soak's findings:
>
> - Every instance logs one "Refused the host's channel layout: it has 64
>   input and 64 output channels" line, then starts at 2 in, 2 out in the
>   same second: REAPER offered each instance 64 channels each way before
>   stereo. The line is DIAG-1's logging, once per instance, as designed. The
>   wording ("Refused", and the advice to use the command-line tool for
>   more channels) reads as a fault in an ordinary REAPER session.
> - At 22:37:55, as the script stopped the transport, read return 1's state
>   and saved the project, REAPER started four more instances (#5 to #8),
>   each of which loaded its return's filter and started. The run does not
>   show what REAPER made them for. They came after the soak's last
>   reading and do not affect its result.

---

## REAPER soak, 2026-11-25 22:07

**Result: no new underrun.** 6 of 6 checks passed. Written by `scripts/reaper_check.py --soak` (HOST-12).

Underruns: Both times unchanged (last audio underrun 0 ms before and 0 ms after; last media underrun 0 ms before and 0 ms after; clock 348145032 ms to 349945057 ms). REAPER reported no audio underrun at any time in this session: none happened, or this device does not report them (REAPER gives the audio underrun time only where the driver does).

| # | Check | Result | Evidence |
| --- | --- | --- | --- |
| 1 | REAPER ran the soak ReaScript to the end | Pass | 1802.8 s |
| 2 | Each of the 4 returns has Convolver's VST3 with its reverb loaded | Pass | Reverb 1 (stereo-10s): VST3: Convolver (Convolver) (type VST3), preset true, Status Filter loaded: 48000 Hz, 2 in, 2 out.; Reverb 2 (true-stereo-10s): VST3: Convolver (Convolver) (type VST3), preset true, Status Filter loaded: 48000 Hz, 2 in, 2 out.; Reverb 3 (stereo-30s): VST3: Convolver (Convolver) (type VST3), preset true, Status Filter loaded: 48000 Hz, 2 in, 2 out.; Reverb 4 (true-stereo-30s): VST3: Convolver (Convolver) (type VST3), preset true, Status Filter loaded: 48000 Hz, 2 in, 2 out. |
| 3 | The audio device ran at 48000 Hz with a block of 1 to 128 frames | Pass | 48000 Hz (48000 Hz while playing), block 128 (128 while playing) |
| 4 | The transport ran for the whole soak | Pass | 1800 s of 1800 s; play position at the end 1799.935 s |
| 5 | Every return read Active at every reading, and at the end | Pass | 724 readings while playing, 0 not Active |
| 6 | Replacing Reverb 1's filter while playing loaded the new one | Pass | at 900.0 s TrackFX_SetPreset returned true; Status 2.0 s later: Filter loaded: 48000 Hz, 2 in, 2 out.; its state after the soak is 15.36 MB, the replacement preset 15.36 MB and the replaced one 3.84 MB: within 10% of the replacement's size |

### Environment

- REAPER: 7.86/macOS-arm64, `/Applications/REAPER.app/Contents/MacOS/REAPER`
- macOS: macOS, 27.2.1, 26B131; CPU: arm64
- Package folder: `Convolver-0.18.23-preview-macos-arm64`
- Package SHA-256: `3afd578b5c166ebe09bcfd5a072072893e5f49496ad23ad66f5ce7f8d6c0c6d7` (`Convolver-0.18.23-preview-macos-arm64.zip`)
- Convolver CLI: Convolver portable 0.18.23; FFT backend: Accelerate / vDSP (system framework); CPU kernels: baseline; libsndfile-1.2.2 (no FLAC output: built without FLAC)
- Installed VST3: `/Users/jrp/Library/Audio/Plug-Ins/VST3/Convolver.vst3` 0.18.23
- Project rate: 48000.0 Hz; audio device: CoreAudio Default at 48000 Hz, block size 128 (128 while playing)
- Channels: 4 stereo source tracks, each sending to its own stereo return (2 in, 2 out) and playing dry to the stereo master
- REAPER's anticipative FX settings, as REAPER gives them: workrender 17, renderaheadlen 200
- Default template: 0 tracks and 0 master effects removed

### Steps

1. REAPER started with a new project at 48000 Hz (process 99572). The ReaScript made 4 source tracks with 494 items of 2-second sounds (hits, chord, clicks, sweep) separated by 3 to 20 s of silence, and a return for each with Convolver (VST3) and a decaying-noise reverb from a preset made by `bin/convolver-preset`: Reverb 1: stereo-10s (10 s stereo), Reverb 2: true-stereo-10s (10 s true stereo), Reverb 3: stereo-30s (30 s stereo), Reverb 4: true-stereo-30s (30 s true stereo).
2. With every return Active, it played for up to 1800 s from the start, reading REAPER's underrun times and transport each second and each return's Status every 10 s. All sources were silent from 450 to 510 s, 1170 to 1230 s, longer than the longest tail.
3. At 900.0 s, while playing, it loaded true-stereo-20s (20 s true stereo) into Reverb 1.
4. It stopped, saved the project as `reaper-soak.rpp` and quit (by itself).

Underruns while the filters loaded: none reported.

### What the ReaScript recorded

```text
stage: soak
reaper_version: 7.86/macOS-arm64
os: macOS-arm64
device_rate: 48000
device_block: 128
device_output: CoreAudio Default
underrun_api: yes
config_workrender: 17
config_renderaheadlen: 200
underrun_setup: 0 0 348144868
template_tracks: 0
template_master_fx: 0
project_rate: 48000.0
items: 494
fx_name_1: VST3: Convolver (Convolver)
fx_type_1: VST3
preset_applied_1: true
fx_name_2: VST3: Convolver (Convolver)
fx_type_2: VST3
preset_applied_2: true
fx_name_3: VST3: Convolver (Convolver)
fx_type_3: VST3
preset_applied_3: true
fx_name_4: VST3: Convolver (Convolver)
fx_type_4: VST3
preset_applied_4: true
tracks: 8
status_ready_1: Filter loaded: 48000 Hz, 2 in, 2 out.
status_ready_2: Filter loaded: 48000 Hz, 2 in, 2 out.
status_ready_3: Filter loaded: 48000 Hz, 2 in, 2 out.
status_ready_4: Filter loaded: 48000 Hz, 2 in, 2 out.
underrun_before: 0 0 348145032
play_started: yes
device_rate_playing: 48000
device_block_playing: 128
underrun_playing: 0 0 348145040
status_readings: 724
status_other: 0
replacement_at: 900.0
replacement_applied: true
replacement_status: Filter loaded: 48000 Hz, 2 in, 2 out.
replacement_settled_after: 2.0
soak_seconds: 1800.0
play_state: 1
play_position: 1799.935
underrun_after: 0 0 349945057
status_end_1: Filter loaded: 48000 Hz, 2 in, 2 out.
status_end_2: Filter loaded: 48000 Hz, 2 in, 2 out.
status_end_3: Filter loaded: 48000 Hz, 2 in, 2 out.
status_end_4: Filter loaded: 48000 Hz, 2 in, 2 out.
state_chars_1: 20480348
project_saved: yes
dirty_at_quit: yes
done: yes
```

### Convolver log (lines written during the run)

```text
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #1 in REAPER (process 99572): Session: Convolver 0.18.23, FFT backend Accelerate (vDSP (system framework)), CPU kernels baseline, built with VST3 SDK 3.8.1_build_84, commit 3cdf9ca5d1f5, process 99572, host program REAPER.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #1 in REAPER (process 99572): Refused the host's channel layout: it has 64 input and 64 output channels; Convolver takes 1 to 8 of each. Use the command-line tool for filters with more channels. The host may go on to offer another layout; only the first refusal is logged.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #1 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. No filter, so audio passes unchanged. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #1 in REAPER (process 99572): Request 1: Loaded a filter for 48000 Hz, 2 in, 2 out.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #2 in REAPER (process 99572): Refused the host's channel layout: it has 64 input and 64 output channels; Convolver takes 1 to 8 of each. Use the command-line tool for filters with more channels. The host may go on to offer another layout; only the first refusal is logged.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #2 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. No filter, so audio passes unchanged. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #2 in REAPER (process 99572): Request 1: Loaded a filter for 48000 Hz, 2 in, 2 out.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #3 in REAPER (process 99572): Refused the host's channel layout: it has 64 input and 64 output channels; Convolver takes 1 to 8 of each. Use the command-line tool for filters with more channels. The host may go on to offer another layout; only the first refusal is logged.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #3 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. No filter, so audio passes unchanged. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #3 in REAPER (process 99572): Request 1: Loaded a filter for 48000 Hz, 2 in, 2 out.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #4 in REAPER (process 99572): Refused the host's channel layout: it has 64 input and 64 output channels; Convolver takes 1 to 8 of each. Use the command-line tool for filters with more channels. The host may go on to offer another layout; only the first refusal is logged.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #4 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. No filter, so audio passes unchanged. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:07:54 Convolver 0.18.23 VST3 #4 in REAPER (process 99572): Request 1: Loaded a filter for 48000 Hz, 2 in, 2 out.
2026-11-25 22:22:54 Convolver 0.18.23 VST3 #1 in REAPER (process 99572): Request 2: Loaded a filter for 48000 Hz, 2 in, 2 out.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #5 in REAPER (process 99572): Refused the host's channel layout: it has 64 input and 64 output channels; Convolver takes 1 to 8 of each. Use the command-line tool for filters with more channels. The host may go on to offer another layout; only the first refusal is logged.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #5 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. No filter, so audio passes unchanged. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #5 in REAPER (process 99572): Request 1: Loaded a filter for 48000 Hz, 2 in, 2 out. The host has not played audio since it started the plug-in, so its channels and rate may still change.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #5 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. Playing a filter for 48000 Hz, 2 in, 2 out. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #6 in REAPER (process 99572): Refused the host's channel layout: it has 64 input and 64 output channels; Convolver takes 1 to 8 of each. Use the command-line tool for filters with more channels. The host may go on to offer another layout; only the first refusal is logged.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #6 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. No filter, so audio passes unchanged. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #6 in REAPER (process 99572): Request 1: Loaded a filter for 48000 Hz, 2 in, 2 out. The host has not played audio since it started the plug-in, so its channels and rate may still change.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #6 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. Playing a filter for 48000 Hz, 2 in, 2 out. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #7 in REAPER (process 99572): Refused the host's channel layout: it has 64 input and 64 output channels; Convolver takes 1 to 8 of each. Use the command-line tool for filters with more channels. The host may go on to offer another layout; only the first refusal is logged.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #7 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. No filter, so audio passes unchanged. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #7 in REAPER (process 99572): Request 1: Loaded a filter for 48000 Hz, 2 in, 2 out. The host has not played audio since it started the plug-in, so its channels and rate may still change.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #7 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. Playing a filter for 48000 Hz, 2 in, 2 out. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #8 in REAPER (process 99572): Refused the host's channel layout: it has 64 input and 64 output channels; Convolver takes 1 to 8 of each. Use the command-line tool for filters with more channels. The host may go on to offer another layout; only the first refusal is logged.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #8 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. No filter, so audio passes unchanged. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #8 in REAPER (process 99572): Request 1: Loaded a filter for 48000 Hz, 2 in, 2 out. The host has not played audio since it started the plug-in, so its channels and rate may still change.
2026-11-25 22:37:55 Convolver 0.18.23 VST3 #8 in REAPER (process 99572): Started at 48000 Hz, 2 in, 2 out, blocks of up to 128 frames. Playing a filter for 48000 Hz, 2 in, 2 out. Settings: gain 0 dB, mix 100% wet, latency 0 frames.
```

Files are in `/Users/jrp/convolver-soak/Convolver-0.18.23-preview-macos-arm64/reaper-soak-20261125-220751`.
