---
name: todo-item
description: Start, resume or hand over work on a Convolver TODO item (IDs such as PLUG-2, LV2-1, AU-1, VST3-1, DOC-1). Use when beginning work on an item, continuing someone else's branch, or stopping part-way.
---
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Work on a TODO item

The rules live in `docs/workflow.md`; read it first. This skill is the
checklist for applying them. Do not duplicate the rules here; if they change,
change the guide.

## Starting

1. Read the item in `TODO.md` and the "Execution priorities" table. Confirm its
   dependencies are done on `main`.
2. Look for an existing claim: search open pull requests for the ID in the
   title (GitHub tools), and read the item's `Progress` lines. If one exists,
   go to **Resuming** instead.
3. Read the linked plan section in `docs/plans/`, the relevant rows of
   `docs/project-structure.md`, and the guide that owns the behaviour
   (`docs/documentation.md`, "One home for each topic").
4. Check the source on `main`; part of the item may already exist.
5. Work on your branch from current `main`. Push a first commit, then open a
   **draft** pull request titled `ID: what it does`, filling in
   `.github/pull_request_template.md` with the handoff note.
6. If anything needs an owner's decision (identities, state or preset formats,
   `.cfg` syntax, licences, defaults that change the sound, shipped
   dependencies, publishing), ask jrp and continue only on what does not depend
   on the answer.

## Resuming someone else's work

1. Read the pull request description (handoff note), its review comments and
   its CI results.
2. Fetch the branch; read `git log main..BRANCH` and the diff.
3. Rebuild and rerun the tests (AGENTS.md, "Build and test") before trusting
   earlier results.
4. Continue on that branch if you can. If you must use another branch, start it
   from theirs and say in both pull requests which replaces which.

## Stopping or handing over

1. Commit and push everything; cloud sessions are discarded.
2. Replace the handoff note in the pull request description: State, Done (with
   commands), Next (concrete), Open questions, Watch out for.
3. If you are stopping before the item is finished and a partial merge is
   useful, the TODO item gets a `Progress (date, PR #n): …` line in that
   pull request.

## Finishing

Use the `review-change` skill in a fresh session (or ask a person) for the
independent review, then work through the "Definition of done" in
`docs/workflow.md`. Mark the pull request ready and request review from jrp.
Never merge, approve, push to `main` or move tags.
