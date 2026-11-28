<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DOC-31

## For contributors

- Documentation (DOC-31): [where a script goes](../docs/develop/architecture.md#where-a-script-goes)
  now says which folder a new script belongs in: `scripts/` for building,
  testing, checking, versioning and releasing Convolver,
  `scripts/maintenance/` for looking after a clone or the developer's own
  computer, and beside its test, example or record for a script that
  belongs to one. A new [`scripts/README.md`](../scripts/README.md) lists
  every script there with what runs it and its test.

## Done

- [x] **DOC-31 — Document where scripts go, and audit them.** 27 November
  2026, PR #365, at jrp's request
  ([where a script goes](../docs/develop/architecture.md#where-a-script-goes)).

## Decisions: Documentation

- **27 November 2026.** Scripts that build, test, check, version or release
  Convolver go in `scripts/`; scripts that look after a copy of the
  repository or the developer's own computer go in `scripts/maintenance/`.
  See [where a script goes](../docs/develop/architecture.md#where-a-script-goes).
