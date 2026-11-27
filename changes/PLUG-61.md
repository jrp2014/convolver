<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-61

## For contributors

- Plug-ins (PLUG-61): every adapter starts its processor, at a host start
  and for a new Latency, with `HostFilter::start()`, which builds it, sets
  its bypass, resets it, records the activation and holds its fault signal
  while a failure is shown, so ROBUST-8's ordering is written once instead
  of in six places (LV2's silent processor for a start that cannot be built
  keeps its own short sequence). The hold now comes before the audio thread
  can take the new processor: the Audio Unit held it only afterwards, in
  its status update, so a fault in the first blocks of a new Latency's
  processor could briefly show Status Fault over a load failure. See [the
  adapter contract](../docs/develop/plugin-contract.md#status-and-log).

## Done

- [x] **PLUG-61 — One processor-start step for every format.** 26 November
  2026 (#321, [host.hpp](../include/convolver/host.hpp)).
