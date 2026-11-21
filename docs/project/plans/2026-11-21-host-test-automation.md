<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Automate more of the host checks

For jrp and whoever carries on the work: which of the manual host checks
can move into CI or into one command on jrp's Mac, in what order, and
which stay manual. jrp asked for this work on 20 November 2026 ("Start on
the host-test automation"), after an external audit of `main` at `d04e5e0`
(0.16.5) suggested it. The audit was checked against `main` at `549ec9c`
(0.16.8) before this plan was written; where it was wrong, the plan says so.

**In short:** three steps, each its own TODO item and pull request. First,
the headless Ardour test plays the VST3 too and saves sessions at a larger
Latency (HOST-9). Second, the AVAudioEngine render of the Audio Unit
follows GarageBand's order of events and changes Latency while running
(AU-18). Third, one command on jrp's Mac drives REAPER through a render,
save, reopen, refusal and Latency change and writes a result file to paste
back (HOST-11). The first two run unattended in CI; the third needs jrp to
run it. None of them narrows the [checks left](../status.md#checks-left-before-publication):
whether a check can then be narrowed is jrp's decision, recorded in the
[decisions log](../decisions.md). Contents: [steps](#steps),
[what each replaces](#what-each-step-can-replace),
[not in this plan](#not-in-this-plan), [what stays manual](#what-stays-manual),
[rules](#rules).

## Steps

| Step | Item | What it does | Runs | State |
| --- | --- | --- | --- | --- |
| 1 | HOST-9 | Ardour, whose LV2 and VST3 hosts are its own code, plays both formats headless. Each case loads a preset from Ardour's preset list and sets Latency through Ardour's parameter: Ardour must be told the new latency without reopening, a second process that reopens the session must see the saved Latency, and a third process's export must match the oracle in time ([Ardour, headless](../../develop/testing.md#ardour-headless)) | CI, Linux: the LV2 and VST3 workflows | Done, 21 November 2026 (PR #237) |
| 2 | AU-18 | The AVAudioEngine render (AU-15) also starts the unit at its defaults and restores the preset while it runs, as GarageBand opens a project, at GarageBand's 44.1 kHz on a mono track; chooses Latency while the engine runs, which the host's property listener must hear of; and restores a second unit from the first one's `ClassInfo` ([AVAudioEngine render](../../develop/testing.md#avaudioengine-render)) | CI, macOS: the Audio Unit workflow | PR #242 |
| 3 | HOST-11 | A script in `scripts/` that jrp runs with one command on his Mac. It drives REAPER through ReaScript, as jrp's own runs did by hand on 7 November 2026 ([record](../../archive/reviews/2026-11-07-reaper-crossover-retest.md)): load the packaged presets, render and compare each with `convolver --compare`, save, quit, reopen and render again, a refused preset and the wrong host rate, and a Latency change with its render in time; VST3 first, then the Audio Unit in REAPER. It writes one result file for jrp to paste back | jrp's Mac, before a preview | First version (render, save and reopen) in progress; [how to run it](../../develop/host-testing.md#reaper-from-one-command) |

Step 3 is last because each change to it costs jrp a run; steps 1 and 2
can be finished unattended. Its first version is the smallest run that
shows REAPER doing one render and one save and reopen from the script, as
the [workflow guide](../../develop/workflow.md#while-working) asks for
behaviour only a host shows.

## What each step can replace

The checks are those in [checks left](../status.md#checks-left-before-publication).
"Can replace" means the automated result shows the same thing in another
host; jrp decides whether that is enough.

| Check | What it needs | Step that covers part of it |
| --- | --- | --- |
| 1, REAPER by ear | Listening, a click on bypass | None: listening stays manual. The click itself is already checked sample by sample (below) |
| 2, REAPER render, save, reopen, refusal, wrong rate | A render and a reopen in a DAW | 1 (in Ardour, both formats, with a saved Latency); 3 (in REAPER itself) |
| 3, REAPER stereo journey and crossover | `convolver --compare` on REAPER's renders | 3 |
| 5 and 8, GarageBand | Restore while running, 44.1 kHz, Latency while playing, save and reopen | 2 (in Apple's own hosting stack, not GarageBand) |
| 9, PERF-2's session | A saved Latency of 1024 reopening in time; Latency changes while playing | 1 (reopen in time), 2 (Latency while running); listening stays manual |
| COMPAT-1's REAPER project | A project saved by a release, reopened by later builds | 3 can save the project; keeping it is COMPAT-1's |

## Not in this plan

- **An automated click detector**, which the audit proposed. The native
  settings tests and the lifecycle tests already check every Gain, Mix and
  bypass ramp sample by sample against the oracle
  (`tests/native/host_settings_tests.cpp`, `tests/plugins/lifecycle.cpp`), so
  a detector would add machinery without protecting anything new. A click a
  host's own bypass makes is the host's.
- **Audacity's window.** Its scripting cannot drive Convolver's
  **Choose…**, and scripting its window would be fragile. Audacity's
  behaviour is encoded instead as adapter tests that replay what it does,
  as PLUG-27 does, with a short manual check when VST3 state code changes.
- **Apple's out-of-process hosting of the Audio Unit**
  (`AVAudioUnit.instantiate` with `.loadOutOfProcess`), which GarageBand
  uses. Worth adding to step 2's render once its first cases pass, because
  the window's custom properties (AU-6) cross that boundary; it gets an ID
  when it starts.
- **The macOS window's layout and parity between the two editor tests**
  (the audit's items 5 and 6): later, each only with a defect or a host
  finding that it would have caught.

## What stays manual

Some checks need a person whatever is automated: listening on real
hardware; installing a downloaded package past Gatekeeper and SmartScreen;
the host's own keyboard focus and window chrome; VoiceOver and NVDA
through the host's wrapper; Audacity's **Choose…** and export without
playing (check 10); Equalizer APO on a real Windows device (HOST-4); and
the independent walkthrough (REL-3).

## Rules

From jrp's standing direction: the tests protect audio, saved state and
host behaviour, not internal machinery; they extend the existing harnesses
([plug-in contract](../../develop/plugin-contract.md), `tests/plugins/`) rather
than adding a framework; each step avoids long CI cycles for small changes;
and the adapters' source is left to the items that own it (PLUG-39,
PLUG-27). Host behaviour a step finds that differs from the guides is
recorded under [unresolved host findings](../../develop/host-testing.md#unresolved-host-findings)
with a TODO item, as HOST-9's Ardour VST3 finding is under HOST-10.
