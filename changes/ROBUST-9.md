<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# ROBUST-9

## Fixed

- Plug-ins (ROBUST-9): when the host started the plug-in and its processor
  could not be built, and memory then ran out while Convolver recorded why,
  the Status could read Active although nothing played (an LV2 instance
  played silence, VST3 and the Audio Unit refused the start). The failure is
  now recorded before anything is allocated for its message, so the Status
  says Load failed, as ROBUST-7 already did for a failed load. Found by the
  pre-1.0 review of 0.19.21. See the
  [adapter contract](../docs/develop/plugin-contract.md#states-and-transitions).

## For contributors

- `HostFilter::fail_activation()` is now `noexcept`; `native.host`
  `unrecorded_failures` checks both overloads with no memory left.

## Done

- [x] **ROBUST-9 — An activation failure is recorded when memory runs out.**
  27 November 2026, from the pre-1.0 review of 0.19.21
  ([adapter contract](../docs/develop/plugin-contract.md#states-and-transitions)).
