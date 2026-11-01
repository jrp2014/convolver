<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Your first sound in a host

For anyone who has [installed](install.md) the Convolver plug-in and wants to
hear it work. You will load the example presets from a preview package, check
what you hear, then make a preset from your own filter.

**In short:** load Convolver on a mono or stereo track, load the matching
`identity` preset (you should hear no change), then the `echo` preset (you
should hear it quieter, with a hollow tone), then the `repeat` preset (you
should hear each sound again a quarter of a second later). Save and reopen the
project to check that the filter is kept.

You need a plug-in host that loads VST3: a DAW such as REAPER, an audio
editor such as Audacity (version 4 or later), or a plug-in rack such as Carla.
GarageBand and Logic load only Audio Units, which preview packages do not
contain yet ([install](install.md)).
Installing a plug-in does not add filtering to an ordinary music player or to
everything the computer plays. For that on Windows, see the
[Equalizer APO export](equalizer-apo.md).

## Load the examples

The package's `presets/` folder has 18 examples: `identity`, `echo` and
`repeat`, in mono and stereo, at 44100, 48000 and 96000 Hz. Each folder is named
`KIND-RATE-LAYOUT` and holds `filter.vstpreset`, the `filter.cfg` it was made
from, and `impulse.wav`.

1. Set the host to 44100, 48000 or 96000 Hz, and select a mono or stereo track.
2. Add Convolver to the track as an audio effect. It has no window of its own;
   the host shows a generic panel with a bypass switch.
3. From the host's preset menu ("Load Preset…" or its preset browser), load
   `presets/identity-RATE-LAYOUT/filter.vstpreset` for your rate and layout.
   Audacity's preset menu cannot open a `.vstpreset` file, and Audacity runs
   the plug-in in stereo even on a mono track;
   [plug-in presets](presets.md#load-it-in-a-host) says where to put the file.
4. Play quiet audio and watch the meters. Identity leaves the sound unchanged,
   apart from a delay of 256 samples that most hosts make up for.
5. Load the matching `echo` preset. Its filter is half the input at once plus a
   quarter of it 257 samples (about 5 ms) later: too soon to hear as a
   separate repeat, so it sounds about 5 dB quieter and hollow, like a comb
   filter. Convolver does not lower the level for you,
   so leave headroom with your own filters.
6. Load the matching `repeat` preset: the same two taps, but the second comes
   a quarter of a second later, so you hear each sound again, quieter.
7. Switch the host's bypass on and off. Save the project, close it and reopen
   it: the filter is back. Presets carry the filter inside them, so the
   original WAV and `.cfg` files are not needed.

If the plug-in plays silence, the preset does not match the track's sample
rate or channel counts; see [troubleshooting](troubleshooting.md#a-plug-in-plays-silence).

## Make a preset from your own filter

From the extracted package folder (the output must not exist yet):

<!-- executable-example -->

```sh
./bin/convolver-preset presets/echo-48000-stereo/impulse.wav room.vstpreset
```

In PowerShell use `./bin/convolver-preset.exe` with the same two arguments.
Use your own IR, `.cfg` or configuration list in place of the example WAV, at
the sample rate your host runs at. [Plug-in presets](presets.md) explains
choosing an entry from a list, and the Audio Unit and LV2 preset formats.

## What to expect

- The filter's sample rate must match the host's, and its input and output
  channel counts must match the track's. Each side can have 1 to 8 channels.
- Processing is single precision, including in hosts that use 64-bit buffers.
- After the input stops, the filter's tail still plays out.
- Loading another preset while audio plays fades over to it. The new filter
  starts fresh: it cannot filter audio that played before it was loaded.

[How the plug-ins behave](plugins.md) has the details. To check a real
filter's output exactly, render it in the host and compare the result by
command: [check a stereo filter in a host](check-a-stereo-filter.md).
