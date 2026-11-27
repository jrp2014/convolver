<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-62

## Changed

- Plug-ins (PLUG-62): the Convolver log's line after an accepted load reads
  the same in every format. The VST3 plug-in now lists the settings restored
  with a project, as the Audio Unit did; the Audio Unit now says, for a
  filter kept that does not suit the host, what plays meanwhile (silence,
  Mix's dry share or what bypass plays), and, while the host may still
  change its rate or channels, that the filter is checked again, as the VST3
  plug-in did.

## For contributors

- Plug-ins (PLUG-62): `accepted_line()` in `host_log.cpp` composes that line
  for every adapter, instead of one copy in the VST3 processor and another
  in the Audio Unit. See [the adapter
  contract](../docs/develop/plugin-contract.md#status-and-log).

## Done

- [x] **PLUG-62 — One accepted-load log line for every format.** 26 November
  2026 ([host_log.hpp](../portable/host_log.hpp)).
