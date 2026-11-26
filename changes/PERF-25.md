<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-25

## Changed

- Saving (PERF-25): plug-ins write a filter's samples into a project or
  preset in one copy per impulse response on little-endian systems (every
  system Convolver ships for), instead of four bytes at a time. Encoding a
  12-million-sample filter took 39 ms instead of 54 ms (Linux x86-64, Clang
  18, Release, best of 9). The bytes saved are the same.

## For contributors

- Saved state (PERF-25): `native.host`'s `saved_state_bytes` case pins a
  small filter's saved bytes word for word, negative zero and a subnormal
  sample included, and reads them back. Other byte orders keep the
  word-at-a-time path, checked here by forcing it.

## Done

- [x] **PERF-25 — Saved samples in one copy.** jrp's review of `7bd024f`,
  26 November 2026
  ([host state](../docs/develop/plugin-contract.md#host-state)).
