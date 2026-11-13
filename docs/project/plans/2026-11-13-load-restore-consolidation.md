<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Consolidate the plug-ins' load and restore path

For whoever carries out PLUG-39, whose plan and order jrp approved on
13 November 2026 (approving the plan, not its eventual implementation: each
step's pull request is reviewed and tested on its own): which decisions the three plug-in adapters each make for
themselves on the path that loads and restores a filter, which mechanisms
they implement more than once, which values they keep as copies of what
another record already knows, and a sequence of small steps, each one
revertible and protected by tests of audio, saved state and host behaviour.
It changes no code. The inventory is of `main` at `1de4452`, with PR #193
(PLUG-38) merged.

**In short:** six decisions about an accepted request are made in 14 places
across the adapters; four mechanisms (the playing processor's lifetime, when
the host counts as settled, the status ordering and the filter's name) have
two or three implementations; and eleven values are copies that must be kept
in step with what they copy. Three reductions remove most of this without
changing what anyone hears, saves or sees: one shared decision for each
accepted request, extending `HostFilter` as PR #193 began, prepared before
anything is committed; one ownership mechanism for the playing processor, in
which the control side offers a replacement and returns, the audio thread
adopts it at a callback boundary and the control side frees the old one
later, under four conditions jrp set on 13 November 2026; and diagnostics
built as a view of state, with the Audio Unit's live audio-side values kept
live. The open-source convolvers compared below decide in one place, defer
freeing and read their status from state; the plan follows them there and
keeps Convolver's stricter rules where theirs are looser. Success is fewer
duplicated decisions, mechanisms and copies, with unchanged audio and host
behaviour, not a count of member variables: moving four pointers into a class
encapsulates them but does not make four states one. Seven steps, numbered 0
to 6, under the one TODO item; none starts before the next preview is
published. Contents: [what is
counted](#what-is-counted), [inventory](#inventory), [what
stays](#what-stays-and-why), [similar plug-ins](#how-similar-plug-ins-do-it),
[success criterion](#success-criterion), [protection](#protection-for-every-step),
[steps](#steps), [relation to other work](#relation-to-other-work),
[not in this plan](#not-in-this-plan), [decisions](#decisions).

## What is counted

jrp's review of 13 November 2026 compared the historical VST 2 plug-in with
today's adapters. The old one shared one mutex between file selection,
filter construction and processing, kept the filter's file name in the
Windows registry, asked the user to reload the plug-in after some channel
changes, and passed attenuation as a number into the convolution. Today's
adapters add portable projects, uninterrupted loading, a kept filter after a
failed load, three formats, automation and diagnostics. Some of the added
state buys those; the rest is copies and repeated rules that every later
change must keep in step.

Three things are counted, not member variables:

- **A duplicated decision:** adapter code that chooses what an accepted
  request requires (keep the filter or replace it, which settings apply,
  whether processing restarts) where another adapter makes the same choice
  in its own code.
- **A duplicated mechanism:** the same rule or concurrency protocol
  implemented separately in two or three places.
- **An independently maintained value:** a value an adapter writes that
  copies or derives from another record (`HostFilter`, the processor,
  another member), so every change must update both. A value counts once,
  however many members hold it. Values the host or the threads genuinely
  need are listed under [what stays](#what-stays-and-why) and not counted.

## Inventory

**Duplicated decisions.** `HostFilter::load()` accepts or refuses a request
and, since PR #193, says whether its filter is the one already playing
(`HostPlayback::playing`). Everything else about an accepted request is
decided in the adapters, 14 places for six decisions:

| Decision | VST3 (`processor.cpp`) | Audio Unit (`effect_state.cpp`) | LV2 (`plugin.cpp`) |
| --- | --- | --- | --- |
| Keep the playing filter or replace it (PLUG-38) | `install()`'s callback, with its own exception for `Origin::project` | `install()`'s callback | `restore_locked()`'s callback; a file chosen through `patch:Set` (`load_request()`) always replaces |
| Which settings an accepted request applies | `install()`: state's settings, or a chosen `.vstpreset`'s when it holds them; bypass from state | `install()`: `store()` for each saved parameter | None: the host restores the ports |
| Find the estimate for the request's Gain mode, and undo that on refusal | `install()`: `set_estimating()` and back | `install()`: the same | `apply_gain_mode()`, later, on the worker |
| The tail while inactive | `install()`: `configuration_tail_frames()` | `install()`: the same | None: LV2 core reports no tail |
| A failed first load plays silence | `refused()` | `silence_failed_first_load()` | `silence_failed_first_load()`, plus `unfiltered` and `refused_mute` for `run()` |
| Whether a filter that does not suit the host is refused or kept until it does | `load_check()` from `Origin` and `played_` | `known_layout()`: while uninitialized, nothing until a configuration has rendered, so such a request is not checked | Always checked: LV2 fixes the rate and channels at instantiation |

**Duplicated mechanisms.**

| Mechanism | Implementations today | Target |
| --- | --- | --- |
| Ownership of the playing processor: who may use it, and when a replaced one is freed | VST3: `host_`, `played_host_`, and a mark for each of its two readers (`processing_host_` for `process()`, `starting_host_` for `setProcessing()`, which also resets the processor), with `install_processor()`, `release_processor()` and `hold()`; the control side waits until neither mark holds the old processor. Audio Unit: `host_`, `playing_`, `rendering_`, the same two functions and the mark in `Render()`; the control side waits. LV2: `host`, `retiring`, `handover`, `playing`, with `stage()`, `retire()` and `drop_processors()`; `run()` adopts and nothing waits | One, with every reader protected ([step 3](#steps)); a format it cannot cover keeps its own with the reason recorded |
| When the host's rate and channels count as settled | VST3: `activated_`, `active_`, `played_`, `known_layout()`, `load_check()`. Audio Unit: `initialized_`, `rendered_`, `committed_`, `known_layout()`. They differ for a file chosen while inactive after an activation that never played | One shared rule fed with the same events (step 2) |
| The status ordering (a load failure, then a fault, then the filter) | `host_status()` for VST3 and the Audio Unit; LV2 orders them again in `update_status()` and `shown_status()` | One (step 4) |
| A filter's name shows only while that filter plays | `HostRequest::name` in `HostFilter::accepted()` for the Audio Unit; VST3's processor and controller each keep their own rule with a second, "kept" name pair, so that a refused preset does not rename the playing filter | One, in `HostFilter` (step 4) |

**Independently maintained values.**

| Value | What it copies or derives from | Target |
| --- | --- | --- |
| VST3 `tail_`, written in four places; Audio Unit `tail_` | The playing processor's tail and the accepted filter's | Computed by one function from the processor and filter, which already follows a replacement inside a processor (the harness's `tail-after-replacement`) |
| VST3 `active_latency_`; Audio Unit `latency_` (the newest processor's latency, or the chosen one while uninitialized) | The newest processor's latency, and the chosen setting | The newest processor's latency, written by the ownership mechanism when it offers that processor, so the host reads at once the latency the next callback uses; the Audio Unit's second meaning comes from its parameter |
| Audio Unit `status_`, `status_log_written_`, `status_bypassed_`, `report_cache_` | `host_status()` and `host_report()` of `HostFilter`, the settings and bypass | One control-side snapshot, combined when read with the live audio-side values (step 4) |
| VST3 controller `status_text_`, `status_value_`, `report_text_` | The report the controller already holds (`report_`) | Derived from it |
| VST3 processor `kept_name_` and `kept_identity_`, controller `kept_name_` and `kept_identity_` | The name pair of the playing filter, kept beside the newest offered pair | Removed by the shared name rule; the processor's offered pair moves into `HostFilter` |
| LV2 `shown` | The status ordering above | Still the value `run()` reads, written through the shared ordering |

Text that goes round in a circle, written and then parsed back:

- `report_fields()` (`portable/host_report.cpp`) writes the window's audio
  facts as tab- and newline-separated lines, which `window_model.cpp` parses
  back; the VST3 processor also sends `report_text()` beside the fields.
- Exceptions carry `Advice` as English-labelled text ("Cause: ", "Fix: ",
  "Note: ") that `parse_advice()` (`portable/advice.hpp`) reads back.
- The VST3 processor's `load_file()` throws `"refused"` or `"no reader"` only
  to reach its own catch block, after it has recorded the refusal.

## What stays and why

These are not counted, because the format or the threads require them; a
step may simplify their code but does not try to remove them:

- **Settings the host owns:** VST3's `gain_db_`, `mix_`, `latency_`,
  `gain_mode_` and `bypass_`, the Audio Unit's parameters and `bypass_`,
  LV2's ports and their `std::atomic` copies. The audio thread reads them
  without locking, and Gain and Mix automation may arrive on the audio
  thread.
- **One mark per reader of the playing processor**, however the mechanism is
  implemented: each thread that uses the processor must be protected
  separately (condition 1 of [step 3](#steps)).
- **Request numbering where the format is asynchronous:** the VST3
  controller's `load_requests_`, `load_answered_`, `load_accepted_`,
  `latency_requests_` and `latency_waiting_` (hosts may put a proxy between
  the components, VST3-15); the Audio Unit's Latency queue and its wake bits
  (AU-12, AU-16); LV2's `requested`, `answered`, `handled`, `installed`,
  `dropped`, `latency_settled`, `latency_queued`, `gain_mode_settled` and
  `gain_mode_queued`, and its notify-port bookkeeping, which the worker and
  `run()` need.
- **VST3's `shown_bypass_`**, which lets the Status say Bypassed before the
  audio thread has seen the change (PLUG-22).
- **One copy of the report in the VST3 controller** (`report_`, `format_`):
  the processor and the controller may run in different processes.
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
| JUCE `dsp::Convolution` and its VST3, AU and LV2 wrappers (`d60b7a7`) | The wrappers translate each format's calls into one `AudioProcessor`, and its `setStateInformation()` is the plug-in's one place to decide; no wrapper decides | A background thread builds the new engine into a one-place holder where the newest wins; at the start of a block the audio thread takes it with a try-lock, crossfades for 50 ms, and queues the old one to be freed on the background thread. `destroyPreviousEngine()` frees it at once, on the audio thread, when that queue is full. The wrappers also hold a callback lock around `processBlock()` | Getters on the playing engine (`getLatency()`, `getCurrentIRSize()`) | Left to the plug-in |
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
- **Freeing is deferred.** All of them that keep building off the audio
  thread let the audio side take the new engine at a callback boundary and
  free the old one later, so the control side does not wait for the audio
  thread to let go, as VST3's and the Audio Unit's `release_processor()` do.
  This supports deferred reclamation, with a qualification: JUCE takes the
  engine with a try-lock and frees on the audio thread when its queue is
  full, and others use locks the audio thread shares. They are not evidence
  for "no locks and never free on the audio thread"; Convolver keeps that
  stricter rule ([architecture invariants](../../develop/architecture.md#invariants-to-preserve)).
  Convolver's LV2 adapter, and `HostProcessor` itself for a replaced filter
  (`publish()`, then `collect()`), already defer freeing that way.
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

1. Each of the six decisions in the [inventory](#inventory) is made in one
   shared place, and no adapter decides what an accepted request requires:
   each translates the host's call into the shared terms, carries out the
   shared decision and notifies the host.
2. Each duplicated mechanism has one implementation, or a step's pull
   request records, with jrp's agreement, why a format keeps its own.
3. Each independently maintained value is derived where it is read, or
   written in one place together with what it describes, or a step records
   with jrp why it stays.
4. Audio, saved state and host behaviour are unchanged. No expected value is
   edited to make a step pass, except in a commit of its own that names the
   defect, the decided difference or the deliberate test change (such as
   step 3's) and its effect.
5. No step adds a decision, mechanism or value of these kinds; each step's
   pull request gives the inventory before and after. Ownership that is
   simpler to follow counts for more than a smaller number of members.

## Protection for every step

Each step keeps three things the same, and the tests that show it are the
ones that run today, with the additions of step 0. They test what users and
hosts see, so the machinery under them can be replaced.

| What must not change | Shown by |
| --- | --- |
| **Audio** | The PLUG-2 harness in every format (`vst3.harness`, `au.harness`, `lv2.harness`) against the independent oracle, including `same-filter-restore`, `live-replacement`, `tail-after-replacement`, `reset` and `failed-initial-load`; the lifecycle runner's sequences (`tests/plugins/lifecycle.hpp`); `native.host_settings`' history tests (`estimate_keeps_history`, `settings_keep_history`, `restore_keeps_history_with_estimate`); the allocation guard in `native.engine`; the oracle tests' worst error, which no step may raise |
| **Saved state** | `native.state` and `native.presets`; the lifecycle runner's saved filter, "load failed" flag and VST3 bypass; COMPAT-1's kept presets and expected audio from 0.13.2 and 0.13.12; `vst3.preset_format` |
| **Host behaviour** | Each format's adapter and lifecycle tests (`vst3.adapter`, `au.adapter`, `au.lifecycle`, `lv2.adapter`, `lv2.lifecycle`): what the Status shows, what is logged, latency and tail reporting, restart and listener notifications; pluginval; Ardour headless; AVAudioEngine; the Audio Unit's ThreadSanitizer build; the load-timing limits (PERF-9) |

Where a test reaches into the machinery rather than the behaviour (the
`*_for_tests` hooks in `processor.hpp` and `effect.hpp`, LV2's
`WorkerPoint` pauses), the step that replaces that machinery moves the hook
to the shared code with the same meaning. One test asserts the machinery
itself and is changed on purpose: `latency_handover_waits()` in
`tests/vst3/adapter_tests.cpp` requires the control call to wait while
`process()` holds the old processor, which step 3 removes; step 3 replaces
that assertion with the safety requirement and keeps its latency and audio
checks. Tests that state what an adapter must do follow the contract, not
the production decision functions: as PLUG-36 requires, an expected result
is never computed by the shared decision that step 1 introduces.

Real hosts come once, at the end: a short REAPER and GarageBand check on the
candidate built after the last step, in jrp's usual session, rather than one
per step.

## Steps

Each step is one pull request titled `PLUG-39 step N: …` (steps 3 and 4 as
substeps `3a` to `3c` and `4a` to `4c`), merged as one commit so that reverting it undoes it alone, with the
independent review asking the
[question on special cases](../../develop/workflow.md#check). The order puts
the step that removes the most decisions first; the dependencies are
stated, and steps 3 to 5 do not depend on steps 1 and 2.

0. **Fill the gaps in protection (tests only).** The lifecycle runner's
   model knows nothing of the settings: add Gain and Latency changes and a
   restore with different settings as operations, and check the saved
   settings and the reported latency after each, extending the runner as
   jrp asked on 8 November 2026 rather than adding a framework. Add the
   harness case the scope table implies and nothing checks: restored state
   holding the playing filter with a different Latency builds a new
   processor. List the tests that reach into the machinery each later step
   replaces. No production change.
1. **One decision for each accepted request.** `HostFilter::load()` hands
   the install callback one plain value, provisionally `HostChange`, saying
   what the request requires: keep the playing filter or replace it, the
   settings to apply (none, the state's, or a chosen preset's), whether the
   Latency needs a new processor, the tail while inactive, and whether a
   failed first load must silence the processor. It arrives where
   `HostPlayback::playing` arrives today, inside `load()`'s transaction, so
   preparation still comes before commit: the adapter prepares the
   replacement (which may throw) before `HostFilter` records the new filter
   as the one saved, and a refusal or running out of memory leaves the
   saved filter, the settings and what plays unchanged, as the tests of a
   request rejected whole check today. Returning the decision must never
   commit anything. The adapter says what kind of request it is (a restore
   during playback, a session the host is opening, a chosen file, a chosen
   preset with settings) and what it knows of the host. This moves the
   first five rows of the decisions table into shared code: PR #193's three
   keep-or-replace callbacks and VST3's project exception, the settings
   applied in VST3's and the Audio Unit's `install()`, both adapters'
   estimate switching, both inactive-tail calculations and the three
   failed-first-load paths. VST3's `load_file()` returns its recorded
   refusal instead of throwing to its own catch block. The rows of the
   [scope table](../../develop/plugin-contract.md#scope-of-each-change) become
   the specification of `HostChange`, and the contract says so. A
   difference found while counting: LV2 replaces a chosen file that holds
   the filter already playing, where VST3 and the Audio Unit keep it. jrp
   chose to align LV2 (13 November 2026): re-choosing a file whose decoded
   filter configuration is unchanged keeps the playing filter and its
   history, decided by comparing the configuration in full as PLUG-38 does,
   never by the file name, so a file whose contents changed still loads; any
   settings that come with it apply by the change-scope rule. It changes
   what is heard (the tail rings on), so it is a separate commit in this
   step, with an LV2 test that re-chooses the same file during a tail and
   one that re-chooses the same name with changed contents.
2. **One rule for when the host has settled its rate and channels.** Replace
   VST3's and the Audio Unit's two implementations with one small shared
   record that each adapter feeds with the same events (activated with a
   configuration, played audio with it, told a project is being opened) and
   asks whether a request is checked, refused or kept until it suits. The
   step keeps both formats' present behaviour, including their difference
   for a file chosen while inactive, as jrp decided (13 November 2026).
   After step 1, because the decision value uses the answer.
3. **Deferred reclamation of the playing processor.** An ownership redesign,
   not a tidying of fields into a class, and its own reviewed change after
   the preview. Today VST3 and the Audio Unit publish the replacement and
   then wait until every reader has released the old processor, freeing it
   before the control call finishes. Instead the control side offers the
   replacement and returns; the audio side adopts it at a callback
   boundary, as both formats already switch; and a later control call frees
   the old one. What improves is the control thread no longer waiting. It
   does not shorten filter preparation, does not remove the deliberate
   interruption when a new Latency starts a processor with empty history,
   and leaves an ordinary filter replacement's crossfade inside the
   processor as it is. Four conditions (jrp, 13 November 2026):
   1. **Every reader is protected, not only the audio callback.** VST3's
      `setProcessing()` also uses and resets the processor and is protected
      by its own mark today. The mechanism keeps a mark for each reader and
      frees nothing a mark holds; copying LV2's single-reader handover
      literally would leave `setProcessing()` unprotected.
   2. **REAPER's latency contract holds.** The host reads, at once, the
      latency the next audio callback will use, so the offered processor's
      latency is what `getLatencySamples()` and the Audio Unit's
      `GetLatency()` report from the moment it is offered, and the host is
      notified without waiting for a callback, which may never come while
      playback is stopped (VST3-16). The tail reported follows filter
      replacements inside a processor as well as a new processor.
   3. **Retirement is bounded, and stopped playback is defined.** A newer
      offer discards a superseded offer no reader ever adopted, rather than
      accumulating them. Retired processors are accounted for every reader:
      if two readers (VST3's `process()` and `setProcessing()`) can hold
      different generations, each held one stays protected until its reader
      lets go, and the number kept alive is bounded by the number of
      readers. Each is freed by a later control call once no reader holds
      it, or by deactivation or destruction, when no audio runs. No new
      thread.
   4. **The ownership test changes deliberately.** `latency_handover_waits()`
      becomes a test that the control call returns promptly while
      `process()` holds the old processor, that the old processor is not
      destroyed while a reader still uses it (a destruction hook, with
      AddressSanitizer as a second check), that the new Latency is reported
      at once, and that the next block plays the new processor.

   Three pull requests: the shared class with its native tests (offering,
   superseding, adoption, retirement, deactivation and destruction under the
   sanitizer build, with a stress test that offers continuously while two
   readers mark and adopt), then VST3, then the Audio Unit. LV2 adopts it
   only if its worker and `run()` fit without losing what LV2 has; otherwise
   LV2 keeps `stage()` and `retire()` and the architecture page says why.
   Success is simpler ownership with unchanged audio and host behaviour.
   Independent of steps 1 and 2.
4. **Diagnostics as a view of state.** In three pull requests, one per
   format, each removing copies:
   - The Audio Unit keeps one snapshot of what the control side knows (the
     filter, its failure, the log), rebuilt whenever the control side
     changes something, and combines it, each time the Status or the report
     is built, with the values that change on the audio side: Gain and Mix
     from automation, bypass, and the faults `Render()` counts. This
     replaces `status_`, `status_log_written_`, `status_bypassed_` and
     `report_cache_` without making a reading stale when nothing on the
     control side has changed, and removes the report cache from PLUG-30's
     list of paths that skip work when the lock is busy.
   - VST3 offers names to `HostFilter` with their filter identity, as the
     Audio Unit already does through `HostRequest::name`, and shared code
     applies the rule that a name shows only while its filter plays, so a
     refused preset never renames the playing filter. The processor's name
     members and both "kept" pairs go; the controller keeps only the pair it
     saves.
   - LV2 sets `shown` from the shared status ordering through one mapping to
     its status values, instead of ordering failures, filters and faults
     again in `update_status()` and `shown_status()`; the audio-side
     overlays (Loading, a refusal `run()` made itself, Fault) stay, since
     `run()` must answer without the lock.
5. **Typed reports and advice.** The report reaches the window as typed
   values encoded once at the transport (the VST3 message, the Audio Unit
   property) by one codec, rather than tab-separated lines parsed back; the
   VST3 controller derives the report's text and the Status text from the
   report it holds; exceptions carry `Advice` as a typed value, keeping the
   public exception types and their `what()` text. No message a user reads
   changes.
6. **Recount and close.** Recount the inventory on `main`; update the
   [implementation map](../../develop/architecture.md) and the
   [adapter contract](../../develop/plugin-contract.md) where they describe the
   old arrangement; the CHANGELOG entry and PLUG-39 moved to done; jrp's
   REAPER and GarageBand check on the next candidate. This plan then moves
   to the archive.

## Relation to other work

- **PR #193 (PLUG-38)** is the first move in this direction:
  `HostPlayback::playing` is decided once, in `HostFilter`, inside `load()`'s
  transaction. Step 1 builds on it, folding its three adapter call sites and
  VST3's project exception into the shared decision.
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
  sessions on jrp's Mac test code this work has not changed. Step 0 waits
  too, as jrp decided: the host sessions and publication first, then the
  protection tests, then production changes.

## Not in this plan

- Splitting `host.cpp` into smaller files for its own sake.
- A general command framework or a state machine for requests: the decision
  value is a plain struct handed over by one function.
- Threads inside an instance (jrp, 8 November 2026), a new thread for
  reclamation, or a change to which thread a format loads on.
- Any change to saved state or preset formats, identities, `.cfg` reading or
  the sound, other than a separately approved commit such as LV2's same-file
  keep in step 1.
- The designs from the [comparison](#how-similar-plug-ins-do-it) that the
  plan does not copy: a lock shared with the audio thread, skipping
  processing during a swap, freeing on the audio thread, and saving only a
  file path.

## Decisions

jrp's decisions of 13 November 2026 ([decisions log](../decisions.md#direction)):

1. **The plan and its order are approved.** Success means fewer duplicated
   decisions and clearer ownership, with existing audio, saved-state and
   host behaviour preserved. Approving the plan does not approve its
   implementation: each step's pull request is reviewed and tested on its
   own.
2. **Step 0 waits until after the next preview:** the host sessions and
   publication first, then the protection tests, before any production
   change.
3. **One TODO item, PLUG-39,** with numbered step pull requests, each
   independently reviewable and revertible, and substeps (`step 3a`, `3b`,
   `3c`; `4a`, `4b`, `4c`) where the plan already calls for several.
4. **LV2 is aligned with VST3 and the Audio Unit** for a re-chosen file
   whose decoded configuration is unchanged (step 1, its own commit), and
   the VST3 and Audio Unit difference in when the host counts as settled is
   kept (step 2).
5. **Deferred reclamation is approved in principle,** with the four
   conditions in [step 3](#steps), as its own reviewed change after the
   preview.
