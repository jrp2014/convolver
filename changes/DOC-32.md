<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DOC-32

## For contributors

- Workflow (DOC-32): five rules for spending full CI runs. Push the final
  commit while the pull request is still a draft and mark it ready once;
  start with two pull requests in full CI at a time; group fixes only when
  they belong in the same review, with one commit and one fragment for each
  TODO item; re-run a recognised flake's failed job once, on the current
  commit; keep scope-based CI. A pull request with several IDs now has one
  fragment for each. CI-59 checks after a fortnight whether the rules
  helped. See [full CI runs](../docs/develop/workflow.md#full-ci-runs).

## Done

- [x] **DOC-32 — Rules for spending full CI runs.** 28 November 2026, at
  jrp's request ([full CI runs](../docs/develop/workflow.md#full-ci-runs)).

## Decisions: Documentation

- **28 November 2026.** jrp adopted five rules for spending full CI runs
  (DOC-32): push the final commit while still a draft and mark the pull
  request ready once, when a full-CI slot is available ("The aim is one full
  run on the intended SHA, not zero cancelled draft jobs"); start with two
  full-CI pull requests at a time, as a coordination rule, "not a new
  scheduler or CI gate"; group fixes when they belong in the same review ("A
  shared code path or regression test is a good reason to group; a shared
  pre-1.0 deadline alone isn't"), keeping one commit and one fragment per
  TODO item; retry a recognised flake's failed job once on the current
  commit, investigating if the signature differs or recurs; and keep
  scope-based CI. He chose "a short edit to the contributor workflow, not
  another maintenance script", and measures success by the macOS
  runner-minutes spent on cancelled full runs over the following fortnight
  (CI-59), "not the number of red or cancelled entries". See
  [full CI runs](../docs/develop/workflow.md#full-ci-runs).
