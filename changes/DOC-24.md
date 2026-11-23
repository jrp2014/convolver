<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DOC-24

## Added

- README (DOC-24): a short **What to expect** section for the three uses
  Convolver is built around: room correction and crossovers, music at each
  track's own sample rate, and a reverb on a send. Each says what works
  today, with the measured load, and what you do. See
  [what to expect](../README.md#what-to-expect).

## For contributors

- TODO (DOC-24): new items from jrp's usage review of 22 November 2026 (summarised in
  PR #277):
  PLUG-51 (every output's routes in the window and Copy Diagnostics),
  HOST-12 (a 30-minute REAPER soak with four reverb returns, counting
  dropouts) and PERF-21 (the everyday load budget); PERF-16 also reports
  the time until a chosen filter is fully heard, and EDGE-4 suggests
  preparing the second filter only when a comparison starts. See
  [TODO](../TODO.md#execution-priorities).

## Done

- [x] **DOC-24 — What to expect, for each use.** 23 November 2026, PR #277
  ([README](../README.md#what-to-expect)).

## Decisions: Direction

- **23 November 2026.** EDGE-1 (a filter for every sample rate in one
  plug-in state) comes first among the edges, with IO-2's documented
  route; IO-2's offline conversion follows. A player that runs the plug-in
  at each track's own rate needs the right entry of a list at every rate,
  which no conversion can give, and should show which rate's entry plays.
  The state format still needs jrp's decision. Replaces the order of
  12 November 2026 (jrp approved the usage-review plan: "proceed"). See
  [edges plan](../docs/project/plans/2026-11-06-edges-over-other-convolvers.md#1-a-filter-for-every-sample-rate).

## Decisions: Releases

- **23 November 2026.** An everyday load budget beside the supported
  ceiling of 21 November: stereo room correction and the 2-to-6 crossover
  stay at or under 3% of one core at 48 kHz, Latency 0 and 64-frame
  callbacks, on the named machines of the workload measurements, checked
  before each preview (jrp approved the usage-review plan: "proceed";
  PERF-21). See [supported workloads](../docs/develop/fft-measurements.md#supported-workloads).
