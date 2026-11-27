<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-60

## Changed

- Library (PLUG-60), incompatible, hence 0.19.0: `HostProcessor` takes what
  to play only as a `HostPlayback`, which carries whether the filter has a
  dry signal; the constructors, `replace()` and `prepare()` that took a bare
  `Configuration` and decided it from the channel counts are gone. Give
  `filter_playback(configuration)` instead. The constructor taking any
  latency in frames, not only one the plug-ins offer, remains for tests and
  measurements; it takes a `HostProcessor::AnyLatency`, which a frame count
  converts to, so `HostProcessor(playback, frames, {})` means the default
  settings, not a latency of 0.

## Done

- [x] **PLUG-60 — One way to build a HostProcessor.** 26 November 2026
  (#317, [host.hpp](../include/convolver/host.hpp)).
