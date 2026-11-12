<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# System-wide filtering with PipeWire

[Using Convolver](README.md) · [Install](install.md) ·
[How the plug-ins behave](plugins.md#lv2-filter-files)

For anyone on Linux who wants a Convolver filter applied to everything the
computer plays. You will add a sound device called **Room correction** to
PipeWire that runs the Convolver LV2 plug-in.

**In short:**

- PipeWire's `filter-chain` module can run the Convolver LV2 plug-in as a
  virtual output device. Whatever plays to that device is filtered, then
  played on your real device.
- It needs **PipeWire 1.6 or later with its LV2 support**. Older versions
  start the plug-in without a filter, so audio passes unfiltered. Ubuntu
  24.04's PipeWire (1.0) has no LV2 support at all.
- You write one small configuration file naming the plug-in's channel layout
  and your filter: an IR file, a `.cfg` or a configuration list.
- The filter is read for the rate PipeWire runs at, 48 kHz unless you allow
  others. When PipeWire changes rate, a configuration list chooses its entry
  for the new rate; nothing is resampled. A filter that does not fit plays
  silence, and PipeWire's log says why.

**Status:** this setup was tried with PipeWire 1.6.8 on virtual devices only,
not yet on a desktop with a real sound card
([status](../project/status.md)).

Contents: [check your PipeWire](#check-your-pipewire) ·
[add the device](#add-the-room-correction-device) ·
[choose a filter for each rate](#choose-a-filter-for-each-rate) ·
[compared with PipeWire's own convolver](#compared-with-pipewires-own-convolver) ·
[troubleshooting](#troubleshooting)

## Check your PipeWire

```sh
pipewire --version
```

The version must be 1.6 or later; earlier versions ignore the filter setting
below. PipeWire's LV2 support is often a separate package: on Ubuntu 26.04,
which has PipeWire 1.6, it is `libspa-0.2-modules-extra`. Then install the Convolver bundle in
`~/.lv2/` as described in [install](install.md). PipeWire runs as a service,
so it finds plug-ins only in the usual LV2 folders, not through an `LV2_PATH`
set in a terminal.

## Add the Room correction device

1. Choose the layout whose counts match your filter
   ([LV2 layouts](plugins.md#lv2-channel-layouts)): `discrete-2-2` for a
   stereo room correction, `discrete-2-6` for a stereo crossover into six
   outputs. `convolver --inspect` shows a filter's counts.
2. Save this as `~/.config/pipewire/pipewire.conf.d/convolver-room.conf`,
   with the path to your own filter (an absolute path):

   ```text
   context.modules = [
     { name = libpipewire-module-filter-chain
       flags = [ nofail ]
       args = {
         node.description = "Room correction"
         filter.graph = {
           nodes = [
             { type = lv2  name = convolver
               plugin = "https://github.com/jrp2014/convolver/lv2#discrete-2-2"
               config = {
                 "https://github.com/jrp2014/convolver/lv2#filter" = "/home/me/room/room.cfg"
               }
             }
           ]
         }
         capture.props  = { node.name = "convolver_room" media.class = Audio/Sink
                            audio.position = [ FL FR ] }
         playback.props = { node.name = "convolver_room.output" node.passive = true
                            audio.position = [ FL FR ] }
       }
     }
   ]
   ```

   `flags = [ nofail ]` lets PipeWire start, without this device, if the
   plug-in cannot be loaded; without it, a mistake here stops all sound.
   For more outputs, list one position per output in `playback.props`, in
   the filter's output order, such as `[ FL FR FC LFE RL RR ]` for
   `discrete-2-6`. To send the result to one particular device, add
   `target.object = "<device name>"` to `playback.props` (`wpctl status`
   and `pw-cli ls Node` list the names).
3. Restart PipeWire:

   ```sh
   systemctl --user restart pipewire
   ```

4. Choose **Room correction** as the output in your desktop's sound settings,
   or with `wpctl set-default <id>` (the id from `wpctl status`).

**How to tell it worked:** play something. With a filter that changes the
sound clearly (a test configuration that silences one side, for example), the
change is heard at once. Turn the volume down first if the filter adds gain.

PipeWire reads the filter file when it starts the filter: at the first
sound after a restart and again when it changes rate. After editing the file,
restart PipeWire so the change is heard.

## Choose a filter for each rate

By default PipeWire runs at 48 kHz and resamples everything played to that
rate, so a filter for 48 kHz is enough. If you let PipeWire follow what is
playing (several rates in `default.clock.allowed-rates`, in PipeWire's own
configuration), the filter must fit each of them. Name a
[configuration list](configuration.md#paths-and-configuration-lists) instead
of a single `.cfg`, with one entry per rate:

```text
room-44100.cfg
room-48000.cfg
```

The plug-in chooses the entry for PipeWire's rate and the layout's channels,
as in any host ([loading a filter](plugins.md#loading-a-filter)). A rate no
entry has plays silence.

The [Settings](plugins.md#settings) (Gain, Mix and Latency) can be set in the
node's `control` section, for example `control = { gain = -6.0 }` beside
`config`.

## Compared with PipeWire's own convolver

PipeWire has a built-in `convolver` filter. It reads one channel of one IR
file per filter, resamples the IR to PipeWire's rate, and, given several
files, uses the one with the closest rate. For a single IR per channel it is
the simpler choice. Convolver adds:

- **The filters you already have.** The `.cfg` files and configuration lists
  that Roon, JRiver, Audiolense and the old Windows Convolver use
  ([other programs](other-programs.md)), with their routes, gains and delays.
- **Routing in one place.** Any input to any output with its own gain or
  inversion, which with the built-in filter means one filter per path and
  hand-written mixer links.
- **Delays** per input and per output, as the configuration gives them.
- **The filter made for each rate.** A list's entry for each rate plays as it
  was designed, never a resampled IR; a rate with no entry is silent, not
  played with another rate's filter.
- **Checks and explanations.** A filter that does not fit the channels or
  rate is refused with a message saying what differs and what to change.

## Troubleshooting

- **No Room correction device.** The configuration file has a syntax error,
  PipeWire has no LV2 support, or the plug-in is not in `~/.lv2/` (for
  either, the log says `can't load plugin type 'lv2'`). Read the log:
  `journalctl --user -u pipewire -b`.
- **The device plays, but unfiltered.** PipeWire is older than 1.6 and did
  not pass the filter to the plug-in. Upgrade PipeWire.
- **Silence.** The filter does not fit PipeWire's rate or the layout's
  channel counts. PipeWire writes Convolver's explanation to its log only
  at the *info* level. Raise the level, play again and read the log:

  ```sh
  pw-metadata -n settings 0 log.level 3
  journalctl --user -u pipewire -f | grep Convolver:
  ```

  Set the level back with `pw-metadata -n settings 0 log.level 2`.

For other symptoms, see [troubleshooting](troubleshooting.md), and
[report a problem](report-a-problem.md) if this setup misbehaves on your
system.
