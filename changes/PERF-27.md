<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-27

## Changed

- Loading (PERF-27): when the plug-ins accept a new filter, they find its
  worst-case peak (the window's Worst-case peak and Clip-safe) and its gain
  for steady noise in one pass over the filter instead of two, and Gain
  mode Estimated no longer works out the worst-case peak a second time. For
  a 30-second true-stereo filter at 48 kHz the two figures took 12 ms
  instead of 23 ms (four paths of 1,440,000 taps; a 4-vCPU Xeon cloud VM,
  Linux, Clang 18, `-O2`, best of 9). Both figures
  are the same, bit for bit.

## For contributors

- Filter analysis (PERF-27): `detail::kernel_sums()` in the private
  `portable/attenuation.hpp` builds each output's combined kernel per input
  once and sums both its absolute taps (`worst_case_peak()`) and its squares
  (the noise gain, formerly `noise_gain()` in `host.cpp`), each in the order
  its own pass used, so paths that partly or wholly cancel cancel before
  either sum. `detail::estimate_attenuation(c, worst_case_peak)` takes the
  bound `HostFilter` already has instead of working it out again; it
  validates the filter itself, and the public `estimate_attenuation()`
  validates once before working out the bound, so each entry point
  validates once. `worst_case_peak()` and `estimate_attenuation()` keep
  their public signatures. `native.engine`'s `kernel_sums_one_pass` compares both
  figures with reference two-pass copies, exactly, for true stereo with
  shared input mixes, wholly and partly cancelling paths and silent
  routing, and the estimate given the bound with the public one. See
  [the implementation map](../docs/develop/architecture.md).

## Done

- [x] **PERF-27 — Peak and noise gain in one pass.** jrp's review of
  `7bd024f`, 26 November 2026, PR #323
  ([implementation map](../docs/develop/architecture.md)).
