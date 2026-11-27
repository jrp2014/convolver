<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-17

## For contributors

- Releasing (REL-17): a full release's host tests are now per plug-in
  format. Each `host` entry in `packaging/evidence/X.Y.Z.json` names the
  `formats` it tested, and where a platform's host tests are required,
  every format its files ship needs its own passing test: a macOS ZIP or
  installer needs a VST3 test and an Audio Unit test. Until now one REAPER
  VST3 run passed the gate for an untested Audio Unit in the same file, and
  a second (GarageBand) test for that file was refused as a duplicate. The
  release notes name each test with its formats, and list what lacks one by
  format ("VST3 and LV2 on Linux"); a full release's LV2 limitation no
  longer says nobody used it in a desktop host, which a test now names.
  Found by the pre-1.0 review of 0.19.21. See
  [production gates](../docs/develop/releasing.md#production-gates).

## Done

- [x] **REL-17 — The production gate needs a host test for each plug-in
  format.** 27 November 2026, from the pre-1.0 review of 0.19.21, PR #356
  ([releasing](../docs/develop/releasing.md#production-gates)).
