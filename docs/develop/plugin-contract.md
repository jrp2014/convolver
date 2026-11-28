<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in adapter contract

[Documentation index](../README.md) · [Implementation map](architecture.md) ·
[Plug-in identities and layouts](plugin-identities.md) · [Testing](testing.md#plug-in-comparisons-plug-2)

For anyone writing, changing or reviewing a plug-in adapter (VST3, Audio Unit,
LV2), and for host integrators. This page is the reference for what every
adapter must do; [how the plug-ins behave](../use/plugins.md) says how that
looks to a user. The rules were designed in the archived
[plug-in formats plan](../archive/plans/2026-10-21-plugin-formats.md#shared-adapter-contract)
and are maintained here.

**In short:**

- An instance is empty, active, kept but unsuited, or failed; the
  [state table](#states-and-transitions) says what each plays, saves and
  shows. An empty instance passes its input, at the level Gain and Mix
  give it, only when the host's channel counts are equal; otherwise it
  plays silence.
- A filter that cannot be used as an instance's first filter makes it play
  **silence**, never unfiltered audio. A rejected replacement leaves the
  working filter playing.
- State is checked in two stages: the state itself at once, and the host's
  sample rate and channels as soon as the host has settled them.
- Every failure keeps a structured reason on the control side, for the host's
  log, LV2's status port and, later, a status display in the other formats.
- A filter plays on exactly its outputs and at least its inputs: on a wider
  input bus it reads the first inputs and ignores the rest, and says so.
  Where neither fits, two default routings: a one-input, one-output filter plays on
  each channel of a host with as many outputs as inputs, and a four-channel
  IR file chosen on two inputs and two outputs plays as true stereo
  ([below](#default-routings)), and Convolver says so.
- Bypass ramps over 1.33 ms (64 samples at 48 kHz) to the input delayed by
  the latency, or to silence when there is no dry signal: the filter's
  counts differ, or with no filter playing, the host's or a kept filter's
  differ, or a load failed and no filter is kept ([bypass](#bypass)).
- Gain and mix are host parameters whose defaults keep the sound unchanged
  ([settings](#settings)); the Audio Unit has them first.
- Nothing on the audio thread allocates, locks or does I/O; filters are
  prepared elsewhere and handed over.
- Each change does only the work it needs: a new gain ramps on the playing
  filter, and only a different filter or Latency prepares new processing
  ([scope of each change](#scope-of-each-change)), and restored state
  holding the filter already playing keeps it.

These rules hold for VST3, LV2 and AU alike. Each adapter's tests check them
through the [PLUG-2 harness](testing.md#plug-in-comparisons-plug-2), and a
review of plug-in work checks them ([independent review](workflow.md#independent-review)).
Changing a rule is an owner's decision and goes in its own pull request.
Compatibility and numerical correctness take priority over CPU-specific
optimization in every adapter.

Contents: [loading and failure](#loading-and-failure) ·
[failure reasons](#failure-reasons) ·
[scope of each change](#scope-of-each-change) ·
[initial restoration and live replacement](#initial-restoration-and-live-replacement) ·
[bypass](#bypass) · [settings](#settings) · [channel layouts](#channel-layouts) ·
[replacement and threads](#replacement-and-threads) · [host state](#host-state) ·
[preset files](#preset-files) · [format notes](#format-notes)

## Loading and failure

An adapter instance is always in one of four states: **empty**, **active**,
**kept but unsuited** and **failed**. The table in
[states and transitions](#states-and-transitions) is the authoritative
account of what each plays, what is saved and what Status shows, and of what
each request or host event does to each; the user guide, this page's other
sections and the tests' expected answers follow it. Three rules shape it:

- **An empty instance** plays its input, at the level Gain and Mix give it,
  only where the host has as many inputs as outputs. Otherwise it plays
  silence, because an unfiltered 2→6 crossover would send full-range audio
  to whatever is on outputs 1 and 2.
- **A first filter that cannot be used** (a session restore or the first
  preset: a missing file without an embedded copy, the wrong rate or channel
  counts, corrupt state) plays silence, never a silent fallback to identity:
  a room correction or crossover that quietly plays unfiltered audio is worse
  than an obvious failure. In VST3 and the Audio Unit, whose host may still
  change its rate or channels, a first filter whose only fault is that it
  does not suit the host's rate or channel counts is kept instead: it plays
  silence, with the dry signal the state table allows, until a start that
  suits it (PLUG-27, PLUG-42, below).
- **A rejected replacement** leaves the working filter playing, unchanged,
  and saved.

A refusal is returned to the host where the format allows it (VST3's
`setState` returns `kResultFalse`; [format notes](#format-notes)), Status
shows Load failed and the Convolver log explains
([status and log](#status-and-log)). The rest of this section says when a
request is checked against the host, and so whether it is refused or kept.

The control side of these rules is shared code in the host layer, so every
adapter gets the same decisions and messages: `HostFilter` (which filter a load
replaces, which is saved and what plays) and `unfiltered_configuration` in
[`host.hpp`](../../include/convolver/host.hpp). `HostProcessor` installs a
filter directly when nothing has been processed since construction or reset.

**Saving after a failed initial load** saves the failure: the host state
records "load failed" (flag 4, state version 2) and no filter, so reopening the project plays silence again instead of
an empty instance's identity. Only this state is written as version 2.
Restoring it is a successful load of what was saved, not a rejected
replacement, so it replaces a working filter with silence.

A replacement is accepted only after it has been fully validated and prepared,
so rejection never disturbs the running filter. The rejected request is not
retried automatically.

**State that arrives before the host is configured.** Hosts can restore state
before they set the sample rate and channel layout (VST3 `setState` before
`setupProcessing`/`setBusArrangements`; AU `RestoreState` before
`Initialize`). Two kinds of check are therefore kept apart:

- **Checks on the state itself** run immediately: decoding, the version, the
  resource limits and the 1–8 channel bound. Malformed state is rejected at
  once, and the previously accepted state is unchanged.
- **Checks against the host's configuration** (sample rate, channel counts,
  layout) run as soon as that configuration is known. If it is already known
  when the state arrives, a mismatch is rejected at once, as a failed
  replacement (or a failed initial load if nothing was accepted before).
- **When the configuration is not known yet,** well-formed state replaces the
  previously accepted state straight away: the host is restoring what it saved.
  The configuration check then happens at activation. If it fails there, that is
  a failed initial load (silence and an error), but the restored state is kept
  as the saved state, so opening a project at the wrong sample rate and then
  correcting the rate brings the filter back, and saving the project does not
  lose it.
- VST3 keeps a host rate that is not a whole number of hertz exactly
  (`HostLayout::fractional_rate`, set by `set_host_rate()`; VST3-9). No
  filter suits it, so activation plays silence and records a sample-rate
  failure naming both rates (`host_rate_text()`), keeping the filter for
  when the host returns to its rate; a file chosen meanwhile is refused the
  same way. Only a rate within 1e-12 of whole hertz, relative to the rate,
  counts as whole: rounding, not a different rate.
- LV2 knows the sample rate at instantiation and its ports are fixed, so its
  configuration is always known and both kinds of check run in `restore()`.
- The Audio Unit counts its formats as known while it is initialized, and,
  uninitialized, while they still match the last initialization that
  rendered audio. JUCE hosts initialize it at AUBase's default 44.1 kHz to
  try a channel layout, then restore the project before preparing to play,
  so counting that probe would refuse a 48 kHz project's filter
  (`au.adapter` `layout_probe_before_restore`). Once the formats are known,
  restored state is checked against them; a chosen file follows the rule
  below.
- **A file chosen in the window** (PLUG-27, jrp, 20 November 2026), in VST3
  and the Audio Unit alike: it is refused for not suiting the host only
  while the host plays audio through the plug-in (started, and audio with
  samples processed in this start: `HostSettling::playing()`, for which
  `HostSettling::check()` answers `keep_unless_one_plays`, so PLUG-42 below
  refuses it only while a suiting filter plays; audio from an earlier
  start does not count, nor does a render or `process` call of zero frames,
  jrp's review of 21 November 2026, so a file chosen just after a restart
  waits in VST3 as in the Audio Unit, and an Audio Unit render of zero
  frames neither refuses a later choice nor binds restored state), and is
  then read for the host's configuration, so a
  list chooses its entry for it. Otherwise the host's rate and channels may
  be a placeholder: Audacity 4.0.2 sets up its window's copy at 44.1 kHz
  until it first plays, whatever the project's rate, and starts it only for
  a moment, with one block of silence, to pass on a parameter edit
  (`InitializeComponents` and `FlushParameters` in Audacity 4.0.2's
  [`VST3Wrapper.cpp`](https://github.com/audacity/audacity/blob/Audacity-4.0.2/au3/libraries/au3-vst3/VST3Wrapper.cpp)), so in jrp's Audacity run of 0.14.0 a 48 kHz `.cfg`
  chosen before playback was refused and the failure was what the project
  saved. So a single IR file or `.cfg` is read as it is
  (`read_filter_file()` with `settled` false) and kept
  (`keep_until_suited`): checked at once against a configuration the host
  is known to have, kept silent if it does not suit, and checked again at
  the next start. Until the host plays it, it awaits validation
  (`HostFilter::awaiting()`, cleared by `confirm()`, which the adapters
  call only when a control call finds the host running the plug-in with
  audio processed in that start, `HostSettling::playing()`: in VST3
  `Processor::status()` and `Processor::view()`, so the window's timed
  report read or any other time the controller reads the processor, as
  the controller's Status parameter never asks it; in the Audio Unit `update_status()` and
  `confirm_rendered()`, so also the host's Status and report reads; never at the stop, so Audacity's start, one
  block and stop with no call between confirms nothing, without guessing
  which starts are brief; jrp, 21 November 2026): a start
  that found it unsuited is too little evidence, so the status and the
  window treat the host's configuration as not known and say "Filter
  selected. Waiting for the host's audio settings." (jrp, 20 November 2026;
  `HostFilter::waiting()`, `shown_failure()`); the log still says what each
  start found. Once played, a mismatch shows as Silent with both rates. A configuration list cannot be read
  without a configuration: it chooses its entry for the one the host last
  started with (`known_layout()`), and before any start it is refused as
  any file is, with jrp's words, "No entry selected. ... Play audio once so
  Convolver can check the host's sample rate and channel counts, then
  choose the configuration list again." (`HostFailureKind::settings_unknown`).
  PLUG-40 (jrp, 21 November 2026: "don't make the user choose again
  unnecessarily"): such a list is read again by itself, as if chosen, once,
  when the window reads its report while the host plays audio through the
  plug-in (`HostSettling::playing()`, `HostFilter::take_unsettled_list()`),
  on the window's thread: VST3's controller in its `report()`
  (`read_waiting_list()`), marking the project changed as for a chosen
  file (setDirty, or VST3-17's Latency edit), and the Audio Unit in
  its `report()`, through `load_file()`. An accepted list joins the
  windows' recent files there, as a chosen file does, so it does not matter
  which window reads the report (PLUG-47). PLUG-49: Audacity 4.0.2 restores
  the state it last read from the plug-in at every playback start
  (`VST3Wrapper::Initialize` calls `FetchSettings`), and that state holds
  no list, so in jrp's live session of 22 November 2026 the list was
  forgotten, never read again, and the export was silent. While a list
  waits (`HostFilter::list_waits()`), VST3's `setState` with state that is
  exactly what `getState` would save now (`Processor::unchanged_state()`,
  compared encoded) changes nothing; a project the host says it is
  opening still replaces everything. A restore of that same state meant
  to choose the filter already there (an undo back to before the list,
  say) is then also ignored, and the list, still waiting, is read at the
  next playback with the window open. Audacity's flush plays no audio a
  window could see, so it never triggers this. Until then, once a control
  call has seen the host playing with the configuration it still has
  (`HostFilter::confirm()` with it, `HostFilter::played_with()`; not a
  configuration a brief start only passed, such as Audacity's flush at
  44.1 kHz, which following the advice would load the wrong entry for; the
  independent review), Status and the window say so with it,
  from the same layout: the window's Fix "The host's settings are now
  known (48000 Hz, 2 in, 2 out): choose the configuration list again."
  (`settings_now_known()`, `shown_failure_message()`), Status "now known:
  48000 Hz, 2 in, 2 out; choose the list again.". The window offers its drop
  hint only when nothing was refused. The saved state format is
  unchanged. One case stays wrong: in Audacity, a list chosen after a
  parameter edit (choosing a file reports one, VST3-17) but before the
  first playback chooses its 44.1 kHz entry,
  since that moment's start is indistinguishable from a real one; it then
  plays silence at the project's rate, with the reason, until chosen again
  after playing; and a list with no 44.1 kHz entry is refused there as a
  normal mismatch naming 44100 Hz, whose fix adds, whenever the host is
  not playing at that moment (`settled` false; also when only the channels
  do not match), "If the host has not played audio at your project's rate yet,
  play audio once so Convolver can check the host's sample rate and channel
  counts, then choose the configuration list again." (jrp, 21 November
  2026; no guess at whether a start was Audacity's) (`vst3.adapter` `chosen_before_host_plays` replays
  Audacity's order; `native.filter_file` `before_host_settles`,
  `native.host_settling` `chosen_file`; in a host, jrp's
  [Audacity pass](../archive/reviews/2026-11-21-vst3-17-audacity-pass.md)
  on 0.16.14: a list refused before playback, then its 48 kHz entry after
  one).
- One shared record holds both formats' rules: `HostSettling` in
  [`host_settling.hpp`](../../portable/host_settling.hpp) (PLUG-39 step 2,
  `native.host_settling`), which each adapter tells when the host starts
  it, plays audio with it and stops it, and asks what the host's
  configuration is and whether a request that does not suit it is refused
  or kept. VST3 uses `HostBinding::activation` and the Audio Unit
  `HostBinding::rendering`, and their differences stay as jrp decided
  (13 November 2026): while stopped after a start that never played, VST3
  still knows that start's configuration, while the Audio Unit knows only
  the last initialization that rendered; and while started, VST3 keeps
  restored state that does not suit until audio has played and never
  refuses a project the host is opening, while the Audio Unit refuses both
  at once. A chosen file has one rule in both (above; PLUG-27 replaced, for
  chosen files, the VST3 rule of 13 November that checked one once the
  plug-in had started, and the Audio Unit rule that checked every request
  once the formats were known).
- **Refused only to protect a working filter** (PLUG-42, jrp, 21 November
  2026), in VST3 and the Audio Unit: where the rules above refuse a
  request for not suiting the host (`HostSettling::check()` answers
  `keep_unless_one_plays`), it is refused only while an accepted filter
  that suits the host plays (`HostFilter::plays_suiting()`: no failure
  found at its activation; also while bypassed or stopped). The refusal's
  note then says "The filter Convolver has now stays selected when the
  host restarts the plug-in, even with settings that suit this one", and
  it does: the refused filter is not held for a later start. With nothing
  suiting playing (an empty instance, a failed load, or a kept filter that
  does not suit the host either), a refusal would protect nothing, so the
  request is kept as `keep_until_suited` keeps one: Kept, unsuited
  (`HostFilter::kept_unsuited()`), saved as the filter rather than as a
  failed load, and played at the next start that suits it. Meanwhile it
  plays as any kept filter does: silence, with Mix's dry share and
  bypass's input where the [state table](#states-and-transitions) gives
  them, where a refused first load played
  silence with no dry signal. What played before it, if it could be heard
  (a filter that suited the host, or an empty instance's input), mutes
  over 1.33 ms, as for a failed first load, not after the new playback's
  warm-up (`HostProcessor::silence(through)`; jrp's review of #258
  measured 90.6 ms at Latency 4096 before), and so does a filter it was
  still fading out. A filter kept before, or a failure, is not muted
  where the new one carries on its dry share and bypass: it plays silence
  with at most those, so a host restoring the same preset again
  (GarageBand) leaves them without a gap. Where the new one has no dry
  signal (a crossover kept after a filter with equal counts), they mute
  too. A muted empty instance or suiting filter that was
  bypassed passes the input again only once the kept playback has warmed
  up (about 84 ms at Latency 4096; on main such a load was refused and
  stayed silent): skipping the mute when bypass is on and the kept
  playback has a dry signal would close that gap. A chosen IR
  file or `.cfg` is kept the same way: `read_chosen_file()`'s
  `keep_unsuited` reads a single filter that does not suit as it is,
  rather than refusing it; a configuration list with no entry for the
  host's configuration is still refused, having no entry to keep. The
  log adds "No other filter was playing, so it was not refused: it waits
  for audio settings that suit it, ..." and the window's report "The
  filter is kept and waits for audio settings that suit it". The latest
  request is the one kept. Found by HOST-10: Ardour 8.4 restarts the
  plug-in with the outputs set in its Pin Connections a little after they
  are set, and a preset loaded in between was refused, never tried at the
  outputs that suited it, and saved as a failure. Holding a refused filter
  beside a playing one, to replace it at a start that suits it, was left
  until a user asks for it (TODO PLUG-43). LV2 refuses as before
  (`refuse_mismatch`): its configuration never changes. `native.host`
  `kept_unless_one_plays` (jrp's two tests: a waiting filter starts when
  the settings fit, and a refused replacement leaves the working filter
  selected across a restart; the mute, and none between kept filters),
  `native.host_settling`,
  `vst3.adapter` `kept_until_channels_fit` and
  `chosen_file_kept_until_it_fits`, `au.adapter` `chosen_file_kept` and
  `au.editor` `files` (a three-channel file chosen on an empty stereo unit).
- VST3 counts its configuration as known once the plug-in has been activated
  with it and has played audio with it (a `process` call with samples, in
  that activation or an earlier one since the rate or buses last changed),
  and while the rate and buses stay the same. `setupProcessing` alone is not
  binding: JUCE-based hosts, pluginval among them, call it with placeholder
  values (44.1 kHz) before restoring state made for another rate, which a
  check at that point would wrongly reject (found by
  `vst3.pluginval.acceptance` in VST3-7). Activation alone is not binding
  either: on reopening a project, and on FX Offline then Online, REAPER
  activates the plug-in on stereo buses, restores the saved state, then
  deactivates it and gives it the track's channels (jrp's crossover retest,
  7 November 2026), so a check at that point refused a saved 2→6 crossover
  and left nothing to play on six channels. State the host marks as a
  project (`IStreamAttributes`, `PresetAttributes::kStateType` "Project") is
  never refused for not suiting the host. REAPER 7.85 does not mark its
  state this way: in jrp's second retest (7 November 2026) the log gave the
  "has not played audio" reason on reopening and on FX Online. Restored state that arrives while
  the configuration is not binding is kept even if it does not suit
  (`HostFilter::load` with `HostLoadCheck::keep_until_suited`): while the
  host's configuration is known (the plug-in is active, or inactive with the
  configuration it was last started with), it is recorded at once as a
  failure found at activation and plays silence with the reason, and the
  next activation checks it again; otherwise it is checked at the next
  activation. A filter file chosen in the window follows the rule for
  chosen files above. A preset the host loads into a
  new instance before it has played audio, or just after the host changed
  its rate or buses and before it plays again, is therefore kept silent
  rather than refused; VST3 gives no other way to tell it from a restored project
  when the host does not say. `HostSettling::check()` holds the rule
  (each load names its kind, `HostRequestKind`: a chosen file, restored
  state or a session the host is opening),
  and `vst3.adapter`'s `restored_while_host_settles` replays REAPER's order.

### States and transitions

PLUG-36 (20 November 2026) defines the four states and what each request
or host event does to each; this is the authoritative table
([above](#loading-and-failure)). What plays (channel
safety) is decided first and alone; which message Status shows is decided
after it, from the same state, and never changes what plays. The expected
results in the tests named here are written from these tables, not from
production decision functions: the reference model in
[`tests/plugins/lifecycle.cpp`](../../tests/plugins/lifecycle.cpp) cites the
rules it follows.

| State | What plays (active, Bypass off) | What is saved | Status |
| --- | --- | --- | --- |
| **Empty:** nothing accepted, or an empty state accepted | Equal host counts: identity, at the level Gain and Mix give it. Unequal: silence | No filter | No filter |
| **Active:** an accepted filter that suits the host | The filter at Gain, with Mix's dry share when the filter's own counts are equal | The filter (VST3: with the settings and bypass) | Active; Waiting while the host's configuration is not known, as the filter is not checked yet |
| **Kept, unsuited:** an accepted filter that does not suit the host's rate or counts: found at activation, restored into VST3 before audio has played, chosen in VST3 or the Audio Unit while the host was not playing audio through the plug-in (PLUG-27), or kept in VST3 or the Audio Unit because no filter that suits the host was playing (PLUG-42) | Silence; Mix's dry share below 100% when the host has as many inputs as outputs and so has the filter (a kept 4-in, 4-out filter on a 2-in, 2-out track has one; a kept 2→6 crossover has none) | The filter, so correcting the host brings it back | Waiting with what it waits for: "Silent, waiting for 44100 Hz: the host runs at 48000 Hz.", or "Only the dry signal plays, waiting for …" (PLUG-44, `HostFilter::waits_for_settings()`); while a refused request is still shown, Load failed with the same reason ("Silent: …") |
| **Failed:** a first load failed, or a saved failure was restored | Silence, with no dry signal whatever the counts or Mix | The failure (flag 4), no filter | Load failed: "Silent:" with the reason, or "an earlier load failed" |

A host start whose processor cannot be built (`fail_activation()`,
ROBUST-8) plays silence with no dry share and shows Load failed ("Silent:")
whatever the state (VST3 and the Audio Unit refuse the start; LV2's
`activate()` cannot refuse, so it plays a silent processor); the saved state is unchanged, and the next start that
builds a processor returns to the state's own behaviour (`native.host`
`failure_reasons`, `status_lines`). It holds when memory runs out while the
failure is recorded too (ROBUST-9): `fail_activation()` never throws, and
records the failure (silent, with the accepted request's source and number)
before it allocates its message, so the status never reads Active with no
processor playing (`native.host` `unrecorded_failures`).

Each row gives the state after the event, from each starting state. Tests
are named as CTest runs them: lifecycle operations (`vst3.lifecycle`,
`au.lifecycle`, `lv2.lifecycle`, `native.host_lifecycle`), PLUG-2 harness
cases ([testing](testing.md#plug-in-format-tests)) and the adapters' and the
host layer's own tests.

| Event | From Empty | From Active | From Kept | From Failed | Tests |
| --- | --- | --- | --- | --- | --- |
| State or preset that suits the host, or arrives while its configuration is not known | Active | Active (replaced, or kept playing when the same filter and settings arrive) | Active | Active | Lifecycle `state-A`, `state-B`; harness `initial`, `restore-after-activation`, `restore-before-setup`, `live-replacement`, `same-filter-restore`; `native.host` `same_filter_kept`, `request_changes` |
| State that does not suit, while the configuration is not known | Waiting, then Kept at activation | The same | The same | The same | Lifecycle `state-other-rate` before `setup`; harness `restore-before-setup-mismatch` |
| State that does not suit, VST3, started but not yet played with this configuration | Kept | Kept | Kept | Kept | `vst3.adapter` `restored_while_host_settles`; `native.host` `status_lines` ("kept, unsuited"); lifecycle `state-other-rate` (VST3) |
| State that does not suit, configuration known | Failed (VST3 and the Audio Unit: Kept, PLUG-42) | Active, previous filter kept: Status "previous filter kept" | Kept: Status still gives why the kept filter is silent; the log gives the refusal (VST3 and the Audio Unit: Kept with the new filter, PLUG-42) | Failed, with the new reason (VST3 and the Audio Unit: Kept, PLUG-42) | Lifecycle `state-other-rate`; `native.host` `kept_unless_one_plays`; harness `failed-replacement`, `failed-replacement-inactive`, `failed-initial-load`; `vst3.adapter` `failed_first_loads`, `failure_records`; `au.adapter` `rejected_presets`; `lv2.adapter` `refused_first_loads`; `native.host` `failure_reasons` |
| Malformed or newer state (checked at once, in every format) | Failed | Active, previous filter kept | Kept | Failed | Lifecycle `state-malformed`; `vst3.adapter` `failed_first_loads`; `native.host` `failure_reasons` |
| Empty state; LV2's empty state map | Empty | Empty | Empty | Empty | Lifecycle `state-empty`, `empty-map`; `lv2.adapter` `empty_state_resets` |
| Saved failed state | Failed | Failed: silence replaces the filter | Failed | Failed | Lifecycle `state-failed`; `native.host` `processing_faults` (the saved failure holds the fault signal) |
| Chosen filter file accepted (VST3 and the Audio Unit while the host is not playing: checked at once against a configuration the host is known to have, and again when it starts; PLUG-27) | Active; Kept if it does not suit a configuration the host is known to have | The same | The same | The same | Lifecycle `file-A` then `work` (LV2); `vst3.adapter` `chosen_preset`, `chosen_preset_before_activation`, `chosen_before_host_plays`, `chosen_file_on_wider_bus`; `au.adapter` `chosen_preset`; `lv2.adapter` `worker_loads`, `rechosen_files` |
| Chosen filter file refused (missing, unreadable, a list before the host has started, or not suiting a host that is playing, in VST3 and the Audio Unit only while a suiting filter plays: PLUG-42; and an Ambisonic B-format file once the host's two inputs and two outputs are known: [default routings](#default-routings)) | Failed | Active, previous filter kept | Kept | Failed, with the new reason | Lifecycle `file-missing`, `file-refused`; `lv2.adapter` `refused_first_loads`; `vst3.adapter` `report_and_files`, `chosen_preset` |
| Activation after a rate or bus change | Empty: identity or silence by the new counts | Active if the filter suits, otherwise Kept | Active if the filter now suits, otherwise Kept | Failed | Lifecycle `rate`, `layout`, `start`; harness `restore-before-setup-mismatch`; `native.host` `status_lines` ("kept, then suited"), `processing_faults` |
| Reopening the project (the saved state restored into a new instance) | Empty | Active | Waiting, then Kept at activation (LV2: Kept at once) | Failed | Lifecycle `reopen` |
| Bypass | Unchanged. The untouched input where the host's counts are equal, else silence | Unchanged. The input where the filter's counts are equal, else silence | Unchanged. The input where the host has as many inputs as outputs and so has the filter, else silence | Unchanged. Silence | Lifecycle `bypass`; harness `bypass`; `native.host` `unequal_bypass`, `status_lines`; `native.host_report` `cases` |
| Mix below 100% | Unchanged. The input at the level Gain and Mix give it, where the counts are equal | Unchanged. The filter with the dry share, where its counts are equal | Unchanged. The dry share, where the host and the filter each have as many inputs as outputs | Unchanged. Silence | `native.host_settings` `unequal_mix`, `diagnostics_follow_settings`; `native.host_report` `cases` ("wrong rate, Mix 50%", "failed first load, Mix 50%") |
| Processing fault (rejected input, output overflow, a failed engine call, an invalid call) | Unchanged; the affected samples or call play silence | Unchanged; the same, and an overflow restarts the filter | Unchanged | Unchanged | `native.host` `processing_faults`; harness `nonfinite-input` |

Only an accepted filter or empty state moves an instance out of Failed;
a refused request or another saved failure leaves it there. Bypass, Mix
and faults never change the state, so they never change the filter or
failure that is saved (VST3 saves its bypass and settings beside it, as
[host state](#host-state) says).

**Which message Status shows** (`host_status_value()` in
[`host.cpp`](../../portable/host.cpp), which `host_status()`, the window's
report and LV2's status port all follow; `native.host` `status_lines`),
first match wins:

1. A load failure: the failure found at activation while it lasts, otherwise
   the newest refused request's. A kept filter that does not suit therefore
   keeps showing why it is silent while a later refusal is logged. With no
   refused request since the last accepted load, that kept filter only
   waits for settings that suit it (`waits_for_settings()`), and the value
   is Waiting instead, with the same reason (jrp, 21 November 2026,
   PLUG-44); LV2's port still shows Load failed for it, as its only value
   for Waiting is running.
2. A fault, unless a failed load left no filter. Faults stay until an
   accepted load or an activation; while a load failure is shown the report
   adds them as a note.
3. Bypassed, when a filter is loaded.
4. Active, or Waiting before the host's configuration is known.
5. Load failed after a failed load.
6. No filter.

What the line says when no filter is heard, Mix's dry share and bypass
included, is in [status and log](#status-and-log).

## Failure reasons

Added 22 October 2026 from jrp's
[release-readiness guidance](../review-2026-10/release-readiness-guidance-2026-10-22.md);
PLUG-8 implements it. Silence can be correct processing and still confuse the
user, so every failure in the [state table](#states-and-transitions) keeps a structured reason; where it
is shown is in [status and log](#status-and-log) (DIAG-1):

- **What failed:** a missing or unreadable filter file, unreadable or
  malformed state, a newer state version, the sample rate, the channel counts
  or layout, a filter too large to save (PLUG-12), preparation, or activation
  (DIAG-1's table, which predates the size check).
- **The request it belongs to:** where the filter came from (preset, saved
  state, file path), its rate and channel counts, and a request number, so a
  later request's success or failure is never confused with it.
- **The outcome:** a working filter kept, or silence.
- **The explanatory message,** written as the CLI writes its diagnostics: what
  failed, why, and what to change. It is kept in its parts (`Advice` in
  [`advice.hpp`](../../include/convolver/advice.hpp): problem, cause, fix,
  note), and each surface formats it where it leaves the library: the log's
  one line, the window's rows, the text `HostFilter::error()` gives a report
  or a test (DIAG-4). Exceptions keep the parts too (PLUG-39 step 5b:
  `detail::AdvisedError`, read with `detail::advice_of()` in
  [`portable/advice.hpp`](../../portable/advice.hpp)), with `what()` the
  same parts as text, one labelled line per part; nothing parses that text
  back. An adapter hands a failed start's exception to
  `HostFilter::fail_activation()`, which keeps its problem as the reason, so
  its cause and fix never appear inside the record's own.

The reason lives on the control side (in `HostFilter` or beside it) and never
on the audio thread: the audio callback only plays what it was given. Every
adapter records a reason on every failure path, including activation and
worker completions, and clears it when a later request succeeds. Tests read it
through the adapter even where the format cannot display it yet. Notices that
are not failures use the same record unless PLUG-8 records why not.

Implemented 23 October 2026 by PLUG-8 (PR #46): `HostFailure` in
[`host.hpp`](../../include/convolver/host.hpp), returned by
`HostFilter::failure()`. A rejected request's reason stays until a later
request succeeds, across activations, so the "load failed" status does not
disappear when the host restarts the plug-in. A mismatch found at activation
belongs to the request that supplied the filter, comes first while it lasts,
and clears at the next activation that suits the filter. Restoring a saved
failed load is a successful request whose instance plays silence, so it keeps
a reason of its own kind (the original reason is not saved; the state format
is unchanged). The speaker-layout notice stays a separate record
(`HostFilter::notice()`, an `Advice` too): it never changes what plays, and it describes the
host as it is now rather than a request, so it is recomputed at every check
instead of being kept until a later request succeeds. Adapters log
`HostFilter::rejection()`, the newest request's own reason, when a load is
rejected, so a failure found at activation never hides why the new preset was
refused. A host format that the adapter refuses before any filter is
involved (an Audio Unit asked for different input and output sample rates)
is returned to the host as an error and logged, but not recorded: the unit
does not start, and no filter request failed.

## Status and log

Decided by jrp (DIAG-1, brought forward before the first
named-host smoke tests): adapters without a host log interface show why they
are silent or refused a load in two places, with the same words in every
format.

- **A read-only Status parameter** whose value is a `HostStatus` (Active, No
  filter, Load failed, Waiting, Fault, Bypassed; PLUG-22 renamed
  Playing to Active and added Bypassed as the last value, so saved automation
  of other parameters is unaffected; DOC-9 renamed Not checked yet to Waiting,
  the value unchanged) and whose text for the current value
  is `host_status()`'s one line of at most 127 ASCII characters
  ([`host.hpp`](../../include/convolver/host.hpp)). The status follows
  `HostFilter::failure()`, so a refused replacement shows Load failed while
  the previous filter plays, as LV2's status port does. Before the host's
  configuration is known it is Waiting and names the filter's rate and
  channels ("waiting for audio settings from the host"). Adapters with [settings](#settings) pass them, so that the
  line says what is heard when no filter plays (jrp's review of 0.5.5): the
  input at the level Gain and Mix give it when no filter is loaded, and only
  the dry signal, not "Silent", when a kept filter does not suit the host at
  a Mix below 1 and there is a dry signal ([bypass](#bypass)). While
  bypassed it keeps a failure visible but describes what bypass plays, the
  untouched input where there is a dry signal, never silence or attenuation
  that is not happening (jrp, 20 November 2026, PLUG-36): a kept filter that
  does not suit the host reads "Bypass is on: only the dry signal plays.
  Waiting for 44100 Hz: the host runs at 48000 Hz." (PLUG-44; "Load
  failed. Bypass is on: only the dry signal plays. The filter is for 44100
  Hz, …" while a refused request is shown, with no room for the log's
  pointer), and an instance with no filter
  reads "No filter. Bypass is on: audio passes unchanged." whatever Gain and
  Mix are. Where bypass plays silence the line says "Silent" as before: a
  failed load with no filter kept, a filter or host with unequal counts,
  and an activation whose processor could not be built. The window's report
  says the same ("Bypass is on: only the dry signal plays.";
  `native.host` `status_lines`, `native.host_report` `cases`). Since
  PLUG-39 step 4 every other text follows bypass the same way, from
  `playing_words()`, `failure_consequence()` and `rejected_line()` in
  [`host_log.cpp`](../../portable/host_log.cpp): the start log line
  ("Bypass is on: the input plays unfiltered. Loaded: …"), a refused
  replacement (the log's "The previous filter is kept. Bypass is on: the
  input plays unfiltered."; the window's report names the kept filter
  instead, "room.cfg is still selected. Bypass is on: …", from
  `kept_words()` in `host_report.cpp`, which says "keeps playing" only
  while the host runs the plug-in and, for a kept filter that does not
  suit the host, what it will play at, explaining the refused request
  first, PLUG-44), its log line ("the previous filter is kept (bypass is
  on)"), the report's fault explanation ("Bypass is on: the input plays
  unfiltered.") and VST3's line for restored state kept until the host
  suits it never say that the filter plays while bypass is on, and a list
  entry chosen for a wider bus "is used" rather than "plays". The start
  line's Settings part still names what the Gain mode would play. Before the
  host's configuration is known it cannot tell whether a dry signal will
  play, so it says "no filter plays". DIAG-10 adds what the window's report
  says about the settings, in short, after the line's own words: a Latency
  chosen but not in use yet ("Latency 1024 frames not in use yet.", from
  the latency the processor plays at, except after a load failure, a kept
  filter's wait or a fault, which say more; with no processor, as while
  the Audio Unit is uninitialized, the choice is selected for the next
  start, so it is never "not in use yet", although for a moment, until the
  Latency queue publishes it, the host may still read the previous
  latency, AU-19), and, with a filter, Estimated chosen without an
  estimate ("No estimate yet: Clip-safe gain plays.") or no gain figure at all ("Silent: no gain figure for this
  filter.", or "No gain figure for this filter: only the dry signal
  plays." where Mix's dry share plays, and nothing beyond Mix's own note at
  Mix 0%); the gain notes come before the speaker-label and Mix 0% notes,
  and the 127-character limit cuts what does not fit
  (`native.host_settings` `status_names_settings`, which also checks that
  a 3e38 tap has its estimate: a filter that fails its estimate has no
  bound either, FFT-5). With a filter that plays and Gain mode at Estimated or
  Clip-safe, the line ends with the gain that mode plays ("Estimated gain
  -7.3 dB.", PLUG-44), which hosts showing only their generic controls
  cannot see beside Gain; not where Mix lets only the dry signal through.
  VST3's `Processor::status()` passes the same latency as its
  report. The Status value does
  not change for these. The Audio Unit
  recomputes the line each time a host reads it, as Gain and Mix may have
  changed. The parameter is not automatable and is not part of the host
  state. VST3: parameter ID 1001 (Bypass is 1000), a list parameter with `kIsReadOnly`; the
  controller reads the line from the processor after every load, activation,
  pairing, `setupProcessing` and `setBusArrangements`, and asks the host to
  reread its parameters when it changed (`restartComponent(kParamValuesChanged)`;
  hosts may ignore it). Audio Unit: global parameter 1001, indexed, readable
  and not writable; the line is the current value's
  `kAudioUnitProperty_ParameterStringFromValue`, recomputed after every
  restore, initialization, cleanup and stream format change, and a
  parameter-change event is sent outside the control lock. A bypass switch
  (`kAudioUnitProperty_BypassEffect`) updates it at once when the control
  lock is free. The setter never waits for the lock: when another call
  holds it, the Latency work's serial queue updates the Status once the
  lock is free, so a host that reads it straight after setting bypass may
  see the old value until the call holding the lock or the Latency work
  updates it. The setter wakes that work with its own bit of the dispatch
  source's data, and such a run only updates the Status: it does not
  retry a Latency that could not be built (AU-16; [calls that do not wait
  for the control lock](#calls-that-do-not-wait-for-the-control-lock)).
  The line is built when read, from the diagnostics snapshot and the Gain,
  Mix and bypass set then (PLUG-39 step 4a). Host writes to it
  are ignored, and the value `AUBase::SaveState` writes with the other
  parameters is ignored on restore. When the log file cannot be written
  (`host_log_written()` is false), the text says so instead of pointing to it.
- **The editor report** (PLUG-9, [`host_report.hpp`](../../portable/host_report.hpp)):
  for a window and its Copy diagnostics, `host_report()` gathers the status
  and its line (`host_status()`), DIAG-2's message in parts (the problem as a
  sentence, what plays (`failure_consequence()` or `playing_words()`), the
  cause, the fix, a note, and the window's action, "choose filter" or "copy
  diagnostics"), taken from the failure's or notice's parts as they are
  (DIAG-4), and all of it as one explanation text with `Cause:`, `Fix:`
  and `Note:` lines ([`advice.hpp`](../../portable/advice.hpp)); then
  the filter's name for the heading (the file's name with its extension;
  after a restore the name saved with the project, `HostRequest::name` in
  the Audio Unit, or VST3's controller state (which a `.vstpreset` made by
  `convolver-preset` also holds, PLUG-31), or "Restored filter" without
  one, which claims no origin the host did not give; PLUG-22, PLUG-31.
  One rule in `HostFilter` (`offer_name()`, PLUG-39 step 4) shows a name
  only while its filter plays: VST3 offers the controller state's name
  with the filter identity saved beside it (`HostFilter::filter_identity()`,
  a hash of every stored value, found once when the filter is accepted),
  which state naming nothing takes when its identity matches, in whichever
  order the host sends the two; an accepted file's own name is offered the
  same way; and state of the filter that already plays keeps the name it
  shows. A host sends a refused preset's controller state as well, and
  that never renames the playing filter. The report's `filter saved name`
  is the playing filter's name, which `Controller::getState` saves) and a
  note when
  the newest request was
  refused, the filter's own values for under its name (rate, channels,
  length and taps, speakers; PLUG-22), the Audio facts as short labelled
  values (`HostFact`: host sample rate, host channels, routing, ring-out,
  loudest peak from `HostFilter::filter_peak()`, host block size; each with a
  short note, a problem flag for what does not match, and help text), the
  settings in words, and reference lines: the filter file and request,
  each output's routes (PLUG-51), `execution_line()` and the log. Everything a window says about the
  filter and the audio comes from here, so the VST3 windows (VST3-11 to
  VST3-13) show the same; the control labels, hints and tooltips are the
  view's own. Messages passed to `HostFilter::reject()` and the other
  failure records use the same parts; the adapters log them on one line
  (`advice_line()`), so `host_log()` and the LV2 log only join any other
  lines with " / ".
  `encode_report()` gives the whole report as bytes for a format's
  transport, which `decode_report()` reads back whole or not at all
  (PLUG-39 step 5: one codec, so no window parses text lines back),
  `report_text()` gives it as plain text, and `diagnostics_text()` adds
  this instance's recent log lines (`host_log_recent()`, at most the last
  64 KiB of the file). Built on the control side; reading the log happens
  only when diagnostics are asked for. Audio Unit (AU-6): global custom
  properties 64000 (`encode_report()`'s bytes as a `CFData`) and 64001
  (the diagnostics text as a `CFString`), read-only, values released by the
  caller, with a property-change notification for 64000 wherever the Status
  parameter's event is sent. They are internal between the unit and its own
  window, which ship together, so they are not permanent identities.
  Audio Unit listeners (AU-17): the unit's own notifications of the
  latency, the tail, 64000, and those of a file chosen in the window
  (AU-21) reach the host's property listeners on the main thread. AUBase keeps its listeners in a vector without a lock, which
  hosts change on their main thread, so a notice raised on any other thread
  (the Latency queue, or a host call such as a restore, `Initialize` or a
  format change made off the main thread) is handed to the main queue
  through a `DISPATCH_SOURCE_TYPE_DATA_OR` source, as the JUCE Audio Unit
  wrapper does, instead of reaching listeners during that call. Notices
  posted before the main thread runs coalesce, and one still pending when
  the unit closes is dropped. A host whose main thread never runs its run
  loop is not told of those changes, and reads the latency, the tail and
  the present preset when it asks for them; each instance it opens then
  keeps a small record and a dispatch source until the process ends. A file
  is chosen by setting custom property 64002, which the window does on the
  main thread but an out-of-process host may pass on from another thread.
  Its notices, `ClassInfo`, `PresentPreset` and AUBase's own notice that
  64002 was set (which `Effect::PropertyChanged` intercepts), are told at
  once on the main thread and handed over otherwise. The Audio Unit SDK's
  notices for a host's calls to its standard properties, such as a stream
  format, are still sent on the calling thread.
  `kAudioUnitProperty_CocoaUI` names the window's factory class in this
  image's own component bundle; the factory and view class names carry the
  version. Unlike the archived editor plan's rule that the window copies reports
  pushed to it ([threads](../archive/plans/2026-10-24-plugin-editor.md#threads),
  rule 2), the AU window reads 64000 itself, as Audio Unit views do: twice a
  second while its window is on screen, on the main thread. A read builds
  the report under the unit's control lock when the lock is free, and keeps
  a copy; while a load holds the lock (a state restore or a chosen file,
  preparing its filter), a read returns that copy at once instead of
  waiting; the window's next timed read shows the result. Only a read
  before any other waits. This keeps the window prompt while the host
  restores a state on another thread; a file chosen in the window is
  loaded on the main thread itself, so the window waits for it. Property 64002 (read and write, a `CFString`)
  loads a filter file chosen in the window: the unit reads it with
  `read_chosen_file()` ([`chosen_file.hpp`](../../portable/chosen_file.hpp)):
  a filter file through `read_filter_file()`
  ([`filter_file.hpp`](../../portable/filter_file.hpp), the step
  `convolver-preset` uses) and macOS's AudioToolbox rather than libsndfile
  ([`audio_toolbox.cpp`](../../portable/audio_toolbox.cpp)), for its current
  rate and channels, or a Convolver `.vstpreset` through the reader
  `convolver-preset` uses for one (PLUG-26, `presets::read_vst3_preset_file()`),
  then
  installs it through the same `HostFilter::load` as a state restore, with
  `HostRequest{HostFilterSource::file, path}`, so the failure rules,
  replacement and saving are unchanged and the saved state is the one the
  tool writes. A chosen `.vstpreset` that holds settings also sets Gain, Mix
  and Latency, as a restored state's do (applied once the filter is
  accepted, the Latency at once, and parameter listeners told); one without
  them (from `convolver-preset`, or saved at the default settings before
  PLUG-35 made the state record them) leaves them; one saved at the defaults
  since then sets them back to the defaults. Setting it returns an error when the file was not loaded;
  reading it gives the path of this session's accepted file load. The file
  is read outside the control lock, on the thread that set the property
  (the host's main thread for the window); preparing the filter runs under
  the lock there, as a restore does. A file load, accepted or refused,
  also sends a `ClassInfo` change notification, since the saved state
  changes either way, for hosts that mark the project changed from it. An
  accepted file also becomes the present preset, a user preset (number -1)
  named after the file, with a `PresentPreset` change notification:
  GarageBand 10.4.14 marks the project changed from that, not from
  `ClassInfo` alone. While faults
  go on, a read takes them in (and logs them) at most every 10 seconds, once
  Render's Fault is explained.
- **The Convolver log** (`host_log()` in
  [`host_log.hpp`](../../portable/host_log.hpp)): one dated line per accepted
  load (`accepted_line()`, worded the same by the VST3 plug-in and the Audio
  Unit, PLUG-62: what was loaded, the settings a chosen preset or restored
  state brought, for a kept filter that does not suit the host what plays
  meanwhile and why, and whether the host may still change its
  configuration), start (`started_line()`: rate, channels, block size
  and what plays, with the failure or the filter's notice (`filter_notice()`:
  a filter that plays as silence, or the speaker notice) in full, and what Gain
  and Mix let through when no filter is heard) and rejection
  (`HostFilter::rejection()`'s message), tagged with the version, format,
  instance number and host program. An instance skips a line identical to the
  one it wrote last, as hosts restart plug-ins often; the Audio Unit also
  compares failure lines without their request number, as GarageBand
  restores one preset several times in a row (accepted loads are always
  logged). Adapters write the file
  outside their control lock. On macOS the lines also go to the unified log.
  Control thread only. Loads and rejections are numbered by request
  (`loaded_line()`, `rejected_line()`: `Request 3: ...`), every line names the
  process ID, and the process's first line is `execution_line()`: version,
  FFT backend and its version, CPU kernels, the format SDK the adapter was
  built with (`set_host_log_sdk()`, from the build's `CONVOLVER_SDK_DESCRIPTION`;
  [supporting libraries](build.md#supporting-libraries)), process and host
  program. A line
  the file does not take goes to standard error (and `OutputDebugString` on
  Windows), after one line saying why, and `host_log_written()` turns false;
  the file gets the execution line with its first written line. An empty
  `CONVOLVER_LOG` turns the file off on purpose and is not a failure. Locations are in the
  [user guide](../use/plugins.md#status-and-the-convolver-log); tests point
  `CONVOLVER_LOG` into the build folder. Host requests the plug-in refuses
  are logged with the reason, as the host only gets an error code: VST3's
  `setupProcessing` (a rate that is not finite or outside 1 to 768000 Hz,
  block size, sample size) and `setBusArrangements`
  (only an instance's first declined layout, as hosts offer one after another,
  worded as an offer Convolver declined rather than a failure, since REAPER
  offers every instance 64 channels each way first),
  and LV2's `instantiate` (through the host's `log:log`, on standard error
  without it). A failed save (`getState`, `SaveState`, LV2 `save` when the
  filter itself was not stored) logs `save_failure_message`.
- **Processing faults** (jrp's review of 24 October 2026). `HostProcessor`
  counts, per `process()` call and without locking or allocating, rejected
  input (nonfinite or out-of-range samples, played as silence), output
  overflow (extreme but finite input or filter values: those samples are
  silenced and the filter restarts), processing failure (the engine threw:
  the call is silenced and returns false) and invalid calls (more frames or
  channels than set up: refused; only the Audio Unit reaches this, as the
  VST3 adapter refuses such calls itself and LV2's are split to fit). `HostProcessor::faults()` reads the counts
  from any thread. On the control side, `HostFilter::observe()` takes them and
  returns the new ones, which adapters log with `fault_message()`;
  `HostFilter::faults()` keeps them until an accepted load or `activate()`,
  and `host_status()` shows them as Fault unless a load failure is shown.
  An activation is recorded only once its processor will play (ROBUST-8):
  `HostFilter::activation()` finds what to play and why, and changes
  nothing. Every adapter starts its processor, at a host start and for a
  new Latency, with `HostFilter::start()` (PLUG-61), which builds the
  processor from it, sets its bypass, resets it, and only then calls
  `activate()`, which cannot fail, and holds the processor's fault signal
  while a failure is shown; handing it to the audio thread cannot fail
  either. A new Latency whose processor cannot be built
  (memory runs out) leaves the fault count, the filter's notice and the
  reason a kept filter plays silence as they were, for the processor that
  keeps playing. A host start whose processor cannot be built records that
  failure instead (`fail_activation()`), with the count started again, as
  no processor plays on.
  While one is shown, the report says so (`held`), and its log text says
  that the status shows Load failed instead of Fault.
  One baseline serves both sides (jrp's review of 0.4.43): an adapter passes
  its `HostProcessor` to `HostFilter::load()`, which, when it accepts the
  filter, takes one snapshot of the counts (`HostProcessor::clear_faults()`)
  as both the audio thread's new baseline and the point `observe()` counts
  from. Faults from before the snapshot, the preparation window included, are
  still returned by the next `observe()`, but no longer shown as Fault. That
  report sets `changed`, and its log text says the faults were met around a
  filter change and that the status shows only those after it; they are not
  divided between the two filters (DIAG-3, jrp's simplification review of
  5 November 2026: exact attribution near a replacement does not change the
  audio), and the audio thread signals only faults after it
  (`faults_since_clear()`, `fault_to_signal()`, `fault_signalled()`, once per
  baseline). The audio thread never publishes Fault over Load failed: an
  accepted load sets `hold_fault_signal(failure_shown())` before the
  snapshot, and VST3 and the Audio Unit set it again after every rejection
  and activation; LV2's status port puts Load failed first itself. When an
  empty instance's first load fails, the processor that plays is silenced
  with `HostProcessor::silence()` (ROBUST-3), by
  `HostFilter::fail_request()`, which every adapter calls with that
  processor after any request that failed (PLUG-39 step 1b), rather than
  a new activation, which would restart the fault count; it neither allocates
  nor fails, so the silence holds even when memory has run out. A first
  load that runs out of memory before its failure is recorded still fails
  (ROBUST-7): `HostFilter` marks the instance failed (`fail_request()`)
  before it allocates anything for the record, which then keeps the kind
  and request number but may lack the message (`failure_message()` gives
  general advice instead, from a constant built when the library loads, so
  that choosing the message needs no memory; VST3-20) or the path. The
  adapters' call to `fail_request()` covers a request that throws before `HostFilter`
  records it too: VST3 in
  `refused()`, the Audio Unit in `fail_unrecorded()`, and LV2 in
  `restore()`, `work()` and `record_dropped()`. The allocation-starvation
  lifecycle tests cover restored state on all three formats; the
  file-loading paths are checked by review only. The audio
  thread shows a new fault at once without logging: VST3 writes Fault to the
  Status parameter as an output parameter change, and when the host passes it
  to the controller (`setParamNormalized`), the controller asks the
  processor, which explains; it also explains on
  `getState`, `setState` and `setActive` (not `setProcessing`, which hosts
  may call on the audio thread). Each episode is logged, even if its text
  repeats the last line. The Audio Unit's
  Render sets the parameter's value, and the next request for its text, save,
  restore, initialization or cleanup explains. LV2 shows Fault on its status
  port in the same `run()` and schedules a fault report (worker request
  generation 0, never answered; one at a time, and only for faults no
  report covered yet), whose `work()` logs through `log:log`; without
  a worker, the next save, restore, activation or deactivation does.

LV2 keeps its own routes (the `status` port, whose values are Active, No
filter, Load failed, Loading and Fault, and `log:log`) and does not write
the file. An editor (HOST-2) would show the same text.

VST3's controller reaches the processor of its own instance directly
(VST3-26, [`link.hpp`](../../plugins/vst3/link.hpp)). The factory does not
mark the processor distributable, so the SDK requires a host to keep both
halves in one process. The processor registers a `Bridge` under a random
key. Each time the controller connects, it starts unpaired, numbers the
connection and asks for the key (`ConvolverPairRequest`, with the number);
the processor answers with its key and that number in a `ConvolverPair`
message, from `notify()`, or from its own `connect()` if the request came
before it was connected. Each time the processor connects it also sends
`ConvolverPairAsk`, which carries nothing, and the controller asks again for
its current connection, so a host that dropped the request still pairs.
Pairing therefore works in either order and through a proxy that delivers
later, but needs the host to deliver messages in both directions, as the
SDK's connection proxy does. The controller pairs only with an answer that
carries its current connection's number, and with none while disconnected
(VST3-27): a host that delivers late may pass on an earlier processor's
answer after it connected the controller to another, and that processor's
key stays valid while it lives, so a key alone would send the next Latency,
Gain mode or chosen file to the wrong instance. A key that finds nothing
(another process, a closed processor) leaves the controller unpaired: it
shows the host's values, reports nothing and loads nothing. A pairing
answer that arrives off the UI thread only pairs; the rest (the name and
choices the processor has not had, then a read that takes the processor's
Gain, Mix and Bypass, keeping automation the host set while unpaired
after the processor's last restore) waits for the next call there, a
restore of the controller's state included, but not one made inside
`restartComponent` (VST3-32). That call finishes the pairing before it
takes a restore that also arrived on another thread. Each pairing starts afresh: what the controller learned from a
processor (its generation, the latency the host was told, what a read
left untold) is not carried to the next, so a controller moved to another
processor only records that one's latency, which the host reads when it
activates it. The processor detaches the `Bridge` when
it terminates, waiting for a call in progress; later calls refuse. Messages
carry no settings and no answers, so one delivered late has nothing stale
in it:

- After a host call that may change what the controller shows (a restore,
  an activation, `setupProcessing`, `setBusArrangements`, a save that
  explained faults), the processor marks the `Bridge` and sends
  `ConvolverChanged`, outside its control lock. The controller reads the
  processor again (`PairView`: the editor report, which holds the Status
  line, the log tag, the settings, Bypass, a generation and the latency) at
  that message when it arrives on the UI thread (the thread that
  initialized the controller, as the SDK requires), otherwise at its next
  call there. Every host callback therefore comes on the UI thread, also
  when an offline render restores the processor on its own thread. The
  controller's own state also changes only there (VST3-29): a restore
  the host makes on another thread waits, and the next call from the host
  on the UI thread that uses the controller's state (a parameter read or
  edit, a message, the window, a state save or restore) takes it first.
  On either thread the controller reads only what a restored state says
  before its filter, the settings and bypass, and reads the filter past
  without decoding it (PLUG-58, `read_state_header()`): checking the
  filter is the processor's, so a state whose filter is damaged but whose
  start is sound gives the controller its settings, while the processor
  refuses it: an unpaired controller shows them, and one restored off the
  UI thread keeps them pending in case its processor goes before the next
  call there. A state cut short is refused.
  An unpaired controller then shows the state's values, so a pairing
  answer that arrives next finds the pending Latency or Gain mode and the
  host's Gain, Mix and Bypass edits replaced, as a restore on the UI
  thread leaves them. A paired one reads the processor at the next of
  those calls, a parameter read included (VST3-31: the host reads the
  restored values straight after its restore). A parameter read asks the
  host for nothing, as a restart inside it must not happen: the request
  to read the parameters again and a new Latency are made at the next
  call that may make them (a parameter edit, the
  processor's `ConvolverChanged`, the window, a state save). A host
  that initialized the controller on another thread would see the
  parameters and the window stop following the processor. The controller
  never calls `restartComponent` inside another: Ardour 8 and 9 read the
  parameters inside it and pass changed values back to
  `setParamNormalized`, and a second call there waits forever, so flags
  asked for meanwhile are sent once the first returns. Ardour holds those
  values as float, so the controller compares Latency and Gain mode by
  their entry, not their exact value. `float_host` covers the comparison
  in the refresh; the deferral and the comparison in `setParamNormalized`
  are defence that no known host reaches yet. Before a window load and a
  report the controller reads what the processor changed, since VST3-17's
  edit, which a host without `setDirty` hands back, carries the Latency
  it shows. The controller keeps the report
  under its own lock, derives the Status parameter, its text and the
  window's content from it, and asks the host to reread the parameters
  only when one changed; Copy diagnostics joins the report's text with the
  recent log lines (`diagnostics_text()`).
- A window reading the report (`Controller::report()`) reads the processor
  each time, since Gain and Mix change in `process()`, which says nothing,
  and first asks it to explain faults at most every 10 seconds, as the
  Audio Unit does.
- A chosen file goes to the processor as a call (`load_chosen()`), which
  reads it with `read_chosen_file()` for the current rate and channels
  (macOS AudioToolbox, as the Audio Unit reads; libsndfile on Windows and
  Linux; builds with no reader refuse it with a reason) and installs it as
  `setState` does, or records the refusal, and returns whether it was
  accepted. PLUG-26: a chosen Convolver `.vstpreset` is read as
  `convolver-preset` reads it, and when it holds settings its Gain, Mix and
  Latency apply with its filter (its bypass flag does not: the host's
  Bypass stays); the answer then carries them, and the controller reports
  each one that changes to the host as an edit (`beginEdit`, `performEdit`,
  `endEdit`), so the host's parameters and saved project agree with what
  plays, then asks for a latency restart when the latency changed. They are
  three separate gestures: a host writing automation records them, which
  its own preset menu never does, and undoing one in the host changes that
  setting back, not the filter. The host hands Gain and Mix back in
  `process()` with the values the processor already has, and Latency to
  `setParamNormalized` as no change. A preset without settings leaves them;
  one holding the defaults reports any that change (PLUG-35).
  The controller then calls
  `setDirty(true)` for an accepted file, so the host marks the project
  changed. When `setDirty` does not succeed (a host without
  `IComponentHandler2`, such as Audacity 4.0.2), the controller instead
  reports one edit of Latency that keeps its value (`beginEdit`,
  `performEdit` with the current value, `endEdit`), so that such a host
  stores the new state (VST3-17; the Audacity reasoning is in its
  [archived TODO text](../archive/todo/2026-11-20-open-item-history.md),
  and jrp's [Audacity pass](../archive/reviews/2026-11-21-vst3-17-audacity-pass.md)
  confirms it), unless a chosen preset's settings were already reported as
  edits. Latency is used because it is not automatable and `process()`
  ignores it (the controller gives a new Latency to the processor only when
  the value changes), so neither the sound nor Gain, Mix or automation
  change. It is a narrow fix: a host whose `setDirty` succeeds sees nothing
  new, and the general fix is the host implementing `IComponentHandler2`.
  Reading happens on the window's thread, never in `process()`.
- Until VST3-26 the two halves exchanged eight kinds of message, each
  carrying settings or an answer, which a host could deliver late
  (VST3-15) or between the two calls of a restore; each fix for an answer
  overtaken by a later action (VST3-18, VST3-19 and the window-preset cases
  of pull request #281) added
  bookkeeping. JUCE's VST3 wrapper also lets its controller use the
  processor directly, and Steinberg's simplest example is a single object.

## Scope of each change

jrp's rule (12 November 2026): each kind of change does only the work it
needs, in a narrow function of its own, so that changing one number never
replaces the filter. PLUG-10's first estimate replaced the playing filter to
change its gain, which restarted its history and silenced it for the
filter's delay; `HostProcessor::update_gains()` now changes the figure in
place.

| Change | What it does | What must stay unchanged |
| --- | --- | --- |
| Gain, Mix, Gain mode or a calculated gain; bypass | Updates the value and ramps: `set_gain_db()`, `set_mix()`, `set_gain_mode()`, `update_gains()`, `set_bypass()` | The filter, its history, the latency and the tail |
| Filter name, diagnostics, the window | Updates what is shown only | Everything the audio thread uses |
| A different filter | Prepares it off the audio thread and crossfades (`HostProcessor::replace`, [replacement and threads](#replacement-and-threads)) | The settings and the latency |
| The filter already playing, in restored state or a preset | Keeps the processor (`HostProcessor::keep()`, below); the state's settings take their rows | The filter, its history, the latency and the tail |
| Latency, or the host's rate or channels | Builds a new `HostProcessor`, only when the value differs (Latency) or at activation (the host's) | The filter and the settings |
| Saving | Encodes the state | Playback |

A request can hold several changes (restored state holds a filter and the
settings), and each part takes its own row.

The rows are the specification of `HostChange` (PLUG-39 step 1b): one
decision for each accepted request, made in `HostFilter::load()` inside its
transaction from the request's kind (`HostRequestKind`: restored state, a
session the host is opening, or a file chosen in the plug-in) and the
state, so that no adapter makes it again. The install callback receives
what plays (`HostPlayback`, with `playing` set when the filter is the one
already playing) and passes it to `HostProcessor::play()`, which keeps the
processor or replaces it; `load()` then returns the rest: the settings to
apply (the request's own, none to leave them; VST3 fills in its defaults
for restored state without them, VST3-4, and the Audio Unit lays its saved
parameters over the present values; the Audio Unit and LV2 ignore settings
a Convolver state carries, such as a VST3 state's, because their settings
are parameters and ports), the bypass (restored state's, never a
chosen file's; only VST3 applies it). The tail an adapter reports while
no processor plays is the accepted filter's (`HostFilter::filter_tail()`,
found when the filter was accepted, PLUG-39 step 6), in the filter's own
frames at its own rate: the Audio Unit's seconds, VST3's host samples and
the report's Ring-out convert it at that rate, not the host's (PLUG-46). The Latency in those settings goes through each format's
own Latency path, which builds a new processor only when it differs. A
refused request returns nothing and changes none of it; every adapter then
calls `HostFilter::fail_request()` with the processor that plays, which
silences it after a failed first load.

Where the table leaves a case
open, the deciding rule is not to surprise the user (jrp, 12 November
2026); the leading reverbs that play impulse responses have usually been
tuned on such cases already, so compare with them before choosing. Tests of a change check what
must stay unchanged as well as the new value: that the output after a gain
ramp is still the filter's whole response to the uninterrupted input, and
that the latency and tail are the same, as `estimate_keeps_history` and
`settings_keep_history` in `native.host_settings` do.

Keeping history is for changes made while audio plays. Restoring a session
after the host has reset or reactivated the plug-in starts from empty
history, as [initial restoration](#initial-restoration-and-live-replacement)
says, and keeping stale history there would be wrong (jrp, 12 November
2026).

During playback, restored state or a chosen preset whose filter is the one
already playing keeps it (PLUG-38, 13 November 2026), so a preset change or
undo that alters only Gain ramps the Gain and the tail rings on.
`HostFilter::load()` sets `HostPlayback::playing` when the filter is the
accepted one, suits the host (no failure found at activation keeps it
silent) and is equal in every stored value: the filter's identity picks the
candidate, a full comparison of every value (floats by their bits) proves
it, and a file name proves nothing. The adapters (VST3 `setState`, a file
or `.vstpreset` chosen in the window, AU `RestoreState` and a chosen file,
LV2 `restore` and a file chosen through `patch:Set`) then call
`HostProcessor::keep()`, which passes the
filter's gains to `update_gains()` and keeps the processor, unless the
processor met a fault since the last accepted load, which replaces it as
before. Saved state, the filter's name, the log and the notifications
update as for any accepted load. VST3 state that the host marks as a
project being opened (`kProject`) is a session (`HostRequestKind::session`),
which `HostFilter::load()` never counts as playing, so it still replaces the
filter and reopening a project keeps nothing from the session before; a reset or reactivation
clears history as always. LV2 files chosen through `patch:Set` go through
the worker, which keeps the filter only if no newer request has arrived by
then; until PLUG-39 step 1b they always replaced it (jrp aligned LV2 with
the other formats on 13 November 2026). `lv2.adapter`'s `rechosen_files`
checks that the tail of a file chosen again rings on exactly as without it,
and that a file of the same name whose contents changed loads. The PLUG-2 harness's `same-filter-restore`
case checks this in every format, with a reset before the same filter
arrives still giving silence; in `vst3.adapter`,
`same_filter_keeps_history` checks a Gain change against the uninterrupted
tail and `same_filter_as_project_starts_clean` checks that project state
replaces the filter without a reset; `restore_keeps_history_with_estimate`
in `native.host_settings` checks that a kept filter plays the estimate its
restored Gain mode needs. A different Latency in the state still builds a
new processor, as its row says, which the harness's
`same-filter-new-latency` case checks in VST3 (PLUG-39 step 0). Not yet
tried in a host.

## Initial restoration and live replacement

- **Initial restoration** installs the filter directly: when no audio has been
  processed since activation, the adapter constructs the `HostProcessor` with
  the restored configuration. There is no warm-up or crossfade, so output
  matches the filter from the first block (after the reported latency).
- **Live replacement** uses `HostProcessor::replace`: the new filter starts with
  fresh history, warms up for the Latency (none at zero) and crossfades for 5.33 ms, as described under
  [replacement and threads](#replacement-and-threads).
- Pending replacements coalesce to the newest, as `HostProcessor` already does.

## Bypass

Bypass ramps over 1.33 ms to the dry signal and back, with the dry path
delayed by the reported latency so wet and dry line up. Wet processing carries
on during bypass, so switching back does not lose history.

Which counts decide is what plays (`dry_signal_rule()` in `host.hpp`):

- **A filter plays:** its own counts, not the host's. A 2→6 crossover on a
  6-in, 6-out bus has no dry signal; a 2→2 filter on a 6-in, 2-out bus has
  one.
- **No filter plays, none loaded:** the host's counts.
- **A kept filter does not suit the host** (another rate, say): the host's
  counts and the kept filter's must both be equal. A 2→6 crossover kept at
  the wrong rate on a 6-in, 6-out bus has no dry signal (jrp, 7 November
  2026).
- **A load failed and no filter is kept** (`HostFilter::failed()`): no dry
  signal, whatever the counts, one input and one output included (jrp,
  7 November 2026: "After a failed load with no retained filter, Convolver
  should remain silent, including its own bypass and Mix controls. Apply
  this to mono too"). A missing or unreadable file, unreadable state and a
  saved failure record no counts, and nothing shows that the failed filter
  was not a crossover. An untouched instance passes its input, and a failed
  replacement keeps the previous filter with its own rule.

Each prepared filter carries the answer: `HostPlayback` holds the
configuration and whether it has a dry signal, and `HostFilter::activation()`,
the `HostFilter::load()` install callback and `HostProcessor` take it.
`filter_playback()` gives a playing filter its own counts' answer and
`unfiltered_playback()` the answer for no filter; processing, bypass, Mix and
the diagnostics all follow that flag, never the shape of the silent
configuration. A failed first load while the instance plays mutes the
processor (`silence()`), and a muted filter's dry signal ramps out with it.
Status (`Silent: ...`), the log's `Settings:` and the window report's
`mix_note` use the same rule and give its reason. Each filter applies Mix by its own rule (a filter with no dry signal is
fully wet), so when a replacement changes whether there is a dry signal,
each filter's output, dry share included, crossfades with that filter (the
replacement's warm-up, then the crossfade), and neither bypass nor Mix
jumps. A filter that gains a dry signal is heard only after its warm-up of
one block, by which time the delay line holds only fresh input.

Bypass here is the plug-in's own bypass parameter. A host that removes the
effect from its signal path (off, offline, or its own pass-through) routes
around the plug-in, and no adapter can stop that.

- **Equal input and output counts:** the dry signal is input *c* on output *c*.
- **Unequal counts:** every output ramps to silence. No output has an unfiltered counterpart: bypassing a 2→6
  crossover must not send full-range audio to a tweeter, and a 1→2 filter that
  forks a mono input to two outputs cannot tell which output the input belongs
  on. This matches the contract for an empty instance with unequal counts.

`HostProcessor` implements this, so every adapter that drives it through
`set_bypass` follows the rule; the PLUG-2 `bypass` case checks it. An adapter
must not use its format's own pass-through bypass (see the AU design in the archived
[plug-in formats plan](../archive/plans/2026-10-21-plugin-formats.md#au-adapter-design)).

Whether saved state records bypass follows each format's convention. VST3 saves its bypass parameter with the
component state, as the SDK's examples do. In LV2 (the `enabled` port) and
Audio Units (`kAudioUnitProperty_BypassEffect`) the host saves and restores
bypass itself, so those adapters do not record it and ignore the flag in a
state from another format.

## Settings

PLUG-10, from the archived [editor plan](../archive/plans/2026-10-24-plugin-editor.md#settings),
whose choices jrp made on 24 and 25 October 2026
([decisions](../project/decisions.md#identities-and-file-formats)). Every
adapter offers the same settings as host parameters, so they work in generic
host controls, automation and host presets without a window. The defaults
(gain 0 dB, mix 100%, latency 0 frames since PERF-2) leave every output
sample exactly as a processor built without settings at that latency plays it, which `native.host_settings` checks
bit for bit. The first version had Gain and Mix only (jrp asked to keep it
simple, 25 October 2026); Latency followed (PLUG-10, 31 October 2026), then
the plan's Gain modes (12 November 2026); its meters follow later
([TODO PLUG-10](../../TODO.md)).

- **The shared code.** `HostSettings` in
  [`host.hpp`](../../include/convolver/host.hpp), and `settings_words()` for
  the log. `HostProcessor` takes the settings at construction; `set_gain_db()` and
  `set_mix()` are audio-thread calls like `set_bypass()`, and each change
  ramps linearly over 1.33 ms. `set_gain_mode()` is one too, and the gain
  it changes ramps the same way. The ramps (also bypass and mute) and the
  replacement crossfade last a fixed time at every sample rate (PLUG-17):
  64 and 256 frames at 48 kHz, to the nearest frame elsewhere, so 128 and
  512 at 96 kHz and 59 and 235 at 44.1 kHz. A ramp must not click, must
  finish promptly and must end exactly at its target (silence, the dry
  signal, or the new gain or mix), also when it is reversed midway; the end
  may come one frame after the ramp's last (PLUG-19, jrp, 5 November 2026:
  one frame is about 5.7 µs at 176.4 kHz and inaudible), and
  `native.host_settings` allows that. Bypass and mute count whole frames, so
  today they end on the last frame itself (PLUG-18). The allowance is for
  setting transitions only: the reported latency and the alignment between
  channels are exact to the sample. The latency is fixed for a processor's life.
- **Gain** applies to the filtered signal, after convolution and before the
  mix; the dry path stays the untouched input. Range −100 to +24 dB, linear
  in decibels (jrp) for the host and its automation; the window's slider
  alone has a taper, most of its travel between −30 and +12 dB
  (`gain_position()`, PLUG-52). In Manual mode both filters of a replacement play at the
  same gain.
- **Gain mode** (PLUG-10, jrp, 12 November 2026) chooses which gain plays:
  *Manual* plays Gain; *Estimated* and *Clip-safe* play the filter's own
  figure from `estimate_attenuation()`, its noise estimate and its
  no-clipping bound, the figures `convolver --gain-db auto` and `no-clip`
  apply. The bound holds for input within full scale; Clip-safe is not a
  limiter, so floating-point input above full scale can still clip. Gain keeps its value meanwhile and plays again in Manual mode. The
  figures are applied as they are, never clamped to Gain's range, and each
  filter carries its own (`FilterGains` in `HostPlayback`), so in a
  replacement the new filter starts at its own figure and the crossfade
  covers the change. Through the crossfade each filter has its own gain,
  which ramps to that filter's target when the mode or Gain changes: a host
  restoring a project may set the mode just after the filter, and neither
  filter jumps. `HostFilter::load` finds the
  bound for every accepted filter (it only sums taps); the noise estimate,
  which filters noise for ten filter lengths (at least 65536 frames, under
  half a second for a 10-second stereo filter and under two seconds for a
  30-second one on the cloud machine, [offline
  blocks](fft-measurements.md#offline-blocks)), only while an adapter has Estimated chosen
  (`HostFilter::estimating()`). A request holding the accepted filter
  again, equal in every stored value, keeps its bound and any estimate
  already found, and finds an estimate only if Estimated is chosen and there
  is none yet, so a restore that changes only the settings does not filter
  the noise again (PLUG-39 step 1a). Choosing Estimated plays the bound at once.

  PLUG-74: every adapter turns on `HostFilter::find_estimates_later()`, so
  neither a load nor `HostFilter::estimate()` filters noise on the host's
  thread: they take an estimate only from the figure cache (PERF-47), and
  the adapter's `HostEstimator` (`portable/host_estimator.hpp`) searches for
  the one `HostFilter::estimate_wanted()` names, after each accepted load
  and Gain-mode change (`search_estimate()`). LV2's `work()` is the
  exception: it turns the option off while it holds the lock
  (`EstimatesInPlace`) and finds the estimate in place, as before PLUG-74,
  since it already runs off the host's audio and main threads, and hosts
  that run it inside `run()` while free-wheeling make an offline render
  play the same audio whether or not the figure cache had the estimate
  (john's audit found 0.25 against 0.749 at frame 768 without it; the LV2
  adapter test `estimate_offline` renders it cold and warm). It takes no
  claim, so a `restore()` search for the same filter running meanwhile
  repeats the work, costing only time; a search and `finish_estimate()`
  there instead would log the Clip-safe line first, then apply it silently. Each instance has one thread
  for it, started at its first search and idle between searches until the
  instance ends. A newer search, Manual or the instance's end stops a
  running one between blocks of noise. Instances of the same filter share
  one search: the figure cache lets one search at a time claim a filter
  (`detail::claim_estimate()`), and another waits for it and takes the
  cached estimate, so opening a project with several instances of one
  reverb costs one search, not one on each core.

  The search caches the estimate and calls back on its own thread with
  what it found (`HostEstimateFound`: the filter's identity, keyed hash and
  estimate), where the adapter takes its control lock and
  `estimate_ready()` applies it only if the filter it was found for still
  waits for it in Estimated, and logs "The filter's estimate is ready" (or
  why it could not be found). The estimate goes straight to the instance
  that asked (`HostFilter::estimate_found(found)`), and `finish_estimate()`
  takes the last one found from the estimator, never only from the cache,
  which keeps eight filters: others loaded meanwhile could push it out,
  leaving the instance on the bound (the second audit of pull request 395).
  The new figures reach the playing filter through `change_gain_mode()`
  (PLUG-64, the one step every adapter takes for a new Gain mode) or
  `estimate_ready()`, with `HostProcessor::update_gains()`, which ramps to
  them like a Gain change. The engine is not replaced, so it keeps its
  history: a replacement would restart it, silent for the filter's own
  delay (jrp's review found 245 ms of silence with a quarter-second
  delay). VST3 only marks the `Bridge` from the search's thread, without
  `ConvolverChanged`, so no message reaches the controller off the UI
  thread; the window reads it at its next poll.

  Before a processor starts (VST3 `setActive(true)`, the Audio Unit's
  `Initialize`, LV2's `activate()`), `finish_estimate()` waits for the
  search and takes its estimate, so a render never depends on how long the
  search took; restores before the activation have searched meanwhile,
  each instance on its own thread. VST3 also waits when it accepts a
  restore while the host renders offline (`kOffline`), but not for a
  Gain-mode choice, which comes from the controller's thread while
  `process()` renders on, so waiting would only hold the controller. The
  Audio Unit adapter does not read its host's offline signal
  (`kAudioUnitProperty_OfflineRender`) and LV2's `restore()` searches on
  the estimator's thread, so a restore during their render plays the bound
  until the estimate is ready; LV2's `work()` is in place, as above, which
  makes a render exact in hosts that run the worker inside `run()` while
  free-wheeling (Ardour; the specification allows either). The wait holds the adapter's control lock,
  which the callback for an earlier search may be waiting for, so it never
  waits for that callback: it waits only for the search running, and runs
  a search still queued on the waiting thread. The estimator stops at VST3's
  `terminate()` and destructor (once early, and again after the `Bridge`
  detaches, when no controller call can start a search, and before the
  `Bridge` goes, which its callback marks), in the Audio Unit's destructor
  (again after the Latency work ends, which may have started a search)
  and in LV2's `cleanup()`, before anything its callback uses goes.

  The estimate
  is kept only once applied, so a failure leaves nothing shown that does
  not play. A request that brings settings with Estimated chosen (restored
  state, or a chosen preset that holds them) wants its estimate as it loads:
  `HostFilter::load()` follows the settings' Gain mode, and a refused
  request leaves `estimating()` as it was. A filter
  whose noise peaks beyond the largest float has no estimate
  (`estimate_failed`); the noise peak is never above the bound, so such a
  filter has no bound either (FFT-5), and a filter with no finite bound
  plays silence in both modes, which the log and the window say. Without a filter both figures are 0 dB.
  The log line `gain_mode_line()` and `settings_words()` give the mode and
  the gain it plays (`gain_words()`). In the macOS and Windows windows the
  Gain mode menu leads the Gain row (PLUG-50): outside Manual, the Gain
  slider and box are disabled, the box shows the report's `gain_played`
  (`gain_played()`) and the slider sits at its number, `gain_played_db`
  (PLUG-55: never parsed back from the text), and `gain_note` (`gain_note()`) says under the row
  what the box cannot (no filter, no figure, or Clip-safe until
  estimated).
  Not automatable, like Latency, since Estimated may need time before it can
  play.
- **Mix** is the filtered share, 0 to 1, of the output before bypass; the
  rest is the latency-aligned dry signal that bypass uses. Without a dry
  signal ([bypass](#bypass): unequal counts, or a load failed and no filter
  is kept) the mix is always 1, and the window report's `mix_note` field says
  why; the macOS window shows it under Mix and dims the Mix slider
  (PLUG-22). Like bypass, a mix below 1 still
  passes its dry share while a kept filter that does not suit the host plays
  silence, where there is a dry signal.
- **Latency** is the latency `HostProcessor` and its engine are built with
  (the CLI's `--block-size`): one of `host_latencies`, 0 or 64 to 4096 frames
  in powers of two (jrp, 25 October 2026), with 0 first and the default (jrp,
  8 November 2026, PERF-2). A larger value can use somewhat less processor
  time. The settings constructor refuses any other value. Adding 0 first
  left saved projects' latencies unchanged, except some at the old
  default, 256. The VST3 state stores frames, and until PLUG-35 did so
  only when a setting differed from its default: a project saved then with
  every setting at the default has none and opens at the new default, 0,
  while one whose Gain or Mix differed stored 256 and keeps it. Since
  PLUG-35 every VST3 state records its settings, so a later change of
  default leaves saved projects as they were. The Audio Unit saves its
  Latency parameter, which is the menu position, so it now also saves the
  frames under `convolver-latency-frames`; state without them is read
  through the earlier menu, whose 256 always opens at 0 (jrp, 8 November
  2026), even when chosen on purpose. A VST3 test and an AU test restore
  state saved at each earlier choice. At zero latency the first frames after a
  reset are heard, so a bypass, gain or mix change made before them applies
  at once, without the ramp a latency used to hide. It
  cannot change while a processor runs, so a new choice needs a new
  processor, built off the audio thread. It starts with no history, so the
  sound has a gap of the new latency plus the filter's own delay (its peak's
  position: long for a linear-phase filter), and the audio still inside the
  old processor, including a sounding tail, is dropped.
  The host is told the new latency when the new processor is handed over.
  Not automatable. The load meter the archived
  [editor plan](../archive/plans/2026-10-24-plugin-editor.md#settings) puts
  beside it is for a later preview (PLUG-10).
- **Saved form:** each format's own way. The Audio Unit's parameters are
  saved by `AUBase::SaveState` in `ClassInfo` ("data"); VST3 saves them in
  host state version 3, defaults included since PLUG-35 (VST3-4,
  [host state](#host-state)), or version 4 when the Gain mode is not Manual;
  LV2 has control ports, which the host saves (LV2-7). A preset
  `convolver-preset` makes from a filter carries the filter only (one it
  converts from a host's `.vstpreset` keeps that preset's settings), and a
  state moved between
  formats carries the filter but not the settings, as
  [how the plug-ins behave](../use/plugins.md#settings) tells users.
- **Audio Unit (AU-7).** Global parameters Gain (0, decibels), Gain mode
  (1002, an index of `GainMode` with value names Manual, Estimated and
  Clip-safe, flagged `kAudioUnitParameterFlag_NonRealTime`), Mix (1003,
  percent) and Latency (1004, an index into `host_latencies` with value names
  such as "256 frames" and "0 frames (none)", from `latency_value_text()`;
  once initialized, `kAudioUnitProperty_ParameterStringFromValue` adds the
  time at the output rate, "256 frames (5.3 ms)", PLUG-44; also
  non-real-time), listed before Status. Render
  plays the Gain mode parameter's value; a new one wakes the Latency queue
  with its own bit, and the run gives the playing filter (`update_gains()`)
  the estimate from the figure cache or starts its search (PLUG-74), and
  logs the mode. `RestoreState` restores the mode with the other
  parameters, and state saved before the mode existed restores Manual. Values a host writes are stored within their ranges
  (Latency rounded to a whole index), and Render hands Gain and Mix to the
  processor on every call. `Initialize` builds the processor with the chosen
  Latency and posts the latency and tail notifications. A new choice plays
  at once ([decisions](../project/decisions.md#plug-in-behaviour): GarageBand
  starts the unit before it restores a project, so waiting for the next
  `Initialize` would leave a saved Latency unused). Only the unit's own
  serial dispatch queue applies a choice: `SetParameter` may arrive on any
  thread, including an audio thread, a pre-render notification or the input
  callback inside Render, so it stores the value and wakes the queue
  through a `DISPATCH_SOURCE_TYPE_DATA_OR` source (AU-12).
  `dispatch_source_merge_data` is an atomic OR, then a wake-up that may
  make a system call to ask for a thread; the open-source libdispatch
  neither allocates nor takes a user-space lock there, but Apple does not
  document it as real-time safe. Wakes that arrive before the work runs
  coalesce into one run.
  `RestoreState` wakes it too. The work reads the settings once, builds a
  processor with the new block size and hands it to Render through an
  atomic pointer, and the reported latency is the installed processor's.
  Render marks the processor it uses for the length of each call, and the
  work frees the replaced one, without holding the control lock, as soon as
  Render no longer marks it, waiting at most for the rest of one render
  call. Destroying the unit cancels the source and waits for work already
  running. Uninitialized, `kAudioUnitProperty_Latency` simply follows the
  choice. The host is told of the new latency and tail, and the
  log says what changed; the log line is best effort, so running out of
  memory while writing it does not undo or misreport a change. The new
  processor starts the fault count again, so the window's fault totals
  restart, and faults the old one counts in its last moments are not
  reported. If the new processor cannot be built, the old one keeps
  playing, the log says why, the window says the chosen Latency is not in
  use, the status and fault count stay as they were (ROBUST-8), and it is
  not tried again until chosen again.
  `RestoreState` reads the parameter values from "data" itself, with bounds
  checks (AUBase's reader has none), and applies them only when the filter is
  accepted; it ignores Status and unknown IDs, keeps settings the state does
  not hold (a `convolver-preset` preset has an empty "data"), and keeps both
  when "data" is malformed (and logs it). The start line in the log lists the
  settings (`started_line()` with `settings_words()`), Latency included.
- **VST3 (VST3-4).** Parameters Gain (0, a `RangeParameter` in dB, normalized
  linearly over −100 to +24), Mix (1003, 0 to 100 %, tagged `kDryWetMix`
  through `IParameterFunctionName`), Latency (1004, a `StringListParameter`
  "0 frames (none)" to "4096 frames", not automatable; once the processor
  has reported the host's rate, `getParamStringByValue` adds the time,
  "256 frames (5.3 ms)", and `getParamValueByString` takes text starting
  with one of the listed frame counts and refuses any other, PLUG-44) and, after Gain, Gain mode
  (1002, a `StringListParameter` Manual, Estimated, Clip-safe, not
  automatable), listed before Bypass and Status, with the same IDs as the
  Audio Unit. Each has a short title for narrow host controls: Gain, Mode,
  Mix, Lat, Bypass and Status (PLUG-44). A new Gain mode goes from the controller to the processor
  as a call (`chosen_gain_mode()`); the processor plays the new mode at
  once, with the Clip-safe figure until its search finds the estimate
  (PLUG-74), which it then gives the playing filter. The
  processor reads the last
  point of each parameter queue in `process()`, as the SDK examples do, and
  hands Gain and Mix to the processor on every call. A Gain within 1e-5 dB
  of 0 counts as 0 dB, because hosts that keep normalized values as float
  cannot send 0 dB exactly, and a project reset to the default must still
  save as version 1 or 2. A normalized value that is not a number counts as
  the parameter's default, as on the Audio Unit, in the processor's
  conversions and in the controller's `getParamStringByValue`,
  `normalizedParamToPlain` and `plainParamToNormalized` (the SDK's list
  parameters would cast it to an index), and the controller's
  `setParamNormalized` refuses it with `kInvalidArgument`, so no parameter
  holds one (VST3-34). A new Latency also goes to the processor as a
  call (`chosen_latency()`), and the controller then asks the host to
  restart the component (`restartComponent(kLatencyChanged)`). While
  active, the processor builds a processor with the new Latency and plays
  it before the call returns (VST3-16), as the Audio Unit does: REAPER 7.85
  reads `getLatencySamples()` inside `restartComponent` and does not
  deactivate and activate the component, as the SDK describes
  ([trace](../archive/reviews/2026-11-07-vst3-16-reaper-trace.md)), so the
  latency must already be the one that plays. `getLatencySamples()` always
  reports the latency the running processor has (the choice while
  inactive), never a choice that does not play yet. `process()` switches to
  the new processor at its next block, and the control side frees the old
  one only once `process()` has left it. If the new processor cannot be
  built, the old Latency keeps playing, the log says why, the window says the
  choice is not in use yet, the host is not asked to read a latency that
  did not change, and the next activation builds it; while the processor
  is active, a restored state that holds the same Latency builds it at
  once (VST3-32; the Audio Unit already retried it). A host that
  does restart the component builds the processor a second time, at the
  same Latency. A restored state sets all three (a version 1 or 2 state
  resets them to their defaults).
  VST3-26: Latency and Gain mode belong to the processor, so the controller
  always shows the processor's. Gain, Mix and Bypass belong to the host,
  which passes them to the processor in `process()`; the controller takes
  the processor's only when something other than `process()` set them (an
  accepted restore or a chosen preset that holds settings, counted by the
  processor's generation), so reading the processor late never undoes a
  value the host set since. Whenever the latency the processor reports
  differs from the one the controller last told the host, it asks the host
  to read it again. Paired, `setComponentState` shows what the processor
  plays rather than the stream: a state the processor refused (PLUG-48:
  REAPER's preset menu offering a 44.1 kHz preset at 48 kHz while a filter
  plays, jrp's live session of 22 November 2026) leaves the processor's
  settings and Bypass as they were, and the window and host show those, not
  the refused state's; a host that restores the controller first brings
  the processor's own restore after it, which says it changed. The
  controller then shows the earlier values until the host rereads the
  parameters at `kParamValuesChanged`, which hosts do (a host that ignores
  it shows them until its next read). A Latency or Gain mode chosen before
  the halves pair (a proxy that delivers later) goes to the processor when
  they pair, unless the host restored either half after the choice (a
  process-wide count, `Bridges::stamp()`, orders the choice and the
  processor's last accepted restore), and only if the controller made it on
  the connection it pairs through, or while connected to nothing (before its
  first connection or between two) (VST3-28). A choice made on an earlier
  connection whose processor had not answered is dropped: the controller
  cannot tell which processor that was, and a host that moved it to another
  instance must not pass the choice on. Reconnecting to the same processor
  drops it too; the controller then shows that processor's own setting. Likewise Gain, Mix or Bypass the
  host set before they pair stays if it is newer than that restore and
  than any state the host restored to the controller since, and if it was
  set on the same connection or on none (VST3-29), and
  the filter name a restored controller state gives reaches the processor
  when they pair. The host's
  order and when it delivers messages do not matter: `convolver_vst3_sync`
  checks every sequence of one and two host actions, window choices and
  deliveries in every order, 300 longer ones and the sequences jrp's
  earlier reviews found, with an Ardour-like and an Audacity-like host
  too, against the same expectations (VST3-25), and `vst3.adapter` keeps the earlier cases
  (`edits_survive_late_answer`, `edits_before_controller_first_restore`,
  `accepted_restore_beats_queued_edit`,
  `accepted_restore_follows_its_own_request`).

## Channel layouts

What a channel means (discrete or a named speaker layout), how presets with no
recorded layout are treated, and the per-format details are in
[plug-in identities and channel layouts](plugin-identities.md#channel-layouts).
In short: version-1 presets load on any arrangement with matching channel
counts, and a recorded output speaker mask only ever gives a notice.

**A wider input bus (PLUG-23).** A filter is accepted when the host gives it
exactly its outputs and at least its inputs (`HostFilter::mismatch()`); fewer
inputs, or other outputs, are refused as before. Some hosts give a plug-in as
many inputs as outputs (REAPER does for VST3, found in HOST-1), so a 2→6
crossover would otherwise never load there. The filter reads the first
inputs and the rest are ignored: `HostProcessor` takes the host's input count
and widens each prepared engine to it, the extra inputs routed nowhere, so
every filter in one processor shares the input stride while keeping its own
dry rule ([bypass](#bypass) for the fade when that changes). The user is told: Status ends "plug-in inputs 3-6 ignored.",
the loaded, playing and started log lines add "Inputs 3–6 are ignored: the
filter reads inputs 1–2.", and the window report's Channels fact says the
same (`ignored_inputs()`). Outputs stay exact: a filter is never cut to
other outputs, and only a one-channel filter is spread over them (below).

A file chosen in a window or through LV2's file parameter follows the same
rule (PLUG-33, `read_filter_file()`): it is read for the host's exact
counts first and, when nothing matches, for the most inputs below the
host's among the single filter or list entries for the host's rate and
outputs, so a list's exact entry always wins
([decision](../project/decisions.md#plug-in-behaviour)). When a list's
entry is chosen that way, `HostRequest::wider_entry` names it, and the
loaded line in the Convolver log (LV2 writes it as a note to the host's
log, for that choice only) and the window's reference lines ("List
entry") say which entry plays and why. The command line still selects
exactly, apart from the default routings below. With no filter, Status names choosing a filter before a
preset where the plug-in has a window (`host_status()`'s `chooses_files`).

<a id="default-routings"></a>**Default routings (EDGE-3).** A filter with
one input and one output (a mono IR file, a `.cfg` or a preset) suits a
host with two or more inputs and as many outputs, and plays on each
channel: `plays_on_each_channel()` answers `mismatch()`, and `played_on()`
hands the accepted filter, shared, with `HostPlayback::each_channel` set to
the host's channel count when it is installed and at each activation. Only
a processor built or replaced from that playback lays it out with
`on_each_channel()`, for as long as it builds the engine, so a filter that
plays on with its history (`keep()`) is never copied (PLUG-57). The
accepted filter, the saved state and a preset keep its own 1/1 and the
layout follows the host's channels at every start. A
four-channel IR file is read as true stereo (paths LL, LR, RL, RR) only
when it is read for a host known to give two inputs and two outputs
(`read_filter_file()` tries the known counts first, then the file as it
is), because that is decided in `load_configuration()`'s
`default_layout()`; the state then holds the 2→2 configuration. A
four-channel file read before any start and started on two channels is
refused with a note to choose it again. A WAV, RF64, BW64 or Wave64 file
marked as Ambisonic B-format is never taken for true stereo (each reader
reads the marking once as it opens the file, `SoundFile::ambisonic()`;
libsndfile does not open BW64): chosen while the host's two
channels are known, it is refused with that reason, never kept as four
channels by PLUG-42 or while the host is stopped (`read_for()`; jrp's
review of `a404df0`). In a list, an entry that matches
exactly wins over the whole list, then the wider-input rule above, and
only then a default routing. Status ends `on each of 2 channels` or `true
stereo`, the log's loaded line adds a sentence (`routing_sentence()`), and
the report's Routing fact a note
([configuration](../use/configuration.md#mono-and-true-stereo-ir-files),
[decision](../project/decisions.md#configuration-and-sound)). Tests:
`native.io` (the reading rule), `native.filter_file` `default_layouts`,
`vst3.adapter` `kept_until_channels_fit`, `au.editor` `files`, the Ardour
`mono` VST3 case on a Flexible-I/O track.

## Replacement and threads

The host owns the audio device, clock and scheduling. Each callback supplies
the next section of one continuous stream; convolution history persists
between callbacks, and processing produces one output frame per input frame
after the reported latency (the Latency setting, none by default,
independent of the callback length).

Filters are prepared outside processing and transferred through a bounded
single-producer/single-consumer pointer handoff. A filter that arrives before
any audio has been processed since activation or reset is installed directly.
A reset (the host stopping and restarting processing) also ends a crossfade in
progress, so the newer filter plays at once, and a filter published before
audio resumes is installed directly. Later, a replacement runs for the Latency
to fill its latency (at once at zero latency), then crossfades for 5.33 ms. It starts with fresh
input history: it cannot reconstruct audio that preceded loading. Old engines
are retired on the control thread. Pending changes coalesce to the newest; at
most one transition and one pending replacement are retained, with up to two
old engines awaiting control-thread collection after the last publication.
Collection does not hold up pending changes. The tail is available before
activation and includes routed delays; it can grow conservatively during a
session, and once the host stops the plug-in it is the accepted filter's
again (PLUG-39 step 6).

A new Latency while active (the Audio Unit, VST3 since VST3-16, and LV2
with a worker) replaces the whole `HostProcessor`, since the block size
changes. The adapter builds it on the control side and offers it to the
audio side. Who may use a processor, and when a replaced one is freed
(PLUG-39 step 3, under jrp's conditions of 13 November 2026), as VST3 and
the Audio Unit do it with `PlayingProcessor` ([implementation map](architecture.md)):

- **Every reader is protected.** Each call that uses the processor (VST3's
  `process()` and `setProcessing()`, which hosts may call on different
  threads; the Audio Unit's `Render`) adopts the newest offer when it
  starts and holds it, in a mark of its own, until it returns. Nothing a
  mark holds is freed.
- **The host reads the new latency at once.** The offered processor's
  latency is what `getLatencySamples()` and `GetLatency()` report from the
  moment it is offered, and the host is told in the same control call
  (VST3's restart request, the Audio Unit's property listeners), without
  waiting for a callback, which may never come while playback is stopped
  (VST3-16). The latency and tail the host reads without the control lock
  are atomics that one function in each adapter writes
  (`publish_timing()`, PLUG-39 step 6), after every change to the newest
  processor or the accepted filter: the newest processor's, or with none
  the accepted filter's tail and, for the latency, the chosen Latency.
- **The control side never waits for audio.** A replaced processor is freed
  by a later control call once no reader holds it; at most one replaced
  processor per reader stays alive, an offer no reader adopted is freed by
  the next control call, and deactivation and destruction, when no audio
  runs, free them all. No thread of its own does this. The number kept is
  bounded, not the time: a replaced processor and its buffers stay
  allocated until the next control call after its reader lets go, however
  long the host makes none. The Audio Unit's uninitialization offers no
  processor rather than wait under its control lock, since a render's
  input callback may call into the unit.

LV2 offers the new processor through `handover`, which `run()` takes
at the start of its next call; the control side frees the one before
(`retiring`) once `run()` has taken it (`retire()`), and the latency port
follows from that call, since only `run()` writes ports.

The new processor starts with empty history, so the sound pauses for the
new latency and a sounding tail is cut.

`Convolver::host`, a target of the source build that is not installed
(REL-19), gives the adapters `HostProcessor`, `HostFilter` and the state codec
without SDK or file I/O dependencies ([`host.hpp`](../../include/convolver/host.hpp)).
Prepare, replace, collect and destruct calls must be serialized on the control
side. `replace()` is `prepare()` then `publish()`; an adapter that must check
for a newer request between the two (LV2's worker) calls them itself, and an
install callback that throws `SupersededRequest` abandons the request without
changing the filter or its failure record. `HostFilter::load` builds
everything it keeps (messages, the speaker notice) before calling the install
callback, and nothing after the callback publishes can throw, so running out
of memory never leaves one filter playing while another is saved. One audio
thread calls process, reset, set_bypass and mute, and reads publications.
`mute()` is for a failure found on the audio thread itself: the current
filter ramps to silence over 1.33 ms, 64 frames at 48 kHz (or is silent from
the first sample, when nothing has been processed since construction or reset) and stays silent until a filter published after
the mute takes over and fades in from silence; one published before it but
not yet picked up by the audio thread is silenced too. The muted filter's dry
signal (bypass, Mix's dry share) ramps out and stays silent with it, with
one output too: a failed first load has no dry signal ([bypass](#bypass)).
`silence()` is the same mute requested from the control side, for
a failed first load (ROBUST-3): it only stores the number of publications so
far, so it cannot allocate or fail, and the audio thread mutes at the start of
its next `process()`, before adopting a pending filter. Destruction and
reconfiguration require audio to have stopped. Reset invalidates history
without allocation; its work scales with the block buffers and delay lines,
not the filter length, and until the history refills the callbacks after it
cost less than warmed ones (PERF-5). Filter preparation can
be expensive; neither FFT planning nor file access belongs in a callback.

Finite input outside float range, and nonfinite input, become silence.
Nonfinite output is contained and history reset. The processing boundary does
not throw; invalid dimensions return failure without advancing audio.

### Calls that do not wait for the control lock

PLUG-30 (finished with PLUG-39 step 4a, 20 November 2026): a call that must
not wait and finds the control lock taken must not skip an update and leave
what the host sees stale. These are every such call; the audio threads
(`process()`, `Render`, `run()`) never take the lock at all.

| Adapter | Call | When the lock is taken | Why nothing stays stale | Held-lock test |
| --- | --- | --- | --- | --- |
| Audio Unit | The window's report (`Effect::report()`) | Builds the report from the diagnostics snapshot (`Effect::Diagnostics`) and the Gain, Mix, bypass, latency and tail set now | Every control change publishes a new snapshot (`update_status()`) before it lets the lock go, and then notifies the window, so the snapshot describes what plays until the holder finishes. Faults `Render` counted since, and a chosen filter the host now renders (PLUG-27's confirmation), are taken in at the next read that gets the lock | `au.adapter` `report_while_latency_work_holds_lock`; `au.editor` (a restore held open) |
| Audio Unit | Bypass (`SetProperty`, `kAudioUnitProperty_BypassEffect`) | Stores the bypass and wakes the Latency work's serial queue (AU-16) | The queue may wait for the lock, then updates the Status and tells the host and the window, even when the Status value is the same both ways (no filter, a failed load, a filter waiting for the host's settings), since the text and the report still change (AU-20); a run woken for something else updates it only when its value is still the one for the other bypass setting (`host_status_value()`), and a Fault that Render wrote stays until the host reads the Status text, which explains it | `au.adapter` `bypass_while_latency_work_holds_lock`, `bypass_without_filter_while_latency_work_holds_lock` |
| LV2 | `deactivate()` | Waits for it: it only tries first, to signal the tests (`WorkerPoint::deactivate_waits`) | It does not skip | `lv2.adapter` `held_while_deactivating` |
| VST3 | None | Every call takes its locks without trying | | |

The Audio Unit's snapshot holds a copy of the `HostFilter`, which shares the
accepted filter rather than copying it (`HostFilter::filter()`, immutable,
jrp, 20 November 2026), with the host's configuration and whether the log
can be written. It is built whole and published under its own lock, which
guards only the pointer, so a reader never mixes two generations; `Render`
never touches it, so its allocation, its lock and the release of an old
filter stay off the audio thread (`native.host` `shared_filter`: an old
copy stays whole after a replacement, and a refused replacement keeps the
accepted filter).

## Host state

Host state uses a bounded, versioned little-endian format (64 MiB encoded
limit), validated transactionally: state that fails decoding or its own checks
leaves the previous state untouched. What is accepted can always be saved
(PLUG-12): `HostFilter::load` refuses a filter whose encoding would exceed the
limit (`encoded_host_state_size()`, counted without encoding) before
installing it, with `HostFailureKind::too_large` and the explanation from
`host_state_size_error()`, as a check on the state itself. The check keeps
`host_settings_room_bytes` (4096 bytes since PLUG-70; 256 before) free for
the settings and a preset name, counted in place of any the state holds, so
a filter accepted at default settings still saves after a VST3 setting
changes or a name is added, including settings added later (PLUG-13). The
room grew before 1.0 (jrp, 27 November 2026) so later metadata fits without
refusing filters 1.0 accepts; a state saved by an earlier version (up to 0.19) whose
filter is within 960 samples of the old limit decodes but is refused on
load like any filter too large to save, with the same explanation, never
cut short; every format and `convolver-preset` use the same limit, and
restored state is checked the same way. A `.cfg` may hold
64 Mi samples, four times what the state holds, so a filter file can be too
large; `convolver-preset` makes the same check. Checks against the host's configuration
follow [loading and failure](#loading-and-failure), including state restored
before the host is configured. Version 1 records the filter (or an empty
instance) and the bypass setting (used by VST3 only; see [bypass](#bypass)); version 2 is written only for a saved failed
initial load, with no filter. Version 4 (PLUG-10, VST3 only) is version 3
followed by the Gain mode as one little-endian word (1 Estimated, 2
Clip-safe; decoding refuses any other value), written only when the mode is
not Manual (jrp, 12 November 2026), so other states keep their version and
older builds refuse only a state they could not play as saved, with the
"newer" message. Version 3 (VST3-4, VST3 only) adds the
[settings](#settings) after the flags (Gain in dB and Mix from 0 to 1, each
an IEEE double as two little-endian words, then Latency in frames) and is
written whenever the state holds settings, with a filter or a failed-load
marker as before. The VST3 component state always holds them, defaults
included (PLUG-35; jrp, 12 November 2026), so a preset a host saved at the
defaults is told apart from a filter-only one and resets them when chosen
in the window; before PLUG-35 they were written only when one differed
from its default; decoding checks each setting's range and
refuses the state otherwise. The Audio Unit and LV2 read a version 3 state's
filter and ignore its settings, which they save their own way: the Audio
Unit as parameter values under `data`, plus its Latency in frames under
`convolver-latency-frames` since PERF-2 (see [settings](#settings)).
Version 5 (PLUG-53, every format; jrp, 26 November 2026: one shared
field) adds a preset's own name (`HostState::preset_name`), which heads the
[window](../use/plugins.md#convolvers-window) in place of the filter file's
name. After the flags come the name's length in bytes and its UTF-8 bytes
packed into little-endian words, zero-padded, then, when flag 8 is set, the
settings as in version 3 followed by the Gain mode word (0 for Manual), then
the filter. It is written only when the state holds a name, and only a
`storable_preset_name()`: at most `max_preset_name_bytes` (200) bytes of
valid UTF-8 without control characters, which `host_settings_room_bytes`
keeps room for beside the settings; decoding refuses an empty name, a bad
one, nonzero padding and flag 8 before version 5. `HostFilter::load` sets
`HostRequest::preset` from it, so a filter file chosen directly, whose
state has none, clears the name of the preset before it, and every adapter
saves it again with the filter. Older builds refuse a version-5 state with
the "newer" message: jrp accepted that previews up to 0.18.29 cannot open
projects and presets saved with a name, to be said plainly in the release
notes. Decoders read all five; encoders write version 1 whenever nothing
new is recorded (every state without settings or a name: the Audio Unit's
and LV2's states for a filter that came from no named preset), so older
builds still read those. Every published preview reads version 3, since VST3-4 came
before the first, except that 0.12.12, from before PERF-2 added Latency 0,
refuses a version-3 state with Latency 0 as damaged: since PLUG-35 that is
every VST3 project and preset saved at the defaults. Version 4 (a Gain mode
other than Manual) is read only by builds with the Gain mode. How each format stores it (the
`.vstpreset` chunk, the AU `convolver-state` key, the LV2 state property) is in
[preset files](#preset-files).

A save (VST3 `getState`, the Audio Unit's `SaveState`, LV2 `save`) takes a
`HostSnapshot` under the control lock (PERF-26; jrp's review of `7bd024f`):
the accepted filter shared, not copied (`HostFilter::snapshot()`), with the
preset name, the failed-load marker and, in VST3, the bypass and settings,
in the Audio Unit the filter file's name and in LV2 the path of the file it
came from, all from that one moment. It encodes and writes the state after
releasing the lock, through `encode_host_state(HostStateView)`, which
borrows the snapshot's parts, so a long filter's save neither copies the
filter nor holds up the controls, a restore or LV2's worker, and a restore
that lands meanwhile reaches only the next save (`vst3.adapter`,
`lv2.adapter` and `au.adapter` `save_overlapping_replacement`, through
`saving_for_tests` and LV2's `WorkerPoint::saving`). The Audio
Unit's settings are its parameters, which `AUBase::SaveState` writes
itself, outside the snapshot, as before.

## Preset files

`convolver-preset` ([user guide](../use/presets.md)) writes the same **encoded
host state** into every format: the magic `CNVS`, state version 5 with the
preset's own name (PLUG-53: `--name`, or the first AU or LV2 output's file
name, or with only `.vstpreset` outputs the first one's), or version 1 for
a name the state cannot hold, then the sample rate, channel counts, output channel mask, delays, and every filter
path's coefficients, routes and gains, as the plug-ins save it in a host's
project. The containers around it are fixed, so presets made today keep
loading. These names are permanent, like the
[plug-in identities](plugin-identities.md):

- **VST3 (`.vstpreset`):** Steinberg's documented preset file: the header
  `VST3`, format version 1, the processor class ID, then one component-state
  chunk (`Comp`) holding a 32-bit little-endian length and the encoded state.
  After it, a controller-state chunk (`Cont`) names the filter for the window
  as a file chosen there does (PLUG-31): the controller state `CVCS` version
  1 ([PLUG-22](#status-and-log), `portable/vst3_controller_state.hpp`) with the
  name of the file the preset was made from, with its extension and without
  its folder, and the filter's identity. Converting a `.vstpreset` keeps its
  name only while that identity still matches the filter; a name that is
  empty, not UTF-8 or longer than the plug-in reads is left out, never
  refused. The tool's writer needs no SDK; `vst3.preset_format` checks that
  its output, with and without the name, is byte-identical to the SDK's
  `PresetFile::savePreset`.
- **AU (`.aupreset`):** an XML property list, the dictionary that Audio Units
  save as `ClassInfo`. It has Apple's standard keys (`version` 0, `type`,
  `subtype` and `manufacturer` as the numbers of `aufx`, `Conv` and `CONV`,
  `name`) and an empty `data`, so the AudioUnitSDK's own checks accept it
  and the unit's [settings](#settings) keep their values when it is loaded.
  The encoded state is under the key `convolver-state`. The name of the file
  the filter came from, as the window's file details show it, is under
  `convolver-filter-name` (PLUG-53), which the unit's own `ClassInfo` also
  saves; it is left out when empty or not storable. Without it, a state
  with no preset name of its own (saved before PLUG-53) takes the filter's
  name from `name` unless that is AUBase's "Untitled"; a state with its own
  name has no filter file name (VST3-31: `name` is then a name the host
  gave the preset).
- **LV2 (`NAME.preset.lv2/`):** a preset bundle with `manifest.ttl` and
  `preset.ttl`. The preset is a `pset:Preset` that applies to the plug-in URI
  for the filter's channel counts, with one state property, the LV2 base URI
  followed by `state`, holding the encoded state as an `atom:Chunk` (base64 in
  Turtle). No file path is stored, so the preset does not depend on where the
  filter files were.

The AU key, the LV2 state property and the bare state inside them (without the
VST3 length prefix) were decided by jrp
([decisions log](../project/decisions.md#identities-and-file-formats)).

Compatibility rules for presets:

- Presets hold host state version 1, or version 5 when they carry the
  preset's own name, which records channel counts and an
  advisory output channel mask but no layout. Plug-ins must load every
  version-1 preset whose rate and counts fit; a stricter layout check never
  rejects one ([old presets](plugin-identities.md#presets-without-a-layout)).
- Host state version 2 only marks a plug-in saved after its first filter failed
  to load; it holds no filter. The tool never writes it and refuses to convert a
  `.vstpreset` that holds it. VST3 builds before VST3-7 reject version 2, so a
  fresh instance there plays as an empty instance.
- If a later version records more (for example an input layout), the tool must
  keep writing the oldest version that holds what a preset needs, so older
  plug-in builds can still read every preset they could play as made. The
  preset's own name is the exception jrp accepted (PLUG-53, 26 November
  2026): every named preset is version 5, so bundled and user-made presets
  head the window the same way, and previews up to 0.18.29 refuse them.
- Presets without a `Cont` chunk, as the older `convolver-vst3-preset` wrote
  them until IO-18 removed it, still load, without a filter name.

## Format notes

How each adapter meets the contract where its format differs:

- **VST3:** reports a rejected `setState` as `kResultFalse`; the Status
  parameter and the Convolver log ([status and log](#status-and-log)) say why,
  including for a mismatch found at activation, and the log carries the
  [speaker check](../use/plugins.md#channels) notice (`HostFilter::notice()`). The adapter handles
  variable block sizes, in-place host buffers, calls with no audio and inactive
  channels; it reads input buffers even when the host flags them silent, and
  flags an output channel silent exactly when a call left it all zero (VST3-10):
  a tail that is still sounding is never flagged, while the latency, an output
  no path feeds and drained output are. `getTailSamples()` is the filter's
  tail plus the latency (`getLatencySamples()`), measured at the output as Steinberg's tail
  test does. 32- and 64-bit host buffers both use the float engine.
- **Audio Unit:** latency and tail are reported in seconds, with a property
  change notification when either changes, except while the host
  uninitializes the unit, which reads them again when it next asks; the
  tail stays at the longest filter used until the host uninitializes the
  unit, and is then the accepted filter's; the latency uninitialized is the
  chosen Latency, read from its parameter, and whichever call publishes a
  new one (the Latency queue, a chosen file or a restore) notifies. Bypass is
  `kAudioUnitProperty_BypassEffect` driving `HostProcessor::set_bypass`, not
  `AUEffectBase`'s pass-through, and is saved in `ClassInfo`. The output is
  marked silent only once the tail has decayed. Rejections are returned to the
  host as errors, shown by the Status parameter and written to the Convolver
  log and the unified log.
- **VST3 window on macOS (VST3-11).** Reached as the SDK documents an
  editor without VSTGUI: `Controller::createView(ViewType::kEditor)` returns
  the SDK's `EditorView` for `kPlatformTypeNSView` only, sized to the shared
  view's fitted content (`getSize`), which `attached()` adds to the host's
  view and `removed()` takes away. It is not resizable by the user
  (`canResize` stays false); when its content changes height (the folded
  lines) it asks the host with `IPlugFrame::resizeView`, and the host's
  `onSize` keeps it in place. Everything goes through the edit controller on
  the host's main thread: settings as `beginEdit`, `performEdit` and
  `endEdit` (a drag is one edit; a click, a typed value, a menu choice or a
  reset is one each), with `setParamNormalized` for the controller's own
  copy; the Bypass checkbox edits the bypass parameter (1000) the same way,
  one edit per click; the report as `Controller::report()`, which reads the
  processor; a chosen or dropped file as `Controller::load_file()`. A bypass
  change through the controller's `setParamNormalized` also tells the
  processor (`show_bypass()`), so its status and report say Bypassed before
  the audio thread sees the change (PLUG-22). The controller state (`getState`, `setState`) holds
  the accepted file's name and its filter's identity (`"CVCS"`, version 1,
  each string as a little-endian 32-bit length and UTF-8); an empty or
  unknown stream reads as no name, so projects saved before PLUG-22, whose
  controller state is empty, still open. `setState` passes the name and
  identity to the processor (`offer_name()`), and the processor offers
  them to its `HostFilter` (`HostFilter::offer_name()`); `getState` saves the report's
  `filter saved name` for the playing filter; when that filter has none,
  the pair `setState` brought only if its identity is the playing
  filter's (VST3-31), and with no filter playing, that pair. The view's blocks hold a
  shared link that the editor clears in its destructor, so nothing the view
  does afterwards (a timer, a closing file dialog) reaches a controller the
  host has released. The view does not touch the processor or the audio
  thread.
- **VST3 window on Windows (VST3-12).** `createView(ViewType::kEditor)`
  returns an `EditorView` for `kPlatformTypeHWND` only (`editor_win.cpp`):
  `attached()` makes a child window of the host's window with Windows'
  standard controls, and `removed()` destroys it. The view stays alive
  between the two, and a second `attached()` makes every control afresh. It
  talks to the edit controller on the host's main thread exactly as the
  macOS window does (the same edits, one per click, key, typed value or
  Latency choice, a trackbar drag as one edit; the same report, load and
  bypass messages), and does not touch the processor or the audio thread.
  Sizes follow the window's DPI (`WM_DPICHANGED_AFTERPARENT`); before
  attachment, `IPlugViewContentScaleSupport` gives the host's scale; a
  taller content asks the host with `IPlugFrame::resizeView`, as on macOS.
  Unlike the macOS sheet, the file dialog (`IFileOpenDialog`) and the
  Choose Filter menu (`TrackPopupMenu`) are Windows' own modal loops, owned by the
  host's top-level window, as Windows applications expect. During either
  loop the host may remove and release the view, so the view holds a
  reference to itself until the loop returns, `removed()` closes an open
  dialog, and a file chosen for a window that has gone is not loaded. If
  the host destroys its own window before calling `removed()`, the view
  notices the window has gone and `removed()` only forgets it. The window
  polls the report twice a second while it is on screen and the filter's
  files once a second while it is open, with window timers on the host's
  thread.
- **Audio Unit window in a host** (the evidence is in TODO AU-6); the VST3
  window (VST3-11) follows these too:
  - The host may run the plug-in in a process of its own (GarageBand:
    `AUHostingServiceXPC_arrow`). The view's window is then an
    `NSServiceViewControllerWindow` of that process, shown at a floating
    level, while the host keeps the keyboard. A host that runs the
    plug-in in its own process puts the view in one of its own windows
    instead, and the sheet then attaches there.
  - A file dialog is a sheet on the window that holds the view, begun
    asynchronously with `beginSheetModalForWindow:completionHandler:` as
    Apple's
    [file panel guide](https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/UsingtheOpenandSavePanels/UsingtheOpenandSavePanels.html)
    describes. There is no separate dialog window: in an out-of-process
    host it cannot become key. Never `runModal`.
  - An accepted file changes the saved state without a parameter change,
    and a `ClassInfo` notification alone does not make GarageBand mark
    the project changed. The file therefore also becomes the present
    preset (a user preset, number -1, named after the file), with a
    `PresentPreset` notification. A refused file keeps the filter, so it
    sends only `ClassInfo`.
  - Each change from the window is bracketed by the parameter's change
    gestures (`kAudioUnitEvent_BeginParameterChangeGesture` and `End…`,
    sent with `AUEventListenerNotify`), as Apple's generic view does: a
    slider drag from mouse down to mouse up, and a typed value, a menu
    choice or a reset as one each (PLUG-52), so a host's automation and
    undo record one change, as VST3's edits do.
  - Choose Filter stays a pull-down (`NSPopUpButton`), not an
    `NSComboButton` with Choose File… as its main part: the window
    highlights the button that carries out a fix with `bezelColor`, which
    `NSComboButton` does not have (PLUG-52; the Windows split button can be
    drawn as the default button, so it has both).
- **LV2:** ports are fixed, so each channel layout is its own plug-in
  ([identities](plugin-identities.md#channel-layouts)). Bypass is the `enabled`
  port, which the host saves, so the adapter's state does not record bypass and
  ignores a bypass flag in another format's preset. Activation is LV2's reset:
  it starts a fresh processor with the current filter, no history and its exact
  tail. `run()` and the worker's answers (`work_response()`) do not allocate,
  lock or do I/O; a file choice on the `filter` parameter reaches the worker as
  a request numbered by generation, which is also its failure record's request
  number. When `run()` itself refuses a file (the host has no worker, or
  cannot queue the request), an instance with no accepted filter mutes at
  once (`HostProcessor::mute()`), so a failed first load is silent without
  waiting for a control call; an accepted filter keeps playing. The worker's
  `work()`, state restore and save, activation and
  cleanup share a lock that `run()` never takes, and the plug-in does not
  declare `state:threadSafeRestore`. `work()` stops before recording anything
  for a request a newer one has overtaken: after reading the file, and again
  after `HostProcessor::prepare()`, just before `publish()`. Restoring a map
  with neither of the adapter's properties resets it to an empty instance, as
  the LV2 state specification requires ("This allows the host to reset the
  plugin state with an empty map"): no filter, no failure, no path, and loads
  still outstanding are superseded. The status port
  follows the newest request (3, Loading, while it is outstanding), and
  otherwise the record's status in the order Status gives it in the other
  formats: the control side maps `host_status_value()` to the port's values
  (`port_status()` in `plugin.hpp`; Active, Waiting and Bypassed are all 0,
  running; PLUG-39 step 4), and `run()` lays its own Fault and a refusal it
  made itself over that, Load failed first; the path
  it reports and saves follows the installed filter. The design, with the full
  lifecycle, is in the archived formats plan's
  [worker and state lifecycle](../archive/plans/2026-10-21-plugin-formats.md#lv2-adapter-design).
  Settings (LV2-7) are the control inputs `gain` (dB), `mix` (percent) and
  `latency_frames` (an enumeration of `host_latencies`, `pprops:notAutomatic`
  and `pprops:expensive`; another value counts as the nearest), after the
  atom ports so the earlier ports keep their numbers, then `gain_mode`
  (PLUG-10, an enumeration of the three modes, also not automatic and
  expensive). `run()` hands Gain, Mix and the Gain mode to the processor on
  every call. A new Gain mode is a worker request, one queued at a time as
  for Latency: `work()` finds the estimate and gives it to the playing
  filter (`update_gains()`); `activate()` finds it for a mode chosen while inactive, so a
  restored session plays the estimate from its first frame. Without a
  worker, Estimated plays the bound until the next activation. A new Latency while active is a worker
  request: `work()` builds a processor with it under the lock, as activation
  does, and offers it through an atomic pointer that `run()` takes at the
  start of its next call; the control side keeps the processor `run()` may
  still play until then, and frees it at its next call after. The `latency`
  output always reports the processor that plays. If the new one cannot be
  built, the old one keeps playing and the log says why; without a worker,
  the next activation builds it. The audio inside the old processor,
  including a sounding tail, is dropped, as in the other formats. One
  Latency request is queued at a time; `work()` builds the newest Latency,
  and one chosen after it read the control is asked for once it finishes.
  A processor taken after `run()` refused a first load (the worker queue
  full) is muted as the one it replaces was.
  A narrow fix for one host family: JUCE's LV2 host (pluginval, and hosts
  built on it) ignores `pprops:notAutomatic`, destroys the instance at every
  `prepareToPlay`, and still runs work it queued after `cleanup()`, on the
  freed instance. `cleanup()` therefore waits, up to 100 ms, for every
  request that `run()` queues one at a time by itself to be worked: a
  Latency, a Gain mode and a fault report (`wait_for_queued_requests` in
  `plugins/lv2/plugin.cpp`). It is narrow because file requests are not
  waited for: they follow a person's choice, several can be queued, and
  waiting for them would slow every host that drops queued work on
  closing, as the specification lets it; a file request JUCE works after
  `cleanup()`, or its answer delivered late, still reaches the freed
  instance. It is not a guarantee either: JUCE runs one worker thread for
  every LV2 instance in the process, so another plug-in's work lasting
  over 100 ms can still leave a queued request to run after the instance
  is freed, and a request queued before a deactivation and activation of
  the same instance is no longer waited for, because `activate()` clears
  the flags for hosts that dropped their queue.
  It needs `urid:map`, uses `work:schedule` to load files, logs through
  `log:log` (otherwise standard error), and reads `bufsz:maxBlockLength` from
  `options` when given; without it, or for longer `run()` calls, it processes in
  pieces of at most 4096 frames with the same output. It reads filter files
  with libsndfile, whose error reporting is shared by the whole process, so
  Convolver opens one file at a time; that lock does not cover the host or
  other plug-ins using libsndfile at the same moment, which at worst report
  each other's error message.
