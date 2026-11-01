<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Using Convolver

For anyone who wants to apply a filter to audio with Convolver. This page
helps you choose how to run it and points to the page for each step.

**In short:**

- Convolver applies filters you supply: room correction, crossovers, channel
  routing. It does not measure a room or design filters.
- There are three ways to run it: the **command-line tool** filters files and
  streams, the **plug-ins** filter audio inside a plug-in host such as a
  digital audio workstation (DAW), and an **Equalizer APO export** filters
  everything a Windows computer plays.
- A filter is an impulse response (IR) sound file, or a `.cfg` configuration
  file that routes channels through IRs. The same files work in all three.
- The filter's sample rate and channel counts must match the audio. Convolver
  never resamples.

**Status:** Convolver is a developer preview. Preview packages are not signed,
and the plug-ins have not yet been tested in named DAWs. The
[status page](../project/status.md) says what is ready.

## Choose how to run it

| I want to… | Start with | Then |
| --- | --- | --- |
| Filter WAV files, or a stream through a pipe | [Your first filter](first-filter.md) | [Command-line reference](command-line.md) |
| Filter audio in a DAW or other plug-in host | [Install](install.md), then [your first sound in a host](first-sound-in-a-host.md) | [Check a stereo filter in a host](check-a-stereo-filter.md), [plug-in presets](presets.md), [how the plug-ins behave](plugins.md) |
| Filter everything Windows plays | [Equalizer APO export](equalizer-apo.md) | |
| Use a filter made for Roon, JRiver, Audiolense or the old Windows Convolver | [Other programs](other-programs.md) | [Configuration files](configuration.md) |

## All pages for users

| Page | Kind | What it covers |
| --- | --- | --- |
| [Concepts](concepts.md) | Background | Audio terms, latency, tails, files versus streams |
| [Your first filter](first-filter.md) | Tutorial | The bundled example filters with the command-line tool |
| [Your first sound in a host](first-sound-in-a-host.md) | Tutorial | The example presets in a plug-in host |
| [Check a stereo filter in a host](check-a-stereo-filter.md) | Tutorial | From download to a host render checked against the expected output |
| [Install](install.md) | How-to | A preview package or your own build, for every plug-in format and system |
| [Plug-in presets](presets.md) | How-to | Turning a filter into a preset for a plug-in |
| [Equalizer APO export](equalizer-apo.md) | How-to | System-wide filtering on Windows |
| [Other programs](other-programs.md) | How-to | Configurations shared with Roon, JRiver, Audiolense and the old Windows Convolver |
| [Troubleshooting](troubleshooting.md) | How-to | Symptoms, causes and fixes |
| [Command-line reference](command-line.md) | Reference | Every option of `convolver` |
| [Configuration files](configuration.md) | Reference | The `.cfg` and list formats |
| [How the plug-ins behave](plugins.md) | Reference | Channels, latency, bypass, loading and failures, for VST3, Audio Unit and LV2 |
| [Limits](limits.md) | Reference | What Convolver does not do |

To build Convolver yourself, see [build from source](../develop/build.md).

## Tools in a preview package

A preview package holds the VST3 plug-in, three tools in `bin/`, example presets
in `presets/`, the [first-use examples](first-filter.md#2-filter-the-examples)
with presets made from them in `examples/first-use/`, these pages in
`docs/use/` and licence notices in `licenses/`.
Check the package's SHA-256 checksum against the one published with it before
installing ([install](install.md)).

Run the tools from the extracted package folder. On Windows add `.exe`
(`./bin/convolver-preset.exe` in PowerShell). Outputs must be new paths: the
tools never replace an existing file or folder. All three print `--help`.

### convolver

Filters files and streams, and compares a render with the output it should
give ([command-line reference](command-line.md)):

<!-- executable-example -->

```sh
./bin/convolver --filter examples/first-use/mono/impulse.wav --input examples/first-use/mono/input.wav --output mono.wav
```

### convolver-preset

Makes a plug-in preset from an IR, a `.cfg` or a configuration list
([plug-in presets](presets.md)):

<!-- executable-example -->

```sh
./bin/convolver-preset presets/echo-48000-stereo/filter.cfg echo.vstpreset
```

### convolver-equalizer-apo

Exports the same inputs for Equalizer APO on Windows. It runs on every system,
so you can make the export on a Mac and copy the folder to Windows
([Equalizer APO export](equalizer-apo.md)):

<!-- executable-example -->

```sh
./bin/convolver-equalizer-apo presets/echo-48000-stereo/filter.cfg room
```
