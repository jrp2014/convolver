<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in presets

[Using Convolver](README.md) · [How the plug-ins behave](plugins.md) ·
[Configuration files](configuration.md)

For anyone loading their own filter into a Convolver plug-in. You will make a
preset from an IR or `.cfg` with `convolver-preset` and load it in your host.

**In short:**

- The plug-ins have no editor. You choose a filter by loading a **preset**, and
  `convolver-preset` makes one from an impulse response (IR), a
  [`.cfg`](configuration.md) or a configuration list.
- The name you give the new file picks the format: `room.vstpreset` (VST3),
  `room.aupreset` (Audio Unit) or `room.preset.lv2` (an LV2 preset folder).
  Name several to make them all at once:
  `convolver-preset room.cfg room.vstpreset room.aupreset room.preset.lv2`.
- The filter itself is copied into the preset, so the preset keeps working when
  the original WAV and `.cfg` files move or are deleted. All three formats carry
  the same bytes, so the filter sounds the same in every format.
- A preset must match the host's sample rate and channel counts. A plug-in that
  cannot use a preset as its first filter plays **silence** and reports an error,
  rather than passing unfiltered audio to your speakers.
- `convolver-vst3-preset`, the tool's older name, still works in source
  builds for existing scripts. Preview packages contain only `convolver-preset`.

Contents: [make a preset](#make-a-preset) · [convert a VST3 preset](#convert-a-vst3-preset) ·
[load it in a host](#load-it-in-a-host) · [when a preset does not fit](#when-a-preset-does-not-fit) ·
[messages](#messages) · [old and new versions](#old-and-new-versions)

## Make a preset

Preview packages have `convolver-preset` in `bin/`. A source build makes it
beside `convolver` (`build/convolver-preset`, or in `build/Release/` with Visual
Studio).

```sh
convolver-preset room.cfg room.vstpreset
convolver-preset room.cfg room.vstpreset room.aupreset room.preset.lv2
convolver-preset --name "Living room" impulse.wav living.aupreset
```

- **Input:** anything `convolver --filter` accepts: an IR file used directly (as
  many outputs as inputs, input *c* to output *c*), a `.cfg`, or a configuration
  list. Relative paths in a `.cfg` are read from the `.cfg`'s folder, as always.
- **Lists:** `--sample-rate HZ`, `--input-channels N` and `--output-channels N`,
  before the file names, choose the first matching entry, as the CLI does.
  Without them the first entry is used. Choose the entry that matches your
  host's sample rate and channel counts.
- **Name:** `--name` sets the name hosts show for AU and LV2 presets. Without it,
  the file name of the first AU or LV2 output is used (`room.preset.lv2` gives `room`). A
  `.vstpreset` stores no name; hosts show its file name.
- **New files only:** every output must be a new path. If any output cannot be
  written (it exists, its format is unknown, or the filter does not fit that
  format), the tool writes nothing and explains why. Remove or rename the old
  file first.

Routing works as in any `.cfg`. For example, one input forked to two outputs
(a mono IR feeding both speakers) is a 1-input, 2-output filter:

```text
48000 1 2 0
0
0 0
mono-ir.wav
0
0
0 1
```

It makes a VST3 or AU preset for a plug-in with one input and two outputs, and
an LV2 preset for the `discrete-1-2` LV2 plug-in.

## Convert a VST3 preset

Give an existing `.vstpreset` as the input to make the other formats from it,
even after its original files are gone:

```sh
convolver-preset room.vstpreset room.aupreset room.preset.lv2
```

The filter is copied byte for byte. The preset must be one of Convolver's: a
preset for another plug-in is refused. Presets saved by a host (which add the
host's own data) are accepted. The list options do not apply, since the preset
already holds one filter.

## Load it in a host

- **VST3:** use the host's preset menu ("Load Preset…", or the preset browser)
  on the plug-in. Hosts that
  keep a preset library also find presets in their VST3 preset folder, for
  example `~/Library/Audio/Presets/Convolver/Convolver/` on macOS.
- **AU:** load the `.aupreset` from the host's preset menu. Logic and
  GarageBand list user presets saved under
  `~/Library/Audio/Presets/Convolver/Convolver/`.
- **LV2:** copy the whole `room.preset.lv2` folder into a folder
  LV2 hosts scan, such as `~/.lv2/` on Linux, then choose the preset by its name
  in the host's preset list for that plug-in. The folder's name only has to end
  in `.lv2`. LV2 plug-ins have fixed channel counts, so the preset belongs to the
  plug-in whose counts match the filter; the tool prints that plug-in's URI.
  The LV2 plug-in has no file chooser yet, so a preset (or a saved session) is
  the only way to give it a filter.

The preset holds the whole filter, and the host saves the same data in your
project, so the project keeps working after the original WAV and `.cfg` files
are moved or deleted.

## When a preset does not fit

A preset holds one filter at one sample rate, with fixed input and output
channel counts, and it must match the host's. If the **first** filter of a
plug-in does not fit, the plug-in plays **silence** rather than unfiltered
audio. If a **new** filter does not fit while another is playing, the working
filter keeps playing. [How the plug-ins behave](plugins.md#when-a-filter-does-not-fit)
gives every case and how each format reports it.

Presets record channel *counts*, not speaker positions, so a 2-input, 6-output
preset loads on any host arrangement with 2 inputs and 6 outputs.

## Messages

| Message | What to do |
| --- | --- |
| `choose the preset format by the name's ending` | End the output name in `.vstpreset`, `.aupreset` or `.preset.lv2` |
| `… already exists` | Choose a new name, or remove the old preset |
| `the LV2 plug-in comes in fixed channel layouts …` | LV2 has only the [listed layouts](plugins.md#lv2-channel-layouts). Use the VST3 or AU plug-in, or ask for the layout to be added |
| `… presets support 1 to 8 input and output channels` | The plug-ins have at most 8 channels on each side; use the CLI for wider filters |
| `this VST3 preset belongs to another plug-in` | Only Convolver's own presets can be converted |
| `a .vstpreset already holds one filter` | Leave out `--sample-rate`, `--input-channels` and `--output-channels` |
| `the preset name … contains control characters`, `… is not valid Unicode text` | AU and LV2 presets store a name, by default the first AU or LV2 file name. Give a plain one with `--name` |
| `NAME: cannot create output file …` | The folder for that output must exist and be writable. Nothing from the run is kept, so fix it and run the same command again |

Problems with the filter itself (a missing IR, a list with no matching entry)
give the same explanations as `convolver`; see
[troubleshooting](troubleshooting.md#filters-and-configurations).

## Old and new versions

- Presets made today keep loading in later versions of every plug-in, on any
  arrangement with matching sample rate and channel counts. A recorded speaker
  layout only ever gives a note; it never makes a preset fail.
- A preset made by a newer version that records something an older plug-in
  cannot read is refused by that older plug-in, which keeps its working filter.
- `convolver-vst3-preset`, the older name, takes the same arguments and writes
  the same bytes as before, and makes only `.vstpreset` files.

What a preset file contains is described for developers in the
[plug-in adapter contract](../develop/plugin-contract.md#preset-files).
