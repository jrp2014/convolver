---
name: todo-item
description: Start, resume or hand over work on a Convolver TODO item (IDs such as PLUG-2, LV2-1, AU-1, VST3-1, DOC-4). Use when beginning work on an item, continuing someone else's branch, or stopping part-way.
---
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Work on a TODO item

The procedure lives in `docs/develop/workflow.md`; this skill only says which
section to follow. If a step seems missing, add it to that page, not here.

- **Starting:** follow "Before you start", then "While working".
- **Continuing someone else's branch:** follow "Picking up someone else's work".
- **Stopping or handing over:** follow "Handoff note".
- **Finishing:** work through "Definition of done", then have the
  "Independent review" done in a fresh session (the `review-change` skill) or
  by a person. Mark the pull request ready and request review from jrp.

Builds and tests are in `docs/develop/build.md` and `docs/develop/testing.md`;
the rules that apply everywhere are in `AGENTS.md`. Never merge, approve, push
to `main` or move tags.
