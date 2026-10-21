<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Configuration files

[Documentation index](README.md) · [CLI reference](portable-core.md)

A configuration describes how to route and filter audio. It does not contain
the IR samples themselves: it names sound files holding them. Keep the
configuration and those files together when moving a setup to another computer.
This page describes the current portable parser, not the old Windows installer.

**In short:**

- For a simple per-channel filter, pass a multichannel IR sound file directly;
  use a `.cfg` file for routing, mixing, delays or sharing one mono IR.
- A `.cfg` file is a header line, a line of input delays, a line of output
  delays, then four lines per path: IR file, IR channel, input routes, output
  routes. Channels count from zero.
- A route number's integer part is the channel and its fractional part is the
  gain, so `1.25` is channel 1 at gain 0.25, and `-0` is channel 0 inverted.
- Paths are relative to the file that names them. Use relative paths so a setup
  can be copied to another computer; Windows drive paths fail on macOS/Linux.
- A list file names several configurations; the first that matches the input's
  sample rate and channel count is used. Check any file with `--validate` or
  `--inspect`.

## The simplest filter: a sound file

Pass an IR sound file directly to `--filter` to filter each input channel with
the matching IR channel. Input and IR must have the same sample rate and channel
count. Use a `.cfg` file for cross-channel routing, delays, or applying one mono
IR to multiple channels.

## A stereo example

Save the following as `room.cfg`, beside a two-channel `room.wav` at 48 kHz:

```text
// sample rate, input channels, output channels, hexadecimal speaker mask
48000 2 2 3
// input delays in milliseconds
0 0
// output delays in milliseconds
0 0
// path 1: IR file, IR channel, input routes, output routes
room.wav
0
0
0
// path 2
room.wav
1
1
1
```

This filters input 0 with IR channel 0 and sends it to output 0. The second path
does the same for channel 1. To use one **mono** IR on both channels, change the
second path's IR-channel line from `1` to `0`; keep its input and output lines `1`.

Check and render it with:

```sh
convolver --validate room.cfg
convolver --inspect room.cfg
convolver --filter room.cfg --input music.wav --output corrected.wav
```

These examples assume `convolver` is on PATH. From a local single-configuration
build, use `./build/convolver` instead.

## File layout

Ignore blank lines and text from `//` to the end of a line. The remaining lines are:

| Line | Contents |
| --- | --- |
| 1 | Sample rate in Hz, input channel count, output channel count, hexadecimal output speaker mask |
| 2 | One input delay in milliseconds per input channel |
| 3 | One output delay in milliseconds per output channel |
| Next four lines, repeated for each path | IR filename; zero-based IR channel; input routes; output routes |

There must be at least one path. The IR filename occupies the whole line, so
spaces in a name need no quotes. Quotes would become part of the name. `//`
starts a comment even on a filename line. Use a dot for decimal numbers.
UTF-8 text, a UTF-8 byte-order mark, and LF or CRLF line endings are accepted.

Delays must be nonnegative. Milliseconds are converted using the parser's
single-precision arithmetic, then truncated to whole frames, not rounded.
For example, at 48 kHz, `0.5` ms is 24 frames. Inspection shows both the original
value and the effective frame count. Different paths may use IRs of different
lengths, but each IR must have the configuration's sample rate.

The speaker mask is metadata, not a routing instruction. `0` means unspecified;
`3` labels front left/right and `3F` labels six WAVE channels. The CLI writes
those labels only with `--speaker-map config`, and never reorders samples to
match them. See [speaker labels](portable-core.md#speaker-labels).

## Route numbers combine a channel and a gain

Each input/output route line contains one or more space-separated numbers.
Channels start at zero. The absolute integer part selects the channel; the
fractional part supplies the gain, with the sign preserved. An integer has
unit gain with that sign.

| Token | Channel | Gain |
| --- | ---: | ---: |
| `0` | 0 | +1 |
| `1` | 1 | +1 |
| `0.5` | 0 | +0.5 |
| `1.25` | 1 | +0.25 |
| `-1.25` | 1 | -0.25 |
| `-0` | 0 | -1 |

In particular, `1.25` does **not** mean a gain of 1.25. Input routes are summed
before the path's filter; output routes multiply and add that filtered signal.
Repeated routes are legal and add again. Several paths may add to one output.
Preserve route order and signed zero when generating configurations.

## Paths and configuration lists

Relative IR filenames are resolved beside their `.cfg` file. List entries are
resolved beside the list containing them, not the working directory. `/`, `\`
and mixed separators work in relative references. Native absolute paths work
on their own platform; Windows drive/UNC paths are not mapped onto macOS/Linux.
Prefer relative paths and match the stored spelling and letter case exactly.

A list is a text file containing one configuration, sound-file or nested-list
filename per nonblank line:

```text
filters/room-44100.cfg
filters/room-48000.cfg
```

Rendering selects the first entry matching the input sample rate and channel
count. `--output-channels N` adds an output-count constraint. Inspection and the
VST3 preset tool accept explicit rate/input/output selectors; with none, they
select the first entry. Lists are searched in order, including nested lists.
A malformed visited entry can fail selection; a list is not a fallback that
silently skips broken configurations. Unselected configurations' IR samples are
not decoded. `--all-configurations` instead diagnoses errors and continues to
later entries.

## Troubleshooting and compatibility

| Symptom | Next step |
| --- | --- |
| Rate or channel mismatch | Read the error: it lists each candidate filter and what differs. Choose the matching configuration or generate the right IR; for a mono IR it suggests a configuration to save |
| Cannot find an IR | Check its name relative to the configuration, including case; use `--profile portable-paths` |
| A Windows drive path fails on Mac/Linux | Copy the IRs beside the configuration and replace the drive path with a relative path |
| Old configuration rejected for missing delays | Add explicit input and output delay lines using the counts in the header; review the rest before rendering |
| Output too loud | Check summed routes and use gain attenuation; integer output clips |
| Unsure which list entry was used | Run `--inspect` with the same rate/channel selectors and read the traversal report |

[Inspection options and JSON output](portable-core.md#inspecting-and-validating-configurations)
include syntax-only checks and warning policy. Resource limits are recorded in
[the core reference](portable-core.md#resource-limits).

The format is used by other audio programs, but their accepted subsets differ.
See the [compatibility matrix](configuration-compatibility.md) before assuming a
configuration is interchangeable. The original [4.4 specification](historical/convolver-4.4/config.html)
and [examples](historical/convolver-4.4/configegs.html) are historical references.
The maintained implementation is [config_parse.cpp](../portable/config_parse.cpp)
and [config.cpp](../portable/config.cpp); executable examples are generated by
[the compatibility corpus](../tests/compatibility/README.md).
