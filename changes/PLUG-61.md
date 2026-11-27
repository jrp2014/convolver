<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-61

## Fixed

- Audio Unit (PLUG-61): a processing fault no longer shows Status Fault over
  a load failure that is showing, after the host initializes the unit or a
  new Latency starts a processor; the load failure comes first, as in the
  other formats.

## For contributors

- Plug-ins (PLUG-61): every adapter starts its processor, at a host start
  and for a new Latency, with `HostFilter::start()`, which builds it, sets
  its bypass, resets it, records the activation and holds its fault signal
  while a failure is shown, so ROBUST-8's ordering is written once instead
  of seven times. See [the adapter
  contract](../docs/develop/plugin-contract.md#status-and-log).

## Done

- [x] **PLUG-61 — One processor-start step for every format.** 26 November
  2026 ([host.hpp](../include/convolver/host.hpp)).
