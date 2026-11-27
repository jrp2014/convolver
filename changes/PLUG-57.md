<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-57

## Changed

- Plug-ins (PLUG-57): a mono filter playing on each channel is no longer
  copied for every channel when a host restores or chooses the filter that
  is already playing, which then plays on with its history. Until now a
  30-second mono filter at 48 kHz was copied in full every time: 11.5 MB on
  a stereo track and 46 MB on eight channels (jrp's measurements of
  `7bd024f` on his Mac: 0.23 ms and 0.84 ms). Nothing heard changes.

## For contributors

- Host layer (PLUG-57): `HostPlayback::each_channel` carries the channel
  count a filter with one input and one output plays on, and `filter` stays
  the accepted filter, shared. `HostProcessor` lays it out with
  `on_each_channel()` only when it is built or replaced from the playback,
  for as long as building the engine takes, so `keep()` copies nothing.
  Test: `native.filter_file`'s `default_layouts` checks that what plays on a
  stereo track shares the mono filter, and that a processor built from it,
  one replacing its filter with it and one publishing it prepared each
  filter each channel on its own (1 and 2 in give 0.5 and 1 out through the
  0.5 tap); a processor that skips the layout in any of the three, or
  routes both channels from the first, fails it.

## Done

- [x] **PLUG-57 — Mono filters laid out on each channel only where they are
  built.** jrp's review of `7bd024f`, 26 November 2026, PR #330
