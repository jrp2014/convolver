<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DOC-20

## For contributors

- Records (DOC-20): pull requests no longer edit the top of `CHANGELOG.md`,
  TODO's done list, the decisions log or `VERSION`, the places where two
  open pull requests used to conflict whenever one merged. Each adds its
  own file, `changes/<ID>.md`, with its changelog entries, its finished
  item's done line and any decision of jrp's
  ([form](changes/README.md#the-form)), and `scripts/fold_changes.py` folds
  the fragments into those pages, in the order they merged, and deletes
  them: in the release-notes pull request before each preview, or sooner
  in a pull request of its own. Pre-commit refuses a fragment that could
  not be folded, and the repository checks count an ID ticked in one as
  done. A minor or major version step is now a `version:minor` or
  `version:major` label on the pull request, which the version workflow
  reads through the GitHub CLI on merge; an explicit `VERSION` edit is
  still honoured. A new rule clears whatever a change has processed:
  folded fragments and scratch files are deleted, finished plans and dated
  evidence archived
  ([change fragments](docs/develop/workflow.md#change-fragments),
  [clearing processed records](docs/develop/workflow.md#clearing-processed-records),
  [source versioning](docs/develop/versioning.md#minor-and-major-versions)).
  Pre-commit now also runs the versioning tests when the version helper
  changes; before, they ran only on `main` after a merge.

## Done

- [x] **DOC-20 — Shared records without merge conflicts.** 21 November
  2026, PR #257
  ([change fragments](docs/develop/workflow.md#change-fragments)).

## Decisions: Documentation

- **21 November 2026.** Pull requests record their changelog entries,
  finished TODO items and jrp's decisions in their own change fragments
  under `changes/`, folded into the CHANGELOG, TODO and this log before each
  preview or sooner, instead of editing those pages; a version step is a
  `version:minor` or `version:major` label applied by the version workflow
  on merge, not an edit to `VERSION`. Whatever has been processed or
  consolidated is deleted or archived in the same change (jrp: "Yes,
  complete with a rule to get rid of, or archive, cruft once things are
  processed or consolidated"; DOC-20). Every finished TODO item still gets
  a changelog entry. See [Change
  fragments](docs/develop/workflow.md#change-fragments), [clearing
  processed records](docs/develop/workflow.md#clearing-processed-records),
  [source versioning](docs/develop/versioning.md#minor-and-major-versions).
