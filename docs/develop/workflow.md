<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Taking on, handing over and reviewing work

[Documentation index](../README.md) · [TODO](../../TODO.md) · [Agent rules](../../AGENTS.md)

**In short:** work on one [TODO](../../TODO.md) item, or a tightly coupled
group, per branch and pull request. Open a **draft pull request as soon as you
start**: its title names the TODO ID, so everyone can see the claim. Keep a
**handoff note** in the pull request description, so anyone can pick the work
up from there. Mark the pull request ready only when the
[definition of done](#definition-of-done) holds, including an
[independent review](#independent-review). jrp reviews and merges; agents
never merge.

This page applies to people and coding agents alike. Several streams of work
(the shared plug-in groundwork, then VST3, LV2 and AU) run at the same time,
often in sessions that end without warning. These rules let one contributor
continue where another stopped.

Contents: [before you start](#before-you-start) ·
[while working](#while-working) · [handoff note](#handoff-note) ·
[picking up someone else's work](#picking-up-someone-elses-work) ·
[definition of done](#definition-of-done) ·
[independent review](#independent-review) ·
[merging](#merging-and-afterwards) ·
[recording progress in TODO](#recording-progress-in-todo)

## Before you start

1. Choose from the [execution priorities](../../TODO.md#execution-priorities).
   Check that the item's dependencies are done.
2. Check that nobody has claimed it. Search the open pull requests for the ID
   (for example `PLUG-2`), and read the item's `Progress` lines in TODO.
   If someone has, pick it up (see below) or choose something else.
3. Read the item, the plan section it links to, the
   [implementation map](architecture.md) and the guide that owns the
   behaviour ([page ownership](documentation.md#one-home-for-each-topic)).
4. Check the source on `main`: part of the item may already be done.
5. Create a branch from current `main`. Push a first commit and open a
   **draft** pull request titled `ID: what it does` (for example
   `PLUG-2: shared adapter test harness`). Fill in the
   [template](../../.github/pull_request_template.md), including the handoff note.
   The open draft pull request is the claim, and CI runs
   automatically only once it exists ([CI triggers](testing.md#ci-triggers)).

## While working

- **Push at every stopping point.** Cloud sessions are discarded when they end,
  and unpushed work is lost.
- **Run `pre-commit run --all-files` before every push,** draft commits
  included; stage any files it changed and run it again until it passes. jrp
  sees every red CI run. Never bypass the Git hook with `--no-verify`.
- **Keep commits focused.** State the audible or numerical effect of a DSP
  change in its commit message ([AGENTS.md](../../AGENTS.md#rules)).
- **Update the handoff note** whenever you stop, even for a short break.
- **Do not make the owner's decisions.** Ask jrp before changing anything that
  is permanent or changes what users get: plug-in identities (IDs, URIs, AU
  codes), state or preset formats, `.cfg` syntax, licences, defaults that
  change the sound, dependencies shipped in releases, and anything published.
  Continue only on work that does not depend on the answer. Record the answer,
  with its date, in the [decisions log](../project/decisions.md) and in the pull
  request. Developer and project pages that describe the work may add
  "(owner's decision, 21 October 2026)"; user pages (`docs/use/`) never carry
  decision dates. When a decision changes what users get between releases, the
  [changelog](../../CHANGELOG.md) entry says what changed.
- **Keep shared files small to merge.** Several streams edit `TODO.md`,
  `CHANGELOG.md`, `CMakeLists.txt`, `packaging/release-policy.json`,
  `docs/README.md` and the [plug-in formats plan](../project/plans/2026-10-21-plugin-formats.md).
  Change only the lines your item needs, and merge `main` into your branch
  often. A change to something every stream relies on (the
  [shared adapter contract](plugin-contract.md),
  the PLUG-2 harness, the PLUG-4 identity list) goes in its own pull request,
  not inside one stream's.
- **New dependencies:** if your work needs a new build or test dependency, add
  it in the same pull request to the CI workflow and to
  `.claude/hooks/session-start.sh`, so the next cloud session has it.

## Handoff note

Whenever you stop, even part-way:

1. Run `pre-commit run --all-files`, then commit and push everything.
2. Replace the handoff note in the pull request description (below).
3. If a partial merge is useful, add a `Progress` line to the TODO item
   ([recording progress](#recording-progress-in-todo)).

The pull request description holds one handoff note, kept current. Replace it
rather than appending, so it always describes the branch as it is now:

- **State:** draft, ready for review, or blocked (and on what).
- **Done:** what works, with the commands that show it.
- **Next:** the next concrete step, specific enough to start on without
  reading the whole conversation that produced the branch.
- **Open questions:** owner decisions needed, and anything you were unsure of.
- **Watch out for:** traps you found (a flaky test, an SDK quirk, a host
  behaviour), so the next person does not rediscover them.

## Picking up someone else's work

1. Read the pull request description, its review comments and its CI results.
2. Read `git log main..BRANCH` and the diff.
3. Rebuild and rerun the tests ([build from source](build.md#sanitizer-build-for-changes))
   before relying on the earlier results.
4. Continue on the same branch where you can. If your tooling requires its own
   branch, start it from theirs, and say in both pull requests which one
   replaces the other, then ask jrp to close the old one.

## Definition of done

A pull request is ready for review when all of these hold, and the template's
checklist says so:

- **Built and tested, as reported.** The full CTest configuration passes; say
  which platform, compiler and options you ran. Do not claim something passes
  unless you ran it, and keep results you ran apart from what CI is
  configured to run.
- **DSP changes:** the direct double-precision oracle tests in
  `tests/native/engine_tests.cpp` pass; report the worst error.
- **Plug-in changes:** the format's validator and the PLUG-2 comparisons pass,
  and pluginval where it applies ([pluginval](testing.md#pluginval)).
- **Checks:** `pre-commit run --all-files` (formatting included) and
  clang-tidy, as in [CONTRIBUTING](../../CONTRIBUTING.md#lint-checks).
- **Documentation:** the guide that owns the behaviour is updated
  ([page ownership](documentation.md#one-home-for-each-topic)); a new page is in
  the [index](../README.md); user-visible changes are in the
  [CHANGELOG](../../CHANGELOG.md).
- **TODO:** the item is ticked with its evidence, or has a `Progress` line for
  what remains. A plan that is now fully implemented moves to
  [`archive/plans/`](../archive/plans/README.md).
- **Reviewed:** an [independent review](#independent-review) has been done and
  its findings answered in the pull request.

## Independent review

Before marking a pull request ready, get it reviewed by someone or something
that did not write it: a person, or a fresh agent session with no stake in the
change. In Claude Code, the repository's `review-change` skill starts this
procedure. The reviewer assumes nothing the author says is true until they have
checked it.

### Gather

1. Identify the pull request or branch and its TODO IDs. Read the description,
   the handoff note and any earlier review comments.
2. Get the full diff against `main`, following renames, and read every changed
   file, not only the hunks, where behaviour depends on context:

   ```sh
   git fetch origin main
   git diff -M --stat origin/main...HEAD
   git diff -M origin/main...HEAD
   ```

3. Read the TODO items, and the plan or contract sections the change claims to
   implement.

### Check

Report only problems you can point to in the diff, with file and line.

- [ ] **Correctness:** the code does what the TODO item and plan say. Look for
  off-by-one latency or tail values, channel-order mistakes, unhandled failure
  paths and state left half-changed after an error.
- [ ] **Real-time path:** nothing reachable from `Engine::process`,
  `HostProcessor::process` or an adapter's audio callback allocates, locks,
  logs or does I/O. FFT planning and filter preparation stay off that thread.
- [ ] **Compatibility:** `.cfg` behaviour and the other rules in
  [AGENTS.md](../../AGENTS.md#rules) (relative paths, signed-zero routes,
  fractional gains, delay truncation, list selection); saved state and presets
  (version-1 presets keep loading); permanent identities (VST3 class IDs, AU
  codes, LV2 URIs) unchanged unless jrp decided otherwise.
- [ ] **Plug-in work:** the [plug-in adapter contract](plugin-contract.md)
  (failed loads, initial restore, replacement, bypass) and
  [channel layouts](plugin-identities.md#channel-layouts), checked by the PLUG-2
  comparisons against the independent oracle, not only the CLI.
- [ ] **Moves and deletions:** text removed from one page exists on another,
  and old paths and anchors are gone from code comments, scripts, workflows,
  skills and maintained index pages under `docs/archive/`, which the link
  checker does not read (`git grep -n 'old/path'`).
- [ ] **Tests:** a test would fail if the change were wrong; tolerances are
  fixed and justified; new tests are registered in CTest and run by CI.
- [ ] **Claims:** commands and results in the description match what was run;
  "passes" means it was run, on the stated platform.
- [ ] **Documentation and tracking:** as in the [definition of done](#definition-of-done),
  and the pages follow the [documentation rules](documentation.md#writing-rules)
  for their reader.
- [ ] **Repository checks:** run the checks without changing files:
  `python3 scripts/check_format.py`, `python3 scripts/check_repository.py` and
  `python3 scripts/check_doc_links.py` (or `pre-commit run --all-files` on a
  clean tree, then report any diff it leaves, since its formatter edits files).
  Report any red CI run on the branch, including formatting on an older commit.

If you can build, run the tests yourself ([sanitizer build](build.md#sanitizer-build-for-changes))
and say what you ran.

### Report

Post the review on the pull request: each finding with file and
line, why it matters and a suggested fix; then a short list of what you checked
and found sound. Say plainly if you found nothing. The author answers each
finding: fixed (with the commit), or not fixed and why. Reviewers do not
approve or merge; jrp does that.

## Merging and afterwards

- jrp reviews and merges. Agents never merge, approve, push to `main`, or
  move tags.
- CI must be green on the final commit. A failure that is also red on `main`
  is reported in the pull request, not ignored.
- After a merge, the version workflow bumps `VERSION` and tags `main`
  ([source versioning](versioning.md)). Delete the merged branch.

## Recording progress in TODO

Each item in [TODO](../../TODO.md) has a stable ID. Do not reuse or renumber IDs;
add new ones after the highest in their group. While an item is partly done,
add one line under it per merged pull request:

```text
  Progress (21 October 2026, PR #31): mono and stereo cases run for VST3;
  2→6 and the lifecycle cases remain.
```

When the item is complete, move lasting behaviour into the owning guide and
any owner's decision into the [decisions log](../project/decisions.md). Then
replace the item with one ticked line in TODO's [done](../../TODO.md#done) list:
its ID and title, the date, the pull request if there was one, and a link to the page that now
describes it. TODO keeps open work only; the one-line entry keeps the ID
defined, and the [repository checks](#automated-checks) reject a TODO ID that
is used but never defined.

## Automated checks

`scripts/check_repository.py` runs in pre-commit and lint CI. It checks that:

- every TODO ID mentioned in the maintained documentation is defined in TODO,
  and no ID is defined twice;
- every maintained page under `docs/` is listed in the [index](../README.md)
  and sits in a reader folder (`use/`, `develop/` or `project/`);
- every open plan in [`project/plans/`](../project/plans/) has a dated file name, and every
  archived plan is listed in the [archive index](../archive/plans/README.md);
- repository paths named in `AGENTS.md`, `CONTRIBUTING.md`, the
  [implementation map](architecture.md) and this page exist, and so do those
  four pages;
- new source, script and maintained documentation files carry an SPDX licence
  line;
- only known folders and files sit at the top level of the repository, and
  every adapter under `plugins/` has tests under `tests/` and is named in
  [how the plug-ins behave](../use/plugins.md).

`scripts/check_format.py` finds the C++ sources to format, so a new adapter
folder is covered without editing a file list. pre-commit runs it with a pinned
clang-format 18 and lint CI fails on any formatting difference, so run
`pre-commit run --all-files` before every push, not only before review. Extend these checks, with a
regression test in `tests/repository_checks_tests.py`, when a new rule should
hold everywhere.
