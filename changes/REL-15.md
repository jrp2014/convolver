<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-15

## For contributors

- Releases (REL-15): the production gate accepts host evidence that covers
  44.1, 48 and 96 kHz and mono and stereo among other rates or layouts
  (192 kHz, a 6-channel crossover); it required exactly those. Evidence of
  the wrong shape (no list, or entries that are not whole numbers) is
  reported as lacking coverage instead of stopping the gate with a Python
  error. Only `"production_enabled": true` turns production on, and
  `contract.validate_policy()` refuses a value that is not a JSON boolean
  (`"false"` in quotes read as on) and a `required_production_evidence`
  that is not a non-empty list of distinct names, or is missing while a
  platform has production rules. A tag's policy from before REL-1, with
  none of these, still loads. `tests/release/gate_tests.py` and
  `manifest_tests.py` check each case.

## Done

- [x] **REL-15 — Production gate: covered evidence and a boolean switch.**
  27 November 2026, from jrp's first-pass review of 0.18.27 to 0.18.34,
  PR #348 ([production gates](../docs/develop/releasing.md#production-gates)).
