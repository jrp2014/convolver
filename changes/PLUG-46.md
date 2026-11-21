<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-46

## Fixed

- Plug-ins (PLUG-46, jrp's review of 21 November 2026): while the host has
  the plug-in stopped, the tail it reports and the window's Ring-out are the
  filter's own length when the filter's sample rate differs from the host's.
  Before, the filter's frames were counted at the host's rate: a 48 kHz
  filter with a 1 s tail, after the host started the Audio Unit at 96 kHz
  and stopped it, reported a 0.5 s tail and the window showed 500.0 ms. The
  VST3 plug-in now gives that tail in the host's samples at its last rate,
  rounded up. See the
  [adapter contract](../docs/develop/plugin-contract.md).

## For contributors

- Tests (PLUG-46): `au.adapter` `timing_in_seconds` follows jrp's sequence
  (a 48 kHz filter, the host's rate changed to 96 kHz, a start and a stop);
  `vst3.adapter` `edge_cases` checks the inactive tail at 44.1 and 96 kHz;
  `native.host_report` checks Ring-out at the filter's rate.

## Done

- [x] **PLUG-46 — Report a stopped plug-in's tail at the filter's own
  rate.** 21 November 2026, jrp's review of the 0.18 candidate.
