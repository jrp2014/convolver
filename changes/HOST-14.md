<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-14

## For contributors

- Host testing (HOST-14): `scripts/maintenance/host_doctor.py` lists the
  Convolver plug-ins installed on the test computer before a host test: each
  one's format, version, user or system scope, path and whether a running
  program has its current files loaded now (`lsof` on macOS, `/proc` on
  Linux), which is the only confirmation of the copy a host uses. It names
  duplicate installs, mixed versions, installs older than the newest
  download in `~/Downloads` or than `--expect`, and programs still running a
  copy that has since been replaced, and never changes anything. Its tests
  run on Linux; the macOS folders and `lsof` output are tested with fixtures
  only ([host doctor](../scripts/maintenance/README.md#installed-plug-ins-before-a-host-test)).

## Done

- [x] **HOST-14 — Check the installed plug-ins before a host test.**
  27 November 2026, at jrp's request; not yet run on a Mac
  ([host doctor](../scripts/maintenance/README.md#installed-plug-ins-before-a-host-test)).
