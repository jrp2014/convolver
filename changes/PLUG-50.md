<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-50

## Changed

- Plug-ins (PLUG-50): in Convolver's window on macOS and Windows, the
  **Gain mode** menu now starts the Gain row, so the cut-off "Gain mo…"
  label is gone and one row says which gain plays. With Estimate or No
  clipping, the Gain slider and box are dimmed and the box shows the gain
  that mode plays; Gain keeps its value and plays again when you choose
  Set. A note under the row appears only when the box cannot say what
  plays: no filter loaded, a filter with no figure, or No clipping playing
  until the filter is estimated. See [Convolver's
  window](../docs/use/plugins.md#convolvers-window).
- Plug-ins (PLUG-50): one **Choose Filter** menu replaces the Choose…,
  Recent and Reload buttons and the "Reload when the file changes" box. It
  holds Choose File…, Reload (named for the file, as `Reload room.cfg`),
  Reload When the File Changes, the recent files and Clear Recent. On
  Windows it is a split button: its main part chooses a file and its arrow
  opens the menu. It is highlighted when choosing a filter is the fix, and
  dropping a file on the window still loads it. See [Convolver's
  window](../docs/use/plugins.md#convolvers-window).
- Plug-ins (PLUG-50): the Worst-case peak note says "(won't clip at this
  Gain and Mix)" instead of "(cannot clip …)".

## For contributors

- Plug-ins (PLUG-50): the window report has a new `gain_played` field
  (`gain_played()`), and `gain_note` now holds only what the Gain box
  cannot say; the report's encoding number is 2. The Windows window takes
  the Choose Filter menu's commands as `WM_COMMAND` (`win::menu_reload` and
  the others), which its test sends instead of opening the menu.

## Done

- [x] **PLUG-50 — Gain mode in the Gain row; one Choose Filter menu.**
  22 November 2026, PR #273 ([Convolver's
  window](../docs/use/plugins.md#convolvers-window)).

## Decisions: Plug-in behaviour

- **22 November 2026.** jrp approved the
  [mock-up](https://claude.ai/artifact/2WrL3kAy39753gfJtkW3sn) of the
  window's settings and file button ("Go ahead"): Gain mode inside the Gain
  row, one Choose Filter menu for every file action ("do we need choose +
  recent + reload? Can we not do that in one button?"), and "won't clip"
  rather than no note for a Worst-case peak that cannot clip, since a blank
  note could mean safe or unchecked.
