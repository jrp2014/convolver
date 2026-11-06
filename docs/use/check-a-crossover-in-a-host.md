<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Check a crossover in a host

For anyone who has downloaded a preview package and wants to see, from start to
finish, that the Convolver plug-in can split stereo into six outputs in their
host, as a three-way crossover does. You will render the 2→6 crossover example
through the plug-in, check the result with a command, save and reopen the
project, and see what happens when a preset does not fit.

**In short:** install the VST® 3 plug-in as for the
[stereo check](check-a-stereo-filter.md), put Convolver on a six-channel track
at 48000 Hz with 2 inputs and 6 outputs, load
`examples/first-use/crossover/filter.vstpreset`, render `input.wav` through it
as a six-channel file, and run
`./bin/convolver --compare host-crossover.wav examples/first-use/crossover/expected.wav`.
It should print `Match`. The filter must survive saving and reopening, and a
preset for another rate or channel count must leave it playing.

**Status:** Convolver is a developer preview, and these steps have not yet
been followed in a host from start to finish. How a host gives an effect more
outputs than inputs differs most between hosts, so the routing steps below
are the most likely to differ from what you see. In the one REAPER session so
far (REAPER 7.85 on macOS) they did not work: REAPER gave the
VST3 plug-in 6 inputs and 6 outputs, so the crossover preset was refused
([details](../develop/host-testing.md#unresolved-host-findings)). The
[status page](../project/status.md) says what is ready.

The steps name REAPER, whose menus they were written for.
Hosts that run effects only in mono or stereo, such as GarageBand and
Audacity, cannot run this example. Commands are for macOS and Linux, run in
Terminal from the extracted package folder; on Windows, use PowerShell and
`.\bin\convolver.exe`.

## 1. Check, extract and install the package

Follow steps 1 and 2 of
[check a stereo filter in a host](check-a-stereo-filter.md#1-check-and-extract-the-package):
check the package's SHA-256 checksum, extract it, clear the macOS quarantine
for `bin` and install `Convolver.vst3`, then rescan the host's plug-ins.

## 2. See the expected result with the command-line tool

Render the example with `convolver` itself and compare it with the expected
output:

<!-- executable-example -->
```sh
./bin/convolver --filter examples/first-use/crossover/filter.cfg --input examples/first-use/crossover/input.wav --output cli-crossover.wav
```

<!-- executable-example -->
```sh
./bin/convolver --compare cli-crossover.wav examples/first-use/crossover/expected.wav
```

The second command reports both files as `48000 Hz, 6 channels, 4249 frames`
and ends with `Match: no sample differs by more than the limit (0.0001 of the
expected peak, -80 dB, or two 16-bit steps).`

The filter takes left and right in and gives six outputs: 1 left low, 2 left
mid, 3 left high, 4 right low, 5 right mid and 6 right high. The high bands
are 6 dB lower and 0.5 ms later, so a swapped, missing or misrouted output
would not match. `examples/first-use/crossover/` holds the filter
(`filter.cfg` and `crossover.wav`), `input.wav`, `expected.wav` and
`filter.vstpreset`, a preset made from the filter. Its bands show routing;
they are not a crossover ready for real speakers.

## 3. Render the example in the host

1. Set the project and the audio device to 48000 Hz (REAPER: *File › Project
   settings*).
2. Insert a new track and give it six channels (REAPER: the track's
   **Route** button, then *Track channels: 6*). Insert
   `examples/first-use/crossover/input.wav` at the very start of the project,
   with no fade in or out (REAPER: *Item properties*, both fades 0).
3. Add Convolver to the track as an effect. On macOS its window opens and
   shows the status; on Windows and Linux the host shows its generic controls,
   with a read-only **Status**.
4. Load `examples/first-use/crossover/filter.vstpreset` from the host's
   preset menu (REAPER: the **+** button beside the preset menu, then the
   item that imports a VST preset file). Status reads
   `Filter loaded: 48000 Hz, 2 in, 6 out.`

   If Status instead reads `Silent: the filter has 2 in, 6 out, the plug-in
   6 in, 6 out. Details in the Convolver log.` (or other counts for the
   plug-in), the host has not given
   Convolver 2 inputs and 6 outputs. Check the track's channel count and the
   plug-in's routing (REAPER: the button at the top right of the effect
   window that shows the plug-in's inputs and outputs); the
   [Convolver log](plugins.md#status-and-the-convolver-log) records each
   layout the host offered.
5. Leave the track's volume, pan and the master at their defaults, and render
   the track offline to `host-crossover.wav` in the package folder: 48000 Hz,
   six channels, 32-bit float WAV, no normalising or dither, with a tail of at
   least 100 ms (REAPER: *File › Render*, *Source: Selected tracks (stems)*
   with the track selected, *Bounds: Entire project*, *Channels: 6*, tick
   *Tail*). Rendering the master instead keeps only the channels the master
   has.
6. Compare the render with the expected output:

   ```sh
   ./bin/convolver --compare host-crossover.wav examples/first-use/crossover/expected.wav
   ```

   It prints `Match`. If it prints `Different`, the line after it says why:
   another channel count means the render did not keep all six channels;
   output that is late means the host did not make up for the plug-in's
   256-frame delay; output that ends too soon needs a longer tail; another
   level means a volume, pan or gain setting changed it.

## 4. Save and reopen

Save the project, quit the host, open the project again and render a second
time to `host-crossover-reopened.wav`. Comparing it with `expected.wav` again
prints `Match`: the project kept the filter and its six outputs.

## 5. Load a preset that does not fit

1. Load `examples/first-use/stereo/filter.vstpreset`, a filter with 2 inputs
   and 2 outputs. The crossover keeps playing, and Status reads `Load failed,
   previous filter kept: the filter has 2 in, 2 out, the plug-in 2 in, 6 out.
   Details in the Convolver log.`
2. Load `presets/echo-44100-stereo/filter.vstpreset`, made for 44100 Hz. The
   crossover keeps playing, and Status reads `Load failed, previous filter
   kept: the filter is for 44100 Hz, the host runs at 48000 Hz. Details in the
   Convolver log.`
3. Render again and compare: still `Match`. To recover from a preset that
   does not fit, load the crossover preset from step 3 again, or a preset made
   for your host's rate and the plug-in's 2 inputs and 6 outputs.

## With the Audio Unit (macOS)

REAPER also loads Audio Units. Install `Components/Convolver.component` as
[install](install.md) says, insert Convolver's Audio Unit in step 3 in place
of the VST3 plug-in, and load the filter in its window: **Choose…** beside
Filter, then `examples/first-use/crossover/filter.cfg`. The window reads
Playing with 48000 Hz, 2 in, 6 out, and the render and comparison are as
above. Its preset menu takes `.aupreset` files rather than `.vstpreset`
([plug-in presets](presets.md)).

## If something went wrong

- The host does not list Convolver, or the plug-in plays silence:
  [troubleshooting](troubleshooting.md).
- `--compare` refuses a file at another rate or channel count: render at
  48000 Hz with six channels. Its options are in the
  [command-line reference](command-line.md#comparing-two-files).
- How the plug-in takes its channels: [channels](plugins.md#channels).

<!-- VST trademark notice: written by scripts/trademarks.py from packaging/trademarks/vst.json -->

<img src="images/VST_Compatible_Logo_Steinberg.png" alt="VST Compatible logo" width="120">

Convolver supports VST® 3. VST is a registered trademark of Steinberg Media Technologies GmbH.

<!-- End of the VST trademark notice -->
