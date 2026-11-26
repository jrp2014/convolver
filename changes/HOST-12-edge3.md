<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-12

## For contributors

- Host testing (HOST-12): the REAPER soak expects each return's own Active
  text: since EDGE-3, Status names a true-stereo filter
  ("Filter loaded: 48000 Hz, 2 in, 2 out; true stereo."), so on 0.18.25 and
  later the soak's true-stereo returns failed checks 2, 5 and 6 and gave no
  result (jrp's review of 25 November 2026). Check 6 now also needs the
  replaced return to read the true-stereo replacement's text, and the
  ReaScript waits for that text before reading it. See
  [a long REAPER soak](../docs/develop/host-testing.md#a-long-reaper-soak).
