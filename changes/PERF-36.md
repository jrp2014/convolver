<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-36

## Changed

- convolver-preset (PERF-36): writing a `.vstpreset` no longer copies the
  whole encoded filter an extra time; the file's bytes are the same.

## For contributors

- `vst3_file()` in `portable/presets.cpp` appends the component state's
  length and the encoded state straight into the file buffer instead of
  building `component_bytes()` first. Checked byte-identical against main's
  `convolver-preset` for a 3-second stereo WAV, as well as by
  `tests/plugins/preset_tests.py`.

## Done

- [x] **PERF-36 — Write a `.vstpreset` without a second copy of the
  filter.** 28 November 2026, from the release audit of 0.22.0.
