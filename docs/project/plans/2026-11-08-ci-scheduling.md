<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Reduce CI waiting without losing useful coverage

For CI maintainers: a separate, proposed workstream (CI-34) to reduce duplicated
builds and queueing while preserving the tests that protect Convolver.

**In short:** implement scheduling changes only in a dedicated PR. The
[preview acceptance plan](2026-11-08-preview-acceptance.md) proceeds independently.
This page changes no workflow and authorises no test removal or weaker threshold.
The first step is done: since 8 November 2026 a pull request builds the
candidates only when it changes what they are built from or hold
([CI triggers](../../develop/testing.md#when-pull-requests-build-the-candidates)),
approved by jrp to stop spending a candidate run on each code tweak.

## Baseline and constraints

- [ ] Before editing, record representative recent run IDs, revisions, runner
  images, queue delay, build/test duration and cache state. Report time to the
  first actionable failure and time to all required results, plus macOS runner
  occupancy. Job-hours alone are not the user's waiting time.
- [ ] Inventory each test's distinct configuration: compiler, FFT backend,
  sanitizer, plug-in format, installed package versus build tree, and source
  archive rebuild. A repeated test name does not prove duplicate coverage.
- [ ] Keep numerical oracles, allocation checks, saved-state/lifecycle
  regressions, validators, fuzz-found input replay and required platform
  builds. Keep useful cross-platform coverage even for this macOS preview.
  Do not change CI-32's cheap numerical tests as part of scheduling work.

## Proposed scheduling changes to evaluate

- [x] Evaluate candidate builds on demand during development and before a
  host-testing PR is ready, retaining automatic runs for packaging, release,
  installation, dependencies, relevant CMake/build changes and packaged
  executable guides. Ordinary code can affect packaging too: retain a full
  candidate qualification before release and a regular integration run.
  Identify required-check and artifact-consumer dependencies before gating.
  Done 8 November 2026: no status check is required on `main`; host testing
  and publishing use runs on `main` or a tag, or a run started by hand on a
  branch. Most code pull requests also change a page in `docs/use/`, so jrp
  chose the same day that such a change builds the candidates only when the
  page has a command they run, before or after the change.
- [ ] Evaluate narrower fuzz-campaign triggers covering configuration/list
  parsing, audio readers, saved-state decoding and all shared code, headers,
  limits, dependencies, harnesses and build options those targets depend on.
  Add a scheduled campaign and retain manual execution. Known crash-input
  replay remains in ordinary applicable tests.
- [ ] Preserve existing documentation and per-format scoping. Do not add
  unconditional Windows/AU builds to prose-only PRs. Unknown paths or failed
  change detection must conservatively run coverage, not silently skip it.
- [ ] Evaluate post-merge duplication separately. Do not assume a PR-head
  result validates a different merged tree; preserve integration and version
  workflow checks. Leave this unchanged unless the evidence supports a safe
  replacement.

## Verification and acceptance for the separate PR

- [ ] Update `scripts/ci_scope.py`, its tests in `tests/ci_scope_tests.py` and
  only the affected workflows. Test positive and negative path cases, shared
  dependencies, workflow/manual/scheduled events and unavailable diff data.
- [ ] Publish a before/after coverage table: every existing test configuration
  has a retained trigger, owner and observable result. Keep skipped,
  not-qualified, failed and passed distinct. No skipped required check may
  make an untested release eligible for publication.
- [ ] Run pre-commit, scope regression tests and affected workflows. Verify
  artifacts remain available for host testing and release qualification.
- [ ] Compare matched runs, recording cache warmth and runner variability.
  State measured savings without treating cancelled, packaging and test hours
  as disjoint totals. If a proposed restriction loses unique coverage, retain
  the old trigger or provide the missing check before merging.

No new CI service, scheduler or broad retry/suppression machinery is needed.
The statistical review motivates this work; its approximate historical counts
are not acceptance criteria or promised savings.
