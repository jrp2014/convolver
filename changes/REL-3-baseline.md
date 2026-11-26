<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-3

## For contributors

- Releases: after publishing Convolver 0.18.29, jrp asked to move the
  baseline for the checks left, REL-3's walkthrough and COMPAT-1's REAPER
  project from 0.14.0 to 0.18.29
  ([decision](../docs/project/decisions.md#releases)).

## Decisions: Releases

- **26 November 2026.** The published 0.18.29 replaces 0.14.0 as the
  baseline for the checks left, REL-3's independent walkthrough and
  COMPAT-1's REAPER project: they run on its exact files, which hold the
  fixes for the refused preset that reset Latency (PLUG-48), the list that
  exported silence in Audacity (PLUG-49) and the VST3 window pairing with
  another instance (VST3-27, VST3-29). It is the first step on the path to
  a 1.0 release (jrp: "yes, move the baseline forward"). Results recorded on
  earlier builds stay as records of those builds, and 0.13.2's to 0.14.0's
  kept files stay in COMPAT-1; this supersedes the 19 November choice of
  0.14.0. See [Status: checks left](../docs/project/status.md#checks-left-before-publication),
  [TODO](../TODO.md#execution-priorities).
