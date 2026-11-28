<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-68

## Fixed

- VST3 window on Windows (PLUG-68): clicking the Gain or Mix trackbar after
  typing a value in its box, without pressing Return, set the typed value
  first (or beeped at text that is not a number) and then the click's own
  value: two changes in the host's undo, or a beep and the change. The click now drops the unfinished
  typing, as moving a slider does on macOS, so it is one change.

## For contributors

- `Editor::control_key` drops a slider's typing on any left click on it,
  not only Ctrl-click (PLUG-67), before the trackbar takes the focus from
  the box. `vst3.editor` (Windows) clicks the Gain track after typing `-6` and
  `abc` and requires that nothing is set for the typing
  ([testing](../docs/develop/testing.md#plug-in-format-tests)).

## Done

- [x] **PLUG-68 — A click on a Windows trackbar drops unfinished typing.**
  27 November 2026, from the pre-1.0 review of 0.19.21, PR #361
  ([Convolver's window](../docs/use/plugins.md#convolvers-window)).
