<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DOC-23

## For contributors

- Measurements (DOC-23): [performance then and now](../docs/develop/fft-measurements.md#performance-then-and-now)
  records jrp's comparison of the 2006 release with the current engine on
  his Apple M5 Max. With a 65,536-tap stereo filter in large blocks both
  engines ran at about 1,540 times real time, against 40 times on a 2006
  Pentium 4; at a Latency of 256 frames (5.8 ms) the current engine was 4.5
  times as fast as the historical one with the same FFT library, and 3.4
  times as fast with Accelerate. The README's "Light on the system" point
  links it.

## Done

- [x] **DOC-23 — Record performance then and now.** 22 November 2026,
  PR #270 ([measurements](../docs/develop/fft-measurements.md#performance-then-and-now)).
