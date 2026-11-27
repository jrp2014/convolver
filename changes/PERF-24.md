<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-24

## For contributors

- Plug-ins (PERF-24): the plug-ins' mixing of Gain, Mix, bypass, mute and a
  replacement's crossfade is one sum per output sample, the filter's output,
  the old filter's and the dry signal each times a share computed once per
  frame, instead of three formulas chosen per sample. With no replacement,
  bypass or mute in progress the output is bit-identical, apart from the sign
  of some zero samples under full bypass; during those transitions it
  changes by at most one float rounding step of the signal level, about
  2e-8 of the output peak (1,451 of 345,600 samples in a scripted session).
  See [HostProcessor](../portable/host_processor.cpp).

## Done

- [x] **PERF-24 — Plug-in mixing as one formula.** 26 November 2026
  (#313, [HostProcessor](../portable/host_processor.cpp)).
