<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-52

## Changed

- Plug-ins (PLUG-52): the Gain mode choices are now **Manual**, **Estimated**
  and **Clip-safe** (were Set, Estimate and No clipping), in the window, the
  hosts' own controls, Status and the log. Saved projects and presets store
  the choice by number, so they open unchanged. Estimated and Clip-safe are
  the command line's `--gain-db auto` and `no-clip`. See
  [settings](../docs/use/plugins.md#settings).
- Plug-ins (PLUG-52): the window keeps its height, and its controls their
  places, when a message comes, goes or changes (Bypass, a refused file, a
  reload): the message's space stays, and keeps the height of the longest
  message shown since the window opened. See
  [the window](../docs/use/plugins.md#convolvers-window).
- Plug-ins (PLUG-52): Worst-case peak for a long reverb says the bound is
  rarely reached by music and names Gain mode Estimated, with its figure
  once found, instead of only a Gain tens of decibels lower.
- Plug-ins (PLUG-52): a kept filter that does not suit the host, and so
  plays silence, shows Waiting in orange with its message in an orange box,
  rather than in grey.
- Plug-ins (PLUG-52): in the Audio Unit window, a slider drag is one change
  for the host's automation and undo, and a typed value, a menu choice or
  Option-click one each, as in the VST3 window.
- Plug-ins (PLUG-52): most of the Gain slider's travel is now between −30
  and +12 dB; the setting and its automation stay in decibels.
- Plug-ins (PLUG-52): under the filter's name, the window says which file
  Reload When the File Changes watches (`Reloads when room.cfg changes.`).
- Plug-ins (PLUG-52): the window's Cause and Fix name a refused file by its
  name; the full path stays under File, software and log.
- Plug-ins (PLUG-52): with no filter, the window says "No filter" once (in
  the heading) instead of three times.
- Plug-ins (PLUG-52): on Windows, Ctrl-click on the Gain or Mix trackbar
  puts its default back, as Option-click does on macOS; not yet tried in a
  Windows host. The arrow keys move Gain 1% of its travel.

## For contributors

- Window model (PLUG-52): `gain_position()` and `gain_at_position()` (the
  Gain slider's taper), `message_slot()` (the steady message height) and
  `watch_note()`, with `native.window_model` tests; `HostFilter` keeps the
  filter's noise gain (`filter_noise_gain()`), which Worst-case peak
  compares with the bound before any estimate is found. The Choose Filter
  button stays a pull-down because `NSComboButton` has no `bezelColor` for
  the fix's highlight ([format notes](../docs/develop/plugin-contract.md#format-notes)).
- Tests (PLUG-52): `au.editor` checks the Audio Unit's change gestures, the
  taper, the steady height after a shorter message and the watch line;
  `vst3.editor` on Windows checks the taper, Ctrl-click and the steady
  height; report tests cover the long-reverb note and file names in the
  Cause.

## Done

- [x] **PLUG-52 — The window's design review of 26 November 2026.** jrp
  chose findings 1 to 8, 10 and 11, and then the Gain mode names (finding
  9). 26 November 2026, PR #300
  ([the window](../docs/use/plugins.md#convolvers-window)).

## Decisions: Plug-in behaviour

- **26 November 2026.** From the window's design review, jrp chose
  "Everything except the naming": a steady window height, Worst-case peak
  naming Estimated for long reverbs, Audio Unit change gestures, the
  warning colour for a silent kept filter, a Gain slider taper, a visible
  reload note, file names in the Cause, a Ctrl-click reset on Windows and
  plainer no-filter words. Choose File… as a one-click `NSComboButton` only
  if it could carry the fix's accent highlight; it cannot, so Choose Filter
  stays a pull-down. Then the Gain mode names Manual, Estimated and
  Clip-safe, replacing Set, Estimate and No clipping (jrp: "Go with 1. And
  align documentation"). See [settings](../docs/use/plugins.md#settings),
  [the window](../docs/use/plugins.md#convolvers-window).
