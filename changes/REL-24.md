<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-24

## For contributors

- Releases (REL-24): drafting a release requires the latest manual run of
  each full CI workflow on the candidate tag and commit to pass. A failed,
  cancelled or unfinished run now blocks the draft even if an earlier run passed.

## Done

- [x] **REL-24 — Require the newest manual tag CI runs to pass before drafting
  a release.** 28 November 2026, PR #399
  ([releasing](../docs/develop/releasing.md#publishing-an-unsigned-preview)).
