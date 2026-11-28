<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-18

## For contributors

- Release (REL-18): `tests/release/source_tests.py` now assembles a
  corresponding-source archive from a stand-in repository and checks what CI
  never exercises, because CI assembles from a clean checkout with
  `--latest`: a dirty checkout still yields only the committed tree; a
  release policy or `VERSION` that differs from the commit is refused, with
  an error that now names both files and the commit; and with `--lock` the
  archive holds exactly the recorded releases, refusing a missing record, a
  cached archive that is another release, or `--version` alongside it.
  `fetch_nsis.py` needed no test of its own; why is in
  [plug-in candidate checks](../docs/develop/testing.md#plug-in-candidate-checks).

## Done

- [x] **REL-18 — Release scripts that could pass on bad input.** 28 November
  2026, PR #NNN ([plug-in candidate checks](../docs/develop/testing.md#plug-in-candidate-checks)).
