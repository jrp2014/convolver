<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-31

## For contributors

- Gain figures (PERF-31): `detail::kernel_sums()`, which gives the
  worst-case peak and the noise estimate's gain for **No clipping**,
  **Estimate** and the plug-ins' gain figures at every filter load, sums an
  input and output joined by one path straight from its taps, as nearly
  every real filter is, and allocates its buffer as long as the longest
  impulse only when two paths join the same pair. The figures are
  bit-identical where the compiler does not fuse multiplies and adds by its
  own choice (Clang, and GCC without FMA); with GCC's default fusion and FMA
  the noise figure may differ in its last bit, far below the float gain's
  resolution, so no gain changes. Timed alone on a 4-core Linux cloud
  machine for a 120 s stereo filter at 48 kHz, it fell from 50–57 ms to
  17 ms; `native.engine` now checks that one path for each pair allocates
  nothing and that such pairs give the same peak as summing each kernel
  whole, and the same noise gain to within 4 parts in 10^16
  ([load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22)).

## Done

- [x] **PERF-31 — The noise estimate's kernel sums without a copy.**
  28 November 2026, from the pre-1.0 review of 0.19.21, PR #391
  ([load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22)).

## Decisions: Configuration and sound

- **28 November 2026.** The gain figures (the worst-case peak and the noise
  gain) may differ in their last bit between compilers that fuse multiplies
  and adds differently, as GCC's default does on a processor with FMA, since
  that is far below the resolution of the float gain applied: jrp chose
  "Keep it, allow a last-bit change" for PERF-31's quicker sums over making
  the figures identical on every platform or dropping it. See
  [load timing](../docs/develop/load-timing.md#a-hosts-playback-start-perf-22).
