<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-16

## For contributors

- Versioning (REL-16): changes under `packaging/` (installer sources, the
  release policy and the release notes) now count as source changes, so the
  version bot tags them. Since REL-13 moved the notes' words into
  `packaging/release-notes.md`, a preview's release-notes pull request
  changed nothing the bot counted, and its merge made no tag: #350, the
  tenth preview's notes, left the newest tag on the commit before it,
  without the folded changelog. `tests/versioning_tests.py` checks that a
  change to the notes alone makes a tag. See
  [source versioning](../docs/develop/versioning.md#automatic-patch-versions).

## Done

- [x] **REL-16 — A release-notes merge makes the tag.** 27 November 2026,
  found preparing the tenth preview, PR #351
  ([source versioning](../docs/develop/versioning.md#automatic-patch-versions)).
