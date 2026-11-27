<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# EDGE-17

## Changed

- Documentation (EDGE-17): the host quick start no longer says a filter
  plays only with its own channels; it names the two exceptions, a mono IR
  file on each channel and a four-channel IR file as true stereo on stereo,
  and links to where the configuration guide explains them.

## For contributors

- Filter selection (EDGE-17, jrp's review of `547e2dd`, §2): loading,
  `--inspect` and the advice when nothing fits take each candidate's
  default routing from one decision, `detail::candidate_layout()` (the
  wanted rate, `default_layout()`, and the exclusion of a four-channel file
  marked as Ambisonics B-format), with `detail::b_format_refused()` for
  when the advice names B-format; before, each wrapped `default_layout()`
  in its own rate check and B-format exclusion. The four true-stereo routes
  are one table, `detail::true_stereo_routes` (`portable/true_stereo.hpp`),
  read by the loader, `--inspect` and the plug-ins' Routing line, instead
  of three copies. No selection or routing changes. `native.inspection`
  now checks, for six filters and the B-format files at eight wanted rates
  and channel counts, that `--inspect` calls a filter playable exactly when
  loading takes it, with the routing loading lays out (true-stereo routes
  written out in the test, apart from the table). See
  [the implementation map](../docs/develop/architecture.md).

## Done

- [x] **EDGE-17 — One routing decision for loading and `--inspect`.**
  jrp's review of `547e2dd`, 26 November 2026, PR #TBD
  ([implementation map](../docs/develop/architecture.md)).
