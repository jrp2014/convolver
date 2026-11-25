<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-12

## For contributors

- Host testing (HOST-12): `scripts/reaper_check.py --soak` plays four
  sparse sources into four Convolver reverb returns (10 s and 30 s, stereo
  and true stereo, made by the script and `bin/convolver-preset`) for
  30 minutes at a block of 128 frames or fewer, replaces one return's filter
  halfway, and reports one of three results from REAPER's last-underrun
  times: no new underrun, at least one new underrun, or underrun reporting
  unavailable. Its harness tests run in pre-commit against a stand-in
  REAPER; jrp's first run remains. See
  [a long REAPER soak](../docs/develop/host-testing.md#a-long-reaper-soak).
