<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- markdownlint-disable MD041 -->
<!--
Title: "ID: what it does", for example "PLUG-2: shared adapter test harness".
Open as a draft when you start; that is the claim. See docs/develop/workflow.md.
-->

## TODO items

<!-- IDs this pull request implements or advances, e.g. PLUG-2. "None" for housekeeping. -->

## What changes

<!-- What a user, host or developer will notice. For DSP: the audible or numerical effect. -->

## Handoff note

<!-- Keep this current; replace rather than append. -->

- **State:** draft / ready for review / blocked on …
- **Done:**
- **Next:**
- **Open questions and owner decisions:**
- **Watch out for:**

## Tests run

<!-- Only what you actually ran: platform, compiler, options, result. CI coverage is separate. -->

- [ ] Verification for each kind of change (docs/develop/workflow.md#verification-for-each-kind-of-change): rows followed and what ran; for code, the full CTest configuration:
- [ ] DSP change: oracle tests pass; worst error relative to peak (with its group and B):
- [ ] Plug-in change: validator, PLUG-2 comparisons, pluginval where it applies
- [ ] `pre-commit run --all-files` (includes clang-format), and clang-tidy when portable C++ changed

## Documentation and tracking

- [ ] Owning guide updated (docs/develop/documentation.md#one-home-for-each-topic)
- [ ] Affected product claims and summaries checked against current evidence (docs/develop/documentation.md#current-claims-and-dated-evidence)
- [ ] Change fragment `changes/<ID>.md` added (docs/develop/workflow.md#change-fragments): a changelog entry for user-visible changes and for every finished TODO item (by ID), the Done line, any decision of jrp's; CHANGELOG.md, TODO's done list, decisions.md and VERSION not edited (a version step is a `version:minor` or `version:major` label)
- [ ] TODO item removed with its Done line in the fragment, or its Remains, Worth doing when and Done when rewritten
- [ ] Processed records cleared: folded fragments, finished plans, scratch files (docs/develop/workflow.md#clearing-processed-records)
- [ ] Shared files touched (TODO, CMakeLists.txt, release policy, docs index, plug-in plan) are listed here:

## Independent review

<!-- Who or what reviewed this (a person, or a fresh session using the review-change skill), where the findings are, and how each was answered. -->
