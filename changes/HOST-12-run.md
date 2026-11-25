<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-12

## For contributors

- Host testing (HOST-12): done. jrp's first 30-minute REAPER soak, on
  25 November 2026 with the 0.18.23 candidate at a 128-frame block, had no
  new underrun and passed all six checks; REAPER reported no audio
  underrun time at all, so the run cannot tell no dropout from a device
  that does not report them
  ([record](../docs/archive/reviews/2026-11-25-host12-reaper-soak.md)).

## Done

- [x] **HOST-12 — A long REAPER soak with reverb returns.** 25 November
  2026, PRs #288 and #294; jrp's run on the 0.18.23 candidate: no new
  underrun ([record](../docs/archive/reviews/2026-11-25-host12-reaper-soak.md)).
