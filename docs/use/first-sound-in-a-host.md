<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Your first sound in a host

For anyone who has [installed](install.md) the Convolver plug-in and wants to
hear it work. You will choose an example filter in Convolver's window, check
that it fits the track, play, and save the project.

**In short:** on macOS, on a stereo track at 48000 Hz, add Convolver, click
**Choose…** in its window and pick `presets/identity-48000-stereo/filter.cfg`
from the extracted package. The window shows 48000 Hz and `2 in → 2 out`, and
you hear no change. Choose `echo-48000-stereo/filter.cfg` (quieter and
hollow) and `repeat-48000-stereo/filter.cfg` (each sound again a quarter of a
second later), then save, close and reopen the project: the filter is still
there. Your own IR file, `.cfg` or configuration list loads the same way,
from any folder. Presets are an [optional route](#optional-load-a-preset),
and the checked one [in Audacity](#in-audacity).

You need a plug-in host that loads VST® 3, such as REAPER or Audacity
(version 4 or later), or one that loads Audio Units, such as GarageBand or
Logic. On macOS both formats have the same window, and the VST3 plug-in on
Windows has it too, so the steps are the same. On Linux the VST3 plug-in
has no window yet, nor on Windows up to 0.13.2: use a
[preset](#optional-load-a-preset) instead. On Linux the LV2 plug-in also
works, in hosts such as Ardour: add the Convolver layout that matches the
track (2→2 for stereo, [LV2 layouts](plugins.md#lv2-channel-layouts)) and
pick the same `filter.cfg` with the file chooser of its **Filter**
parameter ([LV2 filter files](plugins.md#lv2-filter-files)); its
**Status** shows Active once the filter plays. Installing a plug-in does not
filter an ordinary music player or everything the computer plays; for that
on Windows, see the [Equalizer APO export](equalizer-apo.md).

**Before you start:**

- The filter's sample rate must match the host's: Convolver does not
  resample. Audacity plays effects at its Default sample rate, which can
  differ from the rate it exports at.
- **In Audacity**, Audacity's own preset menu is the checked way to load
  filters, as [In Audacity](#in-audacity) says; loading from Convolver's
  window should now work but has not been checked in Audacity yet.
- A file chosen in the window must have exactly as many inputs as the host
  gives the plug-in; a preset of the same filter only needs at least that
  many. Where a host gives more inputs, as REAPER does on a six-channel
  track, load the filter as a preset.
- Changing **Latency** during playback leaves a short gap and cuts off a
  tail that is still sounding; hosts differ in when they line the audio up
  again ([latency and tail](plugins.md#latency-and-tail)).
- A host's own switch that turns an effect off or offline can route audio
  around Convolver unfiltered, unlike Convolver's bypass. With a crossover
  that can send full-range audio to a tweeter ([bypass](plugins.md#bypass)).
- The project saves a copy of the filter, not a link to its file: after
  reopening, **Reload** stays dimmed until you choose the file again.

## 1. Choose a filter

The package's `presets/` folder holds 18 examples: `identity`, `echo` and
`repeat`, in mono and stereo, at 44100, 48000 and 96000 Hz. Each folder is
named `KIND-RATE-LAYOUT` (such as `echo-48000-stereo`) and holds the filter
as `filter.cfg` and `impulse.wav`, and the same filter as a preset.

1. Set the host to 44100, 48000 or 96000 Hz, and select a mono or stereo
   track.
2. Add Convolver to the track as an effect. Its window opens from the
   plug-in's slot: in GarageBand, from the plug-in's button in the track's
   Plug-ins area.
3. Click **Choose…** under the filter's name, or drop the file on the window,
   and pick `presets/identity-RATE-LAYOUT/filter.cfg` for your track's rate
   and layout.

**Choose…** reads an IR file (a WAV, AIFF, FLAC or other sound file), a
`.cfg` or a configuration list, from any folder: nothing needs installing or
converting first. It does not read preset files; those go through the
host's preset menu ([optional route](#optional-load-a-preset)). Your own
filter loads the same way; it must be made for the rate and channels your
host runs at. [Loading a filter](plugins.md#loading-a-filter) lists what
each plug-in reads.

## 2. Check the rate and channels

A filter plays only at its own sample rate and with its own channels.
Convolver never resamples or remixes.

Under the filter's name the window shows **Sample rate** and **Channels**
(`2 in → 2 out`), and beside it the status: **Active**, in green, when the
filter fits and plays. The host's Status parameter reads
`Filter loaded: 48000 Hz, 2 in, 2 out.` **Waiting** means the host has not
started audio yet: press play.

If the status reads **Load failed**, the message under the name says which
value differs and what to change: set the host to the filter's rate, use a
track with the filter's channels, or choose a filter made for the track. A
first filter that does not fit plays silence, never unfiltered audio. If
the message names a rate your host is not set to, the host had not started
audio when you chose the file: start playback, then choose it again.

## 3. Play

1. Play quiet audio and watch the meters. Identity leaves the sound
   unchanged, with no added delay at the default Latency of 0 frames.
2. Choose the matching `echo` folder's `filter.cfg`. Its filter is half the
   input at once plus a quarter of it 257 samples (about 5 ms) later: too
   soon to hear as a separate repeat, so it sounds about 5 dB quieter and
   hollow, like a comb filter. The change fades over while audio plays.
3. Choose the matching `repeat` folder's `filter.cfg`: the same two taps, but
   the second comes a quarter of a second later, so you hear each sound
   again, quieter.
4. Switch the host's bypass on and off to compare.

Convolver does not lower the level for you, so leave headroom with your own
filters, or turn down **Gain** in the window ([settings](plugins.md#settings)).

## 4. Save the project

Save the project, close it and reopen it. The filter is back, and the window
names it (`filter.cfg`). The project holds a copy of the whole filter, so it
keeps working after the WAV and `.cfg` files move or are deleted. It keeps
the file's name but not its folder.

**Check it worked:** the status reads Active and you hear the filter after
reopening, and an export or render of a few seconds sounds as the track did
while playing. If the plug-in plays silence or the host does not list it,
see [troubleshooting](troubleshooting.md#a-plug-in-plays-silence); to
report a problem, see [report a problem](report-a-problem.md).

## Optional: load a preset

A preset is the filter saved in the host's preset format, loaded from the
host's preset menu instead of the window. You need one where the plug-in has
no window (the VST3 plug-in on Linux, and on Windows up to 0.13.2), to keep filters in the
host's preset library, or to pass one self-contained file to someone else.

Each example folder holds a VST3 preset named after it
(`presets/echo-48000-stereo/echo-48000-stereo.vstpreset`). Load it from the
host's preset menu ("Load Preset…" or its preset browser; in REAPER, the
**+** button beside the preset menu, then the item that imports a VST preset
file). In Audacity, see [In Audacity](#in-audacity). Audio Unit hosts
such as GarageBand and Logic take `.aupreset` files instead; make one as
below if GarageBand's sandbox refuses a chosen file. Without the window,
carry on with steps 2 to 4 above, loading the matching `echo` and `repeat`
presets in place of choosing their `filter.cfg`.

To make a preset from your own filter, run from the extracted package folder
(the output must not exist yet):

<!-- executable-example -->

```sh
./bin/convolver-preset presets/echo-48000-stereo/impulse.wav room.vstpreset
```

In PowerShell use `./bin/convolver-preset.exe` with the same two arguments.
[Plug-in presets](presets.md) explains the Audio Unit and LV2 formats,
choosing an entry from a list, and the folders where hosts look for presets.

## In Audacity

Audacity's own preset menu is the route checked in Audacity 4. Up to
0.13.2, a filter loaded from Convolver's window (**Choose…**, **Recent**,
**Reload**, or reloading when the file changes) played while you listened,
but Audacity 4.0.2 did not keep it for an export or a saved project: they
kept whatever Audacity stored last, which may be no filter or an older one,
so a wrong export could still sound plausible. Later versions should keep
it, but this has been checked only by automated tests, not yet in Audacity.
If you load a filter from the window, export a few seconds and check that
the export has the filter you chose (for example by comparing it with
`convolver --compare`, as in [check a stereo filter in a host](check-a-stereo-filter.md)).

1. Set *Audio setup › Audio settings › Default sample rate*, and the
   project's rate, to the filter's rate. Audacity plays effects at the
   Default sample rate, which can differ from the rate it exports at. Use
   stereo filters: Audacity runs the plug-in in stereo even on a mono track.
2. Audacity's preset menu does not import `.vstpreset` files; it lists the
   ones in the VST3 folder for all users,
   `/Library/Audio/Presets/Convolver/Convolver/` on macOS
   ([preset folders](presets.md#preset-folders)). Copy the presets there
   (this needs an administrator), then restart Audacity, which reads the
   folder once per session. For your own filter, make a preset first, as
   in [optional: load a preset](#optional-load-a-preset), named after its
   rate (`room-48000.vstpreset`):

   ```sh
   sudo mkdir -p /Library/Audio/Presets/Convolver/Convolver
   sudo cp presets/*-stereo/*-stereo.vstpreset /Library/Audio/Presets/Convolver/Convolver/
   ```

3. Add Convolver as a real-time effect and choose the preset by its file
   name from Audacity's preset menu. To see Convolver's window and status,
   turn on *Preset options › Use vendor UI*.
4. Play, export a few seconds and listen to the export; save the project,
   reopen it and play again.

## What to expect

- Each side of a filter can have 1 to 8 channels.
- Processing is single precision, including in hosts that use 64-bit buffers.
- After the input stops, the filter's tail still plays out.
- A new filter chosen while audio plays fades over from the old one, and
  starts fresh: it cannot filter audio that played before it was loaded. A
  new filter that does not fit is refused, and the old one keeps playing.

[How the plug-ins behave](plugins.md) has the details. To check a real
filter's output exactly, render it in the host and compare the result by
command: [check a stereo filter in a host](check-a-stereo-filter.md).

<!-- VST trademark notice: written by scripts/trademarks.py from packaging/trademarks/vst.json -->

<img src="images/VST_Compatible_Logo_Steinberg.png" alt="VST Compatible logo" width="120">

Convolver supports VST® 3. VST is a registered trademark of Steinberg Media Technologies GmbH.

<!-- End of the VST trademark notice -->
