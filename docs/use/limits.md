<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Limits

For anyone deciding whether Convolver fits a job: what it does not do, and what
to use or do instead.

**In short:** Convolver applies filters you already have, at the sample rate
and channel counts they were made for. It does not design filters, resample,
play audio to a device by itself, or show a window in a host. Its packages are
unsigned developer previews.

| Convolver does not… | Instead |
| --- | --- |
| Measure a room or design filters | Make the filter with a tool such as DRC, REW or Audiolense, then load it |
| Resample a filter, or adapt its channel count | Make the filter at your audio's sample rate, or keep one per rate in a [configuration list](configuration.md#paths-and-configuration-lists); use a `.cfg` to apply a mono IR to several channels |
| Play to or record from an audio device | Use a plug-in host, a pipe between programs ([RAW streams](command-line.md#raw-streams)) or, on Windows, [Equalizer APO](equalizer-apo.md) |
| Filter everything a computer plays, by itself | On Windows, export to [Equalizer APO](equalizer-apo.md) |
| Show a window or file chooser in a plug-in host | Load a [preset](presets.md); the host's own bypass switch works |
| Handle more than 8 channels in a plug-in, or more than one input and one output | Use the [command-line tool](command-line.md), which takes up to 256 channels |
| Offer every channel layout in LV2 | Use one of the [LV2 layouts](plugins.md#lv2-channel-layouts), or the VST3 or Audio Unit plug-in |
| Open projects saved with the old Windows VST 2 plug-in | Load a preset made from the same `.cfg` |
| Limit peaks | Lower the [gain](command-line.md#gain-and-attenuation) yourself; float output can exceed full scale, and integer output clips |
| Write formats other than WAV and raw float | Convert the output with another tool; use raw float for streams longer than WAV's 4 GiB limit |
| Write output files all-or-nothing | Check the exit status: after a failure the output file may be incomplete |
| Run in a plug-in host at double precision | Processing is single precision; 64-bit host buffers are converted |
| Reach audio that ASIO or WASAPI exclusive mode sends on Windows | Use shared mode with [Equalizer APO](equalizer-apo.md), or a plug-in in the player |
| Come signed, or tested in named DAWs | Previews are unsigned ([install](install.md#unsigned-previews)); the [status page](../project/status.md) tracks what is ready |

Some configurations from other programs, or from the old Windows Convolver,
need small changes first; see [other programs](other-programs.md).
