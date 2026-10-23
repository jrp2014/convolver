<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Equalizer APO export

[Using Convolver](README.md) · [Configuration files](configuration.md) ·
[Command-line reference](command-line.md)

For anyone who wants a Convolver filter applied to everything a Windows
computer plays. You will export the filter with `convolver-equalizer-apo` and
install the export in Equalizer APO.

**In short:**

- [Equalizer APO](https://sourceforge.net/projects/equalizerapo/) is a free
  Windows audio-engine filter. It applies its configuration to everything a
  device plays, so a Convolver filter exported to it filters the whole system,
  not just one player.
- `convolver-equalizer-apo room.cfg room` writes a new folder `room` holding
  `convolver.txt` and the impulse responses (IRs) as WAV files. The input can be
  an IR file, a `.cfg` or a configuration list.
- Copy the folder into Equalizer APO's `config` folder and add
  `Include: room\convolver.txt` to its `config.txt`. Set the device's sample
  rate in Windows to the filter's.
- Equalizer APO only knows the device's sample rate and channel count, so those
  choose the filter. A device whose sample rate or channel count no filter
  fits is **silenced**, not played unfiltered, and so are device channels the
  filter does not use. This follows the plug-ins' rule: a crossover must never
  send full-range audio to a tweeter.
- Copy the whole folder. Equalizer APO plays a path whose IR file it cannot
  read unfiltered, and the export cannot detect that.
- ASIO and WASAPI exclusive-mode playback bypass Equalizer APO, so they are not
  filtered.
- The exports are tested in Equalizer APO 1.4.2's own filter code, but not
  yet on a real Windows audio device.

Contents: [make the export](#make-the-export) · [install it on Windows](#install-it-on-windows) ·
[how a filter is chosen](#how-a-filter-is-chosen) ·
[a mono IR on a stereo device](#a-mono-ir-on-a-stereo-device) ·
[what the export contains](#what-the-export-contains) ·
[differences from the CLI](#differences-from-the-cli) ·
[troubleshooting](#troubleshooting)

## Make the export

Preview packages for every system have `convolver-equalizer-apo` in `bin/`. A
source build makes it beside `convolver` (`build/convolver-equalizer-apo`, or
in `build/Release/` with Visual Studio). It runs on any
platform, so you can make the export on a Mac or Linux machine and copy it to
the Windows computer.

```sh
convolver-equalizer-apo room.cfg room
```

The second argument must be a new folder; the tool never writes into an
existing one. If anything fails, it removes the folder again and says so. It
checks the input the way [`convolver --validate`](command-line.md#inspecting-and-validating-configurations) does and
lists every problem with its file and line.

The tool prints each filter it exported, with the sample rate and channel count
of the devices it applies to, any list entry it had to leave out, and the
installation steps.

## Install it on Windows

1. Install Equalizer APO and, in its Configurator, select the playback device.
   Restart Windows if the installer asks.
2. Copy the exported folder into Equalizer APO's `config` folder, usually
   `C:\Program Files\EqualizerAPO\config`.
3. Open `config.txt` in that folder (with the Configuration Editor or a text
   editor) and add the line the tool printed, for example:

   ```text
   Include: room\convolver.txt
   ```

   Other lines in `config.txt`, such as a `Preamp:` or an equalizer, still
   apply before and after it. Equalizer APO reloads the file when it changes.
4. In Windows sound settings, open the device's properties and, on the
   *Advanced* tab, choose the filter's sample rate (for example "24 bit,
   48000 Hz"). Equalizer APO does not resample IRs: another rate is silent.

## How a filter is chosen

Equalizer APO tells a configuration only the device's sample rate and channel
count, so these choose the filter. Every entry of a configuration list is
exported:

1. The entry with the device's sample rate and exactly the device's channel
   count.
2. Otherwise, of the entries with the device's sample rate and fewer channels
   than the device, the one with the most channels, if the device has at most
   32 channels. The filter uses the first channels and the remaining ones are
   silenced. For example, with a stereo filter and a six-channel crossover at
   48 kHz, a four-channel device gets the stereo filter and an eight-channel
   device the crossover.
3. Otherwise every channel is silenced.

The tool prints the range of device channel counts each filter covers.

An entry's channel count is the larger of its input and output counts, because
its inputs and outputs are the same device channels. A list entry with the same
sample rate and channel count as an earlier one can never be chosen, so the
tool leaves it out and says which entry shadows it; export it on its own to use
it.

Unlike the [Convolver CLI](configuration.md#paths-and-configuration-lists), the export cannot see
the stream's own channel count: Windows mixes every stream to the device's
format first.

## A mono IR on a stereo device

An IR file with one channel exports a one-channel filter. On a stereo device it
filters the left channel, and the right channel is silent. To filter both
channels with the same mono IR, export a `.cfg` that applies it to each channel
(here for a 48 kHz `impulse.wav` beside it):

```text
48000 2 2 3
0 0
0 0
impulse.wav
0
0
0
impulse.wav
0
1
1
```

The tool prints a note whenever it exports a one-channel filter.

## What the export contains

- `convolver.txt`: Equalizer APO commands, with comments that explain each
  step. Channel 1 there is Convolver's channel 0, and so on, in the device's
  Windows speaker order (left, right, centre, LFE, rear, side). The `.cfg`
  speaker mask is not used: Windows decides the device's layout.
- `ir-N-RATEHz.wav`: each distinct IR channel, as a mono 32-bit float WAV, named
  in `convolver.txt` relative to it. IRs that several paths share are written
  once.

For the chosen filter, the commands:

- delay the inputs (`Delay: N samples`; Convolver delays are already whole
  frames);
- mix each path's inputs, with their gains and signs, into a channel of its own
  (`Copy: CONVOLVER1=…`; Equalizer APO creates these extra channels);
- filter each such channel with its IR (`Convolution:`);
- sum the paths into the outputs with their gains (`Copy: 1=…`); outputs that no
  path feeds are silent;
- delay the outputs.

The variables `convolverFilter` and `convolverChannels` record the chosen
filter. Silence uses `Preamp: -1000 dB`, a gain of 10⁻⁵⁰, which is zero once
Windows receives 32-bit samples.

## Differences from the CLI

- **Gain.** The CLI's `--gain-db`, dither and output formats do not apply.
  Windows receives 32-bit float samples; if the filter can exceed full scale,
  add a negative `Preamp:` line before the `Include:` in `config.txt`.
- **Latency.** Equalizer APO's convolution adds no latency, so there is nothing
  to trim. The CLI keeps the full tail when a file ends; a device keeps playing.
- **Sample rate.** The device's rate must be the filter's, within 1 Hz. Export
  a list with one entry per rate to cover several.
- **Near half the sample rate.** Equalizer APO's own convolution can leave out
  up to three frequency bins just below half the sample rate (about 22 kHz at
  44.1 kHz) when the audio engine's blocks are not a multiple of four frames.
  This is in Equalizer APO itself and is inaudible, so the export does not
  work around it.

## Troubleshooting

- **Silence.** The device's sample rate or channel count fits no exported
  filter. Compare the device's *Advanced* tab with the rates the tool printed,
  and check that the device has at least the filter's channels.
- **No change at all.** The device is not selected in Equalizer APO's
  Configurator, `config.txt` lacks the `Include:` line, the folder is not
  inside the `config` folder, or the player uses ASIO or WASAPI exclusive
  mode.
- **Only the left channel plays.** The filter has one channel; see
  [a mono IR on a stereo device](#a-mono-ir-on-a-stereo-device).
- **A path plays unfiltered.** Equalizer APO could not open that path's IR
  file: the folder was copied only in part, is outside the `config` folder
  (Equalizer APO runs as the LocalService account, which must be able to read
  it), or its full path is over 259 characters. Equalizer APO then leaves the
  audio unchanged and writes the failure to its log (below). The tool refuses
  folder names that would be too long in the usual `config` folder.
- **What Equalizer APO read.** Equalizer APO writes problems to
  `C:\Windows\ServiceProfiles\LocalService\AppData\Local\Temp\EqualizerAPO.log`.
  Setting `EnableTrace` to `true` under `HKEY_LOCAL_MACHINE\SOFTWARE\EqualizerAPO`
  logs how each line was interpreted; set it back afterwards.

How the export is tested, and how to check it on a real device, is described
for developers in [testing](../develop/testing.md#equalizer-apo-export-tests).
