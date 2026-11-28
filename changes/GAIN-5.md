<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# GAIN-5

## For contributors

- Tests (GAIN-5): `native.cli`'s check that a silent filter prints no gain
  figure looks for the labels GAIN-4 introduced, `no-clip gain` and
  `optimum gain`; it looked for "attenuation", which GAIN-4 removed, so it
  passed whatever was printed. From John's second external audit of
  28 November 2026.

## Done

- [x] **GAIN-5 — The silent-filter test checks the new gain labels.**
  28 November 2026, from the external audit of 0.22.7.
