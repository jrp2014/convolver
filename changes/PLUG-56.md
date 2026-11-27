<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-56

## Fixed

- macOS window (PLUG-56): Option-clicking a slider resets it to its default
  even when Option is let go before the mouse button, and the reset is one
  change, sent once. Before, letting go of Option first moved the setting
  to where the click landed. A click that AppKit reports only once the
  mouse moves or comes up still resets when it carries Option, and an edit
  a drag left open is closed before the reset, so a host never sees it
  still open. See
  [the window](../docs/use/plugins.md#convolvers-window).

## Done

- [x] **PLUG-56 — Option-click reset that holds to the release.** jrp's
  review of `7bd024f`, 26 November 2026, PR #309
  ([the window](../docs/use/plugins.md#convolvers-window)).
