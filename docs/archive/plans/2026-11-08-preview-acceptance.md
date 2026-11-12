<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Finish and qualify the first preview

> **Archived 12 November 2026 as superseded.** This plan set out how the
> first (0.12) preview would be qualified; REL-7, which applied it, was done
> on 8 November 2026. Three previews have been published (0.12.12, 0.13.2
> and 0.13.12), each with the checks left open that the
> [decisions log](../../project/decisions.md#releases) records. Its lasting
> rules, the fixed candidate, how a finding is classified, retesting in
> proportion and the stop rule, are now in
> [releasing: what blocks a preview](../../develop/releasing.md#what-blocks-a-preview);
> the gates and their results are on the [status page](../../project/status.md);
> the deferred work in section 6 is in [TODO](../../../TODO.md#execution-priorities);
> the review and record-keeping rules in section 5 are in the
> [workflow guide](../../develop/workflow.md#recording-progress-in-todo),
> and its contract table and test mapping, never done, are PLUG-36. The
> text below is unchanged apart from relative links.

For maintainers and testers: a bounded plan for demonstrating that the existing
macOS preview works, recording its limits and handing it to an independent user.

**In short:** hold one candidate still, complete the agreed journeys and release
gates, fix blocking defects, then stop expanding the test scope. Keep useful
regression coverage. CI scheduling is a [separate proposal](../../project/plans/2026-11-08-ci-scheduling.md),
not a prerequisite for this walkthrough.

The scope is the existing 0.12-series macOS preview: VST3 and Audio Unit with
the shared window, CLI and preset tool. This plan changes neither the audio
contract nor the package contents. [Status](../../project/status.md#what-still-blocks-the-first-preview)
owns the current gate results; [TODO](../../../TODO.md) owns progress and deferred
items. The [decisions log](../../project/decisions.md#releases) records the owner's direction.
REL-7 tracks applying this plan, not another implementation milestone.

## 1. Fix the candidate and the test boundary

- [ ] Check the gate summary for outstanding fixes before selecting the final
  candidate. VST3-16's fix and its
  [REAPER acceptance](../reviews/2026-11-08-vst3-16-reaper-acceptance.md)
  are already recorded; do not reopen that fix-specific gate merely to
  apply this plan. Keep performance experiments and optional window changes
  on their own branches; do not include them to fill time while testing.
- [ ] Build one candidate after those fixes merge. Record its version, source
  commit, CI run, package SHA-256, host versions, OS and architecture in the
  test record. A PR artifact's merge commit may differ from its PR head;
  record both when using one for a fix-specific retest.
- [ ] Use the existing [host steps](../../develop/host-testing.md) and
  [release procedure](../../develop/releasing.md). List their required and
  optional steps before the session. Existing required steps, including the
  outstanding Audacity checks, do not become optional under this plan.
- [ ] Keep the selected binaries, presets and guide together. Do not silently
  replace a tested binary with a newer build or treat an earlier host result
  as evidence for the final package.

A fix-specific retest can establish that a defect is resolved on its candidate.
It does not close all HOST-1, AU-4 or REL-3 checks. Preserve its dated evidence
with the exact candidate and link it from the relevant TODO item; keep any
unverified listening or other step explicit.

## 2. Preview acceptance criteria

The existing gate list remains binding. These are the observable outcomes
needed to close it; test commands and numerical tolerances stay in their
owning guides and tests, not in another copy here.

| Gate | Pass criterion | Evidence and owner |
| --- | --- | --- |
| Candidate and automated coverage | The exact package has passed applicable build, sanitizer, oracle, adapter, validator, source-rebuild and package checks. No unresolved failing required check is dismissed as an unrelated flake. | PLUG-5; candidate run and checks linked by maintainer |
| Ordinary VST3 use | REAPER and Audacity complete their required smoke steps on the candidate: discover the plug-in and presets, load a valid filter, play, use the window and settings, save, quit and reopen with filter and settings retained. Exports meet the existing comparison tolerances. | HOST-1 / VST3-11; named-host records by tester |
| Ordinary Audio Unit use | GarageBand completes the required AU smoke steps with the current window, including file selection, settings, latency, save/reopen and applicable accessibility checks. | AU-4 / AU-6; named-host record by tester |
| Failure and recovery | An incompatible first load gives the specified silence and explanation; a refused replacement keeps the valid filter. Host rate/layout changes recover as specified without losing saved state. Status describes the audio actually produced. | Required host steps and existing adapter/lifecycle regressions |
| Routing safety | The REAPER VST3 2→6 journey renders the correct outputs, survives save/reopen, and obeys the contract for Mix and bypass without sending unintended full-range audio to crossover outputs. AU crossover testing remains as scoped in the status page. | HOST-1; numerical export comparison and lifecycle coverage |
| Latency compensation | The VST3-16 transitions specified below update Convolver's effective latency and REAPER's compensation before a render or Offline/Online toggle; all four impulses align on both channels in the subsequent exports. The required host listening steps are separately recorded. | VST3-16; display observations, logs and measurements |
| AU thread safety | The AU-3 ThreadSanitizer build exercises bypass while rendering; reports are investigated, with no unresolved race in the tested path. A skipped or unavailable run is not a pass. | AU-3; build configuration and test output |
| Loading responsiveness | REL-6 qualifies the candidate source on the named Mac, cold and with full history, for the agreed 1, 10 and 30 s stereo workloads. Existing limits remain unchanged; failed or not-qualified required entries keep the gate open. | REL-6; machine details and structured timing results |
| Independent use | The independent tester completes the REL-3 journeys from the exact package and its documentation, without undocumented setup help. Findings are classified below; all blockers are resolved. | REL-3; tester's record, followed by jrp's publication decision |

For VST3-16, use a 48,000 Hz stereo project with 512-frame host callbacks,
a four-second stereo impulse clip, and identical unprocessed reference and
identity-filtered tracks. The clip has one unit impulse on both channels at
frames 24000, 72000, 120000 and 168000; every other sample is zero. Keep
monitoring volume low and track gains at unity. Start at 256-frame latency.
Change to 1024 while stopped, back to 256 while looping 0–4 seconds, then to
1024 while playing again. After each choice, before stopping or rendering,
observe the effective delay (21.3 or 5.3 ms) and REAPER's compensation display
(1024/1024 or 256/512 samples). Stop and render separate stereo stems, with
host compensation enabled, at the project rate. Each channel must have all
four peaks at the reference frames, with zero-frame offset. Compare decoded
audio rather than WAV metadata; preserve all six renders and the observations.
These exports do not measure the live transition gap or replace listening.

**Stop rule:** once these gates and the existing required host steps pass,
there are no unresolved blockers, and the deferred limitations are recorded,
the preview is ready for jrp's publication decision. Optional checks that were
not run remain explicitly unverified. Do not add new host permutations or
require another general review solely because further cases can be imagined.
An existing required gate can be waived only by an explicit owner decision.

## 3. Decide whether a finding blocks the preview

Classify by consequence and relevance to the promised use, not merely how
unusual its trigger seems. Every finding needs a reproduction or a concrete
code path, expected and actual behaviour, and an affected version.

| Classification | Examples | Action |
| --- | --- | --- |
| Blocking | Crash or hang; lost saved state; unsafe crossover output; wrong audio or compensation; inability to install or complete a required journey; misleading status that prevents safe recovery; broken required accessibility or qualification step | Fix narrowly with a regression where practical, then retest the affected journey. Record the new candidate and remaining gates. |
| Deferred limitation | Extra host/version combinations; cosmetic inconsistencies that do not hide a failure or prevent use; convenience features; speculative failure-path expansion; broader performance workloads | Record in TODO with impact, workaround if any, and a trigger for reconsideration. Describe user-relevant limits in the owning guide. Do not open an immediate implementation PR. |
| Evidence missing | Tool could not operate a control; audio was not heard; timing run was not qualified; an observation has no reproducible explanation yet | Mark unverified. If it belongs to a required gate, obtain evidence or ask jrp for an explicit scope decision; otherwise defer. Never convert missing evidence into a pass. |

A rare route to a crash, state loss or unsafe output is still blocking.
Existing out-of-memory protections and regressions stay; this plan defers
speculative additions, not known consequential defects. Incorrect install,
recovery or routing instructions are behaviour problems, not cosmetic prose.

## 4. Retest in proportion to what changed

- [ ] For each blocking fix, capture the observed host call order or failure
  input in the smallest existing adapter/lifecycle regression that reproduces
  it. UI focus and accessibility defects need editor or host checks, not an
  artificial lifecycle case. Review the expectation against the contract.
- [ ] During development, run the focused host retest plus the applicable
  automated checks. Do not repeat all manual journeys for each intermediate
  commit or for a change confined to non-shipped project notes.
- [ ] Before the independent walkthrough, run the existing required journeys
  on the final candidate, including steps that passed on earlier binaries.
  After a candidate-changing fix, record which results no longer apply and
  repeat the required final-candidate checks; never relabel older evidence.
- [ ] Leave optional checks and unexplored combinations as follow-up work once
  the stop rule is met. A further ordinary-use failure reopens the affected
  gate; an unrelated suggestion does not reopen all gates.

## 5. Make the rules and records easier to maintain

This is a focused contract clarification, not a new state-machine framework.

- [ ] Extend the existing loading/failure table in
  [the adapter contract](../../develop/plugin-contract.md#loading-and-failure)
  to connect empty, active, retained-but-unsuited and failed states with
  load, restore, rate/bus changes, bypass, Mix and processing faults.
  For each relevant transition state what plays, what saves and what Status
  reports. Separate channel safety from the diagnostic priority rules.
- [ ] Map the rows to existing tests in `tests/plugins/lifecycle.cpp` and the
  format adapter tests. Add focused coverage only for a missing contract
  or a reproduced defect; avoid enumerating an unbounded Cartesian product.
  The reference model must not call production decision functions to derive
  its expected result. Any ambiguous product rule goes to jrp.
- [ ] Put audio, safety, state retention, host interaction and required-gate
  findings first in reviews. Group non-blocking prose suggestions without
  hiding consequential documentation defects. Fix-only review follow-ups
  need not become a fresh repository-wide review.
- [ ] Keep TODO as the progress/backlog owner, CHANGELOG as the durable account
  of changes, and status as the gate summary. Refresh status when a gate,
  feature readiness or scope changes, not for every internal task update.
  Preserve dated evidence; link it instead of retelling every session.

A small table reconciliation supports qualification. An exhaustive rewrite of
the contract or test harness is not a new preview gate.

## 6. Deferred work and reconsideration triggers

| Work | Existing owner | Revisit when |
| --- | --- | --- |
| Non-uniform/zero-latency engine, new FFTs and CPU-specific speed work | PERF-2, PERF-3, FFT-2 | After the walkthrough, with resource budgets and measured need; keep oracle and backend coverage |
| Additional platforms, hosts and windows | HOST-1, VST3-12, VST3-13, LV2 items | A later preview explicitly includes them or a supported host has a reproducible failure |
| Additional gain modes, meters and convenience changes | PLUG-9, PLUG-10, AU-7; relevant open TODO items | After qualification; prioritise actual user difficulty |
| Speculative allocation-failure hardening and extreme combinations | ROBUST-1 and relevant open TODO items | A reproduction, credible severe code path or evidence from normal workloads justifies it |
| Broad concurrency architecture changes | Relevant AU/host items | ThreadSanitizer or a reproduction identifies a remaining ownership problem; no rewrite based on historical counts alone |
| CI scheduling reductions | CI-34, [separate proposal](../../project/plans/2026-11-08-ci-scheduling.md) | A dedicated PR demonstrates preserved coverage and faster feedback; it does not hold up the preview |

No tests, tolerances, validators or release gates are removed by this plan.
