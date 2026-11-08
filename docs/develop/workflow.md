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

Contents: [kinds of task](#kinds-of-task) ·
[before you start](#before-you-start) ·
[while working](#while-working) · [handoff note](#handoff-note) ·
[picking up someone else's work](#picking-up-someone-elses-work) ·
[verification](#verification-for-each-kind-of-change) ·
[definition of done](#definition-of-done) ·
[independent review](#independent-review) ·
[merging](#merging-and-afterwards) ·
[recording progress in TODO](#recording-progress-in-todo)

## Kinds of task

Decide first which kind of task you have been given. Only a change alters the
repository; recording a review's or a host test's results in it is a change.

- **A review** of a pull request, a branch, a page or the repository is
  read-only. Read the files, and build and run tests in a scratch build
  folder if that helps, but change no tracked file, create no branch or
  commit, and post nothing on GitHub unless the person asks. Give the
  findings to whoever asked, in the conversation. The
  [independent review](#independent-review) of an open pull request is the
  exception: it is posted on that pull request ([report](#report)).
  Fixing the findings is a change, started only when the person asks for it.
- **Local host testing** is steps jrp runs in a host on his own computer.
  Write the steps, then record what jrp reports as dated evidence where the
  task says (a pull request, the TODO item's `Progress` line, or a host
  record). Record a result as it was observed, including a step that did
  not work; do not turn it into a new sequence of host operations nobody
  has tried ([while working](#while-working)). A fix the testing calls for
  is a separate change.
- **A change** to the repository: one TODO item, or a tightly coupled group,
  on its own branch with a draft pull request. The rest of this page is about
  changes.

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
  request. The log is the only place decision notes and their dates go: other
  pages describe what was decided as plain fact, and may link to the log. When a decision changes what users get between releases, the
  [changelog](../../CHANGELOG.md) entry says what changed.
- **Keep shared files small to merge.** Several streams edit `TODO.md`,
  `CHANGELOG.md`, `CMakeLists.txt`, `packaging/release-policy.json`,
  `docs/README.md` and the [plug-in formats plan](../project/plans/2026-10-21-plugin-formats.md).
  Change only the lines your item needs, and merge `main` into your branch
  often. A change to something every stream relies on (the
  [shared adapter contract](plugin-contract.md),
  the PLUG-2 harness, the PLUG-4 identity list) goes in its own pull request,
  not inside one stream's.
- **Behaviour only a host or device can show** (a plug-in window, a file
  dialog, a host's save prompt): when jrp has to run each test, start from
  the platform's documented practice and from how established plug-in
  frameworks do it, not from a design of your own. Have the smallest working
  interaction tried in the real host early. Log the state at the first
  unexplained failure before trying another fix, and put any comparison into
  one clearly described run whose logs jrp collects. Check the
  [format notes](plugin-contract.md#format-notes) first. Where the command
  line or an automated test can show the same thing (load times, refusal
  messages, rendered audio), ask for that instead of steps in a host's
  window.
- **New dependencies:** if your work needs a new build or test dependency, add
  it in the same pull request to the CI workflow and to
  `.claude/hooks/session-start.sh`, so the next cloud session has it.

## Handoff note

Whenever you stop, even part-way:

1. Run `pre-commit run --all-files`, then commit and push all of this item's
   work on its branch. Leave out anything unrelated to the item (a scratch
   file, a local experiment, another item's fix): keep it uncommitted, or
   give it its own branch.
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

## Verification for each kind of change

Verify in proportion to the change. Every push runs `pre-commit run --all-files`;
the rest depends on what changed. A change that spans several rows needs every
row it touches, and when unsure which row applies, take the later one. Say in
the pull request which rows you followed and what you ran.

| What changed | Run before pushing | Not needed |
| --- | --- | --- |
| **Prose:** Markdown whose commands no test runs (most of `docs/develop/`, `docs/project/`, `TODO.md`, `CHANGELOG.md`, `AGENTS.md`, `.claude/skills/`, and pages in `docs/use/` whose marked commands did not change) | `pre-commit run --all-files`: Markdown lint, links and anchors, the repository rules | A native build, CTest or clang-tidy. CI runs only Lint, and for `docs/use/`, which the packages ship, the candidate workflow too ([documentation-only pull requests](testing.md#documentation-only-pull-requests)) |
| **Executable examples:** `README.md`, `examples/first-use/README.md` and `docs/use/first-filter.md` (`examples.first_use` runs their commands), or a command marked `<!-- executable-example -->` or `<!-- refused-example -->` in `docs/use/` | An [everyday build](build.md#everyday-build) and `ctest --test-dir build -R '^examples\.first_use$' --output-on-failure`; run each changed marked command with that build's `convolver` and check its output against the page (the [candidate workflow](testing.md#plug-in-candidate-checks) runs them from a package) | The sanitizer build and clang-tidy, unless code changed too |
| **Tooling:** Python under `scripts/` or `tests/` that CTest does not run, `.pre-commit-config.yaml`, `.github/`, `.claude/hooks/` | pre-commit (ruff, actionlint, and the checkers' own tests); the script's own tests, such as `python3 tests/repository_checks_tests.py`; for a workflow, the CI run on the draft pull request | A native build, unless the script is run by CTest or builds something |
| **Code:** C++, CMake, or a test CTest runs, outside the DSP path (CLI, I/O, configuration, presets, host lifecycle) | The [sanitizer build](build.md#sanitizer-build-for-changes) with the full CTest configuration; clang-tidy on the changed portable sources ([CONTRIBUTING](../../CONTRIBUTING.md#formatting-and-analysis)); the focused tests in the [change and test map](architecture.md#change-and-test-map) | |
| **DSP:** convolution, routes, delays, FFT backends, kernels, dither | As for code, and report the worst error of the direct double-precision oracle tests in `tests/native/engine_tests.cpp`; measure any speed claim in a separate Release build ([how to measure](fft-measurements.md#reproduce)) | |
| **Plug-in adapter or window:** `plugins/`, or shared host code they call | As for code, configured with the format's SDK; the format's validator, its [PLUG-2 comparisons](testing.md#plug-in-comparisons-plug-2) and [pluginval](testing.md#pluginval) where they apply. What only macOS builds (the window, the Audio Unit) runs in CI if you have no Mac; say so | Host steps, unless the change is about behaviour only a host shows ([host testing](host-testing.md)) |
| **Versioning:** `VERSION`, `SOURCE_PATHS`, the version workflow | `python3 tests/versioning_tests.py` ([source versioning](versioning.md)) | Running the version helper, which commits and tags |

## Definition of done

A pull request is ready for review when all of these hold, and the template's
checklist says so:

- **Built and tested, as reported.** The checks in the
  [verification table](#verification-for-each-kind-of-change) for every kind
  of change in the pull request pass; for code, the full CTest configuration,
  saying which platform, compiler and options you ran. Do not claim something
  passes unless you ran it, and keep results you ran apart from what CI is
  configured to run.
- **DSP changes:** the direct double-precision oracle tests in
  `tests/native/engine_tests.cpp` pass; report the worst error relative to
  the output peak, which `native.engine` prints with the group of cases, the
  kernel set and B it came from.
- **Plug-in changes:** the format's validator and the PLUG-2 comparisons pass,
  and pluginval where it applies ([pluginval](testing.md#pluginval)).
- **Checks:** `pre-commit run --all-files` (formatting included), and
  clang-tidy when portable C++ changed, as in
  [CONTRIBUTING](../../CONTRIBUTING.md#lint-checks).
- **Documentation:** the guide that owns the behaviour is updated
  ([page ownership](documentation.md#one-home-for-each-topic)); a new page is in
  the [index](../README.md); user-visible changes and every finished TODO
  item, contributor-only ones included, are in the
  [CHANGELOG](../../CHANGELOG.md) under its ID.
- **TODO:** the item is ticked with its evidence, or has a `Progress` line for
  what remains. A plan that is now fully implemented, or superseded,
  moves to [`archive/plans/`](../archive/plans/README.md)
  ([rule](documentation.md#current-claims-and-dated-evidence)).
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
  Every assertion checks a user-visible contract or a meaningful internal
  invariant, and a failure names what broke. Exact audio reproduction
  qualifies ([how to compare audio](testing.md#intermittent-failures));
  incidental metadata, such as a time stamp a library writes into a file,
  does not.
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

Post the review on the pull request it reviews: each finding with file and
line, why it matters and a suggested fix; then a short list of what you checked
and found sound. Change no files and push nothing: the author fixes the
findings. A review that is not this step, such as a review of `main` or of
another contributor's draft, goes to whoever asked for it, and onto GitHub
only if they ask. Say plainly if you found nothing. The author answers each
finding: fixed (with the commit), or not fixed and why. Reviewers do not
approve or merge; jrp does that.

## Merging and afterwards

- jrp reviews and merges. Agents never merge, approve, push to `main`, or
  move tags.
- CI must be green on the final commit. A failure that is also red on `main`
  is reported in the pull request, not ignored.
- After a merge, the version workflow bumps `VERSION` and tags `main`
  ([source versioning](versioning.md)).
- Merged branches are kept for a while, so their history can still be
  read, and jrp deletes them periodically (his choice, 6 November 2026;
  GitHub's automatic deletion stays off). Cloud agent sessions cannot delete
  branches; a reminder every two weeks lists the branches whose work is all
  in `main` (squash merges compared by content) and that have no open pull
  request, with a script jrp runs from a clone. Further work after a merge starts
  again from current `main`, even on a branch of the same name; a merged
  pull request is never reused.

## Recording progress in TODO

Each item in [TODO](../../TODO.md) has a stable ID. Do not reuse or renumber IDs;
add new ones after the highest in their group. While an item is partly done,
add one line under it per merged pull request:

```text
  Progress (21 October 2026, PR #31): mono and stereo cases run for VST3;
  2→6 and the lifecycle cases remain.
```

These lines are the one home for an item's progress. The
[status page](../project/status.md) gives only each feature's level and
each milestone condition's status in a sentence, names what remains and
links the item. Update status in the same pull request when a gate, feature
readiness or scope changes; internal progress alone belongs in TODO rather
than another retelling in status (DOC-6). During preview qualification use the
[acceptance plan](../project/plans/2026-11-08-preview-acceptance.md#3-decide-whether-a-finding-blocks-the-preview)
to distinguish blocking findings from deferred suggestions. Report audio,
safety, state retention, host behaviour and required-gate failures first;
group non-blocking prose suggestions without hiding consequential errors in
installation, routing or recovery instructions.

When the item is complete, move lasting behaviour into the owning guide and
any owner's decision into the [decisions log](../project/decisions.md). Then
replace the item with one ticked line in TODO's [done](../../TODO.md#done) list:
its ID and title, the date, the pull request if there was one, and a link to the page that now
describes it. In the same pull request, add an entry naming the ID under
`[Unreleased]` in the [CHANGELOG](../../CHANGELOG.md), even for work only
contributors see. TODO keeps open work only; the one-line entry keeps the ID
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
  [how the plug-ins behave](../use/plugins.md);
- every `-DCONVOLVER_…` option in the maintained documentation (not the
  changelog or open plans), the workflows and `scripts/` is a CMake option
  of this build, with one of its allowed values where it lists them (in any
  case, as CMake upper-cases them), and
  [building](build.md) describes every option (DOC-2);
- every option shown with `convolver`, `convolver-preset` or
  `convolver-equalizer-apo` in a code block or span of the maintained
  documentation (not the changelog or open plans), run by any path
  (`./bin/convolver`, `./build/convolver`) and as any command of a pipeline
  or `&&` list, with lines ending in `\` joined, and every option on its
  own in a span in `docs/use/`, is
  one that tool parses (a `"--name"` literal in its source), and each tool's
  guide names every option it parses: the
  [command line](../use/command-line.md), [presets](../use/presets.md) and
  [Equalizer APO](../use/equalizer-apo.md) (DOC-2).

`scripts/trademarks.py` also runs in pre-commit and lint CI (PLUG-5): the
README and every user guide that mentions VST must write its first mention
as "VST®" and end with the VST trademark block (Steinberg's unmodified logo
and the approved wording from `packaging/trademarks/vst.json`), and the macOS
installer's text must carry the mark and the notice. Run
`python3 scripts/trademarks.py --fix` after adding a page that mentions VST;
it writes the block but leaves the ® for you
([trademarks](../../packaging/trademarks/README.md)).

`scripts/check_format.py` finds the C++ sources to format, so a new adapter
folder is covered without editing a file list. pre-commit runs it with the latest stable packaged
clang-format and lint CI fails on any formatting difference, so run
`pre-commit run --all-files` before every push, not only before review. Extend these checks, with a
regression test in `tests/repository_checks_tests.py`, when a new rule should
hold everywhere.
