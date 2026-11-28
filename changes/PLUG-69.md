<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-69

## Changed

- Plug-ins (PLUG-69): a filter that plays as silence, because every tap of
  its IRs is zero or below the smallest normal float, now gets the same
  warning as on the command line (ROBUST-11). The Status text ends with
  "The filter plays as silence", and the window and the Convolver log say
  why and what to check. The filter still loads and plays, silently. See
  [plug-ins](../docs/use/plugins.md#channels).

## Done

- [x] **PLUG-69 — Warn about a silent filter in the plug-ins.** 28 November
  2026, at jrp's request
  ([plug-ins](../docs/use/plugins.md#channels)).
