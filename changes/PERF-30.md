<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-30

## For contributors

- Saved state (PERF-30): `decode_host_state()` copies each impulse's
  samples in one `memcpy` on a little-endian system, the reverse of the
  encoder's single append (PERF-25), instead of one `push_back` per sample;
  other systems still read a word at a time. `same_configuration()` compares
  each impulse's samples with one `memcmp`, still by their bits, so a signed
  zero still tells two filters apart (`native.state` now checks one in a
  tap). The bytes saved and the filter decoded are unchanged. Timed alone
  on a 4-core Linux cloud machine for a 120 s stereo filter at 48 kHz,
  decoding fell from 38 ms to 33 ms and the comparison from 11 ms to 5 ms,
  too little to show in a whole plug-in restore
  ([load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22)).

## Done

- [x] **PERF-30 — Decode saved state in bulk.** 28 November 2026, from the
  pre-1.0 review of 0.19.21: decoding and comparison timed before and after,
  not a whole restore, whose spread between runs is larger than the gain
  ([load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22)).
