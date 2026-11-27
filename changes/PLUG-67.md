<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-67

## Fixed

- Plug-in window on macOS (PLUG-67): the captions, labels and notes, and a
  problem's box, are worked out for the appearance the window is drawn in,
  so they stay readable in a dark host window on a light Mac (and the other
  way round) and after switching between Light and Dark while it is open.
- Plug-in window on macOS (PLUG-67): with no filter loaded, hovering over
  the blank space where the filter's facts go no longer shows their
  tooltip.
- VST3 window on Windows (PLUG-67): the status word's tooltip now says what
  the status means; it was empty.
- VST3 window on Windows (PLUG-67): Ctrl-click on a slider while a value
  typed in its box was not yet set resets the setting as one edit; it first
  set the typed value (or beeped at text that was not a number).
- VST3 window on Windows (PLUG-67): a window closed while it showed a
  refusal, reopened after the filter was fixed, no longer keeps the
  refusal's height as a blank gap; tooltips wrap at the same width after a
  DPI change.
- Documentation (PLUG-67): Waiting with a kept filter that does not suit
  the host is orange, as the window section says, not plain; the upgrade
  fixtures' note names the Gain mode Estimated.

## For contributors

- Tests (PLUG-67): `vst3.editor` on macOS checks that the secondary text
  follows the appearance it is drawn in and that the facts' headings have
  no tooltip without a filter; on Windows, the status word's tooltip,
  Ctrl-click after unset typing, and the height of a window reopened after
  a fixed refusal. The macOS and Windows window code and tests are built
  only on those systems.

## Done

- [x] **PLUG-67 — Window fixes from the first-pass review.** 27 November
  2026, from jrp's first-pass review of 0.18.27 to 0.18.34, PR #347
  ([Convolver's window](../docs/use/plugins.md#convolvers-window)).
