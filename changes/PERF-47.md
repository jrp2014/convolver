<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-47

## Changed

- Plug-ins (PERF-47): a filter's gain figures, the noise estimate behind
  Gain mode Estimated among them, are found once for as long as the host
  runs. Another instance of the same filter, a project reopened, or
  Audacity's next Play (a fresh plug-in at every Play) uses them at once:
  restoring a 30-second stereo filter in Estimated took 1.8 s every time
  on a Linux cloud machine, and now 49 ms after the first; a 120-second one
  took 20 s, now 0.3 s. Every figure is the same as before, bit for bit.
  See [settings](../docs/use/plugins.md#settings).

## For contributors

- `portable/figure_cache.hpp` keeps `HostFilter`'s figures (peak, noise
  gain, tail, silence, and the estimate once found) for the last
  `figure_cache_size` (8) filters accepted in the process, keyed by
  `configuration_identity()` and a second hash keyed at random once per
  process, both found in one pass (`detail::filter_key()`), so a file made
  to collide with another's FNV-1a identity cannot take its figures. In
  Manual a cached estimate stays out of `gains()`, as before, and choosing
  Estimated takes it from the cache. `native.host`'s `figures_cached` case
  checks the figures against `filter_gains()`, sharing between instances,
  the Manual-then-Estimated order, eviction and parallel loads. Timings in
  [load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22).

## Done

- [x] **PERF-47 — Find each filter's gain figures once per host session.**
  28 November 2026, from the investigation of user-visible performance wins
  ([load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22)).
