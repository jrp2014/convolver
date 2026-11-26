<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-23

## Changed

- Plug-ins and command line (PERF-23): Gain mode Estimated, and `--gain-db
  auto`, find their figure up to three or four times sooner for long filters:
  about 1.3 s instead of 4.1 s for a 30-second stereo filter on a test
  machine, 2.0 s instead of 7.7 s for 30-second true stereo. The figure
  changes only by rounding (well under a millionth of a decibel). See
  [plug-in settings](../docs/use/plugins.md#settings).

## For contributors

- Engine (PERF-23): offline work gets an engine whose stages grow to eight
  times the largest real-time block (`detail::OfflineEngine`), and
  `convolver_estimate_benchmark` times the estimate. See
  [offline blocks](../docs/develop/fft-measurements.md#offline-blocks).

## Done

- [x] **PERF-23 — The noise estimate in large blocks again.** 26 November
  2026 ([offline blocks](../docs/develop/fft-measurements.md#offline-blocks)).
