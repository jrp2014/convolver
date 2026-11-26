<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-54

## Changed

- `convolver-preset` (PLUG-54): a `--name` longer than 200 bytes of UTF-8,
  with control characters, or not valid Unicode text, is refused with the
  reason, for every format and before anything is written. 0.18.29 wrote
  an overlong name into AU and LV2 presets; the plug-in window now shows a
  preset's name as its heading, and its state keeps at most 200 bytes, so
  such a name is refused instead of being kept in one place and silently
  dropped from the other. A name taken from a file name keeps the
  documented fallback. `--help` now says that every format stores the name
  and the window shows it as its heading. See
  [make a preset](../docs/use/presets.md#make-a-preset).

## Done

- [x] **PLUG-54 — Refuse a `--name` a preset cannot keep.** jrp's reviews
  of `7bd024f`, 26 November 2026, PR #308
  ([make a preset](../docs/use/presets.md#make-a-preset)).
