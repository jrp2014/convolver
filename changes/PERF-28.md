<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-28

## For contributors

- Presets (PERF-28): `convolver-preset` making one preset in several
  formats checks each format once and encodes the filter's state once,
  through `presets::PresetData`, before it creates any file; each writer
  then wraps those bytes. Exporting VST3, AU and LV2 together checked the
  state seven times and encoded it three times. The files are byte for
  byte the same as before; a 30-second stereo IR exported in all three
  formats took 140 ms instead of 153 ms (median of 15 Release runs, Linux).
  Exclusive creation and the removal of every preset after a failure are
  unchanged; every format is now checked before any output path, so with an
  existing output and a format that cannot hold the filter, the format's
  refusal is the one given. `write_preset()`, which nothing called once the
  tool used `PresetData`, is removed. `native.presets` checks that `PresetData` writes what the
  single-format writers give, never replaces a file, refuses a format it
  did not check, and refuses an unwritable format before creating anything.

## Done

- [x] **PERF-28 — Check and encode a preset's state once for every
  format.** jrp's duplication review of `547e2dd8`, 26 November 2026
  ([presets](../docs/use/presets.md)).
