<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-12-block

## For contributors

- Host testing (HOST-12): the REAPER soak's buffer check now needs the
  audio device's block to be a whole number from 1 to 128 frames, read
  before and while playing; a block REAPER reported as 0 passed it before
  (jrp's review of 25 November 2026). See
  [a long REAPER soak](../docs/develop/host-testing.md#a-long-reaper-soak).
