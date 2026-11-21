<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-47

## Fixed

- Plug-ins (PLUG-47, jrp's review of 21 November 2026): a configuration
  list chosen in Convolver's window before the host had played, which the
  window then loads by itself once the host plays (PLUG-40), now joins
  Recent when it loads, as a chosen file does. Before, it was never added.
  See [loading a filter](../docs/use/plugins.md#loading-a-filter).

## For contributors

- Tests (PLUG-47): `native.recent_files` checks when a refused file joins
  the list (`RefusedRecentFile`); `au.editor` `list_read_again` chooses the
  list in the window and checks Recent once it is read again. The Windows
  window uses the same helper; no Windows window test plays a list.

## Done

- [x] **PLUG-47 — Add a list the window reads again to Recent.** 21
  November 2026, jrp's review of the 0.18 candidate.
