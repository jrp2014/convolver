<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Troubleshooting

For anyone whose filter does not load, play or sound as expected. Find the
symptom, then follow the fix.

**In short:** most problems are a sample rate or channel count that does not
match the filter, a file path in a `.cfg` that does not lead to the IR, or a
plug-in the system or host has refused to load. Convolver's own messages say
what differs and what to change; read them first.

Contents: [the host does not list Convolver](#the-host-does-not-list-convolver) ·
[a plug-in plays silence](#a-plug-in-plays-silence) ·
[filters and configurations](#filters-and-configurations) ·
[a tool will not write its output](#a-tool-will-not-write-its-output) ·
[Equalizer APO](#equalizer-apo) · [report a problem](#report-a-problem)

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
| The preset's sample rate differs from the host's | Set the host to the preset's rate, or make a preset at the host's rate; Convolver does not resample |
| The preset's channel counts differ from the track's | Use a track or bus with the preset's input and output counts, or a matching preset |
| A project was opened at another rate or layout | VST3 and Audio Unit: set the host back to the rate and layout the filter was made for, and the filter returns. LV2: do not save; reopen the session at the filter's rate, or load the preset again ([details](plugins.md#when-a-filter-does-not-fit)) |
| No preset is loaded, and the input and output counts differ (a crossover, for example) | Load a preset |
| The project was saved after its first preset failed | Load a matching preset |
| Bypass is on with different input and output counts | Switch bypass off |

To see why, read the plug-in's **Status** parameter in the host's generic
plug-in window or parameter list (VST3 and Audio Unit), then the
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

## Report a problem

Report the package's SHA-256 checksum, the system and its version, the
processor, the host and its version, the sample rate, channel layout and buffer
size, any error message, the [Convolver log](plugins.md#status-and-the-convolver-log)
(or the host's log for LV2), and the smallest example that shows the problem. Use the bundled synthetic examples where you can, and keep private
audio and session files out of the report. Keep levels low when trying
unfamiliar IRs.
