---
name: todo-item
description: Start, resume or hand over work on a Convolver TODO item (IDs such as PLUG-2, LV2-1, AU-1, VST3-1, DOC-4). Use when beginning work on an item, continuing someone else's branch, or stopping part-way.
---
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Work on a TODO item

The procedure lives in the [workflow guide](../../../docs/develop/workflow.md);
this skill only says which section to follow. If a step seems missing, add it
to that page, not here. The link checker checks these links.

- **First:** check that this is a change and not a review or host testing
  ([kinds of task](../../../docs/develop/workflow.md#kinds-of-task)).
- **Starting:** follow [before you start](../../../docs/develop/workflow.md#before-you-start),
  then [while working](../../../docs/develop/workflow.md#while-working).
- **Continuing someone else's branch:** follow
  [picking up someone else's work](../../../docs/develop/workflow.md#picking-up-someone-elses-work).
- **Stopping or handing over:** follow the
  [handoff note](../../../docs/develop/workflow.md#handoff-note).
- **Finishing:** run the checks in the
  [verification table](../../../docs/develop/workflow.md#verification-for-each-kind-of-change),
  work through the [definition of done](../../../docs/develop/workflow.md#definition-of-done),
  then have the [independent review](../../../docs/develop/workflow.md#independent-review)
  done in a fresh session (the `review-change` skill) or by a person. Mark the
  pull request ready and request review from jrp.

The rules that apply everywhere are in [AGENTS.md](../../../AGENTS.md). Never
merge, approve, push to `main` or move tags.
