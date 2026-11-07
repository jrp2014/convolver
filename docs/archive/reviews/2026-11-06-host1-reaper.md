<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-1 REAPER session, 6 November 2026

> **Dated record.** jrp's results from the first REAPER session of HOST-1,
> posted in the project thread on 6 November 2026 at 22:33 UTC and copied
> here verbatim below the line. The links in it name files on jrp's Mac
> (renders, projects, logs and copied diagnostics); they are not in this
> repository. The two findings still open are summarised in
> [host testing](../../develop/host-testing.md#unresolved-host-findings).

---

6 November 2026. Tester: jrp, assisted by Codex UI automation and CLI
comparisons. This is evidence from one named Mac and host, not an
independent-user qualification or a claim about other platforms.

## Outcome

The stereo numerical tests passed, including saved-project restoration and
retaining the filter after incompatible presets. The window's controls,
reset, one-step Gain undo, diagnostic copy and settings persistence worked.

Two issues prevent an all-pass result:

1. **C5: latency does not apply immediately in this REAPER session.** Choosing
   1024 left 256 frames active, including after stopping playback. Saving,
   quitting and reopening activated 1024. Convolver correctly distinguished
   the chosen and active values throughout.
2. **D2: the 2-input/6-output crossover could not be loaded.** REAPER gave the
   plug-in 6 inputs and 6 outputs on a six-channel track. Its bus-size control
   changed both sides together. No separate input/output count control was
   found in the tested menus. D3–D5 are blocked, not passed.

The identity/echo/repeat listening observations were tentative. Dry Mix
listening, the audible latency gap and alignment at 1024 frames remain
unverified. No product code was changed, and no report was posted to GitHub.

## Candidate and environment

- Workflow: 42244913005; artifact: candidate-macos-arm64.
- Candidate: Convolver 0.10.9.
- Package source: 83aa64dd7efeb716286b05e6756b4018e0e34536.
- All four downloaded asset hashes verified; see [SHA256SUMS](SHA256SUMS).
- macOS 27.2 (26B123), arm64.
- REAPER v7.85/macOS-arm64 rev 19ce87 (Nov 5 2026), evaluation installation,
  notarized build; disable-library-validation entitlement is true.
- Device: macOS default devices, MacBook Pro speakers/microphone; requested
  48000 Hz; observed 512-frame audio blocks. The walkthrough used 48 kHz
  projects except the deliberate 44.1 kHz mismatch check.
- FFT: Accelerate / system vDSP; baseline CPU kernels. Packaged CLI reports
  libsndfile 1.2.2 without FLAC output. Native plug-in filter loading reports
  macOS AudioToolbox. SDK: VST3 3.8.1_build_84, SDK commit 3cdf9ca5d1f5.
- Candidate VST3 installed in the user's Library/Audio/Plug-Ins/VST3 folder;
  strict/deep codesign verification passed with an ad-hoc arm64 signature.
  No earlier user/system VST3 existed. The older Audio Unit was preserved.
- Quarantine was cleared for this test's installed VST3 and package binaries;
  this does not qualify an untouched-download Gatekeeper experience.

## CLI baseline

Both packaged examples matched their expected samples. Complete commands and
outputs are in [cli-baseline.txt](cli-baseline.txt).

| Example | Rate/channels/frames | Largest difference | Relative to peak |
| --- | --- | --- | --- |
| Stereo | 48000 / 2 / 7144 | 1.49e-7 | −131.08 dB |
| Crossover | 48000 / 6 / 4249 | 1.19e-7 | −128.47 dB |

## Part A: smoke test

| Step | Result |
| --- | --- |
| A1 | PASS. Re-scan listed `VST3: Convolver (Convolver)`. |
| A2 | PASS. 48 kHz stereo listening file at position zero; 2-in/2-out host layout. |
| A3 | PASS for UI. Custom view: No filter. Generic view: Gain 0 dB, Mix 100%, Latency 256, Bypass Off, and `No filter: audio passes unchanged. Load a Convolver preset.` |
| A4 | Preset loading PASS; listening tentative. Identity 1 tap, echo 258 taps, repeat 12001 taps. jrp said "i think so" for each listening comparison. Restored echo afterwards. |
| A5 | PASS by user observation: no unwanted click/pop reported when host FX Active was toggled off and on. |
| A6 | PASS numerically. Echo render matched the CLI reference without a 256-frame offset. |
| A7 | PASS. Full quit/reopen restored the 258-tap echo; subsequent render matched. |
| A8 | PASS. Incompatible 44.1 kHz preset refused, retained echo verified by render. No separate REAPER error dialog. |
| A9 | PASS by UI/log and user listening. At 44.1 kHz the host was silent; returning both rates to 48 kHz restored sound. Prior refusal message remained visible. |

Host preset import menu: **+ / Import VST3 preset (.vstpreset)...**.
After echo, generic Status was `Filter loaded: 48000 Hz, 2 in, 2 out.`
REAPER continued to show **No preset** as the host preset name.

A8 generic Status:

> Load failed, previous filter kept: the filter is for 44100 Hz, the host runs
> at 48000 Hz. Details in the Convolver log.

A9 generic Status:

> Silent: the filter is for 48000 Hz, the host runs at 44100 Hz. Details in
> the Convolver log.

`a6-echo.wav` and `a8-echo-reopened-refused.wav` each had 1008000 frames
(20-second source plus 1-second tail). Against `a6-echo-expected.wav`, both
reported **Match**, maximum difference 5.18e-17 (−301.63 dB relative to peak
0.0625), channel 2, frame 97817. Render settings were Master mix, Entire
project, offline full speed, 48 kHz stereo WAV 32-bit float, Tail 1000 ms,
postprocessing and dither off.

## Part B: stereo numerical journey

B1/B2 passed: input.wav at position zero, both fades explicitly zero,
default track/master gain and pan, 3001-tap filter and three routes including
In 1 ×0.5 → Out 2 with delays.

| Step | Render | Result |
| --- | --- | --- |
| B3 | package/host-render.wav | Match |
| B4 | package/host-render-reopened.wav | Match after full quit/reopen |
| B5 | package/host-render-refused.wav | Match after wrong-rate and wrong-channel presets |

All three used Master mix / Entire project / offline full speed / 48000 Hz /
stereo / WAV 32 bit FP / Tail 1000 ms / no postprocessing or dither. Each
contained 52096 frames; expected.wav contains 7144 frames. All reported the
same largest difference: **1.49e-7 (−131.08 dB relative to peak 0.534)**,
channel 1, frame 810. Full outputs remain in
[the chronological session notes](walkthrough-session-notes.md).

B5 refused echo-44100-stereo with the 44100/48000 rate explanation, then
refused echo-48000-mono with the 1-in/1-out versus 2-in/2-out explanation.
Both said the previous filter keeps playing. The numerical comparison
confirms it did. Project: [b-stereo.rpp](b-stereo.rpp).

## Part C: window and settings

Used [c-window.rpp](c-window.rpp), a separate project with the 20-second
listening clip, so these changes did not disturb the numerical stereo test.

| Step | Result |
| --- | --- |
| C1 | PASS. Choose… loaded stereo/filter.cfg and its adjacent room.wav; Playing, filter.cfg, 3001 taps and correct routes. Project title became [modified]. |
| C2 | One-step undo PASS. Mouse drag reached −11.8 dB; one main-window Edit > Undo restored 0. No obvious click reported, tentatively. |
| C3 | Mix 0% appeared correctly. Dry listening result unconfirmed. |
| C4 | PASS. Option-click reset Gain from −6 to 0 dB and Mix from 0 to 100%; sliders and boxes settled at the defaults. |
| C5 | Does not meet the immediate-change expectation; details below. |
| C6 | PASS. Disclosure grew the FX window to fit file/software/log lines and collapsed again; displayed lines were visible. |
| C7 | PASS. Copy Diagnostics changed to Copied; actual clipboard text saved in the linked reports below. |
| C8 | PASS. Save/full quit/reopen restored Gain −6 dB, Mix 70%, the 3001-tap filter and Latency 1024. |

At 22:15:13, the log recorded `Latency set to 1024 frames`, but no immediate
host restart followed. The window said **21.3 ms, not in use yet**, with
256 frames still active. REAPER's lower-left FX performance/PDC display
remained **256/512 spls**. Stopping playback did not apply the change.
After save/quit/reopen at 22:16:14, the window reported active **21.3 ms** and
REAPER showed **1024/1024 spls**. The audible gap and numerical alignment at
1024 were not independently verified.

Reports copied from the plug-in:

- [Pending latency](c-diagnostics-pending-latency.txt)
- [Restored settings and active latency](c-diagnostics-reopened.txt)

The user answered "don't think so" to the combined Gain-click/dry question;
this is recorded as tentative absence of an unwanted click, not a firm dry
sound comparison.

## Part D: crossover routing blocker

D1 passed: six-channel track, 48 kHz project/device, crossover/input.wav at
zero with fades zero. The saved project file confirms NCHAN 6, explicit
SAMPLERATE 48000 and the expected source path.

D2 failed to establish the required layout:

1. Added VST3 Convolver to the six-channel track. Host offered 6 inputs and
   6 outputs; the FX button read **6 in+out**.
2. Imported the packaged 2-in/6-out crossover preset. It was refused, with
   no previous filter available, so the plug-in reported silence.
3. Opened the pin connector. It showed six input and six output pins with
   diagonal mappings, **Track channels: 6**, **Plug-in bus size: Auto**.
4. Its I/O menu offered **Request VST3 bus channel count** with Auto, mono,
   stereo and numbered channels per bus. No separate input/output bus-count
   controls were found. Selecting stereo changed both input and output
   pins to two while leaving the track at six; the log confirmed 2 in/2 out.
5. Restored Auto, which returned to 6 in/6 out, and retried the crossover
   preset. It was refused again.

Exact copied Status:

> Load failed. Silent: the filter has 2 in, 6 out, the plug-in 6 in, 6 out.
> Details in the Convolver log.

The explanation correctly named the mismatch and suggested configuring
2 inputs/6 outputs or using a filter matching the host. The tested controls
did not provide the first route. This establishes a blocker in the tested
workflow, not proof that no REAPER configuration can ever support it.

**D3, D4 and D5 were not run:** there was no loaded crossover to render,
restore or retain after refusals. The CLI crossover pass is not substituted
for a host pass. [d-crossover.rpp](d-crossover.rpp) preserves the blocked
setup. [Copied routing diagnostics](d-diagnostics-routing-refusal.txt) and
[final log](final-Convolver.log) preserve the exact refusal and layouts.

## Guide and usability findings

- Clarify REAPER's latency behavior and a tested save/reopen fallback;
  investigate why the immediate restart request was not applied.
- Establish a working REAPER crossover layout before presenting D as an
  end-to-end recipe. The current shared bus-size setting is insufficient.
- The FX window's Edit menu has no project Undo. Switch to the main window
  (Command-backtick worked) without closing the FX chain; closing it adds
  its own undo entry.
- Undo restored embedded plug-in state and changed file provenance to
  **From the saved project or preset**, disabling file reload. The audio
  filter remained intact; this provenance change may surprise users.
- Stop playback before editing REAPER Audio > Device preferences. With
  project override enabled, changing the project rate already changes the
  actual device rate.
- Native picker focus sometimes required user help or separated Go to
  Folder/path/Return/Open actions. Automation also timed out; a process
  sample showed the main event loop, not a blocked Convolver stack. The
  user restarted REAPER after reporting no main screen. Resetting the UI
  automation connection restored reliable control. No Convolver crash or
  deadlock was established by this evidence.

## Evidence and final state

- [Chronological notes, including full render comparisons](walkthrough-session-notes.md)
- [Original walkthrough](reaper-session-steps.md)
- [Final Convolver log](final-Convolver.log); earlier per-part logs retained.
- a-smoke.rpp, b-stereo.rpp, c-window.rpp and d-crossover.rpp are saved.
- All generated renders and CLI references remain under this folder.
- REAPER is left on the saved, stopped d-crossover reproduction. The
  installed candidate VST3 remains available; no cleanup was performed.
