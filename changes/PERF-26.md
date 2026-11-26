<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-26

## Changed

- Saving (PERF-26): when a host saves a project or preset, the plug-ins no
  longer copy the filter first, and they encode it without holding up the
  controls, a filter replacement or a restore. For a 12-million-sample
  filter, the copy took 25 ms and the copy and encoding together 65 ms,
  all of it with the controls held up (Linux x86-64, Clang 18, Release,
  best of 9, with #310's sample writer); now neither the copy nor the
  encoding holds them up, and the 25 ms copy is gone. The
  bytes saved are the same.

## For contributors

- Saved state (PERF-26): VST3 `getState`, the Audio Unit's `SaveState` and
  LV2 `save` take a `HostSnapshot` under the control lock: the accepted
  filter as a `std::shared_ptr<const Configuration>` from
  `HostFilter::snapshot()`, with the preset name, the failed-load marker
  and the format's own parts (VST3's bypass and settings, the Audio Unit's
  filter file name, LV2's source path), then encode it outside the lock
  through the new `encode_host_state(const HostStateView&)` and
  `encoded_host_state_size(const HostStateView&)`, which borrow the parts.
  `encode_host_state({})` is now ambiguous; write `HostState{}`. A new
  `save_overlapping_replacement` case in `vst3.adapter`, `lv2.adapter` and
  `au.adapter` restores another filter between the snapshot and the
  encoding (through test hooks: `saving_for_tests` in VST3 and the Audio
  Unit, a store callback in LV2) and checks that the taps, preset name,
  settings, filter file name and path saved all come from before it, and
  the next save from after it. The Audio Unit case is compiled and run only
  on macOS CI. See [host state](../docs/develop/plugin-contract.md#host-state).

## Done

- [x] **PERF-26 — Saves from a snapshot, encoded outside the lock.** jrp's
  review of `7bd024f`, 26 November 2026, PR #TBD
  ([host state](../docs/develop/plugin-contract.md#host-state)).
