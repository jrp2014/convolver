<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Consolidate the plug-ins' load and restore path

For jrp, who approves the targets and the order, and for whoever then carries
out PLUG-39: which states the three plug-in adapters keep apart today on the
path that loads and restores a filter, how many of each should remain, and a
sequence of small steps, each one revertible and protected by tests of audio,
saved state and host behaviour. It changes no code. Counts are of `main` at
`1de4452`, with PR #193 (PLUG-38) merged.

**In short:** the adapters keep about 40 pieces of state on this path that
must agree with one another or with `HostFilter`, and each adapter decides
for itself, in two or three places, what an accepted request requires. Three
reductions remove most of them without changing what anyone hears, saves or
sees: one shared decision for each accepted request, extending `HostFilter`
as PR #193 began; one shared lifetime mechanism for the playing processor,
in which the audio thread adopts a new processor and the control side frees
the old one later without waiting, as LV2 already does; and diagnostics
built as a view of that state instead of separate copies and text that is
parsed back. The open-source convolvers and frameworks compared below decide
in one place, replace their engines this way and read their status from
state, so the plan follows them there; where they share a lock with the
audio thread, skip processing during a swap or save only a file path, it
does not. The success criterion is the count: VST3 from 22 counted states to
5, the Audio Unit from 12 to 3 and LV2 from 5 to 2, with no adapter left
deciding what a request requires. Seven steps, numbered 0 to 6, each in its own pull request; none
starts before the next preview is published, so the host sessions test code
this work has not touched. Contents: [what is counted](#what-is-counted),
[inventory](#inventory), [what stays](#what-stays-and-why),
[similar plug-ins](#how-similar-plug-ins-do-it), [success criterion](#success-criterion), [protection](#protection-for-every-step),
[steps](#steps), [relation to other work](#relation-to-other-work),
[not in this plan](#not-in-this-plan), [decisions for jrp](#decisions-for-jrp).

## What is counted

jrp's review of 13 November 2026 compared the historical VST 2 plug-in with
today's adapters. The old one shared one mutex between file selection,
filter construction and processing, kept the filter's file name in the
Windows registry, asked the user to reload the plug-in after some channel
changes, and passed attenuation as a number into the convolution. Today's
adapters add portable projects, uninterrupted loading, a kept filter after a
failed load, three formats, automation and diagnostics. Some of the added
state buys those; the rest is copies that every later change must keep in
step.

A state counts here when an adapter writes it on the load or restore path
and it must agree with another record: `HostFilter`, the processor, or
another member of the same adapter. A decision counts when adapter code
chooses what an accepted request requires (keep the filter or replace it,
which settings apply, whether processing restarts) rather than carrying out
what shared code chose.

## Inventory

**Decisions each adapter makes itself.** `HostFilter::load()` accepts or
refuses a request and, since PR #193, says whether its filter is the one
already playing (`HostPlayback::playing`). Everything else about an accepted
request is decided in the adapters:

| Decision | VST3 (`processor.cpp`) | Audio Unit (`effect_state.cpp`) | LV2 (`plugin.cpp`) |
| --- | --- | --- | --- |
| Keep the playing filter or replace it (PLUG-38) | `install()`'s callback, with its own exception for `Origin::project` | `install()`'s callback | `restore_locked()`'s callback; a file chosen through `patch:Set` (`load_request()`) always replaces |
| Which settings an accepted request applies | `install()`: state's settings, or a chosen `.vstpreset`'s when it holds them; bypass from state | `install()`: `store()` for each saved parameter | None: the host restores the ports |
| Find the estimate for the request's Gain mode, and undo that on refusal | `install()`: `set_estimating()` and back | `install()`: the same | `apply_gain_mode()`, later, on the worker |
| The tail while inactive | `install()`: `configuration_tail_frames()` | `install()`: the same | None: LV2 core reports no tail |
| A failed first load plays silence | `refused()` | `silence_failed_first_load()` | `silence_failed_first_load()`, plus `unfiltered` and `refused_mute` for `run()` |
| Whether a filter that does not suit the host is refused or kept until it does | `load_check()` from `Origin` and `played_` | `known_layout()`: while uninitialized, nothing until a configuration has rendered, so such a request is not checked | Always checked: LV2 fixes the rate and channels at instantiation |

**States each adapter keeps.** Members as declared in
`plugins/vst3/processor.hpp`, `plugins/au/effect.hpp` and `struct Instance`
in `plugins/lv2/plugin.cpp`:

| What it records | VST3 | Audio Unit | LV2 | Counted | Target |
| --- | --- | --- | --- | --- | --- |
| The processor that plays, and which one the audio thread is using | `host_`, `played_host_`, `processing_host_`, `starting_host_`, with `install_processor()`, `release_processor()` and `hold()` | `host_`, `playing_`, `rendering_`, with `install_processor()`, `release_processor()` and the mark in `Render()` | `host`, `retiring`, `handover`, `playing`, with `stage()`, `retire()` and `drop_processors()` | 4, 3, 4 | 1, 1, 1 |
| The latency and tail the host reads | `active_latency_`, `tail_` (written in four places) | `latency_` (the newest processor's, or the chosen one while uninitialized), `tail_` | None: the latency port reads the processor `run()` plays | 2, 2, 0 | 0, 0, 0 |
| Whether the host's rate and channels count as settled | `activated_`, `active_`, `played_` | `initialized_`, `rendered_`, `committed_` | None | 3, 3, 0 | 1, 1, 0 |
| What the Status and the window show | Processor: none (built on demand). Controller: `status_text_`, `status_value_`, `report_`, `report_text_`, `format_` | `status_`, `status_log_written_`, `status_bypassed_`, `report_cache_` | `shown`, set by its own `update_status()` and ordered again in `shown_status()` | 5, 4, 1 | 1, 1, 1 |
| The filter's name for saved state | Processor: `saved_name_`, `saved_identity_`, `kept_name_`, `kept_identity_`. Controller: `name_`, `identity_`, `kept_name_`, `kept_identity_` | None: `HostRequest::name`, kept by `HostFilter::accepted()` | None counted (`installed_path` is what the notify port names) | 8, 0, 0 | 2, 0, 0 |
| **Total counted** | | | | **22, 12, 5** | **5, 3, 2** |

LV2's target depends on step 3, whose shared mechanism follows LV2's own
handover; if it cannot cover LV2, LV2 keeps its four and its count stays at
5. Either way LV2 loses its decisions and its second copy of the status
ordering.

The text that goes round in a circle, written and then parsed back, is the
third kind of copy:

- `report_fields()` (`portable/host_report.cpp`) writes the window's audio
  facts as tab- and newline-separated lines, which `window_model.cpp` parses
  back; the VST3 processor also sends `report_text()` beside the fields, so
  the controller holds the same report twice.
- Exceptions carry `Advice` as English-labelled text ("Cause: ", "Fix: ",
  "Note: ") that `parse_advice()` (`portable/advice.hpp`) reads back.
- The VST3 processor's `load_file()` throws `"refused"` or `"no reader"` only
  to reach its own catch block, after it has recorded the refusal.

## What stays and why

These states are counted out, because the format or the threads require
them; a step may simplify their code but does not try to remove them:

- **Settings the host owns:** VST3's `gain_db_`, `mix_`, `latency_`,
  `gain_mode_` and `bypass_`, the Audio Unit's parameters and `bypass_`,
  LV2's ports and their `std::atomic` copies. The audio thread reads them
  without locking.
- **Request numbering where the format is asynchronous:** the VST3
  controller's `load_requests_`, `load_answered_`, `load_accepted_`,
  `latency_requests_` and `latency_waiting_` (hosts may put a proxy between
  the components, VST3-15); the Audio Unit's Latency queue and its wake bits
  (AU-12, AU-16); LV2's `requested`, `answered`, `handled`, `installed`,
  `dropped`, `latency_settled`, `latency_queued`, `gain_mode_settled` and
  `gain_mode_queued`, and its notify-port
  bookkeeping, which the worker and `run()` need.
- **VST3's `shown_bypass_`**, which lets the Status say Bypassed before the
  audio thread has seen the change (PLUG-22).
- **One copy of the report in the VST3 controller:** the processor and the
  controller may run in different processes, so the controller must hold
  what it was sent.
- **Fault pacing and log deduplication:** `report_faults_at_`, LV2's
  `fault_report_queued` and `faults_reported`, and each adapter's `logged_`.

## How similar plug-ins do it

jrp asked on 13 November 2026 how this compares with similar plug-ins. The
commercial convolution reverbs (Altiverb, Space Designer, Reverberate, Hang
Loose Convolver) do not publish their code, so only their behaviour can be
compared, which the [edges plan](2026-11-06-edges-over-other-convolvers.md)
does. These open-source ones were read at the commits given:

| Plug-in | What decides what a change requires | How the playing engine is replaced and freed | What the status and window read | What is saved |
| --- | --- | --- | --- | --- |
| JUCE `dsp::Convolution` and its VST3, AU and LV2 wrappers (`d60b7a7`) | The wrappers translate each format's calls into one `AudioProcessor`, and its `setStateInformation()` is the plug-in's one place to decide; no wrapper decides | A background thread builds the new engine into a one-place holder where the newest wins; at the start of a block the audio thread takes it with a try-lock, crossfades for 50 ms, and hands the old one to the background queue to be freed. The control side never waits. The wrappers also hold a callback lock around `processBlock()` | Getters on the playing engine (`getLatency()`, `getCurrentIRSize()`) | Left to the plug-in |
| LSP Impulse Responses (`4245722`) | One `update_settings()` compares each port with the value last applied: gain, dry/wet and pre-delay are set as values; the FFT size and the IR's own edits (pitch, head and tail cuts, fades) ask for a rebuild | Loading and rebuilding run as tasks on an executor; `process()` checks their state by request and response counters, swaps the playing convolver for the spare when a rebuild finishes (no crossfade that I found), and a separate task frees the old data | `output_parameters()` writes the status, activity and length ports from state on every `process()` | File paths |
| KlangFalter (`a91da61`) | `updateConvolvers()` rebuilds every convolver for any change to the IR's shape; gain is set directly | Convolvers are built on a background thread and swapped under a `CriticalSection` that the audio callback also takes | The window polls getters | File paths, relative to the IR folder |
| HISE convolution (`a1d0f35`) | `reloadInternal()` rebuilds for a new IR, damping or cutoff | The audio thread takes a try-read-lock and skips the convolution for that block when a swap holds the write lock; the old engine fades out and a background thread frees it | Getters | Not checked |
| x42 convoLV2 (`0f35dfb`) | One configuration message; the worker builds a second instance | `work_response()`, on the audio thread, swaps the two instances in one step (no crossfade) and schedules the old one's freeing on the worker; requests during a rebuild are ignored | Nothing beyond the host's ports | File paths |

What this shows for the plan:

- **One place decides.** Each of them decides what a change requires in one
  function, and none lets a format wrapper decide; step 1 does the same.
  LSP's comparison of each setting with the value last applied, setting
  values directly and rebuilding only for what changes the response, is the
  [scope table](../../develop/plugin-contract.md#scope-of-each-change)'s
  split, and the closest model for step 1's decision value. None keeps the
  playing engine when restored state holds the same IR: they rebuild or
  switch. Convolver's PLUG-38 rule goes further than all of them, which is
  a reason to keep it in the one shared place rather than in three adapters.
- **The audio thread adopts; someone else frees.** Every lock-free design
  here (JUCE's convolution, LSP, convoLV2, and HISE for freeing) lets the
  audio thread take the new engine itself and frees the old one elsewhere
  later. None has the control side wait for the audio thread to let go, as
  VST3's and the Audio Unit's `release_processor()` do. Convolver's LV2
  adapter, and `HostProcessor` itself for a replaced filter (`publish()`,
  then `collect()`), already work this way. Step 3 therefore takes that
  model for the shared mechanism, which can then cover all three adapters
  rather than two, and removes the control side's wait.
- **Status is read from state.** All of them read their status and window
  values from the playing state when asked, or write them out each cycle,
  and none keeps copies of status text; step 4 does the same, and LSP's
  per-cycle output is the model for LV2's status port. None explains a
  failure as Convolver's report and log do, so the plan keeps those.
- **What the plan does not copy.** A lock that the audio thread shares with
  loading (KlangFalter, JUCE's callback lock, and the historical Convolver
  VST) makes processing wait behind a load, which the load-timing limits
  (PERF-9) forbid; skipping processing during a swap (HISE) drops audio;
  saving only a file path (KlangFalter, LSP, convoLV2, and the historical
  plug-in's registry entry) gives up portable projects. Embedding the filter
  is what costs the full comparison in PLUG-38 and the state-size checks
  (PLUG-12), and is kept.

## Success criterion

PLUG-39 is done when:

1. The counted states in the [inventory](#inventory) reach their targets, or
   a step's pull request records, with jrp's agreement, why one must stay.
2. No adapter decides what an accepted request requires: each translates the
   host's call into the shared terms, carries out the shared decision and
   notifies the host.
3. Every test of audio, saved state and host behaviour passes unchanged. No
   expected value is edited to make a step pass, except in a commit of its
   own that names the defect or the decided difference it changes and its
   audible effect.
4. No step adds a state that is not in the inventory; each step's pull
   request gives the inventory's count before and after.

## Protection for every step

Each step keeps three things the same, and the tests that show it are the
ones that run today, with the additions of step 0. They test what users and
hosts see, so the machinery under them can be replaced.

| What must not change | Shown by |
| --- | --- |
| **Audio** | The PLUG-2 harness in every format (`vst3.harness`, `au.harness`, `lv2.harness`) against the independent oracle, including `same-filter-restore`, `live-replacement`, `reset` and `failed-initial-load`; the lifecycle runner's sequences (`tests/plugins/lifecycle.hpp`); `native.host_settings`' history tests (`estimate_keeps_history`, `settings_keep_history`, `restore_keeps_history_with_estimate`); the allocation guard in `native.engine`; the oracle tests' worst error, which no step may raise |
| **Saved state** | `native.state` and `native.presets`; the lifecycle runner's saved filter, "load failed" flag and VST3 bypass; COMPAT-1's kept presets and expected audio from 0.13.2 and 0.13.12; `vst3.preset_format` |
| **Host behaviour** | Each format's adapter and lifecycle tests (`vst3.adapter`, `au.adapter`, `au.lifecycle`, `lv2.adapter`, `lv2.lifecycle`): what the Status shows, what is logged, latency and tail reporting, restart and listener notifications; pluginval; Ardour headless; AVAudioEngine; the Audio Unit's ThreadSanitizer build; the load-timing limits (PERF-9) |

Where a test reaches into the machinery rather than the behaviour (the
`*_for_tests` hooks in `processor.hpp` and `effect.hpp`, LV2's
`WorkerPoint` pauses), the step that replaces that machinery moves the hook
to the shared code with the same meaning, so the test keeps checking the
same timing. Tests that stated what an adapter must do follow the contract,
not the production decision functions: as PLUG-36 requires, an expected
result is never computed by the shared decision that step 1 introduces.

Real hosts come once, at the end: a short REAPER and GarageBand check on the
candidate built after the last step, in jrp's usual session, rather than one
per step.

## Steps

Each step is one pull request titled `PLUG-39 step N: …`, merged as one
commit so that reverting it undoes it alone, with the independent review
asking the [question on special cases](../../develop/workflow.md#check). The
order puts the step that removes the most decisions first; the dependencies
are stated, and steps 3 to 5 do not depend on steps 1 and 2.

0. **Fill the gaps in protection (tests only).** The lifecycle runner's
   model knows nothing of the settings: add Gain and Latency changes and a
   restore with different settings as operations, and check the saved
   settings and the reported latency after each, extending the runner as
   jrp asked on 8 November 2026 rather than adding a framework. Add the
   harness case the scope table implies and nothing checks: restored state
   holding the playing filter with a different Latency builds a new
   processor. List the tests that reach into the machinery each later step
   replaces. No production change.
1. **One decision for each accepted request.** Extend
   `HostFilter::load()` so that, with the filter accepted, it hands the
   adapter one plain value, provisionally `HostChange`, saying what the
   request requires: keep the playing filter or replace it, the settings to
   apply (none, the state's, or a chosen preset's), whether the Latency needs
   a new processor, the tail while inactive, and whether a failed first load
   must silence the processor. The adapter says what kind of request it is
   (a restore during playback, a session the host is opening, a chosen file,
   a chosen preset with settings) and what it knows of the host. This moves
   the first five rows of the decisions table into shared code: PR #193's
   three keep-or-replace callbacks and VST3's project exception, the
   settings applied in VST3's and the Audio Unit's `install()`, both
   adapters' estimate switching, both inactive-tail calculations and the
   three failed-first-load paths. VST3's `load_file()` returns its recorded
   refusal instead of throwing to its own catch block. The rows of the
   [scope table](../../develop/plugin-contract.md#scope-of-each-change) become
   the specification of `HostChange`, and the contract says so. A
   difference found while counting: LV2 replaces a chosen file that holds
   the filter already playing, where VST3 and the Audio Unit keep it.
   Making LV2 keep it changes what is heard (the tail rings on), so it is a
   separate commit in this step, and only if jrp agrees.
2. **One rule for when the host has settled its rate and channels.** VST3
   (`activated_`, `active_`, `played_`, `load_check()`) and the Audio Unit
   (`initialized_`, `rendered_`, `committed_`, `known_layout()`) implement
   the contract's rule for state that arrives before the host is configured
   in two ways. Replace both with one small shared record that each adapter
   feeds with the same events (activated with a configuration, played audio
   with it, told a project is being opened) and asks whether a request is
   checked, refused or kept until it suits. The two differ today for a file
   chosen while inactive after an activation that never played: VST3 checks
   it against that activation, the Audio Unit does not check it. The step
   keeps both behaviours unless jrp chooses one. After step 1, because the
   decision value uses the answer.
3. **One lifetime mechanism for the playing processor.** A small shared
   class in `portable/`, provisionally `HostProcessorSlot`, follows the model
   that LV2's `stage()` and `retire()` and the
   [similar plug-ins](#how-similar-plug-ins-do-it) use: the control side
   offers the newest processor with its latency and tail; the audio thread
   adopts it at the start of its next call, without locking or allocating;
   and the replaced processor is freed by a later control call once the
   audio thread has adopted its successor, so the control side never waits
   for the audio thread. The latency and tail the host reads are the
   offered processor's, so a host that reads them straight after a Latency
   change (REAPER, VST3-16) gets the new values, as now. It replaces four
   members and three functions in VST3, three members and two functions in
   the Audio Unit, the latency and tail atomics of both, and LV2's four
   members, in three pull requests: the class with its native tests
   (offering, adoption, retirement and teardown under the sanitizer build,
   with a stress test that offers continuously while another thread
   adopts), then VST3, then the Audio Unit and LV2. The VST3-16 and LV2
   worker hooks move to the class. A format the model cannot cover keeps
   its own, with the reason in the architecture page. Independent of steps
   1 and 2.
4. **Diagnostics as a view of state.** In three pull requests, one per
   format, each removing copies:
   - The Audio Unit rebuilds one immutable snapshot (status line and report)
     whenever the control side changes something, and readers take it
     without the lock, replacing `status_`, `status_log_written_`,
     `status_bypassed_` and `report_cache_`. This also removes the report
     cache from PLUG-30's list of paths that skip work when the lock is
     busy.
   - VST3 offers names to `HostFilter` with their filter identity, as the
     Audio Unit already does through `HostRequest::name`, and shared code
     applies the rule that a name shows only while its filter plays, so a
     refused preset never renames the playing filter. The processor's four
     name members go, and the controller keeps only the pair it saves.
   - LV2 sets `shown` from the shared status ordering through one mapping to
     its status values, instead of ordering failures, filters and faults
     again in `update_status()` and `shown_status()`; the audio-side
     overlays (Loading, a refusal `run()` made itself, Fault) stay, since
     `run()` must answer without the lock.
5. **Typed reports and advice.** The report reaches the window as typed
   values encoded once at the transport (the VST3 message, the Audio Unit
   property) by one codec, rather than tab-separated lines parsed back; the
   VST3 controller derives the report's text instead of receiving it as well;
   exceptions carry `Advice` as a typed value, keeping the public exception
   types and their `what()` text. No message a user reads changes.
6. **Recount and close.** Recount the inventory on `main`; update the
   [implementation map](../../develop/architecture.md) and the
   [adapter contract](../../develop/plugin-contract.md) where they describe the
   old arrangement; the CHANGELOG entry and PLUG-39 moved to done; jrp's
   REAPER and GarageBand check on the next candidate. This plan then moves
   to the archive.

## Relation to other work

- **PR #193 (PLUG-38)** is the first move in this direction:
  `HostPlayback::playing` is decided once, in `HostFilter`. Step 1 builds on
  it, folding its three adapter call sites and VST3's project exception into
  the shared decision.
- **The change-scope rule (PLUG-37)** in the
  [adapter contract](../../develop/plugin-contract.md#scope-of-each-change) is
  the specification step 1 implements: each row of its table becomes one
  part of the decision value.
- **PLUG-36** (the loading-and-failure table for each transition) stays
  parked until after REL-3, as jrp decided. This plan does not need it
  first: step 0 adds only the settings the lifecycle model lacks, and the
  harness and lifecycle model already state their expected results apart
  from the production code. When PLUG-36 is taken up, its load and restore
  rows map onto step 1's single decision instead of three adapters, which
  makes its table shorter to write and check.
- **PLUG-30** (one rule for state a busy lock makes a call skip) stays
  parked; step 4 removes the Audio Unit's report cache from its list.
- **PLUG-27** (reading a chosen file before the host's audio settings are
  known) stays as it is; step 2 gathers the rule it would change into one
  place in both formats, so PLUG-27 becomes a change there.
- **PLUG-24** (`HostProcessor` in its own source file) is navigation only,
  which jrp noted does not resolve these issues on its own. It stays its
  own item, never in a step's pull request.
- **[Keeping `host.cpp` manageable](../../../TODO.md#keeping-hostcpp-manageable)**:
  its three narrow simplifications are carried out by steps 1 and 5.
- **The host sessions and the next preview (PR #192)** come first. Steps 1
  to 6 start once the next preview is published, so the preview and the
  sessions on jrp's Mac test code this work has not changed. Step 0 is tests
  only and could start now, if jrp wants it earlier.

## Not in this plan

- Splitting `host.cpp` into smaller files for its own sake.
- A general command framework or a state machine for requests: the decision
  value is a plain struct returned by one function.
- Threads inside an instance (jrp, 8 November 2026) or a change to which
  thread a format loads on.
- Any change to saved state or preset formats, identities, `.cfg` reading or
  the sound, other than a separately approved commit such as LV2's same-file
  keep in step 1.
- The designs from the [comparison](#how-similar-plug-ins-do-it) that the
  plan does not copy: a lock shared with the audio thread, skipping
  processing during a swap, and saving only a file path.

## Decisions for jrp

1. **Targets and order:** approve the targets in the inventory and the
   order of the steps, with steps 1 to 6 waiting for the next preview to be
   published. Recommended.
2. **Step 0's timing:** tests only, so it could start now, before the host
   sessions; or wait with the rest. Recommended: wait,
   keeping CI time for the preview work.
3. **One TODO item:** PLUG-39 with numbered step pull requests, each merged
   as one revertible commit, rather than one item per step. Recommended,
   following the 12 November decision to reduce obligations before adding
   more.
4. **Differences found while counting:** whether LV2 should keep a chosen
   file that holds the filter already playing, as VST3 and the Audio Unit do
   (step 1), and whether VST3's and the Audio Unit's check of a file chosen
   while inactive should become one rule (step 2). Recommended: align LV2,
   since a tail cut by re-choosing the same file would surprise a user, and
   keep step 2 behaviour-preserving.
5. **Step 3's model:** one lifetime mechanism for all three formats, in
   which the audio thread adopts a new processor and the control side never
   waits for it, as LV2 and the compared plug-ins do, rather than sharing
   VST3's and the Audio Unit's waiting mechanism between those two only.
   Recommended: it removes a design rather than sharing one, and the
   targets above assume it.
