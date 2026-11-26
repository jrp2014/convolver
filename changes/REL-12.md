<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-12

## For contributors

- Releases (REL-12): a full release's notes say what was tested from the
  evidence attached to the candidate, as the production gate accepts it.
  Each host test that holds for all of a platform's files (passed, on those
  bytes, with its record, the platform's architecture, 44.1, 48 and 96 kHz
  and mono and stereo) is named with its host, version and system build;
  only a kind the platform's `unverified` rules allow and its evidence does
  not cover is listed as not verified. Before, the notes listed every
  platform the rules allowed to ship untested, even with accepted evidence
  for it. `prepare_release.py`'s `verification_facts()` gives, for each
  platform, its files, the host test that holds (host, version, system
  build, record) and the kinds still outstanding; the notes are written
  from it and the tests check those facts, not sentences (jrp's review of
  `547e2dd`, §5). `promotion_gate.py` gains `evidence_holds()` and
  `platform_files()`, which the gate and the notes share. See
  [production gates](../docs/develop/releasing.md#production-gates).

## Done

- [x] **REL-12 — Release notes from the evidence.** jrp's review of
  `7bd024f`, 26 November 2026
  ([production gates](../docs/develop/releasing.md#production-gates)).
