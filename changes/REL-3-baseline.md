<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-3

## Decisions: Releases

- **28 November 2026.** The published 0.19.21 replaces 0.18.29 as the
  baseline for the checks left, REL-3's independent walkthrough and
  COMPAT-1's REAPER project: they run on its exact files, which hold the fix
  for a host's first read of the settings after it restores a project or
  preset (VST3-31), the macOS and Windows window fixes (PLUG-67) and the
  pipe and Wave64 fixes (IO-12, IO-16, IO-17) (jrp: "Yes, bring the
  baseline forward"). Results recorded on earlier builds stay as records of
  those builds, and 0.13.2's to 0.18.29's kept files stay in COMPAT-1; this
  supersedes the 26 November choice of 0.18.29, which moved with the earlier
  baselines to the
  [superseded decisions](../docs/archive/releases/2026-11-20-superseded-decisions.md).
  See [Status: checks left](../docs/project/status.md#checks-left-before-publication),
  [TODO](../TODO.md#execution-priorities).
