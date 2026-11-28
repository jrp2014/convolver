<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# GAIN-4

## Changed

- Command line (GAIN-4): `--gain-db no-clip` and `--gain-db auto` report
  `no-clip gain` and `optimum gain` instead of `no-clip attenuation` and
  `optimum attenuation`. The figure is a gain, as `--gain-db` takes it, so
  a positive one amplifies; "attenuation" read the sign the wrong way round.
  The figure and the samples are unchanged
  ([gain](../docs/use/command-line.md#gain-and-attenuation)).

## For contributors

- Tests (GAIN-4): `native.cli` and `native.torture` read the figure from the
  new label.

## Done

- [x] **GAIN-4 — Report the gain estimate as a gain.** 28 November 2026,
  PR #392, as jrp decided
  ([gain](../docs/use/command-line.md#gain-and-attenuation)).

## Decisions: Diagnostics

- **28 November 2026.** The command line labels its gain estimates "gain",
  not "attenuation" ("Yes", to relabelling "no-clip attenuation", since
  the printed figure is a gain). See
  [gain](../docs/use/command-line.md#gain-and-attenuation).
