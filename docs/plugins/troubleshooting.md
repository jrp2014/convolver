<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Preview troubleshooting

Use the archive matching your CPU and OS. A deployment-target flag or a successful
build alone does not establish compatibility with an older OS. No production
compatibility floor is advertised yet.

If a host cannot scan the plug-in, check its diagnostic log for missing libraries,
architecture mismatch or signature rejection. On macOS, a quarantined unsigned
bundle is the usual cause; see [unsigned previews](install.md#unsigned-previews).
Follow the host's documented rescan procedure after correcting the cause. Do not
disable operating-system security system-wide.

A preset must match both the host sample rate and the track's input and output
channel counts. Change the
host configuration or import the matching preset; Convolver does not resample.
Malformed or mismatched state is rejected and the last valid state is preserved.
If nothing valid was loaded before, Convolver plays silence rather than passing
the unfiltered input through. A project opened at a different sample rate or
channel layout from its preset also plays silence; set the host back to the
preset's rate and layout and the filter returns. With no preset loaded and
different input and output counts (for example a 2→6 crossover), Convolver plays
silence too.
A tool failure (`convolver-preset` or `convolver-equalizer-apo`) prints the
cause to stderr and does not replace an existing file or folder. Use a new
output name. Keep levels low when trying unfamiliar IRs.

Report the package hash, OS/build, CPU, host version, sample rate, channel layout,
buffer size, error log and a synthetic minimal reproduction. Do not include private
audio or session files. The synthetic examples provide safe starting inputs.
