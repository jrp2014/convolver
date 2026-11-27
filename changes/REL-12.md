<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-12

## For contributors

- Releases (REL-12): a full release's notes say what was tested from the
  evidence attached to the candidate, as the production gate accepts it.
  The gate judges each ZIP and installer on its own evidence, and so do
  the notes: each host test that holds for a file (passed, on those bytes,
  with its record, the platform's architecture, 44.1, 48 and 96 kHz and
  mono and stereo) is named with its host, version and system build, and
  with the files it holds for when not all of the platform's; only a kind
  the platform's `unverified` rules allow and a file's evidence does not
  cover is listed as not verified, naming the system, the architecture or
  the files that lack it (by file name when a platform has several
  installers). Before, the notes listed every platform the rules
  allowed to ship untested, even with accepted evidence for it, and said
  nobody had tested it in a host; now they say no host test of those
  files was accepted, and that each was checked automatically only when
  the evidence shows it. `prepare_release.py`'s
  `verification_facts()` gives, for each platform and each of its files,
  the kinds that hold, the host test that holds (host, version, system
  build, record) and the kinds still outstanding; the notes are written
  from it, and the tests check those facts and the facts each sentence
  carries (what is named, whether an automatic check is claimed) rather
  than whole sentences (jrp's review of `547e2dd`, §5).
  `promotion_gate.py` gains `evidence_holds()` and `platform_files()`,
  which the gate and the notes share. See
  [production gates](../docs/develop/releasing.md#production-gates).

## Done

- [x] **REL-12 — Release notes from the evidence.** jrp's review of
  `7bd024f`, 26 November 2026, PR #314
  ([production gates](../docs/develop/releasing.md#production-gates)).
