<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-14

## For contributors

- Host testing (HOST-14): `scripts/maintenance/host_doctor.py` lists the
  Convolver plug-ins installed on the test computer before a host test: each
  one's format, version, user or system scope and path. Only a bundle with
  Convolver's identifier counts as installed. It keeps found, opened and
  loaded apart: a program that only reads a bundle's files, such as an
  indexer opening `Info.plist`, is not reported as loading it; loaded means
  a running program has the current executable mapped (`lsof` on macOS,
  `/proc` on Linux), the only confirmation of the copy a host uses. It names
  duplicate installs, mixed versions, installs older than the newest
  download in `~/Downloads` or than `--expect`, and programs still running a
  copy that has since been replaced, and never changes anything. Its tests
  run on Linux; the macOS folders and `lsof` output are tested with fixtures
  only ([host doctor](../scripts/maintenance/README.md#installed-plug-ins-before-a-host-test)).

## Done

- [x] **HOST-14 — Check the installed plug-ins before a host test.**
  27 November 2026, at jrp's request; not yet run on a Mac
  ([host doctor](../scripts/maintenance/README.md#installed-plug-ins-before-a-host-test)).
