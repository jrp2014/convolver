<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Host testing

[Documentation index](../README.md) · [Testing](testing.md) · [Install](../use/install.md)

For anyone testing Convolver's plug-ins or exports in a real host: what to
record, the smoke test for a preview, and the full qualification for a
production release. Automated tests are in [testing](testing.md).

**In short:** no named DAW is qualified yet. jrp's first hosts, on his Mac,
are REAPER and Audacity for the VST3 plug-in and GarageBand for the Audio
Unit; each has a [step-by-step smoke test](#smoke-test-in-each-first-host)
below. An unsigned preview needs the smoke test; a signed production release
needs the full qualification. Every report records the exact package, host,
system and settings.

Contents: [first hosts](#first-hosts) ·
[Steinberg's SDK hosts](#steinbergs-sdk-hosts) · [every report](#every-report) ·
[smoke test](#smoke-test-for-an-unsigned-preview) ·
[smoke test in each first host](#smoke-test-in-each-first-host) ·
[production qualification](#production-qualification) ·
[Equalizer APO on a real device](#equalizer-apo-on-a-real-device)

No named DAW is currently qualified for portable Convolver. Automated SDK
validation, standalone adapter tests and manual DAW tests are different evidence.
Ableton Live, Cubase and Logic need available licences/testers; no
compatibility claim is inferred.

There are two levels of testing. An **unsigned preview** needs the smoke test
below. A **signed production release** needs the full qualification procedure.
Both gates are set out in the
[plug-in formats plan](../project/plans/2026-10-21-plugin-formats.md#unsigned-previews-and-production).

## First hosts

jrp tests on his Mac with these three (it
replaces REAPER and Carla, suggested on 22 October). Together they cover a full
DAW, the audio editor many users already have, and Apple's own Audio Unit host.
Host facts were checked on 23 October 2026.

| Host | Format tested | Cost | What it adds | Limits for this test |
| --- | --- | --- | --- | --- |
| [REAPER](https://www.reaper.fm/download.php) 7 (Cockos) | VST3 | Free 60-day evaluation, fully functional; it keeps running afterwards with a reminder, though the licence then expects a purchase | Projects that save plug-in state, offline rendering, latency compensation, CPU meter. Also loads AU on macOS, for AU-4 later | Tracks have at least two channels, so mono presets are not part of its smoke test |
| [Audacity](https://www.audacityteam.org/download/) 4.0 or later (GPL) | VST3 | Free | An audio editor: real-time effects on a track, and effects applied to a selection. Version 4.0 (3 September 2026) loads VST3 and, on macOS, AU | Its preset menu imports only Audacity's own text files, not `.vstpreset`; presets come from the system VST3 preset folder instead ([preparation](#prepare-the-mac)). It asks Convolver for stereo on every track |
| GarageBand 10.4 (Apple) | Audio Unit only | Free with macOS | Apple's own host, with the same Audio Unit check as Logic | Projects run at 44.1 kHz only, and GarageBand offers only the stereo version of an effect, so only the `44100-stereo` presets apply. Preview packages hold no Audio Unit yet, so build it from source |

Record each result under HOST-1 (VST3 hosts) or AU-4 (GarageBand) in
[TODO](../../TODO.md), with the details in [every report](#every-report).

### Apple silicon and macOS 26

A host loads only plug-ins built for the architecture it is running as. On an
Apple silicon Mac, run every host natively (arm64) with the `macos-arm64`
Convolver; an Intel Mac uses `macos-x86_64`. The `macos-arm64` preview needs
macOS 14 or later. Checked on 23 October 2026:

- **REAPER:** use the **macOS Universal** download (`reaper782_universal.dmg`
  for version 7.82): one app with arm64 and x86_64 code, which runs natively on
  Apple silicon. The sources disagree about macOS 26: the download page says
  REAPER supports macOS 10.5 to macOS 26, but the 7.82 disk image's own text
  says "macOS 10.15 to macOS 15". Treat REAPER on macOS 26 as unconfirmed by
  Cockos: if a step fails, repeat it with another VST3 plug-in before blaming
  Convolver. Do not tick *Open using Rosetta* in the app's Finder *Get Info*
  window: under Rosetta REAPER runs as x86_64 and cannot load the arm64
  plug-in.

  This download is notarized and uses the hardened runtime; Cockos also
  offers a non-notarized build, saying some plug-ins work better with it. The
  hardened runtime refuses plug-ins signed by another developer unless the app
  has the `com.apple.security.cs.disable-library-validation` entitlement, and
  Convolver's previews carry only an ad-hoc signature. A DAW that loads other
  vendors' plug-ins needs that entitlement, so REAPER should have it, but this
  could not be checked from here. Check it before the first test:

  ```sh
  codesign -d --entitlements - /Applications/REAPER.app
  ```

  If the output lists `com.apple.security.cs.disable-library-validation` as
  true, use the notarized build. If not, or if REAPER lists Convolver but will
  not load it, use the non-notarized build from the same page, and record which
  build you used.
- **Audacity:** the **ARM64 DMG** and the **Universal DMG** (4.0.1) run the
  same arm64 code on Apple silicon, built with the same options; either will
  do. The universal one can also be switched to Rosetta (*Get Info › Open
  using Rosetta*) to try the `macos-x86_64` Convolver on the same Mac. The
  download page shows "No plugin support" under the ARM64 DMG, but that note
  was written for Windows on ARM:
  - The website's
    [`DownloadCard.jsx`](https://github.com/audacity/audacity.github.io/blob/191c2e8461a9ee7ec71f32a00a21a98c4924bb92/src/components/card/DownloadCard.jsx#L64-L74)
    adds it to every download whose title contains "ARM64", whichever page
    it is on. It came in one commit,
    ["add ARM warnings"](https://github.com/audacity/audacity.github.io/commit/e55509f19168b758328f1a2bf228a394f0404824)
    (1 September 2025), which also added the `#arm` section to the Windows
    page only.
  - The note's "More info" link points to `#arm`. Only the
    [Windows page](https://github.com/audacity/audacity.github.io/blob/191c2e8461a9ee7ec71f32a00a21a98c4924bb92/src/pages/download/windows.astro#L34-L45)
    has that section, "Windows on ARM notes": "Plugins (VST, OpenVINO, etc.)
    are not supported." The
    [macOS page](https://github.com/audacity/audacity.github.io/blob/191c2e8461a9ee7ec71f32a00a21a98c4924bb92/src/pages/download/mac.astro)
    has no such section, so the link goes nowhere there.
  - Audacity 4.0.1's own build turns plug-ins on for every Mac architecture.
    [`CMakeLists.txt`](https://github.com/audacity/audacity/blob/Audacity-4.0.1/CMakeLists.txt#L95-L104)
    sets the VST module on for all builds and the Audio Unit module on for
    macOS. The
    [macOS build workflow](https://github.com/audacity/audacity/blob/Audacity-4.0.1/.github/workflows/au4_build_macos.yml#L65-L67)
    builds universal, arm64 and x86_64 with the same options.

  Audacity says it is tested on macOS 14 and 15. If something fails on
  macOS 26, repeat the step with another VST3 plug-in before blaming
  Convolver.
- **GarageBand:** the version from the App Store for your macOS is native on
  both architectures. Build the Audio Unit on the Mac that runs GarageBand, so
  it matches.

## Steinberg's SDK hosts

These are tools for developers, not evidence that a DAW works.

- The SDK **validator** loads the built plug-in in every CI run, in default and
  extensive modes ([VST3 tests](testing.md#vst3-tests)). pluginval, an independent host, also runs in CI and renders real
  filters.
- **VST3PluginTestHost** (Steinberg, free, Windows and macOS) is a convenient
  manual check without a DAW: it can load presets, change buses and run audio.
- The SDK's sample hosts `audiohost` and `editorhost` were evaluated on
  22 October 2026 and are **not** added to CI, because they would catch nothing
  that the validator and pluginval do not already catch. (The SDK's third
  sample, the VST3 Inspector, only lists a plug-in's class information.)
  - `editorhost` only opens a plug-in's own editor window. Convolver has no
    editor (VST3-3 is deferred), so it stops at once with "EditController does
    not provide its own editor". It becomes worth a smoke test only if an editor
    is added, and pluginval's GUI tests (skipped in CI today with
    `--skip-gui-tests`) could cover the same ground instead.
  - `audiohost` (cross-platform, but tested by Steinberg only on Linux; needs a
    running JACK server) loads the plug-in and activates it with its default
    stereo layout, but never activates its buses, so Convolver receives and
    writes no audio. It cannot load a preset or state and never checks the
    output. It also exits with status 0 even when the plug-in fails to load,
    so a CI step could not tell success from failure.

  Both build and run headless on Linux (`audiohost` against `jackd -d dummy`,
  `editorhost` under `xvfb-run`). To build them, configure the SDK on its own,
  with `SMTG_ENABLE_VST3_HOSTING_EXAMPLES` left on (its default); `audiohost`
  needs the JACK development package, and `editorhost` on Linux needs GTK 3 and
  gtkmm 3.

## Every report

Record host version, OS version/build, CPU architecture, package SHA-256, FFT
backend, sample rate, input/output channel layout, buffer size and exact steps/results.
Attach the [Convolver log](../use/plugins.md#status-and-the-convolver-log)
from the session (on macOS, `pbcopy < ~/Library/Logs/Convolver/Convolver.log`
copies it): each line names the version, format, instance and host, and says
what was loaded, how the host started the plug-in and why anything failed.
Keep private audio out of reports. Use the supplied synthetic examples.

## Smoke test for an unsigned preview

1. Download the preview the way a user would and check its SHA-256 against the
   release notes. Install it as the [install guide](../use/install.md) says, including
   its quarantine removal on macOS for this bundle only. Never disable security
   system-wide.
2. Scan, insert Convolver, and load one example preset at 48 kHz in mono and
   stereo, where the host offers both ([first hosts](#first-hosts)). Check
   that the identity and echo examples sound as expected.
3. Save the project, close and reopen it, and check that the filter is restored.
4. Try a preset with the wrong rate or channel count while a working filter is
   running. The working filter must keep running, the plug-in's Status
   parameter must say why (`Load failed, previous filter kept: ...`), and the
   Convolver log must explain it in full. Record what the host itself
   displays, and where it shows the Status parameter, if it does.

## Smoke test in each first host

These follow the [smoke test](#smoke-test-for-an-unsigned-preview) above with
the menus of each host. Menu names can change between host versions; record
what you actually chose. Play quiet audio and keep the volume low.

### Prepare the Mac

Do this once. Run the commands in the extracted preview package folder,
except the Audio Unit build, which runs in a source checkout.

1. Get the package for your Mac's processor (`macos-arm64` on Apple silicon,
   `macos-x86_64` on Intel). Until a preview is published, download the
   `candidate-macos-arm64` (or `candidate-macos-x86_64`) artifact from the
   latest successful "Plug-in distribution candidates" run on `main`, and
   unzip the package inside it. Check its SHA-256, install `Convolver.vst3` as
   the [install guide](../use/install.md) says, and clear the quarantine for
   the bundle and for `bin/` only
   ([unsigned previews](../use/install.md#unsigned-previews)).
2. For GarageBand, build the Audio Unit ([Audio Unit build](build.md#audio-unit)),
   copy `build-au/plugins/au/Convolver.component` to
   `~/Library/Audio/Plug-Ins/Components/` and check it:

   ```sh
   killall -9 AudioComponentRegistrar 2>/dev/null || true
   auval -v aufx Conv CONV
   ```

   The output must end with `AU VALIDATION SUCCEEDED`. Then make the two
   GarageBand presets from the package's VST3 examples, so they appear in the
   unit's preset menu (the tool refuses to overwrite, so delete old copies
   before running it again):

   ```sh
   mkdir -p ~/Library/Audio/Presets/Convolver/Convolver
   for kind in identity echo; do
     ./bin/convolver-preset "presets/$kind-44100-stereo/filter.vstpreset" \
       ~/Library/Audio/Presets/Convolver/Convolver/$kind-44100-stereo.aupreset
   done
   ```

3. For Audacity, copy the stereo VST3 examples into the system VST3 preset
   folder, each under its own name (Audacity names a preset after its file,
   and every example file is called `filter.vstpreset`):

   ```sh
   sudo mkdir -p /Library/Audio/Presets/Convolver/Convolver
   for d in presets/*-stereo; do
     sudo cp "$d/filter.vstpreset" "/Library/Audio/Presets/Convolver/Convolver/$(basename "$d").vstpreset"
   done
   ```

   Audacity reads this folder once per session, so restart it if it was
   open. Remove the folder after testing
   (`sudo rm -r /Library/Audio/Presets/Convolver`).
4. Have a stereo test file at 44.1 kHz and one at 48 kHz, with clear
   transients (a click track or spoken word).
5. Start each host's test with an empty
   [Convolver log](../use/plugins.md#status-and-the-convolver-log), and copy it
   into the report afterwards:

   ```sh
   mkdir -p ~/Library/Logs/Convolver && : > ~/Library/Logs/Convolver/Convolver.log
   # ... run the host's steps, then:
   pbcopy < ~/Library/Logs/Convolver/Convolver.log
   ```

   GarageBand is sandboxed, so its copy of the log may be inside its
   container; the system log has the same lines either way:
   `log show --last 1h --info --predicate 'eventMessage CONTAINS "Convolver"' --style compact | pbcopy`.

### REAPER (VST3)

1. Open REAPER's *Settings* (*Preferences* in older versions) › *Plug-ins ›
   VST*, check that the VST3 path includes
   `~/Library/Audio/Plug-Ins/VST3`, and rescan. Convolver should be listed as a
   VST3 effect from the vendor Convolver.
2. Set *File › Project settings* to 48000 Hz and the audio device to 48 kHz.
   Import the 48 kHz test file onto a track.
3. Open the track's FX window and add Convolver. With no preset loaded it
   passes stereo audio unchanged. Convolver has no window of its own, so
   REAPER should show its generic controls: Bypass and a read-only **Status**.
   Status should read `No filter: audio passes unchanged. Load a Convolver
   preset.` Record whether REAPER shows Status, and where (the generic
   controls, or the **Param** button's list).
4. From the **+** button beside the FX window's preset menu, import
   `presets/identity-48000-stereo/filter.vstpreset` (the import item for VST
   preset files). Play: the sound is unchanged. Import
   `presets/echo-48000-stereo/filter.vstpreset`: about 5 dB quieter, with a
   hollow, comb-filtered tone. (Its echo comes 257 samples, about 5 ms, after
   the direct sound, too soon to hear as a separate repeat.) Status reads
   `Filter loaded: 48000 Hz, 2 in, 2 out.`
5. Toggle the plug-in's bypass (the checkbox beside its name) while playing.
   The sound switches between filtered and unfiltered; note any click.
6. Render the project (*File › Render*, offline) to a WAV and check that it
   holds the echo, starting in time with the source rather than 256 samples
   late.
7. Save the project, quit REAPER, reopen the project and play: the echo is
   still there.
8. While the echo plays, import `presets/identity-44100-stereo/filter.vstpreset`.
   The echo keeps playing. Status reads `Load failed, previous filter kept:
   the filter is for 44100 Hz, the host runs at 48000 Hz. Details in the
   Convolver log.`, and the log has a line starting `Filter not loaded; the
   previous filter is kept.` that says to load a 48000 Hz filter or set the
   host to 44100 Hz. Record what REAPER itself shows, if anything.
9. Set *File › Project settings* and the device to 44100 Hz and play: silence,
   with Status `Silent: the filter is for 48000 Hz, the host runs at 44100 Hz.`
   and a log line `Started at 44100 Hz, ...` explaining it. Set both back to
   48000 Hz: the echo returns. Copy the log into the report.

### Audacity (VST3)

1. Use the Audacity build for your Mac's processor. On start, Audacity offers
   to validate new plug-ins: accept. Otherwise use *Effect › Plugin manager ›
   Rescan plugins*. Convolver should be listed with the VST3 effects.
2. In a new project, import the 48 kHz stereo file and check that the project
   rate is 48000 Hz.
3. Open the track's real-time effects (the track's **Effects** button, or
   **E**) and add Convolver. In its preset menu choose `identity-48000-stereo`
   (listed with the plug-in's own presets after [preparation](#prepare-the-mac)).
   Play: unchanged. Choose `echo-48000-stereo`: quieter and hollow, as in
   REAPER. Record whether a preset chosen during playback is heard at once or
   only after stopping and starting playback again. The effect's window lists
   a read-only Status. Audacity's window has its own copy of the plug-in,
   which normally is not started, so Status should read `Filter loaded: 48000
   Hz, 2 in, 2 out; checked when the host starts it.` Audacity may start that
   copy briefly at 44100 Hz, and then Status reads `Silent: the filter is for
   48000 Hz, the host runs at 44100 Hz. ...` although the playing copy is
   fine. Record what it shows, and which `VST3 #n` lines the log has.
4. Switch the effect off and on with its power button while playing; note
   any click.
5. Save the project, quit, reopen and play: the echo is still there.
6. Remove the real-time effect. Select a few seconds, choose Convolver from the
   **Effect** menu with the `echo-48000-stereo` preset, and apply it. Zoom in on
   a transient: the result starts where the original did (Audacity removes the
   reported 256-sample delay by default). Undo.
7. Add the effect again with the echo preset, play, and choose
   `identity-44100-stereo`. The echo keeps playing. Stop and start playback:
   the echo must still play, not silence or unfiltered sound. Record what
   Audacity shows. Status should name the 44100 Hz filter, and the log should
   say what each playing copy did: a `Filter not loaded; the previous filter
   is kept.` line, or, if playback went silent, a `Started at 48000 Hz, ...`
   line saying the filter does not suit the host. Copy the log into the
   report; this step is where the window's copy and the playing copy can
   disagree.
8. Record what a mono track does with a stereo preset; Audacity asks the
   plug-in for two channels even there, so no particular result is expected.

### GarageBand (Audio Unit)

1. Turn on *GarageBand › Settings › Audio/MIDI › Enable Audio Units* and
   restart GarageBand if it asks.
2. Create an empty project with an audio track, and drag the 44.1 kHz stereo
   file onto it (GarageBand projects always run at 44.1 kHz).
3. Show Smart Controls (**B**) and open the **Plug-ins** area. Click an empty
   plug-in slot and choose *Audio Units › Convolver › Convolver*. Click the
   middle of the slot to open its window. With no preset loaded it passes the
   audio unchanged. Record whether the window lists a read-only Status and
   what it reads: the full line (`No filter: audio passes unchanged. ...`), or
   only its name (`No filter`), as Apple's generic window may show.
4. From the window's preset menu choose `identity-44100-stereo`, then
   `echo-44100-stereo`, and listen as in REAPER.
5. Switch the plug-in off and on with the slot's power button while playing;
   note any click. GarageBand, not Convolver, saves whether the plug-in is off.
6. Save the project, quit, reopen and play: the echo is still there.
7. Export the song (*Share › Export Song to Disk*) and check that the file holds
   the echo.
8. Build a 48 kHz preset with `./bin/convolver-preset
   presets/echo-48000-stereo/filter.vstpreset
   ~/Library/Audio/Presets/Convolver/Convolver/echo-48000-stereo.aupreset`
   and choose it while the 44.1 kHz echo plays. The echo keeps playing. Record
   what GarageBand shows. Status, where it is shown, reads `Load failed,
   previous filter kept: the filter is for 48000 Hz, the host runs at 44100
   Hz. ...` (or only `Load failed`); the full reason is in the system log (the `log show` command in
   [preparation](#prepare-the-mac), or the Console application, searching
   for "Convolver").

## Production qualification

1. Download through the actual user-facing route, retaining quarantine metadata.
   Install the signed package and scan without disabling security. Check
   `convolver-preset` operation too.
2. Check identity and echo output at 44.1/48/96 kHz, mono/stereo, variable blocks,
   long-tail silence, stop/start/seek and offline/bounce rendering. Align numerical
   comparison by 256 samples; use an independent time-domain convolution reference.
3. Exercise bypass automation, multiple instances and filter replacements. Check
   levels and nonfinite output. The filter transition starts with fresh history.
4. Save a project, remove its original IRs, close/reopen, and transfer the project
   to another supported architecture. Check a prior release's state after upgrading.
5. Try incorrect rate/layout and malformed presets. Confirm the last valid state
   survives a rejected load. Record the host's actual error display.
6. Install over the previous version, then uninstall. Check that unrelated plug-ins,
   host projects and custom user presets survive. Follow the guide without assistance.
7. Measure callback-time distributions and allocations with short and long IRs.
   Record CPU, host buffer and workload. An average is not a hard-real-time bound.

Nonfinite samples, wrong tails, state corruption or policy-bypass requirements
block qualification. Log each failure and add a deterministic regression when
possible. The current automated tests cover allocation guards and finite numerical
cases; no measured DAW deadline envelope is advertised.

## Equalizer APO on a real device

CI runs the Equalizer APO export in Equalizer APO's own filter code
([Equalizer APO export tests](testing.md#equalizer-apo-export-tests)), but not
through the Windows audio service. To check that on a Windows computer with
Equalizer APO installed on a stereo device:

1. Export the first-use stereo example:
   `convolver-equalizer-apo examples/first-use/stereo/filter.cfg first-use`,
   copy the `first-use` folder into `C:\Program Files\EqualizerAPO\config`,
   and add `Include: first-use\convolver.txt` to `config.txt`.
2. Set the device to the filter's rate (48000 Hz) on its *Advanced* tab and turn
   on `EnableTrace`
   ([Equalizer APO troubleshooting](../use/equalizer-apo.md#troubleshooting)).
   Play any sound in an ordinary player. The log should show
   `outputChannelCount == 2` evaluated to true and list the stereo filter's
   `Convolution:` lines; the sound should change.
3. Set the device to 44100 Hz and play again: it should be silent, and the log
   should show `sampleRate == 48000` evaluated to false.
4. Leave the device at 44100 Hz and play a 44.1 kHz file through a player in
   WASAPI exclusive mode (for example foobar2000 with its WASAPI output): the
   sound is heard, although the configuration silences 44.1 kHz, which shows
   that exclusive mode bypasses Equalizer APO.
5. Remove the `Include:` line, set `EnableTrace` back to `false` and restore the
   device's rate.

Record the result under HOST-4 in [TODO](../../TODO.md).
