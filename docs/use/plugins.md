<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# How the plug-ins behave

For anyone using Convolver in a plug-in host who needs to know exactly what it
does: channels, latency, bypass, loading filters and what happens when a filter
does not fit. Installing is in [install](install.md); making presets is in
[plug-in presets](presets.md).

**In short:**

- Convolver comes as a VST3 plug-in (Windows, macOS, Linux), an Audio Unit
  (macOS) and an LV2 plug-in (Linux). They behave the same way; the table
  below lists the differences.
- One input and one output of 1 to 8 channels each; the counts may differ,
  for example 2 in and 6 out for a crossover.
- There is no window of its own. You choose a filter by loading a preset, and
  the host saves the filter inside your project.
- Latency is always 256 samples, and the host makes up for it.
- A filter that does not fit the host's sample rate or channel counts never
  lets unfiltered audio through: a first filter that does not fit plays
  silence, and one that replaces a working filter is refused, leaving the
  working filter playing.

Contents: [formats](#formats) · [channels](#channels) ·
[loading a filter](#loading-a-filter) ·
[when a filter does not fit](#when-a-filter-does-not-fit) ·
[latency and tail](#latency-and-tail) · [bypass](#bypass) ·
[replacing a filter](#replacing-a-filter) · [audio](#audio) ·
[LV2 channel layouts](#lv2-channel-layouts) · [LV2 ports](#lv2-ports) ·
[identities](#identities) · [Audio Unit licence](#audio-unit-licence)

## Formats

| | VST3 | Audio Unit (AU) | LV2 |
| --- | --- | --- | --- |
| Systems | Windows, macOS, Linux | macOS | Linux |
| How to get it | Preview package, or build | Build from source | Build from source |
| Channel counts | Any pairing of 1 to 8 in and out | Any pairing of 1 to 8 in and out | [Fixed layouts](#lv2-channel-layouts) |
| Preset file | `.vstpreset` | `.aupreset` | A `NAME.preset.lv2` folder |
| Where a refused load is explained | Nowhere yet: the host only sees the refusal | The host gets an error; the reason is in the macOS Console | The host's log, and a status output |
| Tail reported to the host | Yes | Yes, in seconds | No |
| Licence | GPL-2.0-or-later | [GPL-3.0-or-later](#audio-unit-licence) | GPL-2.0-or-later |

## Channels

The plug-in has one input and one output. Each has 1 to 8 channels, and the
counts may differ. More channels, or more inputs and outputs, are refused; use
the [command-line tool](command-line.md) for wider filters.

Channels are numbered in the host's order, and the `.cfg` routes refer to them
in that order. For VST3 the order follows the speaker arrangement: 5.1 is left,
right, centre, LFE, left surround, right surround, the same order as the WAV
speaker mask `3F`. Any arrangement with the right number of channels is
accepted.

**Speaker check (VST3).** If the `.cfg` names its output speakers (the mask at
the end of its first line, which presets keep) and the host's output
arrangement names different ones, the filter still plays, routed by channel
number. For example, a 7.1 filter with side speakers on outputs 7 and 8, on a
7.1 Cine output whose channels 7 and 8 are left and right of centre, plays
those outputs on the centre pair. Convolver notes the difference but cannot
show the note yet, because it has no window. The check never refuses a preset;
in a layout with side speakers and no back ones (5.1 written as `60F`), the
side speakers count as the back pair.

## Loading a filter

Load a preset from the host's preset menu ("Load Preset…" or its preset
browser). [Plug-in presets](presets.md) shows how to make one from an IR, a
`.cfg` or a configuration list. The preset holds the whole filter, and the host
saves the same data in the project, so the project keeps working after the
original WAV and `.cfg` files are moved or deleted.

The filter is prepared away from the audio, then installed. A preset loaded
before any audio plays starts at once; one loaded during playback fades in
([replacing a filter](#replacing-a-filter)).

## When a filter does not fit

A preset holds one filter at one sample rate, with fixed input and output
channel counts. Each plug-in checks it against the host:

| Situation | What plays |
| --- | --- |
| No filter loaded yet, equal input and output counts | The input, unchanged (after the latency) |
| No filter loaded yet, different counts | Silence |
| The first filter does not fit, or its preset is damaged | Silence |
| A new filter does not fit while another is playing | The working filter, unchanged; the project keeps saving it |

Convolver never falls back to unfiltered audio when a filter fails: a room
correction or crossover that quietly plays unfiltered audio can be worse than
an obvious failure, and could send full-range audio to a tweeter.

**A project opened at another sample rate or layout** keeps its filter but
plays silence. Set the host back to the filter's rate and layout and the
filter returns; saving the project keeps it. Some hosts briefly try a default
44.1 kHz before setting the real rate; that does not count.

**Saving after a first filter failed** saves the failure, so reopening the
project plays silence again rather than unfiltered audio. Load a matching
preset to replace it.

How each format reports a refusal:

- **VST3:** the host is told the load failed and shows that in its own way, or
  not at all. A project that turns out not to match when the host starts
  processing plays silence with no message.
- **Audio Unit:** the host gets an error. Open the Console application and
  search for "Convolver" to read why.
- **LV2:** the host's restore fails, the `status` output shows Load failed
  until a filter loads, and the host's log explains what differs and what to
  change.

## Latency and tail

Latency is always 256 samples, whatever the host's buffer size, and the
plug-in reports it so the host can line the audio up. At 48 kHz that is about
5.3 ms.

The tail is the time the filter keeps sounding after the input stops: its
longest response plus any delays. VST3 and the Audio Unit report it (the Audio
Unit in seconds, as Audio Units do). After a filter is replaced during
playback, the Audio Unit keeps reporting the longest tail used until the host
restarts it. LV2 cannot report a tail, so hosts that stop processing at the end
of a region may cut it off: render with a little silence after the audio.

## Bypass

The host's bypass switch fades over 64 samples to the input, delayed by the
same 256 samples so nothing jumps in time, and back. The filter keeps running
underneath, so switching back loses nothing.

With different input and output counts, bypass fades every output to silence
instead: no output has an unfiltered counterpart, and bypassing a crossover
must not send full-range audio to a tweeter.

VST3 saves bypass with the filter. Audio Unit and LV2 hosts save and restore
the bypass switch themselves (in LV2 it is the `enabled` control), so loading
a preset leaves the switch as it is.

## Replacing a filter

Loading a preset while audio plays prepares the new filter in the background,
lets it run for 256 samples, then crossfades to it over 256 samples. The new
filter starts with no history, so it cannot filter audio that played before
it was loaded. When the host stops and restarts processing, a pending filter
is installed at once.

## Audio

- Processing is single precision, including with 64-bit host buffers. Dither
  does not apply: the host receives floating-point audio.
- Input that is not a finite number becomes silence.
- Variable buffer sizes, processing in place and calls with no audio are
  handled.
- The Audio Unit takes 32-bit float audio at a whole-number sample rate, the
  same on input and output. It marks its output silent only once the tail has
  died away.

## LV2 channel layouts

An LV2 plug-in's inputs and outputs are fixed, so each layout is its own
plug-in: 1→1, 2→2, 1→2, 2→4, 2→6, 2→8, 6→6 and 8→8. Choose the one whose
counts match your filter; [`convolver-preset`](presets.md) says which. Their
URIs are `https://github.com/jrp2014/convolver/lv2#discrete-N-M`.

## LV2 ports

| Index | Symbol | Kind | Meaning |
| --- | --- | --- | --- |
| 0 | `enabled` | Control input | 1 processes, 0 bypasses (a 64-sample fade) |
| 1 | `latency` | Control output | 256 samples |
| 2 | `status` | Control output | 0 Running, 1 No filter, 2 Load failed |
| 3… | `in_1` … `in_N` | Audio input | Input channels in order |
| then | `out_1` … `out_M` | Audio output | Output channels in order |

The plug-in needs the host's `urid:map` feature. It writes messages through
the host's log, or to standard error if the host has none. Hosts that cannot
hand it a feature list (such as lilv's `lv2apply`) cannot load it.

## Identities

These plug-ins have new identities. Projects saved with the old Windows
Convolver's VST 2 plug-in do not open them automatically. Each format's
identifier is permanent, so presets and projects made today keep loading in
later versions; the Audio Unit is `aufx Conv CONV`.

## Audio Unit licence

The Audio Unit is built with Apple's AudioUnitSDK, which is under the Apache
License 2.0. That licence is compatible with version 3 of the GPL but not
version 2, so the Audio Unit as a whole is distributed under GPL-3.0-or-later.
Convolver's own source code and every other build stay GPL-2.0-or-later. The
bundle's `Contents/Resources/licenses/` folder holds the notice, GPL version 3
and the AudioUnitSDK licence.
