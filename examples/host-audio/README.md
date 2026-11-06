<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Audio for listening checks in a host

Use [transients-48000-stereo.wav](transients-48000-stereo.wav) as the source
track for the [REAPER smoke test](../../docs/develop/host-testing.md#reaper-vst3),
or for listening checks in another host. It is a 20-second, 48 kHz stereo
16-bit PCM WAV, peaking at -18.06 dBFS. Start with playback volume low.

Import it onto a stereo track at time zero in a 48 kHz project. This is
**input audio**, not an impulse response to load into Convolver. It suits
part A of the REAPER session walkthrough and the settings checks in part C;
keep using the supplied first-use inputs and expected outputs for parts B
and D's numerical comparisons.

| Time | What you hear |
| --- | --- |
| 0–1 s | Silence |
| 1, 2, 3 s | A short click on the left, then right, then both channels |
| 4, 5, 6 s | The same left, right, both pattern again |
| 8–15 s | Sustained harmonic tones: 220 Hz fundamental left, 330 Hz right, with gentle fades |
| 16, 17, 18 s | One click per second on both channels |
| After 18.04 s | Silence through the end at 20 s, leaving room to hear the repeat |

With the identity preset, the sound should stay unchanged. With
`repeat-48000-stereo`, each click should have a quieter repeat 250 ms later,
on the same side. With `echo-48000-stereo`, the 5 ms delay changes the tone
and click colour rather than making a clearly separate repeat. Use the
sustained section to hear Gain, Mix and bypass changes; loop that section
if you need more time. The silence at either end makes looping the whole
file clean, but a settings change during silence cannot be judged by ear.

For a latency check, compare the rendered click onsets against the source
waveform. Listening alone cannot reliably establish a 256-frame offset.

## Regeneration and provenance

The audio is entirely synthetic, with no recorded or third-party material,
and uses the repository's GPL-2.0-or-later licence. The generator uses only
Python's standard library. From the repository root:

```sh
python3 examples/host-audio/generate.py
```

This replaces only the WAV beside the generator. It writes PCM samples with
no timestamps or personal metadata. The checked-in WAV is the reference;
regeneration on a different platform may differ by a quantization step due
to its mathematical library. This listening aid is not a numerical test
oracle and is not automatically included in preview packages.
