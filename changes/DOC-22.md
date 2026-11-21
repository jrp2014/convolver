<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DOC-22

## For contributors

- Host testing (DOC-22): the Mac walkthrough no longer assumes Latency
  starts at 256. GarageBand's settings step sets 256 and checks it is in use
  before the change to 1024, and REAPER's log line reads `from 0 to 1024`.
  Audacity's configuration-list check loops for at least two seconds, so
  the 85 ms example cannot end before the window sees the host's settings,
  and keeps the list selected for the export. The GarageBand preset steps
  delete each old preset first, so one left from an earlier preview cannot
  stand in under the same name, and the Latency checks name the menu value,
  not the detail beside it, which shows the time added. jrp found all three in his 21 November
  review.

## Done

- [x] **DOC-22 — Host-testing steps that match the 0.18 defaults.**
  21 November 2026, PR #268
  ([host testing](../docs/develop/host-testing.md#prepare-the-mac)).
