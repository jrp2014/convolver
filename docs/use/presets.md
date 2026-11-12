<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in presets

[Using Convolver](README.md) · [How the plug-ins behave](plugins.md) ·
[Configuration files](configuration.md)

For anyone who wants a preset of their own filter, for a plug-in without a
window or for a host's preset library. You will make a preset from an IR or
`.cfg` with `convolver-preset` and load it in your host.

**In short:**

- You do not need a preset to use a filter. The ordinary way is to choose
  the IR file, `.cfg` or configuration list itself, from any folder, with
  **Choose…** in [Convolver's window](plugins.md#convolvers-window) (the
  Audio Unit and the VST® 3 plug-in on macOS and Windows) or the host's file chooser
  (LV2, in hosts that show it), then save the project
  ([your first sound in a host](first-sound-in-a-host.md)).
- A **preset** is the same filter in the host's preset format, loaded from
  the host's preset menu. You need one where the plug-in has no window (the
  VST3 plug-in on Linux, and on Windows up to 0.13.2), in an LV2 host with no file chooser,
  or to keep filters in the host's preset library. `convolver-preset` makes
  one from an impulse response (IR), a [`.cfg`](configuration.md) or a
  configuration list.
- The name you give the new file picks the format: `room.vstpreset` (VST3),
  `room.aupreset` (Audio Unit) or `room.preset.lv2` (an LV2 preset folder).
  Name several to make them all at once:
  `convolver-preset room.cfg room.vstpreset room.aupreset room.preset.lv2`.
- The filter itself is copied into the preset, so the preset keeps working when
  the original WAV and `.cfg` files move or are deleted. All three formats carry
  the same bytes, so the filter sounds the same in every format.
