<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-20

## For contributors

- Release (REL-20): the production gate and the release notes read the same
  validated host record. A release evidence file whose host test gives
  `host`, `host_version` or `os_build` as anything but non-empty text (the
  audit's `["7.86"]` passed qualification, then made the notes raise
  `TypeError`) is refused as it is read, naming the field; host evidence
  already in a candidate is not accepted by the gate, so the notes never
  name it, and a `record` must be an `https://` link given as text.
  `prepare_release_tests` takes such evidence through both steps. From the
  release audit of 0.22.0 (fix before release, item 2)
  ([production gates](../docs/develop/releasing.md#production-gates)).

## Done

- [x] **REL-20 — The gate and the release notes accept the same host
  evidence.** 28 November 2026, from the release audit of 0.22.0
  ([production gates](../docs/develop/releasing.md#production-gates)).
