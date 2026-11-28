<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# HOST-16

## For contributors

- Host testing (HOST-16): the host doctor no longer reports a program
  still running an earlier copy of another plug-in that is merely named
  Convolver; it checks only bundles whose identity is Convolver's, as its
  listing already said. A bundle whose identity cannot be read is still
  reported as a problem, but is no longer checked for running copies
  ([host doctor](../scripts/maintenance/README.md#installed-plug-ins-before-a-host-test)).
  `tests/host_doctor_tests.py`
  `test_another_bundle_still_loaded_is_not_a_problem` covers both cases.

## Done

- [x] **HOST-16 — Check only Convolver's own bundles for stale loaded
  copies.** 28 November 2026, from the second release audit of 0.22.0,
  PR #380 ([host doctor](../scripts/maintenance/README.md#installed-plug-ins-before-a-host-test)).
