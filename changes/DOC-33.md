<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DOC-33

## Changed

- Documentation (DOC-33): the preset guide now says up front that a mono
  filter (one input, one output) plays separately on each channel of a
  track with as many outputs as inputs, so a mono reverb IR needs no
  stereo copy; its opening had said a preset's outputs must match the
  host's ([plug-in presets](../docs/use/presets.md)). The command-line
  guide gives `--compare`'s size limit: 67,108,864 samples per file,
  counting every channel, about 2 minutes 55 seconds of 8 channels at
  48 kHz ([comparing two files](../docs/use/command-line.md#comparing-two-files)).

## For contributors

- `TODO.md` records the release audits' remaining performance and CI
  findings as PERF-39 to PERF-46, CI-61 to CI-63 and ROBUST-12, none of
  which blocks 1.0, and asks for PERF-30 and PERF-31 to be measured as
  Audacity's time from Play to first sound.

## Done

- [x] **DOC-33 — The preset guide's mono filters and the comparison
  limit.** 28 November 2026, from the release audit of 0.22.0.
