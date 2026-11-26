<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-60

## Changed

- C++ library (PLUG-60): `HostProcessor` takes what to play only as a
  `HostPlayback`, which carries whether the filter has a dry signal; the
  constructors, `replace()` and `prepare()` that took a bare `Configuration`
  and decided it from the channel counts are gone. Give
  `filter_playback(configuration)` instead. A constructor taking any latency
  in frames, not only one the plug-ins offer, remains for tests and
  measurements.

## Done

- [x] **PLUG-60 — One way to build a HostProcessor.** 26 November 2026
  ([host.hpp](../include/convolver/host.hpp)).
