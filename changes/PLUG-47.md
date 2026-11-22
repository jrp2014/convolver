<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-47

## Fixed

- Plug-ins (PLUG-47, jrp's reviews of 21 and 22 November 2026): a
  configuration list chosen in Convolver's window before the host had
  played, which Convolver then loads by itself once the host plays with the
  window open (PLUG-40), now joins Recent when it loads, as a chosen file
  does, also when the window was closed and opened again meanwhile.
  Before, it was never added. See
  [loading a filter](../docs/use/plugins.md#loading-a-filter).

## For contributors

- Tests (PLUG-47): `vst3.adapter` `list_read_again` and `au.editor`
  `list_read_again` check Recent before and after the list is read again;
  the Audio Unit test chooses the list in a window that then closes.

## Done

- [x] **PLUG-47 — Add a list read again by itself to Recent.** 21 November
  2026, PR #269, jrp's review of the 0.18 candidate
  ([loading a filter](../docs/use/plugins.md#loading-a-filter)).
