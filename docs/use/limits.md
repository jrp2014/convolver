<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Limits

For anyone deciding whether Convolver fits a job: what it does not do, and what
to use or do instead.

**In short:** Convolver applies filters you already have, at the sample rate
and channel counts they were made for. It does not design filters, resample
or play audio to a device by itself, and its VST® 3 plug-in has a window on
macOS and Windows only, as has the Audio Unit on macOS. Its packages are unsigned developer previews.
On four shared machines, Convolver's processing, run as its plug-ins run
it, kept up with room to spare with room correction for up to 8 channels,
stereo reverbs up to 60 s and true-stereo reverbs up to 60 s at 48 kHz or
30 s at 96 kHz; a full 8 × 8 matrix at 96 kHz did not on every machine
([processor load](#filter-length-channels-and-processor-load)).

| Convolver does not… | Instead |
| --- | --- |
| Measure a room or design filters | Make the filter with a tool such as DRC, REW or Audiolense, then load it |
| Resample a filter, or adapt its channel count | Make the filter at your audio's sample rate, or keep one per rate in a [configuration list](configuration.md#paths-and-configuration-lists); use a `.cfg` to apply a mono IR to several channels or a true-stereo reverb to a stereo track ([reverb responses](configuration.md#reverb-responses)) |
| Play to or record from an audio device | Use a plug-in host, a pipe between programs ([RAW streams](command-line.md#raw-streams)), on Windows [Equalizer APO](equalizer-apo.md) or, for playback on Linux, [PipeWire](pipewire.md) |
| Filter everything a computer plays, by itself | On Windows, export to [Equalizer APO](equalizer-apo.md); on Linux, run the LV2 plug-in in [PipeWire](pipewire.md) |
| Show a window in the VST3 plug-in on Linux, or in LV2 (the VST3 plug-in has [one on macOS and Windows](plugins.md#convolvers-window), as has the Audio Unit) | Use the host's controls for the settings and Status. Load a [preset](presets.md) to choose a filter, or in LV2 use the host's file chooser where it shows one ([LV2 filter files](plugins.md#lv2-filter-files)) |
| Handle more than 8 channels in a plug-in, or more than one input and one output | Use the [command-line tool](command-line.md), which takes up to 256 channels |
| Offer every channel layout in LV2 | Use one of the [LV2 layouts](plugins.md#lv2-channel-layouts), or the VST® 3 or Audio Unit plug-in |
| Open projects saved with the old Windows VST 2 plug-in | Load a preset made from the same `.cfg` |
| Limit peaks | Lower the [gain](command-line.md#gain-and-attenuation) yourself; float output can exceed full scale, and integer output clips |
| Write lossy formats such as MP3 or Ogg Vorbis | Write one of the [output formats](command-line.md#output-formats) and convert it with another tool |
| Write output files all-or-nothing | Check the exit status: after a failure the output file may be incomplete |
| Run in a plug-in host at double precision | Processing is single precision; 64-bit host buffers are converted |
| Reach audio that ASIO or WASAPI exclusive mode sends on Windows | Use shared mode with [Equalizer APO](equalizer-apo.md), or a plug-in in the player |
| Come signed, or tested in many hosts | Previews are unsigned ([install](install.md#unsigned-previews)); the [status page](../project/status.md) says which hosts they have been tried in |

Some configurations from other programs, or from the old Windows Convolver,
need small changes first; see [other programs](other-programs.md).

## Filter length, channels and processor load

How much one plug-in instance handles was measured in November 2026 on four
shared machines: three x86-64 servers, with Linux and Windows, and an Apple
M1 virtual machine. A test program ran Convolver's processing as its
plug-ins run it, without a host or the plug-in format's own small share of
each buffer
([measurements](../develop/fft-measurements.md#supported-workloads)). On
each machine, the filters below kept up with room to spare: on average they
used at most a fifth of one processor core, and the slowest 1 in 100 audio
buffers took less than half the buffer's time, with buffers of 64 and 512
frames and at Latency 0 and 256. The percentages are the highest of the
four machines, rounded up.

| Filter | At 48 kHz | At 96 kHz |
| --- | --- | --- |
| Room correction or a crossover, up to 8 channels, one filter per output (1.4 s at 48 kHz) | Up to 4% of a core | Up to 9% |
| A full 8 × 8 matrix (64 filters of 1.4 s at 48 kHz) | Up to 19% | Not on every machine: up to half a core, and the slowest 1 in 100 buffers took up to two-thirds of their time |
| A stereo or true-stereo reverb of up to 30 s | Up to 4% | Up to 17% |
| A 60 s reverb | Stereo up to 5%, true stereo up to 10% | Stereo up to 20%; true stereo is too large to save in a project ([size limit](plugins.md#when-a-filter-does-not-fit)), so the command line only |
| A 10 s reverb on 8 channels | Up to 6% | Up to 19% |

Your computer and host differ from these machines, and the host and its
other plug-ins share the processor.
Latency 0 costs up to about half as much again as Latency 256 for room
correction and crossovers; for long reverbs it showed no consistent extra
cost.

A large filter takes about two to five times the memory of its samples while it
plays, and twice that while a newly chosen filter replaces it: a 30 s
true-stereo filter at 48 kHz (22 MiB of samples) used 76 MiB, and 165 MiB
during a replacement. Preparing it took under 0.1 s; larger filters, such
as 30 s reverbs at 96 kHz, the 8-channel ones at 96 kHz and the 60 s
filters, took up to 0.2 s, during which the host can pause ([loading a filter](plugins.md#loading-a-filter)).

<!-- VST trademark notice: written by scripts/trademarks.py from packaging/trademarks/vst.json -->

<img src="images/VST_Compatible_Logo_Steinberg.png" alt="VST Compatible logo" width="120">

Convolver supports VST® 3. VST is a registered trademark of Steinberg Media Technologies GmbH.

<!-- End of the VST trademark notice -->
