<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-31

## For contributors

- Gain figures (PERF-31): `detail::kernel_sums()`, which gives the
  worst-case peak and the noise estimate's gain for **No clipping**,
  **Estimate** and the plug-ins' gain figures at every filter load, sums an
  input and output joined by one path straight from its taps, as nearly
  every real filter is, and allocates its buffer as long as the longest
  impulse only when two paths join the same pair. The figures are
  bit-identical, so no gain changes. Timed alone on a 4-core Linux cloud
  machine for a 120 s stereo filter at 48 kHz, it fell from 50 to 57 ms to
  17 ms; `native.engine` now checks that one path for each pair allocates
  nothing and that one such pair followed by a shared one gives the same
  figures as summing each kernel whole
  ([load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22)).

## Done

- [x] **PERF-31 — The noise estimate's kernel sums without a copy.**
  28 November 2026, from the pre-1.0 review of 0.19.21
  ([load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22)).
