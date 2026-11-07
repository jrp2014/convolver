<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-1 Audacity session, 7 November 2026

> **Dated record.** jrp's results from the Audacity part of HOST-1 (the VST3
> plug-in, stereo), posted in the project thread on 7 November 2026 at 23:25
> UTC, and the Audacity settings guide that came with it, posted at 23:26
> UTC, both copied here verbatim below the line, without the SPDX line each
> began with; the separator, heading and italic note before the settings
> guide are added. The links and file names in them name files on jrp's Mac
> (the file-loading review, the JSON comparisons, projects, exports and
> logs) and are not in this repository, except the settings guide, whose
> text is the [second part](#settings-guide) of this page.
>
> The candidate was built from PR #131 (PLUG-23) at `c3dbf0d`, as the merge
> `1b0d873` with the `main` of that time (0.10.13), so it has the window from before PR
> #128 (PLUG-22) and before the 0.11 and 0.12 version steps. What it adds to
> HOST-1, what remains and the TODO items it led to are in
> [TODO](../../../TODO.md) under HOST-1; the Audacity steps it changed are in
> [host testing](../../develop/host-testing.md#audacity-vst3). The proposed
> Status wording in its fifth integration problem was taken up by DOC-9 (PR
> #136).

---

# Audacity VST3 stereo walkthrough — 7 November 2026

Actual host exports passed all seven available CLI comparisons, including a full
application restart. The no-filter baseline also passed. Listening checks and
several optional host interactions remain unverified; this is stereo audio and
state-persistence evidence, not complete host qualification.

[Audacity editor and audio settings](audacity-settings.md) ·
[File-loading and location review](file-loading-review.md) ·
[Machine-readable comparisons](comparison-results.json)

## What was tested

- Audacity 4.0.2, build 263060743, arm64; macOS 27.2, build 26B123.
- Convolver 0.10.13 VST3, Accelerate FFT, baseline CPU kernels, installed in
  `~/Library/Audio/Plug-Ins/VST3/Convolver.vst3`.
- Candidate from [Actions run 42404299284](https://github.com/jrp2014/convolver/actions/runs/42404299284).
  PR head `c3dbf0db5faf383e33295c27f1b226bb838068bd`; artifact source merge
  `1b0d873729f65cfba768e2dada1675cccec953fa`. These are different revisions by
  design: the candidate workflow built the merge commit.
- Installed and packaged plug-in executable SHA-256:
  `dbbf9b800f3a7de2f9b999f9a718a2be153b94f559f3da90ea2381c59fd3e383`.
  Strict/deep code-signature verification passed during preparation.
- Input: `~/host1/transients-48000-stereo.wav`, 48000 Hz, stereo PCM16,
  960000 frames (20 seconds). SHA-256:
  `9ae7f715740138b1830c53d5c5a905801a03a8c07054a6325bff37970092ab05`.
- References rendered with the CLI from the same candidate and its identity,
  short-echo and quarter-second-repeat presets. Unity track and plug-in gain,
  centred pan, 100% wet, 256-frame latency, except the explicit bypass test.
- Audacity exports: 48000 Hz, stereo, 32-bit float WAV, full project. Comparison
  used `convolver --compare`, with no alignment, rescaling or timestamp/byte
  comparison. Commands and numerical output are in the JSON above.

## Results

| Check | Result | Largest sample error relative to CLI output peak |
| --- | --- | --- |
| Discovery/validation | Pass: Audacity registered the VST3 as validated and real-time capable | Not an audio test |
| No-filter baseline versus original WAV | Pass | 3.73e-08, −130.51 dB |
| Identity preset | Pass | 5.22e-08, −127.59 dB |
| Short echo preset | Pass | 2.79e-08, −126.99 dB |
| Quarter-second repeat preset | Pass | 2.98e-08, −126.43 dB |
| Echo applied through the Effect menu | Pass | 4.10e-08, −123.67 dB |
| Host bypass versus identity reference | Pass | 3.73e-08, −130.51 dB |
| Echo retained after a refused 44.1 kHz preset, then stop/start | Pass | 2.79e-08, −126.99 dB |
| Save, quit Audacity, restart, reopen and export echo | Pass | 2.79e-08, −126.99 dB |

The refusal happened during playback: process 69947, VST3 #4, Request 9 at
22:56:52. The log explicitly says the previous filter keeps playing; Request 10
accepted the retained 48000 Hz filter on restart. The export matches echo.

The saved project is
`~/Documents/Audacity4/Projects/audacity-realtime-echo.aup4`.
The user completed the application restart after automation lost the main
window. The new process was 72391, versus 69947 before restarting. Its export
instance started at 48000 Hz with 8192-frame blocks, and a brief live playback
started at 48000 Hz with 512-frame blocks. Both logs identify a loaded stereo
filter. `exports/echo-reopened.wav` confirms that the saved filter still produces
the expected echo. The user did not provide a listening judgement for that run.

For the applied-effect test, the older project also contained a real-time
Convolver with no filter, already verified transparent by the baseline. The
applied echo was undone afterwards and that project saved separately. This was
not a completely empty effect chain.

## Length and evidence limits

Every active-effect export had 960255 frames, including identity. The CLI
references had 960000 identity frames, 960257 echo frames and 972000 repeat
frames; bypass had exactly 960000 frames. All unmatched frames were checked:
they are exactly zero. See [the length check](tail-length-comparison.json).
The source ends in silence. These matches therefore do **not** qualify Audacity's
handling of a nonzero reverberation tail beyond the end of a clip.

Not verified in this run:

- Audible clicks/glitches, the perceived sound of preset transitions, or whether
  a valid replacement chosen during playback is heard immediately. Valid repeat
  was loaded while stopped; the incompatible preset was selected while playing.
- VoiceOver, detailed visual layout and screenshots. Screen capture failed with
  “could not create image from display”.
- Mono tracks, wider channel layouts, latency changes, or normal `.vstpreset`
  import through Audacity's interface.
- A fresh successful direct `.cfg` chooser load after matching playback rate.

The first repeat-selection attempt failed in the automation helper; a shell
then incorrectly continued to export echo. That artifact is preserved as
`exports/echo-repeat-selection-did-not-run.wav` and is excluded from passing
results. The later correctly selected repeat export passes. Action scripts
subsequently stopped on errors. This was an automation failure, not a DSP failure.

## Integration problems and settings

1. Audacity's **Preset options > Import** accepts its text preset format, not
   `.vstpreset`. A text-wrapper experiment did not load a filter and is not a
   supported workaround. The completed tests used four deliberately prepared
   user-preset entries containing unchanged component state from the package.
2. Audacity's live playback initially used 44100 Hz while export used 48000 Hz.
   A 48000 Hz `.cfg` was refused against 44100 Hz. Refusing an incompatible
   active rate is correct; the usability issue is making the active rate and
   any provisional host settings clear. Do not infer live rate from export rate.
3. Audacity's generic editor was temporarily enabled for automation. It is
   simpler than Convolver's own editor. **Use vendor UI was enabled again**;
   [the settings guide](audacity-settings.md) gives the exact menu and explains
   what the choice changes.
4. Automation sometimes lost Audacity's main window or keyboard focus. The user
   completed the restart. This run does not establish whether that was an
   Audacity window-management defect, automation behaviour, or interference from
   changing applications.
5. “Checked when the host starts it” is ambiguous. Recommended wording before
   audio settings are confirmed: “Filter loaded: 48 kHz, stereo. Waiting for
   audio settings from the host.” This is a proposal; no plug-in code changed.

## Files and settings left behind

- All test assets and changes are under `~jrp`. The administrator prompt was
  cancelled and no files were installed under `/Library/Audio/Presets`.
- **Audacity's original 44100 Hz default was restored**, verified in the audio
  preferences and its persisted preferences file. Set playback to 48000 Hz again
  before playing these 48000 Hz filters. Playback is stopped.
- **Use vendor UI is enabled**, and its preference survived the application
  restart. Export defaults remain 48000 Hz, stereo, float WAV, targeting this
  run's `exports/` folder; these are recorded in the settings guide.
- Four uniquely named `Audacity-test-…` user presets remain in
  `~/Library/Application Support/Audacity/Audacity4/pluginsettings.cfg` for reuse.
  `user-preset-keys-added.json` lists the exact additions;
  `pluginsettings-before-user-presets.json` preserves the earlier contents.
  Do not restore that whole backup over subsequent user changes.
- Four `.vstpreset` copies also remain in the user's Audio/Presets folder;
  `presets-created.json` lists them. They were not the working Audacity discovery
  route in this version. No existing preset was overwritten.
- The saved test projects and exports are retained. Repository source, installed
  plug-in binaries and the open REAPER project were not changed.

This report supersedes the preparation-only `results-preparation.md`; the earlier
notes are retained as a chronological record. Nothing was committed or pushed.

---

## Settings guide

*Added: posted with the report at 23:26 UTC; the file `audacity-settings.md`
the report links to.*

# Show Convolver's full editor in Audacity

For the macOS VST3 plug-in in Audacity 4.0.2, as tested on 7 November 2026.

## Enable the full editor

1. Open the track's **Effects** panel and click **Convolver** in its real-time
   effects list. Select the VST3 version when adding the effect.
2. In the effect window, open **Preset options** and enable **Use vendor UI**.
   Audacity uses “vendor UI” to mean the editor supplied by the plug-in.
3. Convolver's own editor should replace Audacity's generic parameter controls.
   It includes the filter chooser, status and explanations, filter details,
   Gain, Mix, Latency and diagnostics. The host still supplies its surrounding
   window and preset controls.

If you see only Audacity's generic Gain, Mix, Latency and Bypass controls,
check **Use vendor UI** again. Disabling it changes the view, not the filter or
processing settings. The macOS Convolver editor is also used in REAPER, although
REAPER's surrounding effects window has different controls.

For this test, the generic view was temporarily enabled to make parameter
controls accessible to automation. **Use vendor UI was enabled again** before
restarting Audacity. This temporary test setting accounts for the simpler view;
it is not a separate, reduced version of Convolver.

## Match playback and export rates

For the supplied 48 kHz stereo test clip and filters:

1. Open **Audio setup > Audio settings** and set **Default sample rate** to
   **48000 Hz**. Use a project running at 48000 Hz.
2. Play a short section and check the host rate in Convolver's diagnostics or
   its log. An export configured for 48000 Hz does not establish that live
   playback also uses 48000 Hz. Both rates differed during this session until
   the playback setting was changed.
3. Use **File > Export audio > On your computer**. For comparison with the CLI,
   select **WAV (Microsoft)**, **48000 Hz**, **Stereo**, **32-bit float** and
   **Full project audio**. Leave track gain at 0 dB, pan centred, Convolver Gain
   at 0 dB, Mix at 100% and bypass off. The tests used 256 frames of latency.
4. Save projects and exports under your home folder. These checks require no
   changes to system folders or administrator authentication.

The original Audacity default was **44100 Hz**. Restore that after testing if
it is your preferred default; use 48000 Hz again when playing this test project.
The current restoration status is recorded in [the test report](results.md).

## Preset import is a separate setting

**Use vendor UI** does not add `.vstpreset` support to Audacity's own
**Preset options > Import** command. In this version that command accepts
Audacity's text preset format. For ordinary local filter files on macOS, use
Convolver's **Choose…** button with a `.cfg` or impulse-response sound file and
confirm the host's rate and channels match.

The numerical tests used four specially prepared Audacity user presets under
`~/Library/Application Support/Audacity/Audacity4/`. They are test fixtures,
not proof that Audacity can import `.vstpreset` through its interface. The
[report](results.md) records their provenance and the
[file-loading review](file-loading-review.md) proposes simplifying this route.
