<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-26

## Changed

- Saving (PERF-26): when a host saves a project or preset, the plug-ins no
  longer copy the filter first, and they encode it without holding up the
  controls, a filter replacement or a restore. For a 12-million-sample
  filter (eight paths of 1.5 million taps), the copy took 25 ms and the
  encoding 54 ms, both while the controls waited; now neither holds them
  up, and the copy is gone (a 4-vCPU Xeon cloud VM, Linux, Clang 18,
  Release, best of 9). The bytes saved are the same.

## For contributors

- Saved state (PERF-26): VST3 `getState`, the Audio Unit's `SaveState` and
  LV2 `save` take a `HostSnapshot` under the control lock: the accepted
  filter as a `std::shared_ptr<const Configuration>` from
  `HostFilter::snapshot()`, with the preset name, the failed-load marker
  and the format's own parts (VST3's bypass and settings, the Audio Unit's
  filter file name, LV2's source path), then encode it outside the lock
  through the new `encode_host_state(const HostStateView&)` and
  `encoded_host_state_size(const HostStateView&)`, which borrow the parts.
  `HostStateView` has no default, so `encode_host_state({})` still means
  an empty `HostState`. A new `save_overlapping_replacement` case in each
  adapter's tests restores another filter after the lock is released and
  before the encoding (through test hooks: `saving_for_tests` in VST3 and
  the Audio Unit, `WorkerPoint::saving` in LV2) and checks that what is
  saved comes from before it and the next save from after it: in
  `vst3.adapter` the taps, preset name and settings; in `au.adapter` the
  taps, preset name and filter file name (its settings are parameters
  `AUBase` saves); in `lv2.adapter` the embedded filter and the path (its
  settings are ports the host saves). The Audio Unit case is compiled and
  run only on macOS CI. See [host state](../docs/develop/plugin-contract.md#host-state).

## Done

- [x] **PERF-26 — Saves from a snapshot, encoded outside the lock.** jrp's
  review of `7bd024f`, 26 November 2026, PR #319
  ([host state](../docs/develop/plugin-contract.md#host-state)).
