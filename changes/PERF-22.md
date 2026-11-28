<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-22

## Changed

- Audacity: the guide now says that a long filter delays the start of
  playback. On an M5 Max with Audacity 4.0.2 and Convolver 0.19.21, a 30 s
  stereo filter added about 0.12 s from Play to first sound and a 120 s
  filter about 0.40 s, compared with no effect, on each Play
  ([in Audacity](../docs/use/first-sound-in-a-host.md#in-audacity)).

## For contributors

- Performance (PERF-22): `convolver_vst3_restore_benchmark`, built with
  `CONVOLVER_BUILD_BENCHMARKS` in a VST3 build, times a host's playback
  start step by step with a fresh plug-in pair per trial, as Audacity 4
  makes, and the library passes inside those steps. jrp's Audacity
  measurements and the cloud's split are in
  [load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22);
  a quicker start for long filters (the benchmark on the Mac, then shared
  prepared filter data before parallel preparation) is PERF-29.

## Done

- [x] **PERF-22 — VST3 restore cost at Audacity's playback start.**
  27 November 2026, PR #360: jrp timed Play in Audacity 4.0.2 with 0.19.21
  (a 1 s filter 6 ms, 30 s 122 ms and 120 s 403 ms later than no effect;
  [raw data](../docs/benchmarks/2026-11-27-perf22-audacity.csv)). The cloud
  split the plug-in's part; with no simple, safe fix, the delay is
  documented, and a quicker start is the lower-priority PERF-29
  ([load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22)).
