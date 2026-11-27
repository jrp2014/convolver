<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-59

## For contributors

- Presets (PLUG-59): `PresetData` keeps what its writers need besides the
  encoded state, the LV2 layout and the filter's identity for a
  `.vstpreset`'s filter name, instead of borrowing the whole `HostState`,
  which no longer has to outlive it unchanged. The LV2 layout is looked up
  once, after the check, and `convolver-preset` reports the layout the
  writer used (`PresetData::lv2_layout()`); the filter's identity is worked
  out once for every `.vstpreset` output. The files written are the same.
  jrp's review of `d6d03ec9`, simplification 2.

## Done

- [x] **PLUG-59 — Preset data without a borrowed state.** jrp's review of
  `d6d03ec9`, 27 November 2026, PR #343
  ([architecture](../docs/develop/architecture.md)).
