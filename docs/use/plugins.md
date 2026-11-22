<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# How the plug-ins behave

For anyone using Convolver in a plug-in host who needs to know exactly what it
does: channels, latency, bypass, loading filters and what happens when a filter
does not fit. Installing is in [install](install.md); making presets is in
[plug-in presets](presets.md).

**In short:**

- Convolver comes as a VST® 3 plug-in (Windows, macOS, Linux), an Audio Unit
  (macOS) and an LV2 plug-in (Linux). They behave the same way; the table
  below lists the differences.
- One input and one output of 1 to 8 channels each; the counts may differ,
  for example 2 in and 6 out for a crossover.
- You choose the filter file itself (an IR file, a `.cfg` or a
  configuration list) with **Choose…** in Convolver's window (the Audio
  Unit, and the VST3 plug-in on macOS and Windows), which also opens a
  Convolver `.vstpreset`, or with the host's file chooser
  (LV2, in hosts that show it); a preset from the host's preset menu is
  the other way ([loading a filter](#loading-a-filter)). The host saves
  the filter inside your project. A read-only Status
  parameter says what is playing, and the
  [Convolver log](#status-and-the-convolver-log) says why a filter failed.
  The Audio Unit and the VST3 plug-in on macOS and Windows also have a
  [window](#convolvers-window) that shows the status, why it is silent and
  what to change, the filter and settings, what plays in plain words, with
  Copy Diagnostics and Show Log; the VST3 plug-in on Linux and the LV2
  plug-in have no window yet. (The Windows window is new in 0.13.12.)
- Convolver adds no processing delay by default: the filtered sound comes
  out as the filter shapes it, with no wait for blocks of audio. (A filter
  can still delay the sound itself, with a delay in its impulse response or
  the phase delay of its filtering.) Every format has [settings](#settings) for
  the gain, the wet/dry mix and the latency.
- A filter that does not fit the host's sample rate or channel counts never
  plays unfiltered audio in its place: a first filter that fails to load
  plays silence, even bypassed, and one that replaces a working filter is
  refused, leaving the working filter playing. A filter that is kept but
  does not fit (a project opened at another rate, say) plays silence too;
  bypass and Mix pass the input only if that filter and the track each have
  as many inputs as outputs (see [bypass](#bypass)).

Contents: [formats](#formats) · [channels](#channels) ·
[loading a filter](#loading-a-filter) ·
[when a filter does not fit](#when-a-filter-does-not-fit) ·
[status and the Convolver log](#status-and-the-convolver-log) ·
[settings](#settings) · [latency and tail](#latency-and-tail) · [bypass](#bypass) ·
[replacing a filter](#replacing-a-filter) · [audio](#audio) ·
[Ardour](#ardour) · [LV2 channel layouts](#lv2-channel-layouts) · [LV2 ports](#lv2-ports) ·
[LV2 filter files](#lv2-filter-files) ·
[identities](#identities) · [Audio Unit licence](#audio-unit-licence)

## Formats

| | VST3 | Audio Unit (AU) | LV2 |
| --- | --- | --- | --- |
| Systems | Windows, macOS, Linux | macOS | Linux |
| How to get it | Preview package, or build | Preview package (macOS), or build | Preview package (Linux), or build |
| Channel counts | Any pairing of 1 to 8 in and out | Any pairing of 1 to 8 in and out | [Fixed layouts](#lv2-channel-layouts) |
| Preset file | `.vstpreset` | `.aupreset` | A `NAME.preset.lv2` folder |
| Libraries it needs on macOS | None beyond macOS: Accelerate for the FFT, AudioToolbox to read the files chosen in its window | None beyond macOS: Accelerate for the FFT, AudioToolbox to read the files chosen in its window | Not on macOS |
| Choose a `.cfg` or IR file directly | [On macOS and Windows](#convolvers-window), in its window | [Yes](#convolvers-window), in its window | [Yes](#lv2-filter-files) |
| Where a failure is explained | The Status parameter and the [Convolver log](#status-and-the-convolver-log) | The Status parameter and the [Convolver log](#status-and-the-convolver-log), also in the macOS system log | The `status` output and the host's log |
| Tail reported to the host | Yes | Yes, in seconds | No |
| [Settings](#settings): gain, gain mode, mix, latency | Yes | Yes | Yes, as control ports ([LV2 ports](#lv2-ports)) |
| Licence | GPL-2.0-or-later | [GPL-3.0-or-later](#audio-unit-licence) | GPL-2.0-or-later |

## Channels

The plug-in has one input and one output. Each has 1 to 8 channels, and the
counts may differ. More channels, or more inputs and outputs, are refused; use
the [command-line tool](command-line.md) for wider filters.

**More inputs than the filter needs.** A filter plays on its own number of
outputs, exactly, and on at least its number of inputs: it reads the first
ones and ignores the rest, whether it comes from a preset, a saved project
or a file chosen in the window. Some hosts give a plug-in as many inputs as
outputs: REAPER gives the VST3 plug-in six inputs on a six-channel track, so a
2→6 crossover reads inputs 1 and 2 and ignores 3 to 6. Status then ends with
"plug-in inputs 3-6 ignored.", and the [Convolver log](#status-and-the-convolver-log)
and the window's Host channels line say the same (`6 in → 6 out (inputs 3–6
ignored)`). Make sure the source plays on
the inputs the filter reads. A filter with more inputs than the host gives,
or with a different number of outputs, is refused. From a configuration
list, an entry with exactly the host's inputs comes first; otherwise the
entry for the host's rate and outputs with the most inputs below the host's
is chosen. The Convolver log (for LV2, the host's log) and the window's
**File, software and log** lines (List entry) name that entry and say why.

Channels are numbered in the host's order, and the `.cfg` routes refer to them
in that order. For VST3 the order follows the speaker arrangement: 5.1 is left,
right, centre, LFE, left surround, right surround, the same order as the WAV
speaker mask `3F`. Any arrangement with the right number of channels is
accepted. In Ardour the channels depend on the track's Pin Mode
([Ardour](#ardour)).

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

Choose the filter file itself: an IR file, a `.cfg` or a configuration list,
from any folder. The plug-in reads it as the
[command-line tool](command-line.md) does, for the rate and channels the
host has set, so a list chooses its entry the same way. The project then
holds a copy of the whole filter: moving or deleting the file afterwards
changes nothing. In the Audio Unit and VST3 the project also keeps the
file's name, with its extension, so the window names the filter after
reopening; it does not keep the file's folder, so a project passed to
someone else does not carry your folder names.

A preset, loaded from the host's preset menu, is the other way in. This is
the one list of what each way reads:

| Way in | Plug-ins | Reads |
| --- | --- | --- |
| **Choose…**, or dropping the file, in [Convolver's window](#convolvers-window) | The Audio Unit, and the VST3 plug-in on macOS and Windows | An IR file, a `.cfg` or a configuration list, or a Convolver `.vstpreset` ([a preset in the window](#a-preset-in-the-window)), with at least its inputs ([channels](#channels)); not an `.aupreset` or LV2 preset |
| The host's file chooser for the **Filter** parameter | LV2, in hosts that show it ([LV2 filter files](#lv2-filter-files)) | An IR file, a `.cfg` or a configuration list, with at least its inputs ([channels](#channels)) |
| The host's preset menu ("Load Preset…" or its preset browser) | Every format | Convolver's preset in that format: `.vstpreset` (VST3), `.aupreset` (Audio Unit) or a `NAME.preset.lv2` folder (LV2), with at least its inputs ([channels](#channels)) |
| A saved project | Every format | The filter and settings saved in it |

### A preset in the window

**Choose…** and dropping a file in Convolver's window also open a Convolver
`.vstpreset`, in the Audio Unit as well as the VST3 plug-in, from any
folder: nothing has to be installed or imported first. It loads as the
host's preset menu loads it. Its filter must suit the host's rate and
channels as any preset's must, and the Gain, Gain mode, Mix and Latency
saved in it apply too; the host is told, so its controls and the saved project show
them. A preset made by `convolver-preset` holds only a filter and leaves
the settings as they are. A preset a host saved at the default
settings (0 dB, Set, 100%, 0 frames) sets them back to the defaults, as the
host's own preset menu does. One saved at the defaults by an earlier
version holds no settings, since that version recorded only settings that
differed from the defaults, so it leaves them as they are; save it again
to have it reset them. Bypass stays as the host has it. Another plug-in's preset,
a damaged file or a preset saved with no filter is refused, and the
message says what to choose instead. The window then names the filter
after the preset file. **Reload**, and **Reload when the file changes**,
read the preset again, which sets the settings back to the
preset's, as choosing it again would.

[Plug-in presets](presets.md) shows how to make a preset with
`convolver-preset`, which reads an IR file, a `.cfg`, a configuration list
or a Convolver `.vstpreset`, and lists the folders where hosts look for
presets. A preset is needed where the plug-in has no window (the VST3
plug-in on Linux, and on Windows up to 0.13.2), and in an LV2 host with no file chooser. In
Audacity 4.0.2 (VST3, on macOS) both routes are checked: its preset menu,
and the window, where a filter chosen with Choose… was kept for exports and
saved projects and one chosen from Recent for an export (up to 0.13.2 it
was not kept) ([in Audacity](first-sound-in-a-host.md#in-audacity)).

The filter is prepared away from the audio, then installed. A filter loaded
before any audio plays starts at once; one loaded during playback fades in
([replacing a filter](#replacing-a-filter)).

**When a chosen file is checked.** In the Audio Unit and the VST3 plug-in, a
file chosen while the host plays audio through the plug-in is checked at
once against the host's sample rate and channels, and one that does not fit
is refused while the working filter keeps playing. Before the host has
played, and while the host has stopped the plug-in (some hosts keep it
running after the transport stops), the host may not have given its real
rate yet (Audacity sets up the plug-in at 44.1 kHz until it first plays,
whatever the project's rate), so a single IR file or `.cfg` is kept, saved
with the project, and checked when the host plays audio through the plug-in.
Until then the window keeps the file's name and Status says "Filter
selected. Waiting for the host's audio settings.", even if the host has
started the plug-in briefly at another rate, as Audacity does at 44.1 kHz to
pass on an edit. Once the host plays, the filter plays if it fits; if not,
it plays silence and Status says "Silent" with the filter's and the host's
rates. Convolver takes that result when its window is open during playback;
the Audio Unit also takes it when the host reads Status. In the VST3
plug-in, the host's Status control does not ask by itself: with the window
closed it may keep saying it is waiting until the window is opened while the
host plays, or a setting such as Bypass changes. If playback stops before
the result is taken, Status may say it is waiting again, and the filter
stays selected either way. A configuration list needs the rate and channels
to choose its entry, so it uses the ones the host last ran the plug-in with;
before the host has run it with them, the list is not loaded, and the window
says: "Play audio once so Convolver can check the host's sample rate and
channel counts, then choose the configuration list again." With Convolver's
window open, you need not choose it again: when the host plays, the window
loads the list itself, choosing its entry for the host's rate and channels,
and Convolver tells the host the project has changed and adds the list to
Recent, as for a chosen file, even if you closed and reopened the window
meanwhile. This is checked by automated tests, not
yet in a host. If the window was closed while the host played, choose the
list again, or play again with the window open. Once Convolver has seen the
host play (for example when the host reads the Audio Unit's Status while it
plays; the VST3 plug-in's Status is not read that way), the
window says "The host's settings are now known (48000 Hz, 2 in, 2 out):
choose the configuration list again." (with the host's values), and Status
says the same in short; a brief start the host makes without playing, such
as Audacity's after a parameter edit, does not count. If the list has
no entry for the rate and channels the host last ran with, it is refused
naming them, and whenever the host is not playing audio through the plug-in
at that moment the advice adds the same step, for a host such as Audacity
that runs the plug-in briefly at 44.1 kHz before it first plays. LV2 knows
its host's rate and channels from the start, so it checks every file at
once.

## When a filter does not fit

A filter, chosen as a file or loaded as a preset, has one sample rate and
fixed input and output channel counts. Each plug-in checks it against the
host:

| Situation | What plays |
| --- | --- |
| No filter loaded yet, equal input and output counts | The input, unchanged (after the latency) |
| No filter loaded yet, different counts | Silence |
| The host gives more inputs than the filter needs, and the outputs match | The filter, from a preset, a saved project or a chosen file, reading the first inputs; the rest are ignored ([channels](#channels)) |
| The first filter does not fit, or its preset is damaged | Silence. VST3 and Audio Unit keep a filter that does not fit and save it with the project: it plays when the host next starts the plug-in at its rate and with its channels, and until then Mix's dry share and bypass pass the input where the host's and the filter's input and output counts are all equal ([Mix](#settings), [bypass](#bypass)). A configuration list with no entry for the host is refused |
| A new filter does not fit while another is playing | The working filter, unchanged; the project keeps saving it, and keeps it when the host restarts the plug-in, even with settings that fit the refused filter |
| The filter is too large to save with the project | As for a filter that does not fit: silence for a first filter, otherwise the working filter |

**Size limit.** The host saves the whole filter in the project, and
Convolver can save at most 64 MiB in it: just under 16,777,216 samples
counting every impulse response in the filter together: a stereo filter with
one impulse response per channel can be about 2.9 minutes long at 48 kHz. A larger filter is refused when it is loaded,
with the reason in Status and the log, so a project never plays a filter it
could not save. Shorten the impulse responses (the end of a long one is
usually near silence), or use the [command-line renderer](command-line.md),
which reads filters of up to 67,108,864 samples.

**Rates that are not a whole number of hertz.** Some hosts run video
projects at a pull-down rate, such as 48000 ÷ 1.001 (about 47952.048 Hz). The
VST3 plug-in accepts such a rate but plays silence, as for any filter made for
another rate, since filters are made for a whole number of hertz and
Convolver never resamples. Status and the log give both rates. The filter
stays loaded, and plays again when the host returns to its rate. Only a
difference far smaller than any real rate change, the rounding of a whole
rate, counts as that rate. The Audio Unit and LV2 plug-ins still refuse such
a rate: the Audio Unit refuses the stream format, and an LV2 host cannot
create the plug-in.

Convolver never falls back to unfiltered audio when a filter fails: a room
correction or crossover that quietly plays unfiltered audio can be worse than
an obvious failure, and could send full-range audio to a tweeter.

**A project opened at another sample rate or layout** plays silence. What
happens next depends on the format:

- **VST3 and Audio Unit** keep the filter. Set the host back to the filter's
  rate and layout and the filter returns; saving the project in between keeps
  it. Some hosts briefly try a default 44.1 kHz before setting the real rate,
  or, like REAPER, start the plug-in on stereo buses before giving it the
  track's channels; that does not count. A preset or file that does not fit
  is refused only while another filter that fits is playing, so that one
  stays; the refusal says so, and restarting playback does not switch to
  the refused filter, even with settings that fit it. Otherwise the new
  filter is kept, saved with the project, and plays silence (Mix's dry
  share and bypass still pass the input where the counts are equal) until
  the host starts the plug-in with its rate and channels. Status reads
  Waiting and names what the filter waits for (`Silent, waiting for 44100
  Hz: the host runs at 48000 Hz.`), since nothing was refused; the log says it waits for audio settings that suit it, and
  the window says it plays when the host starts the plug-in with them. A
  configuration list with no entry for the host's rate and channels is
  refused, since there is no entry to keep.
- **LV2** refuses the filter as soon as the session opens, and `status` shows
  Load failed. **Do not save the session**: saving records the failure and
  drops the filter. Close it without saving and reopen it at the filter's rate,
  or load the filter again.

**Saving after a first filter failed** saves the failure, so reopening the
project plays silence again rather than unfiltered audio. Choose or load a
matching filter to replace it. In VST3 and Audio Unit this applies to a
damaged preset or file; a filter that only does not fit is kept and saved,
as above.

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
parameter and the Convolver log; on macOS, and the VST3 plug-in on Windows,
also in [Convolver's window](#convolvers-window).

**The Status parameter** is a read-only parameter that hosts can list with the
plug-in's controls, for example in their generic plug-in window or parameter
list. It never changes the sound, is never automated, and is not restored
from saved projects. Where the host shows parameter text, it reads, for
example (some hosts show only the status name in the first column):

| Status | Example text |
| --- | --- |
| Active | `Filter loaded: 48000 Hz, 2 in, 2 out.` |
| No filter | `No filter: audio passes unchanged. Choose a filter or load a Convolver preset.` (without a window, on Linux: `… Load a Convolver preset.`) |
| Load failed (silence) | `Silent: the plug-in could not start the filter. Details in the Convolver log.` (or, for a damaged first preset or file, what is wrong with it) |
| Load failed (previous filter kept) | `Load failed, previous filter kept: the filter has 2 in, 6 out, the host 2 in, 2 out. Details in the Convolver log.` |
| Waiting | `Filter loaded: 44100 Hz, 2 in, 2 out; waiting for audio settings from the host.` (for a file chosen in the window: `Filter selected. Waiting for the host's audio settings.`, see [when a chosen file is checked](#loading-a-filter)) |
| Waiting (a kept filter that does not suit the host) | `Silent, waiting for 44100 Hz: the host runs at 48000 Hz. Details in the Convolver log.` (`Silent, waiting for 2 in, 6 out: the host has 2 in, 2 out. …` for the channels) |
| Fault | `Fault: output too large, silenced and the filter restarted. Details in the Convolver log.` |
| Bypassed | `Bypassed: the input plays unfiltered.` (`Bypassed: silent, as the filter's inputs and outputs differ.` for a filter with different input and output counts, such as a crossover) |

**Active** means a filter is loaded and the host has started the plug-in at
a rate and channel layout the filter suits. It does not follow the host's
transport: a paused or stopped project still reads Active, as the filter is
ready to play the moment audio arrives. **Bypassed** shows while the host's
bypass (or, in VST3, the window's Bypass checkbox) is on and a filter is
loaded; a problem (Load failed, Fault) still shows first. A host that
switches the effect off altogether, rather than bypassing it, may route the
audio around Convolver and stop calling it, so the status can stay as it
was; what you hear then is the host's doing.

The text also follows the [settings](#settings) when no filter
is heard. With no filter loaded and Gain or Mix changed, it reads, for
example, `No filter: audio passes at -20.0 dB (Gain and Mix).`; when a
kept filter does not fit and Mix is below 100%, `Only the dry signal plays,
waiting for ...` instead of `Silent, waiting for ...`, as the dry share
still plays (only where there is a dry signal: see [bypass](#bypass)).
While bypassed it still shows a failure, or what a kept filter waits for,
but says what bypass plays, the input unchanged where there is a dry
signal: a kept filter that does not fit reads `Bypass is on: only the dry
signal plays. Waiting for 44100 Hz: the host runs at 48000 Hz.`, and with
no filter loaded it reads `No filter. Bypass is on: audio passes
unchanged.` whatever Gain and Mix are. Where
bypass plays silence, after a failed load with no filter kept or when the
filter's or the host's inputs and outputs differ, it still says so. The
window's report and the Convolver log say the same: with bypass on they
say what bypass plays, not that the filter plays. A
filter at Mix
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

**Waiting** means a filter is loaded but the host has not sent its audio
settings (sample rate and channels) yet, usually because it has not started
the plug-in; pressing Play is not needed. Once it sends them, the status
becomes Active if the filter fits. If it does not, the filter is kept and
the status stays Waiting, with what the filter waits for (`Silent, waiting
for 44100 Hz: the host runs at 48000 Hz.`), until the host starts the
plug-in with settings that suit it; the status is plain, not red, as
nothing was refused. Load failed always means a request was refused or a
filter could not start. Some hosts show a separate copy of the
plug-in in its window, not the one that plays. Audacity's real-time effects
window is one: its copy usually reads Waiting, but Audacity may start it briefly at
44100 Hz, whatever the project's rate, and then it can read `Silent,
waiting for 48000 Hz: the host runs at 44100 Hz` while the playing copy is
fine. When the window and the sound disagree, trust the log: each copy has its
own number (`VST3 #1`, `VST3 #2`), and the one playing logs `Started at` with
the rate the host plays at.

**Settings that do not play as chosen.** Status also says, after its own
words, when a setting you chose does not play as chosen: `Latency 1024
frames not in use yet.` when the Latency you chose has not been applied (the
log says why, if it could not be), `No estimate: No clipping's gain plays.`
or `Too loud to estimate: No clipping's gain plays.` when the Gain mode is
Estimate and the filter has no estimate, and `Silent: no gain figure for
this filter.` when the filter has no figure for the Gain mode at all (`No
gain figure for this filter: only the dry signal plays.` when Mix lets the
dry signal through). A load failure, a fault or a kept filter's wait
comes first, and the window's report gives the details. With Gain mode at
Estimate or No clipping, Status also gives the gain that mode plays
(`Filter loaded: 48000 Hz, 2 in, 2 out. Estimate plays -7.3 dB.`), for
hosts that show only their own controls, where Gain's value is not what
plays. LV2's `status` port keeps its five values (see
[LV2 ports](#lv2-ports)): Bypassed reads as Active, and while a new Latency
is applied or the estimate is found the filter plays on and the port stays
at Active; the host's log says what is happening.

**What Status leaves out.** In the VST3 and the Audio Unit nothing says that
a filter is still loading: the earlier status shows until it is done, as
these formats load on the host's own thread. Some things no plug-in
can see: a host that stops calling Convolver instead of bypassing it (see
Active above), a separate copy the host makes for export or playback (see
Waiting above), and whether the host applied the latency Convolver
reports. If a track sounds late after a Latency change, stop and start
playback, and check that the host's delay compensation is on.

**The rate a host plays at can differ from the rate it exports at.** A
filter for another rate than playback's is refused during playback even
when an export at the filter's rate works. Audacity 4 plays real-time
effects at the rate in *Audio setup › Audio settings › Default sample rate*
(44100 Hz unless changed). Check the playing copy's `Started at` line in
the log for the rate it runs at (in Audacity the window's Host line
can belong to a copy that does not play), and set the host's playback rate
to the filter's. Every refusal for the rate ends its Fix with "The host may
play at a different rate from the one it exports at.", wherever the Fix is
shown: the window, Status details and the Convolver log, or LV2's host log.

**The Convolver log** is a text file with one line for each filter loaded,
each time the host sends its audio settings (the sample rate, channels and block
size), and each failure, explained in full with what to change. Each line
starts with the time, the Convolver version, the format and instance (`VST3 #2`),
the host program and its process number. Loads and refusals are numbered
(`Request 3:`), so you can follow a sequence of presets. A failure keeps to
one line: what plays instead, the problem, then its `Cause:`, `Fix:` and
`Note:`, as the window shows them, for example:

```text
Request 2: Not loaded; the previous filter keeps playing: the filter is for 44100 Hz, and the host runs at 48000 Hz. Cause: Convolver never resamples, so a filter plays only at the rate it was made for. Fix: Load a filter made for 48000 Hz, or set the host's project or audio device to 44100 Hz. The host may play at a different rate from the one it exports at.
```

The VST3 plug-in
also logs host settings it cannot use, with the reason: a sample rate outside
1 to 768000 Hz, a block size or sample type it does not take, or
(once for each instance, as hosts try several) a channel layout with more than
8 channels or more than one bus. A save that fails is logged in every format,
since some hosts do not say so: the project or preset saved then does not
hold the filter. The first line each
time a host starts Convolver says what is running: the version, the FFT
library, the CPU kernels, the plug-in SDK it was built with (VST3 and Audio Unit) and the process. When something does not work, send
this file with your [report](report-a-problem.md).

If the file cannot be written (its folder cannot be created or written to, or
there is no home folder), the Status text ends with "The log could not be
written." instead of pointing to it, and the lines go to the
host's standard error output instead (and, on Windows, to the debugger output,
which DebugView shows). On macOS they are in the system log either way.

| System | Log file |
| --- | --- |
| macOS | `~/Library/Logs/Convolver/Convolver.log` (the Console application lists it under Log Reports) |
| Windows | `%LOCALAPPDATA%\Convolver\Convolver.log` |
| Linux | `~/.local/state/convolver/convolver.log` (or under `$XDG_STATE_HOME`) |

On macOS the same lines also go to the system log, which also works when a
sandboxed host such as GarageBand keeps the file inside its own container
(`~/Library/Containers/<host>/Data/Library/Logs/Convolver/`).
[Report a problem](report-a-problem.md#2-collect-convolvers-messages) gives
each system's commands to copy the log, or the system log's recent lines,
for a report.

When the file passes 1 MB it is renamed with `.old` added and a new one is
started. Setting the environment variable `CONVOLVER_LOG` to a file path
sends the log there instead; on macOS and Linux, setting it empty turns the file off (the Status text
still points to the log; on macOS the lines are in the system log). LV2 has
no file of its own: it writes to the host's log, including why it refused to
start at the host's sample rate and when a save failed.

### Convolver's window

The Audio Unit, and the VST3 plug-in on macOS and Windows, offer hosts a
window of their own (`auval` lists the Audio Unit's as a Cocoa view). Hosts that show plug-in
views open it from the plug-in's slot; if a host shows its generic sliders
instead, choose the plug-in's own view in the plug-in window, where the host
offers the choice. GarageBand opens it from the plug-in's button in the
track's Plug-ins area; REAPER and Audacity open the VST3 plug-in's window
when you add the effect or click its name in the effects list. Both formats
show the same window, and the VST3 plug-in on Windows shows it too, with the
differences listed after the description.
From the top, the window has:

- **A title line** with the Convolver version (the release number; the
  Software line under File, software and log also names the source revision
  of a build from a Git checkout), the instance (`Audio Unit #2`,
  as in the log) and the program that runs the plug-in, or "the host's
  plug-in process" when the host runs plug-ins in Apple's separate service,
  as GarageBand does (the Software line names that process). The VST3
  plug-in's Bypass checkbox sits at its right-hand end.
- **The filter's name** as the heading: the name of the file that plays,
  with its extension (`Living room 48k.cfg`), so the kind of filter shows
  too. A long name loses its middle, never its extension; hold the pointer
  over it for all of it. The project and the host's presets keep the name
  with the filter (the Audio Unit as its preset name, VST3 in its
  controller state), so it is still there after reopening the project, but
  not the file's folder: see [loading a filter](#loading-a-filter). A
  VST3 preset made with [`convolver-preset`](presets.md) keeps the name of
  the file it was made from in the same way. A project saved by an earlier
  Convolver, or a preset that holds no name (such as a VST3 preset made by
  an earlier `convolver-preset`), shows "Restored filter", unless it holds
  the same filter that already plays, which keeps its name. With no filter it reads
  "None". Under the name, the filter's own facts as labelled columns:
  **Sample rate**, **Channels** (`2 in → 2 out`), **Length** and, when the
  filter names them, **Speakers** (cut short with "…" if they do not fit;
  hold the pointer over them for all of them). The Audio section further
  down gives the host's values, which need not be the same: a filter made
  for 2 inputs on a host track with 6, say. Under the facts, in
  small text, a note when the newest file was refused (`broken.wav was
  refused.`, also while a kept filter waits for settings that suit it);
  otherwise nothing.
- **The status** beside the name (Active, Bypassed, No filter, Load failed,
  Waiting or Fault), the same as the Status parameter, as a symbol
  and a word, coloured for Active (green), Load failed (red) and Fault
  (orange). Waiting is plain, even with a kept filter that does not suit
  the host, as nothing has gone wrong.
- **The message**, under the heading, only when there is something to say.
  A problem sits in a box tinted with the status colour; Bypassed's line
  ("The input plays unfiltered. Turn bypass off to hear the filter.", or
  that the outputs are silent when there is no dry signal, as with a
  crossover; see [bypass](#bypass)) and a note are plain text. It explains as the log does, in parts:

  | Part | What it says |
  | --- | --- |
  | Problem, in bold | What went wrong, naming the file or the values that differ |
  | What plays | What you hear now: the previous filter, silence, or the dry signal Mix lets through |
  | Cause | Why, in the host's terms |
  | **Fix** | What to do, the most likely fix first |
  | Note | In smaller text: the host or design limit behind it, and when the status clears |

  When a button in the window carries out the fix, it is highlighted in the
  accent colour: **Choose…** when a different filter puts it right, **Copy
  Diagnostics** for a fault in Convolver itself.

  A refused file is explained first, as the latest thing you did, and
  "What plays" then names the filter in the heading once: `room.cfg keeps
  playing.` while the host plays it, `room.cfg is still selected.` after
  Stop or while bypassed, and `room.cfg is still selected and will play
  at 48000 Hz.` when that filter is kept but does not suit the host. So a
  broken file chosen while a 48 kHz filter waits in a 44.1 kHz project
  reads "**broken.wav could not be read …**", then that sentence, not a fix
  for the rate.

  For example, a stereo preset on a GarageBand mono track reads "**The saved
  filter has 2 inputs and 2 outputs, and the host gives the plug-in 1 input
  and 1 output.** Convolver plays silence.", then Cause: "A filter plays only with
  the channel counts it was made for.", **Fix**: "Give the plug-in 2 inputs
  and 2 outputs in the host's track or routing settings, or load a filter
  made for 1 input and 1 output." (with Choose… highlighted), and Note:
  "Hosts run an effect with one channel on a mono track (GarageBand does):
  put Convolver on a stereo track instead." While a filter simply plays,
  there is no message: the heading and the Audio section say what plays.
  With no filter, Fix says what to load, and that a file can be dropped on
  the window, and Choose… is highlighted.
- **Choose…**, **Recent**, **Reload** and **Reload when the file changes**,
  on one row under the message.
  Choose… opens the file dialog for an IR file, a `.cfg`, a
  configuration list or a Convolver `.vstpreset`
  ([a preset in the window](#a-preset-in-the-window)); dropping the file anywhere on the
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
  file, and Convolver tells the host the project has changed. What the
  host does with that differs: most mark the project unsaved. A host that
  cannot be told (Audacity 4.0.2) is shown a change of Latency that keeps
  its value instead, so that it stores the new filter; this is checked in
  Audacity 4.0.2 on macOS ([in Audacity](first-sound-in-a-host.md#in-audacity)).
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
  denied. A preset carries the filter inside it, so it does not depend on
  the sandbox letting Convolver read the original files; its rate and
  channels must still fit.

  Beside Choose…:

  - **Recent** lists the filter files last loaded in a Convolver window, in
    any host or format (up to ten, newest first, each with its folder's
    name; hold the pointer over one for its full path), leaving out files
    that are no longer there. Choosing one loads it as Choose… does;
    **Clear Menu** empties the list. The list is a small text file,
    `recent-filters.txt`, in the Convolver log's folder, never in the
    project. A sandboxed host may refuse to read a file from the list that
    it allowed through the file dialog; the explanation then says
    permission was denied, and Choose… works.
  - **Reload** reads the file again, for example after changing it in DRC,
    REW or a text editor. It works for a file loaded in the window since the
    host opened the project: a reopened project saves the filter, not the
    file, so Reload is dimmed until a file is chosen again.
  - **Reload when the file changes** (ticked when the window opens) does
    the same by itself while the window is open, even behind other windows:
    once a second it checks the file and, for a `.cfg` or list, the files it
    names that make up the filter that plays (for a list, the lists and
    `.cfg` files it reads and the IR files of the entry chosen, not those
    of other entries, which need not be installed), and reloads once a
    change has stayed the same for a second. That makes it less likely to
    read a file still being written, but cannot rule it out: a program that
    pauses while writing, or writes a filter's files one after another, can
    still be caught part-way. A script or tool that writes filters for
    Convolver should write each file under a temporary name in the same
    folder and then rename it over the old one, so the file is only ever
    seen whole, and rename all of a filter's files at the end, together.
    A file that has gone (moved or deleted)
    is not reloaded; the filter that plays stays, as it does when a reload
    is refused (say, a `.cfg` that names a file not written yet): press
    Reload once the file is ready. A filter the host has restored since
    (a preset, an undo) is not replaced by a reload. Each reload tells the
    host the project has changed, as choosing the file does.
- **Gain** and **Mix**: a slider, the value in a box beside it and its unit.
  Type a value in the box and press Return (or move to another control) to
  set it exactly; a value beyond the range stops at its end. Option-click a
  slider to put it back to its default (0 dB, 100% wet), as in Logic.
  Moving or resetting a slider drops a value you had not finished typing
  in its own box.
  Leaving a box without changing it sets nothing, so the rounded value it
  shows does not replace a more exact one, or one the host has set since.
  Mix is labelled Dry and Wet at its ends. When there is no dry signal
  (a crossover, say: see [bypass](#bypass)) Mix has no effect, so its
  label and slider are dimmed and a note under them says why. A host that shows
  Convolver's window shows it instead of its generic controls, so the
  settings are here. Moving a slider changes the setting and tells the
  host, and the sliders follow changes the host makes (automation, its own
  controls), except the one you are dragging. In the Audio Unit the window
  does not yet mark the start and end of a drag, so a host recording
  automation from it may record each step separately; not yet tried in
  GarageBand.
- **Latency**: a menu of delays in frames, from `0 frames (none)` up, with the delay
  a choice adds at the host's rate beside it ("adds 21.3 ms"). A new choice
  plays at once; if it cannot (the computer is out of memory, say), the old
  one keeps playing, the row says the choice is "not in use yet" (the
  message does not repeat it) and the Convolver log says why
  ([settings](#settings)). While the host has the plug-in stopped, the row
  says the choice is "selected for the next start". The host is told the
  delay, so it can delay
  other tracks to keep them in time.
- **Audio**: the host's values and what plays, each a short value. The
  filter's own rate, channels, length and speakers are under its name, so
  these lines say what the host gives, which need not be the same. Hold the
  pointer over a line for what it means.

  | Line | What it says |
  | --- | --- |
  | Host | When the host's rate and channels suit the filter, one line: `48000 Hz, 2 in → 2 out ✓ suits the filter`. Before the host has sent its audio settings: `not known yet` |
  | Host sample rate, Host channels | Otherwise a line each: the rate the host runs the plug-in at, and the inputs and outputs it gives it (`1 in → 1 out`) |
  | Routing | With a filter: which input feeds which output (`In 1 → Out 1`), with route gains such as `×0.5`; the first four paths, then how many more |
  | Ring-out | How long the output goes on after the input stops (the tail), at the host's rate; only when it says more than the filter's Length: delays in the filter, a Latency, or another rate |
  | Worst-case peak | The loudest output any full-scale input could make through the filter, and the Gain that rules out clipping (or that it won't clip at the current Gain and Mix) |
  | Host block size | The largest block the host sends at a time |

  A line that does not match (a filter for another rate, say) is marked with
  a warning sign and says what differs, in red: `44100 Hz ⚠ the filter is
  for 48000 Hz`.
- **File, software and log**, folded away under a disclosure triangle: the
  filter file's path and request number (and a refused file's, and the
  list entry chosen for a wider input bus, [channels](#channels)), the software
  (version, FFT library, CPU kernels, the plug-in SDK, and what reads the
  filter files), and where the log is. On the same row:
  - **Copy Diagnostics** puts all of this, with the settings and this
    instance's recent lines from the Convolver log, on the clipboard as plain
    text, ready to paste into a report.
  - **Show Log** selects the log file in the Finder (in a sandboxed host, the
    copy inside the host's container).

The text can be selected and copied. The controls are standard macOS ones
with names for VoiceOver, which has not been tried with the window yet, and
they follow the system's light or dark appearance. The
window updates twice a second while it is on screen. While the host restores
a project or preset on another thread, it shows what plays until the
filter is ready.

**In VST3**, on macOS and Windows, the window behaves as
above, except:

- The title line names the instance as the log does (`VST3 #2`).
- Dragging a Gain or Mix slider is one change for the host's undo and
  automation, from mouse down to mouse up, as the VST3 SDK asks.
- An accepted file is reported to the host as a change to the project;
  hosts differ in what they do with it (see above).
- A **Bypass** checkbox at the end of the title line is the plug-in's bypass: ticking
  it is the same as the host's own bypass button, which it follows, and the
  host's undo and automation record it. The Audio Unit's window has none,
  since Audio Unit hosts show their own bypass button.
- The filter's name after reopening comes from the controller state, and
  is shown only while the filter saved with it plays: a preset or an undo
  that brings another filter shows "Restored filter". A refused preset
  leaves the playing filter's name, on screen and in the saved project,
  although the host sends that preset's controller state too. A
  preset made by `convolver-preset` holds a controller state too, naming
  the file it was made from.
- Bypass changed in the window or by the host shows as Bypassed at once,
  before audio next runs.
- The window grows when the file, software and log lines are shown, and
  asks the host to make its window fit; the user cannot resize it.
- The software line names macOS AudioToolbox as the reader of filter files,
  as in the Audio Unit. Not yet tried in REAPER or Audacity.

**On Windows** (new since 0.13.2) the VST3 window is built from
Windows' own controls, with the same parts in the same order, and behaves
as the VST3 window above, except:

- Gain and Mix are trackbars: drag one, or use the arrow keys (1 dB or 1%
  a step) and Page Up and Page Down (10). A drag is one change for the
  host's undo; each key press is one change. There is no Option-click
  reset: type the default (0 or 100) in the box.
- Recent is a split button; either part opens the list. The file dialog is
  Windows' own, and the host's window waits while it is open.
- The status is a dot and a word, the dot filled in the status colour; the
  word always says it. Text in the message and in the file, software and
  log lines can be selected and copied; other labels cannot.
- Tab and Shift+Tab move between the window's controls; some hosts take
  keys for themselves first, which has not been tried yet.
- Screen readers (Narrator, NVDA) read each control's name through UI
  Automation, which CI checks; nobody has tried a screen reader with the
  window yet. The window follows the screen's scaling, and uses only the
  system's colours in high-contrast mode; it stays light in a dark host,
  since Windows' standard controls have no dark style.
- Show Log selects the log in File Explorer. The software line names
  libsndfile as the reader of filter files: WAV, RF64, W64, AIFF, CAF and
  headerless files named in a `.cfg`; the packages' reader does not
  read FLAC.
- Not yet tried in any host: CI opens it in a test host and keeps a picture
  of it.

## Settings

Every format has four settings, which hosts show with their other
parameters (in LV2, as the controls `gain`, `gain_mode`, `mix` and
`latency_frames`), and Convolver's window shows as sliders and menus (in
GarageBand, the window replaces the generic controls). Their
defaults play exactly as Convolver did before the settings existed.

| Setting | Values | Default | What it does |
| --- | --- | --- | --- |
| **Gain** | −100 to +24 dB | 0 dB | Scales the filtered signal, as the command line's `--gain-db` does. Negative values make room for a filter that boosts some frequencies; positive values make up level. |
| **Gain mode** | Set, Estimate or No clipping | Set | Which gain plays. **Set** plays Gain. **Estimate** plays the filter's own estimate of how much to turn it down, found by playing noise through it, as the command line's `--gain-db auto`; music rarely clips at it, but it can. **No clipping** plays the gain at which no input within full scale can clip (up to the float rounding of the convolution), found from the filter's taps as `--gain-db no-clip`; it is usually quieter than Estimate, and louder than 0 dB for a filter that can only make input quieter. It is not a limiter: input above full scale, such as floating-point audio another plug-in makes louder than 0 dBFS, can still clip. Each filter plays at its own figure, so loading another filter changes the level to suit it, and Gain keeps its value for Set. Beside the menu, the window shows the gain the mode plays. |
| **Mix** | 0 to 100% | 100% | The share of filtered sound; the rest is the input, delayed by the latency so the two line up. Whenever bypass plays silence there is no dry signal, so the mix stays at 100%: when the filter's input and output counts differ, and in the cases [bypass](#bypass) lists when no filter plays. Where bypass passes the input, the input share of Mix still plays while a kept filter is silent. |
| **Latency** | 0, 64, 128, 256, 512, 1024, 2048 or 4096 frames | 0 frames | How long Convolver delays the audio, in frames (samples per channel): at 48 kHz, 256 frames is 5.3 ms. At 0 Convolver adds no processing delay; any delay in the filter itself still plays. A larger value uses less processor time, most of all for short filters. It is the command line's `--block-size`. A change during playback leaves a short gap; see [latency and tail](#latency-and-tail). |

- In a host's own controls (its generic window or parameter list), a
  Latency value reads as its frames, with `0 frames (none)` for 0, and in
  VST3 and the Audio Unit, once the host has started the plug-in, the time
  it adds at the host's rate: `256 frames (5.3 ms)`. Where a VST3 host lets you type a value, type one of
  the menu's frame counts (`256`); another number is refused. VST3 hosts with narrow controls show
  the short names Gain, Mode, Mix, Lat, Bypass and Status.
- Hosts may leave settings that cannot be automated out of their own
  controls: Ardour does so for the VST3 plug-in's Gain mode and Latency
  ([Ardour](#ardour)).
- Gain and Mix changes fade over 1.3 ms (64 samples at 48 kHz, 128 at
  96 kHz), so automating them does not click. Latency cannot be automated,
  except in LV2 hosts built on JUCE, which automate it anyway; each change
  leaves a gap ([latency and tail](#latency-and-tail)).
- **Gain mode** cannot be automated either. A change fades like Gain.
  Estimate needs a moment for a long filter (about half a second for a
  10-second stereo filter): until it is ready, Estimate plays the No
  clipping figure, then fades to its own. A filter too loud for an estimate
  plays at its No clipping figure, and the log and window say so. In LV2,
  without the host's worker thread, Estimate plays the No clipping figure
  until the host next starts the plug-in. An LV2 preset saved before this
  setting existed has no Gain mode, so loading it leaves the mode as it is.
- The host saves the settings in the project and in presets it saves itself.
  A preset made by `convolver-preset` holds the filter only, so loading it
  leaves the settings as they are. In VST3 and the Audio Unit, the
  [Convolver log](#status-and-the-convolver-log) lists the settings each
  time the plug-in starts, and again when it restores saved settings (some
  hosts, GarageBand among them, start the plug-in before restoring the
  project).
- **Projects from earlier versions.** Earlier versions had no 0 in the
  Latency menu and used 256 frames by default. A project or preset saved
  with one of them opens at the Latency it was saved with, except
  sometimes at 256, the old default:
  - **VST3:** a project saved with Gain, Mix and Latency all at their
    defaults saved no settings, so it opens at the new default, 0 frames.
    One where Gain or Mix had been changed saved its Latency too, and opens
    at 256.
  - **Audio Unit:** 256 always opens at 0 frames, even if you chose it on
    purpose, because the Audio Unit's saved state cannot tell the two
    apart.

  Choose 256 again if you want its delay back, then save. With delay
  compensation on, the host lines the audio up either way.
- **Settings and other formats.** Each format saves its settings its own
  way, so a filter moved between them (a VST3 preset opened as an Audio
  Unit, say) keeps the filter but not the settings: it plays at 0 dB, fully
  wet, with no latency. The same project can therefore sound different in
  another format.
- **LV2 hosts** save the four controls in the session themselves, as they
  save `enabled`. A session saved before the controls existed opens at the
  defaults. A new Latency plays a moment after you choose it, once the
  host's worker thread has prepared it, and the latency output reports it
  from then on. Without that thread, it plays only from the next time the
  host starts the plug-in.
- **VST3 hosts** mark Mix as the plug-in's dry/wet control where they have
  one. A new Latency plays at once, and Convolver tells the host the new
  delay. A
  project saves its settings even at the defaults, so it reopens with them
  whatever later versions choose as their defaults. Every published
  Convolver preview from 0.13 on reads them. The first preview, 0.12.12,
  had no Latency 0, so it refuses a project saved at Latency 0 (the
  default) as damaged, plays silence and says so in the log; a build from
  before the settings existed refuses any such project as a newer state.
  A project saved with Gain mode Estimate or No clipping needs a build with
  the Gain mode: earlier builds refuse it as a newer state. The Audio Unit
  and LV2 plug-ins of earlier builds have no Gain mode either, but they
  play such a project at its Gain setting instead of refusing it, which can
  be louder.

## Latency and tail

Convolver adds no processing delay by default, whatever the host's
buffer size: the engine sums a filter's first taps directly, so its output
starts with the input. Delay that the filter itself contains (a delay in
its impulse response, the phase delay of a crossover or a room
correction) still plays, as it would in any convolver. The [Latency setting](#settings)
can add one, from 64 to 4096 samples, and the plug-in reports it so the host
can line the audio up.

Zero added delay costs some processor time. On one Apple silicon Mac
(November 2026), Latency 0 used about 16% more processor time than 256 for
a 30-second reverb, and nearly twice as much for a short filter of 4,096
taps. A short filter costs little either way, so the difference matters
only when the processor is busy. Other machines and filters give other
figures. Keep 0 when you play, sing or monitor through the plug-in, where
any delay is heard. When you only play back recorded tracks and need the
processor time, a larger Latency can save some. The host's delay
compensation then keeps the tracks lined up, but it cannot remove the delay
you hear when you monitor live input through the plug-in.

A new Latency plays at once (in LV2, a moment later, once the host's worker
thread has prepared it), and the host is told the new delay. Changing it during playback leaves a gap in the
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
as Steinberg's own tests measure it, so it adds the latency: an instance
with no filter reports 256 samples at a Latency of 256, because its output
still carries the last 256 samples of input after the input stops. After a filter is replaced during
playback, both keep reporting the longest tail used until the host stops the
plug-in, and then report the filter's own, as the window's Ring-out row
does (shown when it says more than the filter's Length). LV2 cannot report a tail, so hosts that stop processing at the end
of a region may cut it off: render with a little silence after the audio.

## Bypass

The host's bypass switch fades over 1.3 ms to the input, delayed by the
latency so nothing jumps in time, and back. The filter keeps running
underneath, so switching back loses nothing.

When the filter's input and output counts differ, bypass fades every output
to silence instead: no output has an unfiltered counterpart, and bypassing a
crossover must not send full-range audio to a tweeter. What counts is the
filter, not the track: a 2→6 crossover on a six-channel REAPER track is
bypassed to silence, and a 2-in, 2-out filter given six inputs and two
outputs bypasses to inputs 1 and 2. Mix has no effect when the filter's
counts differ: the sound stays fully filtered at any Mix.

When no filter plays, bypass and Mix pass the input only where Convolver can
tell that is safe:

| Situation | Bypass and Mix |
| --- | --- |
| No filter loaded yet, on a track with as many inputs as outputs | Pass the input |
| A filter is kept but does not suit the track (for example another sample rate) | Pass the input only if the filter and the track each have as many inputs as outputs. A kept 2→6 crossover stays silent |
| A load failed and no earlier filter is kept: a missing or unreadable file or preset, a first filter that does not fit, or a project saved after such a failure | Silent: Convolver cannot tell that the failed filter was not a crossover |
| The track's inputs and outputs differ | Silent |

The same applies to a mono instance (one input, one output). An instance
with no filter passes its input until a load fails; then it stays silent
until a filter loads, or until an empty state (the host's reset to
defaults) is restored. If a replacement fails, the previous filter keeps
playing with its own bypass and Mix. Status says which applies at Mix
below 100%: after a failure, `Silent: ...` means Mix passes nothing either.
At Mix 100%, and for bypass, the Convolver log's `Settings:` line and the
window's copied diagnostics say whether there is a dry signal, and why not,
such as `mix 100% wet (a load failed and no filter is kept, so there is no dry
signal)`.

This is Convolver's own **Bypass** (the host's bypass button where it uses
it). A host that takes the effect out of the signal path altogether, such as
switching it off or offline, may route the track's audio around Convolver,
unfiltered, to its outputs; Convolver cannot prevent that. With a crossover,
check in the host which kind of bypass its button uses before relying on it.

VST3 saves bypass with the filter. Audio Unit and LV2 hosts save and restore
the bypass switch themselves (in LV2 it is the `enabled` control), so loading
a preset leaves the switch as it is.

## Replacing a filter

Loading a preset while audio plays prepares the new filter in the background,
lets it run for the latency (none by default), then crossfades to it
over 5.3 ms (256 samples at 48 kHz). The new
filter starts with no history, so it cannot filter audio that played before
it was loaded. When the host stops and restarts processing, a pending filter
is installed at once.

A preset, or an undo, that holds the filter already playing, with Latency
unchanged, and changes only Gain, Mix or the Gain mode keeps the filter and
what it is playing: the new settings ramp in over 1.3 ms and the reverb tail
rings on. A different Latency still rebuilds the processing and starts the
filter afresh. Choosing the playing filter's file again, in the VST3 or
Audio Unit window or through an LV2 host's file choice, keeps it the same
way, unless the file's contents have changed. Reopening a
project, or a host stopping and restarting the plug-in, starts the filter
afresh.

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

## Ardour

Tried with Ardour 8.4 and 9.0. What Convolver gets in Ardour depends on the
track's **Pin Mode**. A track added in Ardour's window is **Strict-I/O**
by default (the preference **Use 'Strict-I/O' for new tracks or busses**)
unless you choose **Flexible-I/O** as its Pin Mode in the Add Track
dialog, or untick **Strict I/O** in the menu of the track's name button in
the mixer before adding Convolver. The Convolver log's `Started at` line
names the channels Ardour gave it
([Status and the Convolver log](#status-and-the-convolver-log)).

- **Strict-I/O track (Ardour's default).** Ardour keeps the track's channel
  count through its plug-ins, so Convolver gets as many outputs as the
  track has inputs. A mono filter on a mono track and a stereo filter on a
  stereo track play, in the VST3 and the LV2 plug-in. With the VST3 on a
  mono track, Ardour 9's log first says `Refused the host's channel
  layout: it has 0 input and 0 output channels`; the `Started at` line
  after it, ending `1 out`, is what plays. A filter with more
  outputs than inputs, such as a 2→6 crossover, cannot play on such a
  track: use a Flexible-I/O track and the LV2 plug-in.
- **Flexible-I/O track, LV2 plug-in.** Add the
  [layout](#lv2-channel-layouts) that matches the filter, such as 1→1 for
  mono or 2→6 for a crossover, and choose the filter with its
  [Filter parameter](#lv2-filter-files). The mono, stereo and 2→6 layouts
  play as they do elsewhere.
- **Flexible-I/O track, VST3 plug-in.** Ardour gives the VST3 plug-in 2
  inputs and 2 outputs whatever the track has, feeding a mono track's
  channel to both inputs, so a filter with other outputs, such as a mono
  filter or a 2→6 crossover, plays silence. On a mono track, Ardour 9 asks
  for one channel, but the request reaches Convolver as a layout with no
  channels and the log says `Refused the host's channel layout: it has 0
  input and 0 output channels`. A mono filter plays once you set the
  channels by hand: right-click Convolver, choose **Pin Connections…**,
  click **Manual Config**, and set **Audio Out** to 1 with its − button;
  leave Ardour 9's **Audio Input Pins** as they are. The log's next
  `Started at` line then ends `1 out` (with `2 in` on Ardour 8, where
  Convolver plays the first input and ignores the second). A mono filter
  chosen before that, while Convolver still has 2 outputs, waits: Status
  says Silent with the reason, and it plays once that `Started at` line
  shows 1 output (checked by automated tests). A filter chosen while
  another one plays is still refused: choose it again after the channels
  change. These steps are not yet confirmed in Ardour's window.
  A crossover cannot be set up this way: Convolver still gets only 2
  outputs.
- **Settings.** Ardour leaves Gain mode and Latency out of its own controls
  for the VST3 plug-in, because they cannot be automated; Gain and Mix are
  there. On macOS and Windows, Convolver's window has all four (not yet
  tried in Ardour). On Linux the VST3 plug-in has no window, so Ardour's
  controls cannot change those two: they keep what the plug-in's saved
  state or preset gives them, or their defaults. Ardour shows all four of
  the LV2 plug-in's settings by name.
- **Which to use.** On Linux, use the LV2 plug-in for a crossover or to
  reach every setting. Convolver's macOS and Windows packages have no LV2
  plug-in, and its VST3 plug-in cannot play a crossover in Ardour; on
  macOS, Ardour also hosts Audio Units, but Convolver's has not yet been
  tried there.

## LV2 channel layouts

An LV2 plug-in's inputs and outputs are fixed, so each layout is its own
plug-in: 1→1, 2→2, 1→2, 2→4, 2→6, 2→8, 6→6 and 8→8. Choose the one whose
counts match your filter; [`convolver-preset`](presets.md) says which. One with
the filter's outputs and more inputs also plays it, reading the first inputs
([channels](#channels)). Their
URIs are `https://github.com/jrp2014/convolver/lv2#discrete-N-M`.

## LV2 ports

| Index | Symbol | Kind | Meaning |
| --- | --- | --- | --- |
| 0 | `enabled` | Control input | 1 processes, 0 bypasses (a 1.3 ms fade) |
| 1 | `latency` | Control output | The latency that plays, in frames (the Latency setting); hosts show it as Reported latency |
| 2 | `status` | Control output | 0 Active, 1 No filter, 2 Load failed, 3 Loading, 4 Fault |
| 3… | `in_1` … `in_N` | Audio input | Input channels in order |
| then | `out_1` … `out_M` | Audio output | Output channels in order |
| then | `control` | Atom input | The host sets or asks for the filter file |
| then | `notify` | Atom output | Names the file that plays |
| then | `gain` | Control input | Gain, −100 to +24 dB (default 0) |
| then | `mix` | Control input | Mix, 0 to 100% (default 100) |
| then | `latency_frames` | Control input | Latency, one of 0, 64, 128, 256, 512, 1024, 2048 and 4096 frames (default 0); another value counts as the nearest |
| last | `gain_mode` | Control input | Gain mode: 0 Set, 1 Estimate, 2 No clipping (default 0) |

Load failed stays shown until a filter loads successfully, also after the
host reactivates the plug-in; Loading is shown while the newest file choice
is being loaded. While a new Latency is applied or Estimate's figure is
found, the filter plays on and the port stays at Active; the host's log
says so (`Latency 256 frames chosen; the plug-in keeps 0 frames while a
processor for it is built.`, `Finding the filter's estimate for
Estimate; ...`) and then what happened.

The plug-in needs the host's `urid:map` feature, and the host's worker thread
to load files. It writes messages through the host's log, or to standard
error if the host has none. Hosts that cannot hand it a feature list (such as
lilv's `lv2apply`) cannot load it.

## LV2 filter files

The LV2 plug-in has one parameter, **Filter**
(`https://github.com/jrp2014/convolver/lv2#filter`, a path). Hosts that show
LV2 file parameters, such as Ardour and Jalv, give it a file chooser: choose
the IR file, `.cfg` or configuration list you would pass to the
[command-line tool](command-line.md). The file is read for the plug-in's
sample rate and channel counts, as a list's entry is chosen by the
command-line tool, except that a filter with fewer inputs than the plug-in
has also fits ([channels](#channels)); nothing is resampled.

The file is loaded on the host's worker thread, so audio carries on
meanwhile: the status port shows Loading, then Active once the new filter
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

**PipeWire.** PipeWire's `filter-chain` (1.6.2 or later) starts the plug-in
with a file path alone, which is how to filter everything a Linux computer
plays ([system-wide filtering with PipeWire](pipewire.md)).

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
and the AudioUnitSDK licence; macOS preview packages also hold them in
`licenses/` (`Convolver-AU.txt`, `GPL-3.0.txt`, `audiounitsdk.txt`).

<!-- VST trademark notice: written by scripts/trademarks.py from packaging/trademarks/vst.json -->

<img src="images/VST_Compatible_Logo_Steinberg.png" alt="VST Compatible logo" width="120">

Convolver supports VST® 3. VST is a registered trademark of Steinberg Media Technologies GmbH.

<!-- End of the VST trademark notice -->
