<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DIAG-13

## For contributors

- Host layer (DIAG-13): the failure record for a filter Convolver keeps
  although it does not suit the host (its kind, request, rate, channel
  counts, silence and reason) is built by one function,
  `unsuited_failure()` in `portable/host.cpp`, both when a load keeps such
  a filter (PLUG-42) and when a start finds it (`HostFilter::activation()`).
  Each caller still decides what happens next, and a replacement refused
  while another filter plays keeps its own path. A later change to that
  diagnostic reaches both. No change to what plays or what Status, the
  window or the log say (`native.host`, `native.host_report`).

## Done

- [x] **DIAG-13 — One record for a kept filter that cannot play.** jrp's
  duplication review of `547e2dd8`, 26 November 2026
  ([states and transitions](../docs/develop/plugin-contract.md#states-and-transitions)).
