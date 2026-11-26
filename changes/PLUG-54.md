<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-54

## Changed

- `convolver-preset` (PLUG-54): a `--name` longer than 200 bytes of UTF-8,
  or with control characters, is now refused with the reason, for every
  format and before anything is written. Until now a `.vstpreset` was
  written without the name, and with no warning. A name taken from a file
  name keeps the old fallback: left out of the filter's state, so the
  window shows the filter file's name. `--help` now says that every format
  stores the name and the window shows it as its heading. See
  [make a preset](../docs/use/presets.md#make-a-preset).

## Done

- [x] **PLUG-54 — Refuse a `--name` a preset cannot keep.** jrp's reviews
  of `7bd024f`, 26 November 2026
  ([make a preset](../docs/use/presets.md#make-a-preset)).
