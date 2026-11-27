<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-66

## For contributors

- Host reports (PLUG-66): the window's line for each output decides its
  brackets from the routes, not by searching its own words. A path's
  inputs are bracketed before an output gain unless they are one input
  with neither gain nor delay, and an output's paths are bracketed before
  its own delay when more than one path reaches it; before, the first
  rule looked for a space after "In N" and the second for `"; "`.
  `route_word()` in `portable/host_report.cpp` writes one route, so a
  single input is no longer wrapped in a one-element vector. The window's
  text is unchanged: the crossover and matrix expectations in
  `native.host_report` `output_routes` are as they were, and a new case
  pins an input gain before an output gain (`(In 2 ×0.3) ×0.5`). A path
  with no inputs, which every loader refuses, now adds nothing to the
  line rather than an empty part.

## Done

- [x] **PLUG-66 — Routing brackets decided from the routes.** 27 November
  2026, from jrp's review of `d6d03ec9` (simplification 3), PR #341
  ([Convolver's window](../docs/use/plugins.md#convolvers-window)).
