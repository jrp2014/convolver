<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Troubleshooting

For anyone whose filter does not load, play or sound as expected. Find the
symptom, then follow the fix.

**In short:** most problems are a sample rate or channel count that does not
match the filter, a file path in a `.cfg` that does not lead to the IR, or a
plug-in the system or host has refused to load. Convolver's own messages say
what differs and what to change; read them first.

Contents: [reading Convolver's messages](#reading-convolvers-messages) ·
[the host does not list Convolver](#the-host-does-not-list-convolver) ·
[a plug-in plays silence](#a-plug-in-plays-silence) ·
[filters and configurations](#filters-and-configurations) ·
[a tool will not write its output](#a-tool-will-not-write-its-output) ·
[Equalizer APO](#equalizer-apo) · [PipeWire](#pipewire) ·
[report a problem](#report-a-problem)

## Reading Convolver's messages

Every message names the problem first, then its cause and the fix, each on
its own line (the [plug-in window](plugins.md#convolvers-window) shows the
same parts, and the [Convolver log](plugins.md#status-and-the-convolver-log)
keeps them on one line):

```text
convolver: error: room.cfg:4:1: filter 'irs/left.wav': IR file not found
  Cause: There is no file at /Users/me/Filters/irs/left.wav. IR file names are relative to the folder that holds room.cfg.
  Fix: Correct the name on line 4, or put the IR file in that folder.
  Note: Letter case counts on Linux and on case-sensitive macOS volumes.
```

- **The problem** names the file and line (`room.cfg:4:1:`), the value or
  the setting.
- **Cause** says why; **Fix** says what to do, the most likely fix first.
- **Note**, when there is one, gives the limit behind it: Convolver never
  resamples, a host gives a mono track one channel, a library this build
  lacks.

A command-line tool that fails exits with a nonzero status. `convolver-preset`
and `convolver-equalizer-apo` then keep nothing from the run; when
`convolver` fails after it has started writing, its Note says the output is
incomplete.

## The host does not list Convolver

| Cause | Fix |
| --- | --- |
| The bundle is in a folder the host does not scan, or only part of it was copied | Copy the whole bundle to a [plug-in folder](install.md#where-each-plug-in-goes) and rescan |
| macOS quarantined the downloaded, unsigned bundle | Clear the quarantine for the Convolver bundle only ([unsigned previews](install.md#unsigned-previews)) |
| The package is for another processor or system | Use the package for your processor (Intel or Apple silicon, x64) and system |
| A runtime library is missing (Windows) | Install the Microsoft Visual C++ 2015–2022 x64 runtime |
| The Audio Unit was not noticed | Run `killall -9 AudioComponentRegistrar`, then `auval -v aufx Conv CONV` |

The host's scan log names the reason. Follow the host's rescan steps after the
fix. Do not turn off the system's security checks.

## A plug-in plays silence

A plug-in plays silence, rather than unfiltered audio, whenever its filter
cannot be used ([when a filter does not fit](plugins.md#when-a-filter-does-not-fit)):

| Cause | Fix |
| --- | --- |
| The filter's sample rate differs from the host's | Set the host to the filter's rate, or choose a filter made for the host's rate; Convolver does not resample |
| The track has fewer inputs than the filter, or a different number of outputs | Use a track or bus with at least the filter's inputs and exactly its outputs, or choose a filter that matches the track |
| A project was opened at another rate or layout | VST® 3 and Audio Unit: set the host back to the rate and layout the filter was made for, and the filter returns. LV2: do not save; reopen the session at the filter's rate, or load the filter again ([details](plugins.md#when-a-filter-does-not-fit)) |
| No filter is loaded, and the input and output counts differ (a crossover, for example) | Choose a filter in Convolver's window, or load a preset |
| The project was saved after its first filter failed | Choose or load a matching filter |
| Bypass is on, and the filter's input and output counts differ | Switch bypass off |
| Bypass is on (or Mix below 100%) after a load failed and no earlier filter is kept, or with a kept filter whose counts differ, such as a crossover at the wrong rate | Fix the load as above. Bypass passes nothing here on purpose: Convolver cannot tell that the failed filter was not a crossover ([bypass](plugins.md#bypass)) |
| The filter plays but some channels are missing, and Status ends "plug-in inputs … ignored" | The track gives more inputs than the filter reads: it reads the first ones only. Put the source on those inputs ([channels](plugins.md#channels)) |

To see why, read the message in [Convolver's window](plugins.md#convolvers-window)
(macOS) or the plug-in's **Status** parameter in the host's generic plug-in
window or parameter list (VST3 and Audio Unit), then the
[Convolver log](plugins.md#status-and-the-convolver-log), which says what
differs and what to change. The LV2 plug-in shows Load failed on its `status`
output and writes the reason to the host's log.

If Status shows **Fault**, the audio itself went wrong while it played: the
output became too large (lower the input level or the filter's gain), the
host sent invalid samples (check the plug-ins before Convolver), an Audio
Unit host passed buffers that do not match its setup (report it to the
host's developers), or the filter failed while playing (a Convolver bug:
report it with the log). The log says which, and how often. Fault clears when a filter loads or the host restarts
the plug-in, for example when the sample rate changes.

## Filters and configurations

| Symptom | Next step |
| --- | --- |
| Rate or channel mismatch | Read the error: it lists each candidate filter and what differs. Choose the matching configuration or generate the right IR; for a mono IR it suggests a configuration to save |
| Cannot find an IR | Check its name relative to the configuration, including upper and lower case; run `--profile portable-paths` |
| A Windows drive path fails on a Mac or Linux | Copy the IRs beside the configuration and replace the drive path with a relative path |
| An old configuration is rejected for missing delays | Add input and output delay lines ([old Windows configurations](other-programs.md#configurations-from-the-old-windows-convolver)) |
| The output is too loud | Check summed routes and lower the [gain](command-line.md#gain-and-attenuation); integer output clips |
| Unsure which list entry was used | Run `--inspect` with the same rate and channel options and read which entries it skipped and why |

## A tool will not write its output

`convolver-preset` and `convolver-equalizer-apo` never replace an existing file
or folder. Choose a new output name, or remove the old one. A failed run prints
the cause and leaves nothing behind. [Preset messages](presets.md#messages)
explains each `convolver-preset` message.

## Equalizer APO

See [Equalizer APO troubleshooting](equalizer-apo.md#troubleshooting).

## PipeWire

See [PipeWire troubleshooting](pipewire.md#troubleshooting).

## Report a problem

If none of this helps, or Convolver did something it should not, open an
issue: [report a problem](report-a-problem.md) says what to collect on each
system and how to send it.

<!-- VST trademark notice: written by scripts/trademarks.py from packaging/trademarks/vst.json -->

<img src="images/VST_Compatible_Logo_Steinberg.png" alt="VST Compatible logo" width="120">

Convolver supports VST® 3. VST is a registered trademark of Steinberg Media Technologies GmbH.

<!-- End of the VST trademark notice -->
