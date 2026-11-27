<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Configuration files

[Using Convolver](README.md) · [Command-line reference](command-line.md)

For anyone writing or editing a configuration: the `.cfg` and configuration list
formats, line by line.

A configuration describes how to route and filter audio. It does not contain
the IR samples themselves: it names the IR files holding them. Keep the
configuration and those files together when moving a setup to another computer.
This page describes the current portable parser, not the old Windows installer.

**In short:**

- For a simple per-channel filter, pass an IR file directly: one with a
  channel per input channel, a mono file, which filters every channel, or a
  four-channel true-stereo file on stereo audio
  ([mono and true-stereo IR files](#mono-and-true-stereo-ir-files)). Use a
  `.cfg` file for any other routing, mixing or delays.
- A `.cfg` file is a header line, a line of input delays, a line of output
  delays, then four lines per path: IR file, IR channel, input routes, output
  routes. Channels count from zero.
- A route number's integer part is the channel and its fractional part is the
  gain, so `1.25` is channel 1 at gain 0.25, and `-0` is channel 0 inverted.
- Paths are relative to the file that names them. Use relative paths so a setup
  can be copied to another computer; Windows drive paths fail on macOS/Linux.
- A configuration list names several configurations; the first that matches the input's
  sample rate and channel count is used. Check any file with `--validate` or
  `--inspect`.

## The simplest filter: an IR file

Pass an IR file directly to `--filter` to filter each input channel with
the matching IR channel. Input and IR must have the same sample rate, and the
IR file one channel per input channel, except for the two layouts in
[mono and true-stereo IR files](#mono-and-true-stereo-ir-files). Use a `.cfg`
file for cross-channel routing or delays.

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
does the same for channel 1. A path that names IR channel `0` of a mono file
on both inputs gives each channel the same response, as the mono file used
directly does; written out, each channel can have its own delay or gain.

Check and render it with:

```sh
convolver --validate room.cfg
convolver --inspect room.cfg
convolver --filter room.cfg --input music.wav --output corrected.wav
```

These examples assume `convolver` is on PATH. From your own build, use its
path instead, such as `./build/convolver`.

<a id="reverb-responses"></a>

## Mono and true-stereo IR files

An IR file used directly needs one channel per input channel, with two
exceptions, which play without a `.cfg`:

- **A mono IR file on audio with two or more channels** filters each
  channel on its own with the same response, as if the file were repeated
  for each channel. Nothing is mixed: left stays left. A `.cfg` with one
  input and one output does the same, its delays and gains repeated for each
  channel.
- **A four-channel IR file on stereo audio** plays as true stereo. A
  true-stereo response is a space measured from two sources to two
  microphones, so each input reaches both outputs. Its four channels are, in
  order, left to left, left to right, right to left and right to right.
  Convolver feeds the left input through channels 1 and 2, the right input
  through channels 3 and 4, and adds them on each output.

Convolver uses these routings only where the counts leave one sensible
reading:

- **A mono file on several channels** can only be repeated: mixing the
  channels to mono first would lose the stereo image.
- **A four-channel file on two channels** has no other way to use all four
  channels. The order is the one Steinberg's REVerence, SIR3 and IR.lv2's
  `convert4chan` use. On four channels the file filters each channel, as
  any IR file does.
- **An exact match comes first**, across a whole
  [configuration list](#paths-and-configuration-lists) too: a list naming
  `mono.wav` before `stereo.wav` plays `stereo.wav` on stereo audio and
  `mono.wav` on mono audio.
- **Convolver says when it uses one.** The command line prints a line such
  as `convolver: the four-channel IR file plays as true stereo: left to
  left, left to right, right to left, right to right`, `--inspect` reports
  it as the entry's `layout`, and the plug-ins' Status and Routing line say
  so ([Convolver's window](plugins.md#convolvers-window)).

**Not every four-channel file is true stereo.** Check the reverb's own
documentation:

- **Quad or 4.0 surround** (front left, front right, back left, back right)
  would play as true stereo on a stereo track and sound wrong. Use it on a
  four-channel track. Convolver cannot tell the two apart from the file:
  many programs, FFmpeg among them, label every four-channel WAV as quad.
- **Ambisonics B-format** (W, X, Y and Z) has to be decoded for speakers. A
  WAV, RF64 or Wave64 file marked as B-format, as `.amb` files are, is
  refused on stereo audio, with the reason. An unmarked one, such as an
  AmbiX file, cannot be told apart, so do not use it directly; a `.cfg`
  can [play its W channel as a mono reverb](free-impulse-responses.md#play-a-b-format-file-as-a-mono-reverb).
- **True stereo in another order**, or as two stereo files, needs a `.cfg`
  (below).

**A `.cfg` with the audio's channel counts plays as written**, so it
overrides these routings (only a `.cfg` with one input and one output is
spread over the channels). This
one plays a true-stereo `hall.wav` at 48 kHz (the first line's rate must be
the file's) exactly as the file used directly plays:

```text
// sample rate, input channels, output channels, speaker mask
48000 2 2 3
// input delays in milliseconds
0 0
// output delays in milliseconds
0 0
// left to left
hall.wav
0
0
0
// left to right
hall.wav
1
0
1
// right to left
hall.wav
2
1
0
// right to right
hall.wav
3
1
1
```

If the reverb's order is left to left, right to left, left to right, right
to right, swap the IR-channel lines `1` and `2`. A reverb that comes as two
stereo files, one for each input side, needs the same four paths: name the
left input's file on the first two and the right input's file on the last
two, each with IR channels `0` and `1`.

Convolver 4.4, the 2006 Windows version, refused an IR file whose channel
count differed from the audio's, so a mono or true-stereo reverb needed a
`.cfg` there. Those `.cfg` files play as before.

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
A file name that is not UTF-8, in a configuration or a list, stops with an
error (`not-utf8`) when that file is needed, naming its line and the byte that
is wrong, shown as `\xFC`, for example; an IR of a list entry that is not
chosen need not exist, as usual. Such a file was most likely saved in a Windows code page
such as Windows-1252, as older versions of Notepad and the original Windows
Convolver did; save it again as UTF-8. Convolver does not guess the code page.
On Linux a file whose name has exactly those bytes still loads; macOS and
Windows can't hold such a name.

Delays must be nonnegative. Milliseconds are converted using the parser's
single-precision arithmetic, then truncated to whole frames, not rounded.
For example, at 48 kHz, `0.5` ms is 24 frames. Inspection shows both the original
value and the effective frame count. Different paths may use IRs of different
lengths, but each IR must have the configuration's sample rate.

The speaker mask is metadata, not a routing instruction. `0` means unspecified;
`3` labels front left/right and `3F` labels six WAVE channels. The CLI writes
those labels only with `--speaker-map config`, and never reorders samples to
match them. See [speaker labels](command-line.md#speaker-labels).

## Filter files

An IR file can be any sound file libsndfile reads, such as WAV, RF64, W64,
AIFF, CAF, FLAC or Ogg Vorbis. Its sample rate must be the configuration's. The
[preview packages](install.md#what-a-preview-package-contains) read no FLAC or Ogg files;
[FLAC and Ogg](command-line.md#flac-and-ogg) says how to work around that.
A WAV file that was cut short is refused;
[input files](command-line.md#input-files) says what is checked, and which
file from an interrupted write cannot be told from a whole one.
A filter that starts like a WAV, RF64, BW64, W64, RIFX, AIFF, CAF, AU, FLAC or
Ogg file but cannot be read is reported with the reader's own reason, such as a
truncated chunk or too many channels.

A filename ending in `.pcm` or `.raw` (in any letter case) is a raw filter,
unless the file's content is a sound file: one that libsndfile recognises, or
one that starts like a WAV, RF64, BW64, AIFF, CAF, AU, FLAC or Ogg file. A WAV
or AU file named `.pcm` is still read through its header, and a header that
cannot be read is reported as an error rather than read as samples. Two
libsndfile signatures are too short to tell from sample data, so a file
starting with either is a raw filter: a bare MPEG audio frame (an MP3 without
an ID3 tag) and an Akai MPC 2000 sample. A raw filter is a headerless file of mono samples. Convolver reads
it as little-endian 32-bit float (`f32le`), the type
[DRC](https://drc-fir.sourceforge.net/) and BruteFIR write by default, and says
so: rendering prints a line such as
`convolver: raw filter 'left.pcm' read as mono f32le at 48000 Hz`, and
`--inspect` shows `raw mono f32le` beside the path (`raw_format` in JSON).

For raw filters in another sample type, give the command line
`--raw-filter-format` with one of the [RAW stream](command-line.md#raw-streams)
names, for example `--raw-filter-format s24le` or `f64le`; `--inspect` takes it
too. It applies to every raw filter in the configuration. The plug-ins and
`convolver-preset` have no such setting and always read `f32le` (the preset
tool says so for each raw filter), so for them convert raw filters in other
types to WAV first, for example
`sox -t raw -r 48000 -c 1 -e signed -b 24 -L left.pcm left.wav`.

Raw filters have no header to state a sample rate, so they take the
configuration's rate; their IR channel must be `0`. A raw file whose size is
not a whole number of samples of the chosen type is reported as damaged, and the
message names the type. Other programs that read `.cfg` files expect sound
files, so `--inspect --profile portable-paths` warns about raw filters
(`raw-filter`). With `--syntax-only` it opens no filter file, so that warning
goes by the name alone and says so.

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

A configuration list is a text file containing one configuration, IR file or
nested list filename per nonblank line:

```text
filters/room-44100.cfg
filters/room-48000.cfg
```

Rendering selects the first entry matching the input sample rate and channel
count. `--output-channels N` adds an output-count constraint. Inspection and
`convolver-preset` accept explicit rate/input/output selectors; with none, they
select the first entry. Lists are searched in order, including nested lists;
a list that includes itself, directly or through another list, is an error.
A malformed visited entry can fail selection; a list is not a fallback that
silently skips broken configurations. Unselected configurations' IR samples are
not decoded. `--all-configurations` instead diagnoses errors and continues to
later entries.

## Problems and compatibility

[Troubleshooting](troubleshooting.md#filters-and-configurations) lists the
common errors and their fixes. [Inspection options and JSON output](command-line.md#inspecting-and-validating-configurations)
include syntax-only checks and warning policy. Resource limits are in the
[command-line reference](command-line.md#limits).

The format is used by other audio programs, but their accepted subsets differ;
see [other programs](other-programs.md) before assuming a configuration is
interchangeable. The original [4.4 specification](../historical/convolver-4.4/config.html)
and [examples](../historical/convolver-4.4/configegs.html) are historical
references.
