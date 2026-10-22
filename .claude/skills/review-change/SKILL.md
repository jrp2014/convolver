---
name: review-change
description: Independently review a Convolver pull request or branch against this repository's rules before it is marked ready (real-time safety, .cfg compatibility, plug-in contract and identities, oracle tests, docs, TODO, CHANGELOG). Use when asked to review a change here, or as the independent review step in docs/workflow.md.
---
<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Review a change

You are the independent reviewer: assume nothing the author says is true until
you have checked it. Read `AGENTS.md` and `docs/workflow.md`
("Independent review" and "Definition of done") first; they hold the rules.

## Gather

1. Identify the pull request or branch and its TODO IDs. Read the description,
   handoff note and any earlier review comments.
2. Get the full diff against `main` and read every changed file, not just the
   hunks, where behaviour depends on context.
3. Read the TODO items and plan sections the change claims to implement.

## Check

Report only problems you can point to in the diff, with the file and line.

- **Correctness:** does the code do what the TODO item and plan say? Look for
  off-by-one latency or tail values, channel-order mistakes, unhandled failure
  paths and state left half-changed after an error.
- **Real-time path:** nothing reachable from `Engine::process`,
  `HostProcessor::process` or an adapter's audio callback allocates, locks,
  logs or does I/O. FFT planning and filter preparation stay off that thread.
- **Compatibility:** `.cfg` behaviour (relative paths, signed-zero routes,
  fractional gains, delay truncation, list selection); saved state and presets
  (version-1 presets keep loading); permanent identities (VST3 class IDs,
  AU codes, LV2 URIs) unchanged unless jrp decided otherwise.
- **Plug-in work:** the shared adapter contract in
  `docs/plans/2026-10-21-plugin-formats.md` (failed loads, initial restore,
  replacement, bypass, layouts) and the PLUG-2 comparisons against the
  independent oracle, not only the CLI.
- **Tests:** would a test fail if this change were wrong? Are tolerances fixed
  and justified? Are new tests registered in CTest and run by CI?
- **Claims:** commands and results in the description match what was run;
  "passes" means it was run, on the stated platform.
- **Documentation and tracking:** owning guide updated, new pages in
  `docs/README.md`, CHANGELOG for user-visible changes, TODO ticked or a
  `Progress` line added, completed plans archived.
- **Repository checks:** run `python3 scripts/check_repository.py`,
  `python3 scripts/check_doc_links.py` and `scripts/check_format.sh` (if
  clang-format-18 is available).

If you can build, run the tests yourself (AGENTS.md, "Build and test") and say
what you ran.

## Report

Post the review on the pull request: each finding with file and line, why it
matters, and a suggested fix; then a short list of what you checked and found
sound. Say plainly if you found nothing. Do not approve or merge; jrp does
that.
