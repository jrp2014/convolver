<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-74

## Changed

- Plug-ins: in Gain mode Estimated, the filter's noise estimate (seconds
  for a long reverb) is found on a thread of the plug-in's own. Choosing
  Estimated in the VST3 window no longer freezes the host (a 30 s reverb
  held it 1.5 s, a 120 s one 15 s), and a restore returns at once, so
  instances of different filters find their estimates at the same time
  when a project opens (four 30 s instances: 5.8 s → 1.7 s on a 4-core
  machine). Until the estimate is ready, Estimated plays the Clip-safe
  figure, never louder, then fades to its own. The plug-in waits for it
  before it starts processing, and when it restores a project during an
  offline render, so an export always plays Estimated's own figure from
  its first sample. The Audio Unit's Latency changes and LV2's worker no
  longer wait for an estimate either.

## For contributors

- `HostEstimator`, `search_estimate()`, `finish_estimate()` and
  `estimate_ready()` (`portable/host_estimator.hpp`), with
  `HostFilter::find_estimates_later()`, `estimate_wanted()` and
  `estimate_found()`; `detail::estimate_attenuation()` takes a stop flag,
  read between blocks of noise, which changes nothing when unset. Every
  adapter keeps one estimator per instance and stops it before teardown.
  `native.host`'s `estimate_searched` case checks the search, its answer
  for the right filter only, `finish_estimate()` and the stop flag; the
  VST3 and LV2 tests wait for the "estimate is ready" log line
  (`test_log::wait_for()`, `Log::wait_for()`). Timings in
  [load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22).

## Done

- [x] **PLUG-74 — Find Estimated's estimate off the host's thread.**
  28 November 2026, PR #395, from the investigation of user-visible
  performance wins ([contract](../docs/develop/plugin-contract.md#settings)).
