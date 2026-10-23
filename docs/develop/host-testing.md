<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Host testing

[Documentation index](../README.md) · [Testing](testing.md) · [Install](../use/install.md)

For anyone testing Convolver's plug-ins or exports in a real host: what to
record, the smoke test for a preview, and the full qualification for a
production release. Automated tests are in [testing](testing.md).

**In short:** no named DAW is qualified yet. REAPER and Carla are the first
hosts to test. An unsigned preview needs the smoke test; a signed production
release needs the full qualification. Every report records the exact package,
host, system and settings.

No named DAW is currently qualified for portable Convolver. Automated SDK
validation, standalone adapter tests and manual DAW tests are different evidence.
REAPER and Carla are the suggested first hosts (see below). Ableton Live and
Cubase need available licences/testers; no compatibility claim is inferred.

There are two levels of testing. An **unsigned preview** needs the smoke test
below. A **signed production release** needs the full qualification procedure.
Both gates are set out in the
[plug-in formats plan](../plans/2026-10-21-plugin-formats.md#unsigned-previews-and-production).

## Suggested first hosts

Both are free to try and run on Windows, macOS and Linux, so one person can
cover all three platforms without buying a licence.

- **[REAPER](https://www.reaper.fm/download.php)** (Cockos): a full DAW with a
  free, fully functional evaluation period. It loads VST3, LV2 and (on macOS)
  AU, so it can test each Convolver format as it arrives. It saves projects
  with plug-in state, renders offline and shows CPU use. It is the closest of
  the free options to how most users will run Convolver.
- **[Carla](https://kx.studio/Applications:Carla)** (falkTX, free and open
  source): a plug-in host and rack rather than a DAW. Version 2.5 or later loads
  VST3, LV2 and (on macOS) AU, so the same session can compare the formats. It
  runs on JACK or the platform's own audio system and saves its rack,
  including plug-in state, as a project file.

Use the [smoke test](#smoke-test-for-an-unsigned-preview) below in each, and
record results under the TODO item HOST-1.

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
Keep private audio out of reports. Use the supplied synthetic examples.

## Smoke test for an unsigned preview

1. Download the preview the way a user would and check its SHA-256 against the
   release notes. Install it as the [install guide](../use/install.md) says, including
   its quarantine removal on macOS for this bundle only. Never disable security
   system-wide.
2. Scan, insert Convolver, and load one example preset at 48 kHz in mono and
   stereo. Check that the identity and echo examples sound as expected.
3. Save the project, close and reopen it, and check that the filter is restored.
4. Try a preset with the wrong rate or channel count while a working filter is
   running. The working filter must keep running, and the host should show an
   error. Record what the host displays.

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
