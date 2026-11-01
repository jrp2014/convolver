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
- You choose a filter by loading a preset (in LV2 also by choosing a file),
  and the host saves the filter inside your project. A read-only Status
  parameter says what is playing, and the
  [Convolver log](#status-and-the-convolver-log) says why a filter failed.
  The Audio Unit also has a [window](#the-audio-units-window) that
  shows the status, why it is silent and what to change, the filter and
  settings, what plays in plain words, with Copy Diagnostics and Show Log;
  the VST3 and LV2 plug-ins have no window yet.
- Latency is 256 samples by default, and the host makes up for it. The
  Audio Unit has [settings](#settings) for the gain, the wet/dry mix and the
  latency; the VST3 and LV2 plug-ins do not have them yet (their latency
  stays 256 samples).
- A filter that does not fit the host's sample rate or channel counts never
  lets unfiltered audio through (unless you bypass the plug-in or set its
  Mix below 100%): a first filter that does not fit plays silence, and one
  that replaces a working filter is refused, leaving the working filter
  playing.

Contents: [formats](#formats) · [channels](#channels) ·
[loading a filter](#loading-a-filter) ·
[when a filter does not fit](#when-a-filter-does-not-fit) ·
[status and the Convolver log](#status-and-the-convolver-log) ·
[settings](#settings) · [latency and tail](#latency-and-tail) · [bypass](#bypass) ·
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
| Libraries it needs on macOS | None beyond macOS: Accelerate for the FFT | None beyond macOS: Accelerate for the FFT, AudioToolbox to read the files chosen in its window | Not on macOS |
| Choose a `.cfg` or IR file directly | No | [Yes](#the-audio-units-window), in its window | [Yes](#lv2-filter-files) |
| Where a failure is explained | The Status parameter and the [Convolver log](#status-and-the-convolver-log) | The Status parameter and the [Convolver log](#status-and-the-convolver-log), also in the macOS system log | The `status` output and the host's log |
| Tail reported to the host | Yes | Yes, in seconds | No |
| [Settings](#settings): gain, mix | Not yet | Yes | Not yet |
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
those outputs on the centre pair. The Status text then ends with "Speaker
labels differ; see the Convolver log.", and the
[Convolver log](#status-and-the-convolver-log) names the speakers on each
side. The check never refuses a preset;
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

In the Audio Unit you can also choose the `.cfg` file, list or impulse
response itself, in Convolver's window: **Choose…** beside Filter, or drop
the file on the window ([the Audio Unit's window](#the-audio-units-window)). In LV2
you choose it with the host's file chooser
([LV2 filter files](#lv2-filter-files)). Either way the plug-in reads the
file as `convolver-preset` does, for the rate and channels the host has
set, and the project then holds the filter itself, as with a preset: moving
or deleting the file afterwards changes nothing.

## When a filter does not fit

A preset holds one filter at one sample rate, with fixed input and output
channel counts. Each plug-in checks it against the host:

| Situation | What plays |
| --- | --- |
| No filter loaded yet, equal input and output counts | The input, unchanged (after the latency) |
| No filter loaded yet, different counts | Silence |
| The first filter does not fit, or its preset is damaged | Silence |
| A new filter does not fit while another is playing | The working filter, unchanged; the project keeps saving it |
| The filter is too large to save with the project | As for a filter that does not fit: silence for a first filter, otherwise the working filter |

**Size limit.** The host saves the whole filter in the project, and
Convolver's saved state holds at most 64 MiB: just under 16,777,216 samples
counting every impulse response in the filter together: a stereo filter with
one impulse response per channel can be about 2.9 minutes long at 48 kHz. A larger filter is refused when it is loaded,
with the reason in Status and the log, so a project never plays a filter it
could not save. Shorten the impulse responses (the end of a long one is
usually near silence), or use the [command-line renderer](command-line.md),
which reads filters of up to 67,108,864 samples.

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

The VST3 and the Audio Unit say what they are doing in two places, the Status
parameter and the Convolver log; the Audio Unit also shows both in
[its window](#the-audio-units-window).

**The Status parameter** is a read-only parameter that hosts can list with the
plug-in's controls, for example in their generic plug-in window or parameter
list. It never changes the sound, is never automated, and is not restored
from saved projects. Where the host shows parameter text, it reads, for
example (some hosts show only the status name in the first column):

| Status | Example text |
| --- | --- |
| Playing | `Filter loaded: 48000 Hz, 2 in, 2 out.` |
| No filter | `No filter: audio passes unchanged. Load a Convolver preset.` |
| Load failed (silence) | `Silent: the filter is for 44100 Hz, the host runs at 48000 Hz. Details in the Convolver log.` |
| Load failed (previous filter kept) | `Load failed, previous filter kept: the filter has 2 in, 6 out, the plug-in 2 in, 2 out. Details in the Convolver log.` |
| Not checked yet | `Filter loaded: 44100 Hz, 2 in, 2 out; checked when the host starts it.` |
| Fault | `Fault: output too large, silenced and the filter restarted. Details in the Convolver log.` |

The Audio Unit's text also follows its [settings](#settings) when no filter
is heard. With no filter loaded and Gain or Mix changed, it reads, for
example, `No filter: audio passes at -20.0 dB (Gain and Mix).`; after a
failed load with Mix below 100%, `Load failed, only the dry signal plays:
...` instead of `Silent: ...`, as the dry share still plays. A filter at Mix
0% adds `Mix 0%: only the dry signal plays.`

**Fault** means something went wrong in the audio while it played, with or
without a filter loaded: the output became too large to represent (an extreme
input level or filter gain), the host sent invalid samples (not a number or
infinite, usually from a plug-in before Convolver), or an Audio Unit host
passed buffers that do not match its setup (a VST3 host gets an error back
from the call instead). Convolver silences the affected audio rather than pass it on, and
the log says what happened, how often and what to change. The status stays at
Fault until a filter loads or the host restarts the plug-in (stops
and starts it, for example by changing the sample rate). A load that fails
shows Load failed instead, as the more urgent message, and faults from before
a filter loaded do not come back once it plays; the log still records them. Some hosts show Fault
while playing but fill in the log line and the text only when the project is
saved or the host restarts the plug-in.

"Not checked yet" means the host has not started the plug-in at a sample rate
and channel layout yet. Some hosts show a separate copy of the plug-in in its
window, not the one that plays. Audacity's real-time effects window is one:
its copy usually reads "Not checked yet", but Audacity may start it briefly at
44100 Hz, whatever the project's rate, and then it can read `Silent: the
filter is for 48000 Hz, the host runs at 44100 Hz` while the playing copy is
fine. When the window and the sound disagree, trust the log: each copy has its
own number (`VST3 #1`, `VST3 #2`), and the one playing logs `Started at` with
your project's rate.

**The Convolver log** is a text file with one line for each filter loaded,
each time the host starts the plug-in (with the sample rate, channels and block
size), and each failure, explained in full with what to change. Each line
starts with the time, the Convolver version, the format and instance (`VST3 #2`),
the host program and its process number. Loads and refusals are numbered
(`Request 3:`), so you can follow a sequence of presets. The VST3 plug-in
also logs host settings it cannot use, with the reason: a sample rate that is
not a whole number of hertz, a block size or sample type it does not take, or
(once for each instance, as hosts try several) a channel layout with more than
8 channels or more than one bus. A save that fails is logged in every format,
since some hosts do not say so: the project or preset saved then does not
hold the filter. The first line each
time a host starts Convolver says what is running: the version, the FFT
library, the CPU kernels, the plug-in SDK it was built with (VST3 and Audio Unit) and the process. When something does not work, send
this file with your report.

If the file cannot be written (its folder cannot be created or written to, or
there is no home folder), the Status text ends with "The log could not be
written." instead of pointing to it, and the lines go to the
host's standard error output instead (and, on Windows, to the debugger output,
which DebugView shows). On macOS they are in the system log either way.

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
sends the log there instead; on macOS and Linux, setting it empty turns the file off (the Status text
still points to the log; on macOS the lines are in the system log). LV2 has
no file of its own: it writes to the host's log, including why it refused to
start at the host's sample rate and when a save failed.

### The Audio Unit's window

The Audio Unit offers hosts a window of its own (`auval` lists it as a Cocoa
view). Hosts that show plug-in views open it from the plug-in's slot; if a
host shows its generic sliders instead, choose the plug-in's own view in the
plug-in window, where the host offers the choice. GarageBand opens it from
the plug-in's button in the track's Plug-ins area. From the top, the window has:

- **A title line** with the Convolver version, the instance (`Audio Unit #2`,
  as in the log) and the program that runs the plug-in, or "the host's
  plug-in process" when the host runs plug-ins in Apple's separate service,
  as GarageBand does (the Software line names that process).
- **The status** (Playing, No filter, Load failed, Not checked yet or Fault),
  the same as the Status parameter, as a symbol, a coloured word, and under
  them **what plays, why, and what to change**, in full, as the log
  explains it. For example, a stereo preset on a GarageBand mono track
  reads: "The saved filter does not suit the host, so Convolver plays
  silence until it does. The filter has 2 inputs and 2 outputs, but the
  plug-in is set up with 1 input and 1 output. Give the plug-in 2 inputs and
  2 outputs in the host's track or routing settings, or load a filter made
  for 1 input and 1 output. Hosts run an effect with one channel on a mono
  track (GarageBand does): put Convolver on a stereo track instead." While
  a filter simply plays, it gives the filter's rate and channels.
- **Filter:** the name of the file that plays (or "From the saved project or
  preset" after reopening a project, which saves the filter itself, not the
  file), a note when the newest file was refused, and **Choose…** beside it.
  Choose… opens the file dialog for a `.cfg` file, a
  configuration list or a sound file; dropping the file anywhere on the
  window does the same. The Audio Unit reads sound files with macOS's own reader, so
  it needs nothing installed beside it: WAV, AIFF, CAF, W64, RF64, FLAC and
  the other formats macOS reads, and headerless `.pcm` or `.raw` filters
  named in a `.cfg`, as the command-line tool reads them. (A `.pcm` or
  `.raw` file is read through a header only if it starts with one of the
  usual container markers, such as `RIFF` or `FORM`; a headerless one is
  refused if chosen directly, since only a `.cfg` gives its rate and
  channels.) A list chooses its
  entry by the host's rate and channels, as the command-line tool does.
  The status and explanation then say whether it plays and, if not, why,
  naming the file. A file that does not suit the plug-in leaves the
  previous filter playing; if no filter was playing, the plug-in stays
  silent, as for a first preset that does not fit. Either way the project
  then saves the outcome: the filter that plays, with the failure noted.
  An accepted file also becomes the plug-in's current preset, named after the
  file, so the host can tell the project has changed and asks to save it.
  The
  file is read and the filter prepared before the window responds again,
  so a very long filter can make the window (and the host) pause for a
  moment. The file dialog opens as a sheet on the window that holds
  Convolver (the host's own window, in a host that runs Convolver in its
  own process) and takes typing at once. Closing
  Convolver's window while the file dialog is open closes
  the dialog too, and nothing is loaded. GarageBand runs plug-ins in a
  sandbox; in jrp's test (October 2026) it let a `.cfg` read the WAV files
  beside it. Where a sandbox refuses, the explanation says permission was
  denied. A preset always works.
- **Gain** and **Mix**: a slider, the value in a box beside it and its unit.
  Type a value in the box and press Return (or move to another control) to
  set it exactly; a value beyond the range stops at its end. Option-click a
  slider to put it back to its default (0 dB, 100% wet), as in Logic.
  Moving or resetting a slider drops a value you had not finished typing.
  Mix is labelled Dry and Wet at its ends. A host that shows
  Convolver's window shows it instead of its generic controls, so the
  settings are here. Moving a slider changes the setting and tells the
  host, and the sliders follow changes the host makes (automation, its own
  controls), except the one you are dragging. The window does not yet mark the start and end of a drag, so a
  host recording automation from it may record each step separately; not
  yet tried in GarageBand.
- **Latency**: a menu of block sizes, with the delay it adds at the host's
  rate beside it; a new choice plays at once, and "not in use yet" shows
  only until it does ([settings](#settings)).
- **Audio**: what plays, in plain words, with the technical term in
  brackets. Hold the pointer over a line for what it means.

  | Line | What it says |
  | --- | --- |
  | Sample rate | The host's rate, and whether the filter's matches (the filter's own rate when it does not) |
  | Channels | The host's inputs and outputs, the speakers the filter's outputs are labelled for, and the filter's counts when they do not suit |
  | Routing | Which input feeds which output (`In 1 → Out 1`), with route gains such as `×0.5`; the first four paths, then how many more |
  | Filter length | The longest filter, in time and in taps |
  | Added delay | The latency, in time and frames, and a chosen one not in use yet |
  | Ring-out | How long the output goes on after the input stops (the tail) |
  | Loudest peak | The loudest output any full-scale input could make through the filter, and the Gain that rules out clipping (or that it cannot clip at the current Gain and Mix) |
  | Host blocks | The largest block the host sends at a time |

  A line that does not match (a filter for another rate, say) is marked with
  a warning sign and says what differs, in red.
- **File, software and log**, folded away under a disclosure triangle: the
  filter file's path and request number (and a refused file's), the software
  (version, FFT library, CPU kernels, the Audio Unit SDK, and what reads the
  filter files), and where the log is.
- **Copy Diagnostics** puts all of this, with the settings and this
  instance's recent lines from the Convolver log, on the clipboard as plain
  text, ready to paste into a report.
- **Show Log** selects the log file in the Finder (in a sandboxed host, the
  copy inside the host's container).

The text can be selected and copied. The controls are standard macOS ones
with names for VoiceOver, which has not been tried with the window yet, and
they follow the system's light or dark appearance. The
window updates twice a second while it is on screen. While the host restores
a project or preset on another thread, it shows the last report until the
filter is ready. The window has no list of recent files and does not
reload a file yet.

## Settings

The Audio Unit has three settings, which hosts show with its other
parameters and Convolver's window shows as sliders and a menu (in GarageBand,
the window replaces the generic controls). Their defaults play exactly as
Convolver did before the settings existed.

| Setting | Values | Default | What it does |
| --- | --- | --- | --- |
| **Gain** | −100 to +24 dB | 0 dB | Scales the filtered signal, as the command line's `--gain-db` does. Negative values make room for a filter that boosts some frequencies; positive values make up level. |
| **Mix** | 0 to 100% | 100% | The share of filtered sound; the rest is the input, delayed by the latency so the two line up. With different input and output counts there is no dry signal, so the mix stays at 100%. Like bypass, the input share still plays when a filter fails to load and the filtered sound is silent. |
| **Latency** | 64, 128, 256, 512, 1024, 2048 or 4096 frames | 256 frames | How long Convolver delays the audio, in frames (samples per channel): at 48 kHz, 256 frames is 5.3 ms. Smaller values mean less delay and more processor load; larger values the opposite. It is the old Convolver's partitions setting in modern form, and the command line's `--block-size`. A change during playback leaves a short gap; see [latency and tail](#latency-and-tail). |

- Gain and Mix changes fade over 64 samples, so automating them does not
  click. Latency cannot be automated.
- The host saves the settings in the project and in presets it saves itself.
  A preset made by `convolver-preset` holds the filter only, so loading it
  leaves the settings as they are. The
  [Convolver log](#status-and-the-convolver-log) lists the settings each
  time the plug-in starts, and again when it restores saved settings (some
  hosts, GarageBand among them, start the plug-in before restoring the
  project).
- **Settings and other formats.** The VST3 and LV2 plug-ins have no settings
  yet, so a filter moved to them from an Audio Unit project or preset plays
  at 0 dB, fully wet, with a latency of 256 frames. The same project can
  therefore sound different in another format.

## Latency and tail

Latency is 256 samples by default, whatever the host's buffer size, and the
plug-in reports it so the host can line the audio up. At 48 kHz that is about 5.3 ms.
In the Audio Unit the [Latency setting](#settings) chooses it, from 64 to
4096 samples; VST3 and LV2 always use 256.

A new Latency plays at once. Changing it during playback leaves a gap in the
sound, because Convolver starts again with the new size: the gap lasts the
new latency plus the filter's own delay, which is short for most room
correction filters but can be most of a second for a long linear-phase
one, and a filter's tail that was still sounding is cut. The
[Convolver log](#status-and-the-convolver-log) records each change. Hosts
differ in when they line the audio up again after a latency change: some do
it at once, others at the next playback.

The tail is the time the filter keeps sounding after the input stops: its
longest response plus any delays. VST3 and the Audio Unit report it (the Audio
Unit in seconds, as Audio Units do). VST3 counts it at the plug-in's output,
as Steinberg's own tests measure it, so it adds the 256-sample latency: an
instance with no filter reports 256 samples, because its output still carries
the last 256 samples of input after the input stops. After a filter is replaced during
playback, the Audio Unit keeps reporting the longest tail used until the host
restarts it. LV2 cannot report a tail, so hosts that stop processing at the end
of a region may cut it off: render with a little silence after the audio.

## Bypass

The host's bypass switch fades over 64 samples to the input, delayed by the
latency so nothing jumps in time, and back. The filter keeps running
underneath, so switching back loses nothing.

With different input and output counts, bypass fades every output to silence
instead: no output has an unfiltered counterpart, and bypassing a crossover
must not send full-range audio to a tweeter.

VST3 saves bypass with the filter. Audio Unit and LV2 hosts save and restore
the bypass switch themselves (in LV2 it is the `enabled` control), so loading
a preset leaves the switch as it is.

## Replacing a filter

Loading a preset while audio plays prepares the new filter in the background,
lets it run for the latency (256 samples by default), then crossfades to it
over 256 samples. The new
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
- The VST3 marks each output channel silent in any call that leaves it
  silent, so hosts can skip work after it. A filter's tail is never cut short:
  silent input does not mark the output while the tail is still sounding.

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
| 2 | `status` | Control output | 0 Running, 1 No filter, 2 Load failed, 3 Loading, 4 Fault |
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
