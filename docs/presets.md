<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in presets

[Documentation index](README.md) · [VST3 guide](vst3.md) ·
[Plug-in identities](plugin-identities.md) · [Configuration format](configuration.md)

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
- `convolver-vst3-preset` still works exactly as before, for existing scripts.

Contents: [make a preset](#make-a-preset) · [convert a VST3 preset](#convert-a-vst3-preset) ·
[load it in a host](#load-it-in-a-host) · [when a preset does not fit](#when-a-preset-does-not-fit) ·
[messages](#messages) · [what the files contain](#what-the-files-contain) ·
[compatibility](#compatibility)

The AU and LV2 plug-ins are still being written (TODO AU-1, AU-2, LV2-1 and
LV2-2). The tool already writes their presets, in the form those plug-ins will
read, so the same commands work when they arrive.

## Make a preset

`convolver-preset` is built with the [CLI](portable-core.md) (`build/convolver-preset`,
or `build/Release/` with Visual Studio). It needs no plug-in SDK.

```sh
convolver-preset room.cfg room.vstpreset
convolver-preset room.cfg room.vstpreset room.aupreset room.preset.lv2
convolver-preset --name "Living room" impulse.wav living.aupreset
```

- **Input:** anything the CLI's `--filter` accepts: an IR file used directly (as
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
  on the plug-in, as in the [VST3 guide](vst3.md#loading-filters). Hosts that
  keep a preset library also find presets in their VST3 preset folder, for
  example `~/Library/Audio/Presets/Convolver/Convolver/` on macOS.
- **AU (planned, AU-2):** load the `.aupreset` from the host's preset menu.
  Logic and GarageBand list user presets saved under
  `~/Library/Audio/Presets/` in a folder for the plug-in's manufacturer and name.
- **LV2 (planned, LV2-2):** copy the whole `room.preset.lv2` folder into a folder
  LV2 hosts scan, such as `~/.lv2/` on Linux, then choose the preset by its name
  in the host's preset list for that plug-in. The folder's name only has to end
  in `.lv2`. LV2 plug-ins have fixed channel counts, so the preset belongs to the
  plug-in whose counts match the filter; the tool prints that plug-in's URI.

## When a preset does not fit

A preset holds one filter at one sample rate, with fixed input and output
channel counts. The plug-ins check it against the host when they load it
([shared adapter contract](plans/2026-10-21-plugin-formats.md#shared-adapter-contract)):

- **The first filter cannot be used** (wrong sample rate or channel counts, or a
  damaged file): the plug-in plays **silence** and reports an error. It never
  falls back to passing input through unfiltered, because a room correction or
  crossover that quietly plays unfiltered audio can be worse than an obvious
  failure: a tweeter could receive full-range audio.
- **A replacement cannot be used** while a filter is already working: the
  working filter keeps playing, and the error is reported.
- **No preset loaded:** with equal input and output counts the plug-in passes
  input *c* to output *c*; with unequal counts it plays silence.

The [VST3 guide](vst3.md#loading-filters) gives the details for VST3, which
follows these rules since VST3-7.

Presets record channel *counts*, not speaker positions, so a 2-input, 6-output
preset loads on any host arrangement with 2 inputs and 6 outputs. See
[presets without a layout](plugin-identities.md#presets-without-a-layout).

## Messages

| Message | What to do |
| --- | --- |
| `choose the preset format by the name's ending` | End the output name in `.vstpreset`, `.aupreset` or `.preset.lv2` |
| `… already exists` | Choose a new name, or remove the old preset |
| `the LV2 plug-in comes in fixed channel layouts …` | LV2 publishes only the listed input→output counts. Use the VST3 or AU plug-in, or ask for the layout to be added ([identities](plugin-identities.md#channel-layouts)) |
| `… presets support 1 to 8 input and output channels` | The plug-ins have at most 8 channels on each side; use the CLI for wider filters |
| `this VST3 preset belongs to another plug-in` | Only Convolver's own presets can be converted |
| `a .vstpreset already holds one filter` | Leave out `--sample-rate`, `--input-channels` and `--output-channels` |
| `the preset name … contains control characters`, `… is not valid Unicode text` | AU and LV2 presets store a name, by default the first AU or LV2 file name. Give a plain one with `--name` |
| `NAME: cannot create output file …` | The folder for that output must exist and be writable. Nothing from the run is kept, so fix it and run the same command again |

Problems with the filter itself (a missing IR, a list with no matching entry)
give the same explanations as the CLI; see
[configuration troubleshooting](configuration.md).

## What the files contain

Every preset holds the same **encoded host state**: the magic `CNVS`, state
version 1, then the sample rate, channel counts, output channel mask, delays,
and every filter path's coefficients, routes and gains, as the plug-ins save it
in a host's project. The containers around it are fixed, so presets made today
keep loading. These names are permanent, like the
[plug-in identities](plugin-identities.md):

- **VST3 (`.vstpreset`):** Steinberg's documented preset file: the header
  `VST3`, format version 1, the processor class ID, then one component-state
  chunk (`Comp`) holding a 32-bit little-endian length and the encoded state.
  The tool's writer needs no SDK; a test checks that its output is
  byte-identical to the SDK's `PresetFile::savePreset`.
- **AU (`.aupreset`):** an XML property list, the dictionary that Audio Units
  save as `ClassInfo`. It has Apple's standard keys (`version` 0, `type`,
  `subtype` and `manufacturer` as the numbers of `aufx`, `Conv` and `CONV`,
  `name`) and an empty `data`, since the AU has no parameters, so the
  AudioUnitSDK's own checks accept it. The encoded state is under the key
  `convolver-state`.
- **LV2 (`NAME.preset.lv2/`):** a preset bundle with `manifest.ttl` and
  `preset.ttl`. The preset is a `pset:Preset` that applies to the plug-in URI
  for the filter's channel counts, with one state property, the LV2 base URI
  followed by `state`, holding the encoded state as an `atom:Chunk` (base64 in
  Turtle). No file path is stored, so the preset does not depend on where the
  filter files were.

The AU key, the LV2 state property and the bare state inside them (without the
VST3 length prefix) are the owner's decision of 22 October 2026.

## Compatibility

- Presets hold host state version 1, which records channel counts and an
  advisory output channel mask but no layout. Plug-ins must load every
  version-1 preset whose rate and counts fit; a stricter layout check never
  rejects one ([old presets](plugin-identities.md#presets-without-a-layout)).
- Host state version 2 only marks a plug-in saved after its first filter failed
  to load; it holds no filter. The tool never writes it and refuses to convert a
  `.vstpreset` that holds it.
- If a later version records more (for example an input layout), the tool will
  keep writing version 1 for filters that do not need it, so older plug-in
  builds can still read those presets.
- `convolver-vst3-preset` is the same tool under its old name, limited to its
  old behaviour: the same arguments, messages and output bytes, and only
  `.vstpreset` output.
