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
- There is no window of its own. You choose a filter by loading a preset (in
  LV2 also by choosing a file), and the host saves the filter inside your
  project. A read-only Status parameter says what is playing, and the
  [Convolver log](#status-and-the-convolver-log) says why a filter failed.
- Latency is always 256 samples, and the host makes up for it.
- A filter that does not fit the host's sample rate or channel counts never
  lets unfiltered audio through: a first filter that does not fit plays
  silence, and one that replaces a working filter is refused, leaving the
  working filter playing.

Contents: [formats](#formats) · [channels](#channels) ·
[loading a filter](#loading-a-filter) ·
[when a filter does not fit](#when-a-filter-does-not-fit) ·
[status and the Convolver log](#status-and-the-convolver-log) ·
[latency and tail](#latency-and-tail) · [bypass](#bypass) ·
[replacing a filter](#replacing-a-filter) · [audio](#audio) ·
[LV2 channel layouts](#lv2-channel-layouts) · [LV2 ports](#lv2-ports) ·
[LV2 filter files](#lv2-filter-files) ·
[identities](#identities) · [Audio Unit licence](#audio-unit-licence)

## Formats

| | VST3 | Audio Unit (AU) | LV2 |
| --- | --- | --- | --- |
| Systems | Windows, macOS, Linux | macOS | Linux |
| How to get it | Preview package, or build | Build from source | Build from source |
| Channel counts | Any pairing of 1 to 8 in and out | Any pairing of 1 to 8 in and out | [Fixed layouts](#lv2-channel-layouts) |
| Preset file | `.vstpreset` | `.aupreset` | A `NAME.preset.lv2` folder |
| Choose a `.cfg` or IR file directly | No | No | [Yes](#lv2-filter-files) |
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

In LV2 you can also choose the `.cfg` file, list or impulse response itself,
with the host's file chooser ([LV2 filter files](#lv2-filter-files)).

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

**A project opened at another sample rate or layout** plays silence. What
happens next depends on the format:

- **VST3 and Audio Unit** keep the filter. Set the host back to the filter's
  rate and layout and the filter returns; saving the project in between keeps
  it. Some hosts briefly try a default 44.1 kHz before setting the real rate;
  that does not count.
- **LV2** refuses the filter as soon as the session opens, and `status` shows
  Load failed. **Do not save the session**: saving records the failure and
  drops the filter. Close it without saving and reopen it at the filter's rate,
  or load the preset again.

**Saving after a first filter failed** saves the failure, so reopening the
project plays silence again rather than unfiltered audio. Load a matching
preset to replace it.

How each format reports a refusal:

- **VST3 and Audio Unit:** the host is told the load failed and shows that in
  its own way, or not at all. The plug-in's **Status** parameter says in one
  line what is wrong, and the [Convolver log](#status-and-the-convolver-log)
  explains it in full.
- **LV2:** the host's restore fails, the `status` output shows Load failed
  until a filter loads, and the host's log explains what differs and what to
  change.

## Status and the Convolver log

Convolver has no window of its own, so the VST3 and the Audio Unit say what
they are doing in two places.

**The Status parameter** is a read-only parameter that hosts list with the
plug-in's controls, for example in their generic plug-in window or parameter
list. It never changes the sound and is never automated or saved. It reads,
for example:

| Status | Example text |
| --- | --- |
| Playing | `Filter loaded: 48000 Hz, 2 in, 2 out.` |
| No filter | `No filter: audio passes unchanged. Load a Convolver preset.` |
| Load failed (silence) | `Silent: the filter is for 44100 Hz, the host runs at 48000 Hz. Details in the Convolver log.` |
| Load failed (previous filter kept) | `Load failed, previous filter kept: the filter has 2 in, 6 out, the plug-in 2 in, 2 out. Details in the Convolver log.` |
| Not checked yet | `Filter loaded: 44100 Hz, 2 in, 2 out; checked when the host starts it.` |

"Not checked yet" means the host has not started the plug-in at a sample rate
and channel layout yet. Some hosts show a separate copy of the plug-in in its
window: Audacity's real-time effects window, for one, never starts its copy,
so it always shows this line; compare the filter's rate and channels in it
with your project's, and read the log for what the playing copy did.

**The Convolver log** is a text file with one line for each filter loaded,
each time the host starts the plug-in (with the sample rate, channels and block
size), and each failure, explained in full with what to change. Each line
starts with the time, the Convolver version, the format and instance (`VST3 #2`)
and the host program. When something does not work, send this file with
your report.

| System | Log file | Copy it to the clipboard |
| --- | --- | --- |
| macOS | `~/Library/Logs/Convolver/Convolver.log` (the Console application lists it under Log Reports) | `pbcopy < ~/Library/Logs/Convolver/Convolver.log` |
| Windows | `%LOCALAPPDATA%\Convolver\Convolver.log` | `Get-Content "$env:LOCALAPPDATA\Convolver\Convolver.log" \| Set-Clipboard` (PowerShell) |
| Linux | `~/.local/state/convolver/convolver.log` (or under `$XDG_STATE_HOME`) | `xclip -selection clipboard < ~/.local/state/convolver/convolver.log` |

On macOS the same lines also go to the system log, which also works when a
sandboxed host such as GarageBand keeps the file inside its own container
(`~/Library/Containers/<host>/Data/Library/Logs/Convolver/`). To copy the
last hour's lines from there:

```sh
log show --last 1h --info --predicate 'eventMessage CONTAINS "Convolver"' --style compact | pbcopy
```

When the file passes 1 MB it is renamed with `.old` added and a new one is
started. Setting the environment variable `CONVOLVER_LOG` to a file path
sends the log there instead; on macOS and Linux, setting it empty turns the file off. LV2 has
no file of its own: it writes to the host's log.

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
| 2 | `status` | Control output | 0 Running, 1 No filter, 2 Load failed, 3 Loading |
| 3… | `in_1` … `in_N` | Audio input | Input channels in order |
| then | `out_1` … `out_M` | Audio output | Output channels in order |
| then | `control` | Atom input | The host sets or asks for the filter file |
| last | `notify` | Atom output | Names the file that plays |

Load failed stays shown until a filter loads successfully, also after the
host reactivates the plug-in; Loading is shown while the newest file choice
is being loaded.

The plug-in needs the host's `urid:map` feature, and the host's worker thread
to load files. It writes messages through the host's log, or to standard
error if the host has none. Hosts that cannot hand it a feature list (such as
lilv's `lv2apply`) cannot load it.

## LV2 filter files

The LV2 plug-in has one parameter, **Filter**
(`https://github.com/jrp2014/convolver/lv2#filter`, a path). Hosts that show
LV2 file parameters, such as Ardour and Jalv, give it a file chooser: choose
the `.cfg` file, list or impulse response you would pass to the
[command-line tool](command-line.md). The file is read for the plug-in's
sample rate and channel counts, so a list chooses its entry exactly as the
command-line tool does, and nothing is resampled.

The file is loaded on the host's worker thread, so audio carries on
meanwhile: the status port shows Loading, then Running once the new filter
plays (fading in, or starting at once if no audio has played yet). If you
choose several files quickly, only the newest is loaded; one that was still
being loaded when you chose another is never heard, and if it fails, its
failure is not reported either. The plug-in tells the host which file plays,
so the chooser shows the right file after a failed load and when the session
is reopened.

A file that cannot be used (missing, unreadable, another sample rate, other
channel counts, or a path longer than 4096 bytes) is refused like a preset
that does not fit ([when a filter does not fit](#when-a-filter-does-not-fit)):
the status shows Load failed, the log explains what is wrong and what to
change, and a working filter keeps playing and is still the one saved. A host
without a worker thread cannot load files: the status shows Load failed at
once and the log says why; presets and saved sessions still work there. If no
filter was playing, the plug-in falls silent at once, as for any first filter
that cannot be used.

**Resetting.** A host that restores a state or preset holding none of
Convolver's settings (only control values, or nothing at all) resets the
plug-in, as LV2 requires: the filter and its file are dropped, any failure is
cleared, a file still loading is abandoned, and the plug-in plays as if no
filter had been loaded.

**Saved sessions.** The session saves the whole filter, as for a preset, and
also the path of the file it came from (made relative to the session folder
when the host supports that, so the folder can move). Reopening uses the saved
filter, so the session sounds the same after the file changes or disappears;
only if the saved filter cannot be used does it load the file again.

**PipeWire.** PipeWire's `filter-chain` can start the plug-in with a file
path alone, which is how to use Convolver for room correction of all system
audio on Linux:

```text
config = { "https://github.com/jrp2014/convolver/lv2#filter" = "/home/me/room/room.cfg" }
```

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
