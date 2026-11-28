<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-73

## Changed

- VST3 window on Windows (PLUG-73): it now gives the tooltips the macOS
  window gives: all of a long filter name, the filter's taps over its
  length, all of a long speaker list, and what each Audio fact means over
  its label, value and note. The status word is in the ordinary text
  colour, as on macOS, with the colour on its dot, which is filled for
  Active, Load failed and Fault and a ring for Waiting, Bypassed and No
  filter, as macOS draws filled and outline symbols. See
  [Convolver's window](../docs/use/plugins.md#convolvers-window).

## For contributors

- Window model (PLUG-73): both windows take from `portable/window_model`
  what each used to write itself: the layout sizes and timer periods, the
  Bypass, Choose Filter, Copy Diagnostics and Show Log tooltips (and the
  Reload menu items' on macOS), the file dialog's words, Reload's title,
  the line under the name, the rule for when the Gain row shows what the
  Gain mode plays, and the file check that reads the report again before
  reloading (`FileWatch::reload_due()`). The Windows window reads the
  settings once a refresh, not twice. `native.window_model` and the Windows
  `vst3.editor` check them
  ([architecture](../docs/develop/architecture.md#the-shared-macos-window)).
- TODO (VST3-33): the Windows window's remaining differences from macOS,
  from a parity check of both windows, as one running list.

## Done

- [x] **PLUG-73 — Window logic shared by macOS and Windows.** 28 November
  2026, from the pre-1.0 review of 0.19.21, at jrp's request, PR #374
  ([architecture](../docs/develop/architecture.md#the-shared-macos-window)).
