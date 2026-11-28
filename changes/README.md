<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Change fragments

Each pull request adds its own file here instead of editing the top of
[`CHANGELOG.md`](../CHANGELOG.md), the [done list](../TODO.md#done) in
`TODO.md` or the [decisions log](../docs/project/decisions.md). Two pull
requests then never edit the same lines, so these records stop causing
merge conflicts. `scripts/fold_changes.py` later moves every fragment's
entries into those pages and deletes the fragments. When to write one and
when they are folded:
[change fragments](../docs/develop/workflow.md#change-fragments).

## The form

Name the file after the pull request's TODO ID: `changes/PLUG-40.md`. A
second pull request for the same item adds its own file with a lower-case
suffix, such as `changes/PLUG-39-step6.md`; never edit a fragment that is
already on `main`. A pull request with several IDs has one file for each
([full CI runs](../docs/develop/workflow.md#full-ci-runs), rule 3).

```markdown
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-40

## Changed

- Plug-ins (PLUG-40): what a user notices, as the changelog says it. See
  [loading a filter](../docs/use/plugins.md#loading-a-filter).

## For contributors

- Tests (PLUG-40): what only contributors see.

## Done

- [x] **PLUG-40 — Reload a refused list when the host plays.** 21 November
  2026, PR #252 ([plug-ins](../docs/use/plugins.md#loading-a-filter)).

## Decisions: Plug-in behaviour

- **21 November 2026.** What jrp decided, in his words where they help. See
  [loading a filter](../docs/use/plugins.md#loading-a-filter).
```

- Start with the SPDX line and a level-one heading as the title (the ID is enough).
- Use only the sections the pull request needs, each a list:
  - **Added**, **Changed**, **Deprecated**, **Removed**, **Fixed**,
    **Security** and **For contributors** go under the same headings in the
    changelog's `[Unreleased]`;
  - **Done** holds the item's ticked line for TODO's done list, exactly as
    [recording progress](../docs/develop/workflow.md#recording-progress-in-todo)
    describes it, while the pull request removes the open item from
    `TODO.md`;
  - **Decisions:** followed by the name of an existing section of the
    decisions log (Direction, Releases, Plug-in behaviour, Identities and
    file formats, Configuration and sound, Diagnostics, Licences and
    dependencies, Documentation) holds jrp's decision, dated, as a log
    entry.
- Write links relative to the fragment, as in any page
  (`../docs/use/plugins.md`), so that they work where the fragment is read.
  Folding rewrites each for the page it lands in. Use inline links, not
  reference definitions.
- Put a TODO item's line only under **Done**: anywhere else it would be
  folded into that page and the item would vanish from TODO.

`python3 scripts/fold_changes.py --check` (run by pre-commit) refuses a
fragment it could not fold: a wrong name or section, an empty section,
prose instead of a list, a Done line that is not a ticked TODO line, or a
TODO line outside Done. The
repository checks treat an ID ticked in a fragment as defined and done.
