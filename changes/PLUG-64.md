<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-64

## For contributors

- Plug-ins (PLUG-64): a new Gain mode takes one shared step in every
  format, `change_gain_mode()` in `portable/host_log.cpp`: it asks
  `HostFilter::estimate()` for the filter's estimate when Estimated needs
  it, gives it to the playing processor in place, and returns the log line
  and whether it reports a problem. The Audio Unit, VST3 and LV2 adapters
  each repeated those steps; they keep their own scheduling, status and
  host notifications around the shared step. Nothing users see changes.
  `native.host_settings` plays Estimated through it (`estimate_keeps_history`)
  and checks that a failed estimate is explained, not thrown
  (`filter_estimates`).

## Done

- [x] **PLUG-64 — One step for a new Gain mode in every format.**
  27 November 2026, from jrp's duplication review of `547e2dd8`, PR #328
  ([settings](../docs/develop/plugin-contract.md#settings)).
