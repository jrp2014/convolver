<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-37

## Changed

- convolver-preset (PERF-37): an output path that already exists is
  refused before the filter is read, so the message comes at once rather
  than after a long filter has been decoded and encoded.

## For contributors

- `make_presets()` checks each output for an existing path as it checks
  the output's format; writing still creates each one only if it is new.
  `tests/plugins/preset_tests.py` gives a missing filter and an existing
  output and requires the existing-path message.

## Done

- [x] **PERF-37 — Refuse an existing preset path before reading the
  filter.** 28 November 2026, from the release audit of 0.22.0.