- A preset must match the host's sample rate and output channels, and have no
  more inputs than the host gives (extra inputs are ignored,
  [channels](plugins.md#channels)). A plug-in that
  cannot use a preset as its first filter plays **silence** rather than passing
  unfiltered audio to your speakers; [how the plug-ins behave](plugins.md#when-a-filter-does-not-fit)
  says how each format reports it.
- `convolver-vst3-preset`, the tool's older name, still works in source
  builds for existing scripts. Preview packages contain only `convolver-preset`.

Contents: [make a preset](#make-a-preset) · [convert a VST3 preset](#convert-a-vst3-preset) ·
[load it in a host](#load-it-in-a-host) · [preset folders](#preset-folders) ·
[when a preset does not fit](#when-a-preset-does-not-fit) ·
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
  before or after the file names, choose the first matching entry, as the CLI
  does.
  Without them the first entry is used. Choose the entry that matches your
  host's sample rate and channel counts.
- **Name:** `--name` sets the name hosts show for AU and LV2 presets. Without it,
  the file name of the first AU or LV2 output is used (`room.preset.lv2` gives `room`). A
  `.vstpreset` stores no preset name; hosts show its file name.
- **Filter name:** a `.vstpreset` also keeps the name of the file you made it
  from, with its extension but not its folder (`Living room 48k.cfg`), as
  the plug-in's own saved state does for a file chosen in the window. A host
  that gives the plug-in this part of the preset, as Steinberg's own preset
  code does, then shows the name in the VST3 window after the preset is
  loaded and after the project is reopened, as REAPER does. Converting a `.vstpreset` keeps its name while the name still
  belongs to that filter. `--name` does not change it.
- **Help:** `convolver-preset --help` lists these options and the preset
  formats.
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

- **VST3:** load the `.vstpreset` from the host's preset menu ("Load
  Preset…", or the preset browser; in REAPER, the **+** button beside the
  preset menu, then the item that imports a VST preset file). Hosts that
  keep a preset library also list presets saved in their
  [preset folder](#preset-folders).
  **Audacity 4** imports only its own preset files, not `.vstpreset`.
  Instead, copy the presets to the VST3 folder for all users in
  [preset folders](#preset-folders) (this needs an administrator) and
  restart Audacity. They appear in the effect's preset menu under their
  file names, so give each preset its own name,
  such as `room-48000.vstpreset`. This preset menu is the route checked in
  Audacity. Up to 0.13.2 a filter loaded from Convolver's window (Choose…,
  Recent, Reload, or reloading when the file changes) played while you
  listened, but Audacity 4.0.2 did not keep it for exports or saved
  projects; later versions should keep it, checked so far only by
  automated tests ([in Audacity](first-sound-in-a-host.md#in-audacity)). Audacity always runs Convolver with two channels, even on a
  mono track, so use a stereo (2-in, 2-out) filter, and set its playback
  rate to the filter's
  ([the rate a host plays at](plugins.md#status-and-the-convolver-log)).
- **VST3 or AU, from Convolver's window:** choose the `.vstpreset` with
  **Choose…**, or drop it on the window, from any folder; no preset folder,
  import or administrator is needed, and the Audio Unit reads it too
  ([a preset in the window](plugins.md#a-preset-in-the-window)). Its Gain,
  Mix and Latency load with the filter, as from the host's preset menu.
  In Audacity 4 this avoids the preset folder, but it is not yet checked
  in Audacity, and the window route's caveat above applies.
- **AU:** load the `.aupreset` from the host's preset menu. Logic and
  GarageBand list the user presets in their [preset folder](#preset-folders).
- **LV2:** copy the whole `room.preset.lv2` folder into a
  [folder LV2 hosts scan](#preset-folders), then choose the preset by its
  name in the host's preset list for that plug-in. The folder's name only
  has to end in `.lv2`. LV2 plug-ins have fixed channel counts, so the
  preset belongs to the plug-in whose counts match the filter; the tool
  prints that plug-in's URI. Hosts that show LV2 file parameters, such as
  Ardour and Jalv, can also choose the IR file or `.cfg` directly
  ([LV2 filter files](plugins.md#lv2-filter-files)).

The preset holds the whole filter, and the host saves the same data in your
project, so the project keeps working after the original WAV and `.cfg` files
are moved or deleted.

**Check it worked:** the tool prints `Created self-contained … preset` and the
path of each file it wrote, and the host plays the filtered sound once the preset is loaded. If the
plug-in plays silence, see [when a preset does not fit](#when-a-preset-does-not-fit).

## Preset folders

A preset loads from any folder through the host's preset menu. These are
the folders where hosts look for presets on their own, to list them in a
preset library. None of them is needed for **Choose…**.

| Format | System | Your user account | All users (needs an administrator) |
| --- | --- | --- | --- |
| VST3 | macOS | `~/Library/Audio/Presets/Convolver/Convolver/` | `/Library/Audio/Presets/Convolver/Convolver/` |
| VST3 | Windows | `%USERPROFILE%\Documents\VST3 Presets\Convolver\Convolver\` | `C:\ProgramData\VST3 Presets\Convolver\Convolver\` |
| VST3 | Linux | `~/.vst3/presets/Convolver/Convolver/` | `/usr/local/share/vst3/presets/Convolver/Convolver/` or `/usr/share/vst3/presets/Convolver/Convolver/` |
| AU | macOS | `~/Library/Audio/Presets/Convolver/Convolver/` | `/Library/Audio/Presets/Convolver/Convolver/` |
| LV2 | Linux | `~/.lv2/` | `/usr/lib/lv2/` |

The VST3 folders follow Steinberg's
[preset locations](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Preset+Locations.html);
hosts differ in which of them they read (Audacity 4 reads only the folder
for all users).

## When a preset does not fit

A preset holds one filter at one sample rate, with fixed input and output
channel counts. Its rate and outputs must match the host's, and the host must
give at least its inputs: a filter reads the first ones and ignores the rest
([channels](plugins.md#channels)). If the **first** filter of a
plug-in does not fit, the plug-in plays **silence** rather than unfiltered
audio. If a **new** filter does not fit while another is playing, the working
filter keeps playing. [How the plug-ins behave](plugins.md#when-a-filter-does-not-fit)
gives every case and how each format reports it.

Presets record channel *counts*, not speaker positions, so a 2-input, 6-output
preset loads on any host arrangement with 2 inputs and 6 outputs.

## Messages

Each message gives the problem, then its cause and the fix on their own lines
([reading Convolver's messages](troubleshooting.md#reading-convolvers-messages)).
The problems, with what lies behind them:

| Problem | Behind it |
| --- | --- |
| `NAME: the preset format is not known from this name` | The format follows the name's ending: `.vstpreset`, `.aupreset` or `.preset.lv2` |
| `NAME already exists` | The tool never replaces a preset: choose a new name, or remove the old one |
| `no LV2 plug-in takes this filter's …` | LV2 has only the [listed layouts](plugins.md#lv2-channel-layouts). Use the VST3 or AU plug-in, or ask for the layout to be added |
| `the filter is too large to save …` | Projects and presets hold at most 64 MiB of Convolver's data, just under 16,777,216 samples counting every impulse response together ([size limit](plugins.md#when-a-filter-does-not-fit)). Shorten the impulse responses, or render with the CLI |
| `… presets support 1 to 8 input and output channels` | The plug-ins have at most 8 channels on each side; use the CLI for wider filters |
| `this VST3 preset belongs to another plug-in` | Only Convolver's own presets can be converted |
| `--sample-rate, --input-channels and --output-channels cannot choose from a .vstpreset` | A `.vstpreset` already holds one filter: leave out those options |
| `the preset name (by default the file name) contains control characters`, `… cannot be stored` | AU and LV2 presets store a name, by default the first AU or LV2 file name. Give a plain one with `--name` |
| `NAME: cannot create output file (must be a new path)` | The folder for that output must exist and be writable. Nothing from the run is kept, so fix it and run the same command again |

Problems with the filter itself (a missing IR, a list with no matching entry)
give the same explanations as `convolver`; see
[troubleshooting](troubleshooting.md#filters-and-configurations).

## Old and new versions

- Presets made today keep loading in later versions of every plug-in, on any
  arrangement with matching sample rate and channel counts. A recorded speaker
  layout only ever gives a note; it never makes a preset fail.
- A preset made by a newer version that records something an older plug-in
  cannot read is refused by that older plug-in: a filter already playing keeps
  playing, and otherwise the plug-in plays silence.
- `convolver-vst3-preset`, the older name, takes the same arguments and writes
  the same bytes as before, and makes only `.vstpreset` files. Its presets
  do not keep the filter's name.

What a preset file contains is described for developers in the
[plug-in adapter contract](../develop/plugin-contract.md#preset-files).

<!-- VST trademark notice: written by scripts/trademarks.py from packaging/trademarks/vst.json -->

<img src="images/VST_Compatible_Logo_Steinberg.png" alt="VST Compatible logo" width="120">

Convolver supports VST® 3. VST is a registered trademark of Steinberg Media Technologies GmbH.

<!-- End of the VST trademark notice -->
