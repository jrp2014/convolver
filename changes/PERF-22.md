<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-22

## Changed

- Audacity: a long filter delays the start of playback, because Audacity
  sets Convolver up again at every Play. Measured with 0.19.21 on an Apple
  M5 Max: about 0.1 s for a 30 s filter, 0.4 s for a 120 s filter and
  6 ms for a 1 s filter
  ([in Audacity](../docs/use/first-sound-in-a-host.md#in-audacity)).

## For contributors

- Performance (PERF-22): `convolver_vst3_restore_benchmark`, built with
  `CONVOLVER_BUILD_BENCHMARKS` in a VST3 build, times a host's playback
  start step by step with a fresh plug-in pair per trial, as Audacity 4
  makes, and the library passes inside those steps. jrp's Audacity
  measurements and the cloud's split are in
  [load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22);
  preparing the filter is over half the plug-in's part, and spreading it
  over the cores is PERF-29.

## Done

- [x] **PERF-22 — VST3 restore cost at Audacity's playback start.**
  27 November 2026, PR #360: jrp timed Play in Audacity 4.0.2 with 0.19.21
  (a 1 s filter 6 ms, 30 s 121 ms and 120 s 403 ms later than no effect;
  [raw data](../docs/benchmarks/2026-11-27-perf22-audacity.csv)). The cloud
  split the plug-in's part; with no simple, safe fix, the delay is
  documented, and jrp chose parallel preparation as a lower-priority item,
  PERF-29
  ([load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22)).
