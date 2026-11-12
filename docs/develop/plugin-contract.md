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

- An instance with no filter passes input to output only when the channel
  counts are equal; otherwise it plays silence.
- A filter that cannot be used as an instance's first filter makes it play
  **silence**, never unfiltered audio. A rejected replacement leaves the
  working filter playing.
- State is checked in two stages: the state itself at once, and the host's
  sample rate and channels as soon as the host has settled them.
- Every failure keeps a structured reason on the control side, for the host's
  log, LV2's status port and, later, a status display in the other formats.
- A filter plays on exactly its outputs and at least its inputs: on a wider
  input bus it reads the first inputs and ignores the rest, and says so.
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
  ([scope of each change](#scope-of-each-change)).

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

An adapter instance is in one of three situations, and each has one defined
behaviour:

| Situation | Audio | Indication | VST3 (since VST3-7, 22 October 2026) |
| --- | --- | --- | --- |
| **Empty instance:** no filter was ever loaded | Equal input and output counts: identity (input *c* to output *c*). Unequal counts: silence, because an unfiltered 2→6 crossover would send full-range audio to whatever is on outputs 1 and 2 | "No filter" status where the format has one | As specified |
| **Failed initial load:** the first filter (a session restore or the first preset) cannot be used: missing file without an embedded copy, wrong rate or channel counts, corrupt state | Silence, never a silent fallback to identity: a room correction or crossover that quietly plays unfiltered audio is worse than an obvious failure | Error returned to the host where the format allows it, status "load failed", and the explanatory message in the host's log | Silence; `setState` returns `kResultFalse`. A mismatch found at activation plays silence; until DIAG-1 it did so with no message (accepted until VST3-3). Since DIAG-1 the Status parameter says why and the Convolver log explains (see [status and log](#status-and-log)) |
| **Failed replacement:** a working filter is running and a new one is rejected | The working filter keeps running, unchanged | Error returned to the host, status "load failed", message in the log; saved state still describes the working filter | As specified, active or inactive once the plug-in has been activated at the current rate and buses and has played audio with them; `setState` returns `kResultFalse`. Before that, restored state that does not suit is kept, silent (see the VST3 note under [state that arrives before the host is configured](#loading-and-failure)) |

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
  activation. A filter file chosen in the window is checked
  as soon as the plug-in has been activated. A preset the host loads into a
  new instance before it has played audio, or just after the host changed
  its rate or buses and before it plays again, is therefore kept silent
  rather than refused; VST3 gives no other way to tell it from a restored project
  when the host does not say. `processor.cpp`'s `load_check()` holds the rule
  (each load names its origin: a chosen file, a state or a project),
  and `vst3.adapter`'s `restored_while_host_settles` replays REAPER's order.

## Failure reasons

Added 22 October 2026 from jrp's
[release-readiness guidance](../review-2026-10/release-readiness-guidance-2026-10-22.md);
PLUG-8 implements it. Silence can be correct processing and still confuse the
user, so every failure in the table above keeps a structured reason; where it
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
  one line, the window's rows, the text `HostFilter::error()` gives an
  adapter (DIAG-4). Only exceptions carry it as text, one labelled line per
  part, which the adapters' and tools' catch blocks read back.

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
  a Mix below 1 and there is a dry signal ([bypass](#bypass)). Before the
  host's configuration is known it cannot tell whether a dry signal will
  play, so it says "no filter plays". The Audio Unit
  recomputes the line each time a host reads it, as Gain and Mix may have
  changed. The parameter is not automatable and is not part of the host
  state. VST3: parameter ID 1001 (Bypass is 1000), a list parameter with `kIsReadOnly`; the
  processor sends the line to the controller in a `ConvolverStatus` message
  after every load, activation, connection, `setupProcessing` and
  `setBusArrangements` (outside its control lock), and the controller asks the
  host to reread its parameters (`restartComponent(kParamValuesChanged)`;
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
  retry a Latency that could not be built (AU-16; the pattern is TODO
  PLUG-30). Host writes to it
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
  `convolver-preset` also holds, PLUG-31) while the filter's identity
  (`HostFilter::filter_identity()`, a hash of every stored value, found once
  when the filter is accepted) matches the one saved beside it, or
  "Restored filter" without one, which claims no origin the host did not
  give; PLUG-22, PLUG-31. VST3 keeps the pair that named the playing filter
  before the newest controller state and uses whichever pair matches, in
  the report and in `Controller::getState`, since a host sends a refused
  preset's controller state as well, in either order with the component
  state) and a note when
  the newest request was
  refused, the filter's own values for under its name (rate, channels,
  length and taps, speakers; PLUG-22), the Audio facts as short labelled
  values (`HostFact`: host sample rate, host channels, routing, ring-out,
  loudest peak from `HostFilter::filter_peak()`, host block size; each with a
  short note, a problem flag for what does not match, and help text), the
  settings in words, and reference lines: the filter file and request,
  `execution_line()` and the log. Everything a window says about the
  filter and the audio comes from here, so the VST3 windows (VST3-11 to
  VST3-13) show the same; the control labels, hints and tooltips are the
  view's own. Messages passed to `HostFilter::reject()` and the other
  failure records use the same parts (the problem, then labelled lines);
  `host_log()` and the LV2 log join them into one line.
  `report_fields()` gives it as named text values for a format's
  transport, `report_text()` as plain text, and `diagnostics_text()` adds
  this instance's recent log lines (`host_log_recent()`, at most the last
  64 KiB of the file). Built on the control side; reading the log happens
  only when diagnostics are asked for. Audio Unit (AU-6): global custom
  properties 64000 (the fields as a `CFDictionary` of `CFString`s) and 64001
  (the diagnostics text as a `CFString`), read-only, values released by the
  caller, with a property-change notification for 64000 wherever the Status
  parameter's event is sent. They are internal between the unit and its own
  window, which ship together, so they are not permanent identities.
  Audio Unit listeners (AU-17): the unit's own notifications of the
  latency, the tail and 64000 reach the host's property listeners on the
  main thread. AUBase keeps its listeners in a vector without a lock, which
  hosts change on their main thread, so a notice raised on any other thread
  (the Latency queue, or a host call such as a restore, `Initialize` or a
  format change made off the main thread) is handed to the main queue
  through a `DISPATCH_SOURCE_TYPE_DATA_OR` source, as the JUCE Audio Unit
  wrapper does, instead of reaching listeners during that call. Notices
  posted before the main thread runs coalesce, and one still pending when
  the unit closes is dropped. A host whose main thread never runs its run
  loop is not told of those changes and reads the latency and tail when it
  asks for them; each instance it opens then keeps a small record and a
  dispatch source until the process ends. The `ClassInfo` and
  `PresentPreset` notices for a file chosen in the window, and the Audio
  Unit SDK's own notices for a host's property calls, are sent on the
  calling thread as before; the window chooses files on the main thread.
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
  load (`loaded_line()`), start (`started_line()`: rate, channels, block size
  and what plays, with the failure or speaker notice in full, and what Gain
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
  (only an instance's first refused layout, as hosts probe one after another),
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
  empty instance's first load fails, every adapter silences the processor
  that plays with `HostProcessor::silence()` (ROBUST-3) rather than
  `activate()`, which would restart the fault count; it neither allocates
  nor fails, so the silence holds even when memory has run out. A first
  load that runs out of memory before its failure is recorded still fails
  (ROBUST-7): `HostFilter` marks the instance failed (`fail_request()`)
  before it allocates anything for the record, which then keeps the kind
  and request number but may lack the message (`failure_message()` gives
  general advice instead) or the path. The adapters call `fail_request()`
  for a request that throws before `HostFilter` records it: VST3 in
  `refused()`, the Audio Unit in `fail_unrecorded()`, and LV2 in
  `restore()`, `work()` and `record_dropped()`. The allocation-starvation
  lifecycle tests cover restored state on all three formats; the
  file-loading paths are checked by review only. The audio
  thread shows a new fault at once without logging: VST3 writes Fault to the
  Status parameter as an output parameter change, and when the host passes it
  to the controller (`setParamNormalized`), the controller sends
  `ConvolverFaults` and the processor explains; it also explains on
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

VST3 carries the window's traffic in `IMessage`s between the processor and
the controller (VST3-3), which hosts may deliver on any thread and may run in
separate processes, so each carries values, never pointers:

- The processor's `ConvolverStatus` message also carries the editor report:
  `report_fields()` encoded as one binary attribute, `report_text()` and the
  log tag ("VST3 #2"). The controller keeps a copy under its own lock for a
  view, and asks the host to reread the parameters only when the Status line
  changed; Copy diagnostics joins its text with the recent log lines
  (`diagnostics_text()`).
- A window reading the report sends `ConvolverReport`, and the processor
  answers with a fresh `ConvolverStatus`, so faults the audio thread counted
  appear as they do in the Audio Unit's twice-a-second read. It looks for new
  faults at most every 10 seconds, as the Audio Unit does.
- `ConvolverLoad` carries a chosen file's UTF-8 path and a request number.
  The processor reads it with `read_chosen_file()` for the current rate and
  channels (macOS AudioToolbox, as the Audio Unit reads; libsndfile on
  Windows and Linux; builds with no reader refuse it with a reason) and
  installs it as `setState` does, or records the refusal; its
  `ConvolverStatus` answer names the request and whether the file was
  accepted. PLUG-26: a chosen Convolver `.vstpreset` is read as
  `convolver-preset` reads it, and when it holds settings its Gain, Mix and
  Latency apply with its filter (its bypass flag does not: the host's
  Bypass stays); the answer then carries them (`gain`, `mix`,
  `latency frames`), and the controller reports each one that changes to the
  host as an edit (`beginEdit`, `performEdit`, `endEdit`), so the host's
  parameters and saved project agree with what plays, then asks for a
  latency restart when Latency changed. They are three separate gestures:
  a host writing automation records them, which its own preset menu never
  does, and undoing one in the host changes that setting back, not the
  filter. The host hands Gain and Mix back in
  `process()` with the values the processor already has, and Latency to
  `setParamNormalized` as no change. A preset without settings leaves them; one
  holding the defaults reports any that change (PLUG-35).
  The controller then calls
  `setDirty(true)` for an accepted file, so the host marks the project
  changed. When `setDirty` does not succeed (a host without
  `IComponentHandler2`, such as Audacity 4.0.2), the controller instead
  reports one edit of Latency that keeps its value (`beginEdit`,
  `performEdit` with the current value, `endEdit`), so that such a host
  stores the new state (VST3-17 in [TODO](../../TODO.md) has the Audacity
  reasoning), unless a chosen preset's settings were already reported as
  edits. Latency is used because it is not automatable and `process()`
  ignores it (the controller sends a new Latency as a message, only when the
  value changes), so neither the sound nor Gain, Mix or automation change.
  It is a narrow fix: a host whose `setDirty` succeeds sees nothing new, and
  the general fix is the host implementing `IComponentHandler2`. Reading
  happens on the thread that delivered the message, never in `process()`.
- A host may put a proxy between the two components
  ([`IConnectionPoint`](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IConnectionPoint.html))
  that delivers a message after `sendMessage()` returns and answers
  `kResultOk` itself (VST3-15). The controller therefore acts on the
  processor's answers, not on `sendMessage()`'s result: a load is refused at
  once only when the answer came during the call (a direct connection or a
  forwarding proxy, such as the SDK's `ConnectionProxy`); otherwise the
  window's report shows the result when the answer arrives, and only an
  accepted answer marks the project changed. `ConvolverLatency` carries a
  request number too, and only the answer to the last one sent asks the
  host to restart (below).

## Scope of each change

jrp's rule (12 November 2026): each kind of change does only the work it
needs, in a narrow function of its own, so that changing one number never
replaces the filter. PLUG-10's first Estimate replaced the playing filter to
change its gain, which restarted its history and silenced it for the
filter's delay; `HostProcessor::update_gains()` now changes the figure in
place.

| Change | What it does | What must stay unchanged |
| --- | --- | --- |
| Gain, Mix, Gain mode or a calculated gain; bypass | Updates the value and ramps: `set_gain_db()`, `set_mix()`, `set_gain_mode()`, `update_gains()`, `set_bypass()` | The filter, its history, the latency and the tail |
| Filter name, diagnostics, the window | Updates what is shown only | Everything the audio thread uses |
| A different filter | Prepares it off the audio thread and crossfades (`HostProcessor::replace`, [replacement and threads](#replacement-and-threads)) | The settings and the latency |
| Latency, or the host's rate or channels | Builds a new `HostProcessor`, only when the value differs (Latency) or at activation (the host's) | The filter and the settings |
| Saving | Encodes the state | Playback |

A request can hold several changes (restored state holds a filter and the
settings), and each part takes its own row. Where the table leaves a case
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

Not yet: during playback, restored state or a chosen preset whose filter is
the one already playing still replaces it (VST3 `setState` and a
`.vstpreset` chosen in the window, AU `RestoreState`, LV2 `restore`), so a
preset that changes only Gain restarts the filter's history and cuts a
sounding tail. Found by
reading the code on 12 November 2026, not reproduced in a host; listed in
TODO's [limitations](../../TODO.md#documented-limitations).

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
configuration and whether it has a dry signal, and `HostFilter::activate()`,
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
  in decibels (jrp). In Set mode both filters of a replacement play at the
  same gain.
- **Gain mode** (PLUG-10, jrp, 12 November 2026) chooses which gain plays:
  *Set* plays Gain; *Estimate* and *No clipping* play the filter's own
  figure from `estimate_attenuation()`, its noise estimate and its
  no-clipping bound, the figures `convolver --gain-db auto` and `no-clip`
  apply. The bound holds for input within full scale; No clipping is not a
  limiter, so floating-point input above full scale can still clip. Gain keeps its value meanwhile and plays again in Set mode. The
  figures are applied as they are, never clamped to Gain's range, and each
  filter carries its own (`FilterGains` in `HostPlayback`), so in a
  replacement the new filter starts at its own figure and the crossfade
  covers the change. Through the crossfade each filter has its own gain,
  which ramps to that filter's target when the mode or Gain changes: a host
  restoring a project may set the mode just after the filter, and neither
  filter jumps. `HostFilter::load` finds the
  bound for every accepted filter (it only sums taps); the noise estimate,
  which filters noise for ten filter lengths (at least 65536 frames, about
  0.2 s for a 5-second stereo filter and 0.6 s for a 10-second one on the
  cloud machine), only while an adapter has Estimate chosen
  (`HostFilter::estimating()`). Choosing Estimate plays the bound at once;
  `HostFilter::estimate()` then finds the estimate for the filter already
  accepted, on the adapter's control side (VST3's message thread, the Audio
  Unit's Latency worker, LV2's worker), and the adapter gives it to the
  playing filter with `HostProcessor::update_gains()`, which ramps to it
  like a Gain change. The engine is not replaced, so it keeps its history:
  a replacement would restart it, silent for the filter's own delay (jrp's
  review found 245 ms of silence with a quarter-second delay). The estimate
  is kept only once applied, so a failure leaves nothing shown that does
  not play. A restored state with Estimate chosen sets `set_estimating()`
  before it loads, so its filter gets the estimate as it loads. A filter
  whose estimate overflows has none (`estimate_failed`), and Estimate plays
  its bound; a filter with no finite bound plays silence in both modes,
  which the log and the window say. Without a filter both figures are 0 dB.
  The log line `gain_mode_line()` and `settings_words()` give the mode and
  the gain it plays (`gain_words()`), and the window report's `gain_note`
  field says it beside the Gain mode menu in the macOS and Windows windows.
  Not automatable, like Latency, since Estimate may need time before it can
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
  [host state](#host-state)), or version 4 when the Gain mode is not Set;
  LV2 has control ports, which the host saves (LV2-7). A preset
  `convolver-preset` makes from a filter carries the filter only (one it
  converts from a host's `.vstpreset` keeps that preset's settings), and a
  state moved between
  formats carries the filter but not the settings, as
  [how the plug-ins behave](../use/plugins.md#settings) tells users.
- **Audio Unit (AU-7).** Global parameters Gain (0, decibels), Gain mode
  (1002, an index of `GainMode` with value names Set, Estimate and No
  clipping, flagged `kAudioUnitParameterFlag_NonRealTime`), Mix (1003,
  percent) and Latency (1004, an index into `host_latencies` with value names
  such as "256 frames", also non-real-time), listed before Status. Render
  plays the Gain mode parameter's value; a new one wakes the Latency queue
  with its own bit, and the run finds Estimate's figure, gives it to the
  playing filter (`update_gains()`) and logs the mode. `RestoreState` restores the mode with the other
  parameters, and state saved before the mode existed restores Set. Values a host writes are stored within their ranges
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
  use, and it is not tried again until chosen again.
  `RestoreState` reads the parameter values from "data" itself, with bounds
  checks (AUBase's reader has none), and applies them only when the filter is
  accepted; it ignores Status and unknown IDs, keeps settings the state does
  not hold (a `convolver-preset` preset has an empty "data"), and keeps both
  when "data" is malformed (and logs it). The start line in the log lists the
  settings (`started_line()` with `settings_words()`), Latency included.
- **VST3 (VST3-4).** Parameters Gain (0, a `RangeParameter` in dB, normalized
  linearly over −100 to +24), Mix (1003, 0 to 100 %, tagged `kDryWetMix`
  through `IParameterFunctionName`), Latency (1004, a `StringListParameter`
  "64 frames" to "4096 frames", not automatable) and, after Gain, Gain mode
  (1002, a `StringListParameter` Set, Estimate, No clipping, not
  automatable), listed before Bypass and Status, with the same IDs as the
  Audio Unit. A new Gain mode goes from the controller to the processor in a
  `ConvolverGainMode` message; the processor finds Estimate's figure if it
  needs it, publishes the filter with it, then plays the new mode and
  answers with its status. The controller's `setComponentState` mirrors a
  restored mode without sending it back. The processor reads the last
  point of each parameter queue in `process()`, as the SDK examples do, and
  hands Gain and Mix to the processor on every call. A Gain within 1e-5 dB
  of 0 counts as 0 dB, because hosts that keep normalized values as float
  cannot send 0 dB exactly, and a project reset to the default must still
  save as version 1 or 2. A new Latency follows
  the SDK's documented route: the controller tells the processor
  (`ConvolverLatency` message) and, once the processor's `ConvolverStatus`
  answer says it has the new Latency, asks the host to restart the component
  (`restartComponent(kLatencyChanged)`), so a host that delivers messages
  late still reads the new latency (VST3-15). While active, the processor
  builds a processor with the new Latency and plays it before it answers
  (VST3-16), as the Audio Unit does: REAPER 7.85 reads
  `getLatencySamples()` inside `restartComponent` and does not deactivate and
  activate the component, as the SDK describes
  ([trace](../archive/reviews/2026-11-07-vst3-16-reaper-trace.md)), so the
  latency must already be the one that plays. `getLatencySamples()` always
  reports the latency the running processor has (the choice while
  inactive), never a choice that does not play yet. `process()` switches to
  the new processor at its next block, and the control side frees the old
  one only once `process()` has left it. If the new processor cannot be
  built, the old Latency keeps playing, the log says why, the window says the
  choice is not in use yet, and the next activation builds it. A host that
  does restart the component builds the processor a second time, at the
  same Latency. A restored state sets all three
  (a version 1 or 2 state resets them to their defaults), and the
  controller's `setComponentState` mirrors them, asking for a restart when the
  Latency differs.

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
same (`ignored_inputs()`). Outputs stay exact: a filter is never spread over,
or cut to, other outputs.

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
exactly. With no filter, Status names choosing a filter before a
preset where the plug-in has a window (`host_status()`'s `chooses_files`).

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
session, and reactivation recomputes it.

A new Latency while active (the Audio Unit, and VST3 since VST3-16)
replaces the whole `HostProcessor`, since the block size changes: the
adapter builds it on the control side, then publishes it through an atomic
pointer that the audio callback reads at the start of each call and marks
as in use until the call returns; the previous processor is freed on the
control side only once the callback no longer marks it, which takes at most
one callback. The new processor starts with empty history, so the sound
pauses for the new latency and a sounding tail is cut.

`Convolver::host` exports `HostProcessor`, `HostFilter` and the state codec
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

## Host state

Host state uses a bounded, versioned little-endian format (64 MiB encoded
limit), validated transactionally: state that fails decoding or its own checks
leaves the previous state untouched. What is accepted can always be saved
(PLUG-12): `HostFilter::load` refuses a filter whose encoding would exceed the
limit (`encoded_host_state_size()`, counted without encoding) before
installing it, with `HostFailureKind::too_large` and the explanation from
`host_state_size_error()`, as a check on the state itself. The check keeps
`host_settings_room_bytes` (256 bytes) free for the settings, counted in
place of any settings the state holds, so a filter accepted at default
settings still saves after a VST3 setting changes, including settings added
later (PLUG-13); every format and `convolver-preset` use the same limit, and
restored state is checked the same way. A `.cfg` may hold
64 Mi samples, four times what the state holds, so a filter file can be too
large; `convolver-preset` makes the same check. Checks against the host's configuration
follow [loading and failure](#loading-and-failure), including state restored
before the host is configured. Version 1 records the filter (or an empty
instance) and the bypass setting (used by VST3 only; see [bypass](#bypass)); version 2 is written only for a saved failed
initial load, with no filter. Version 4 (PLUG-10, VST3 only) is version 3
followed by the Gain mode as one little-endian word (1 Estimate, 2 No
clipping; decoding refuses any other value), written only when the mode is
not Set (jrp, 12 November 2026), so other states keep their version and
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
Decoders read all four; encoders write version 1 whenever nothing new is recorded
(every state without settings: the presets `convolver-preset` makes from a
filter and the Audio Unit's and LV2's states), so older builds still read
those presets. Every published preview reads version 3, since VST3-4 came
before the first, except that 0.12.12, from before PERF-2 added Latency 0,
refuses a version-3 state with Latency 0 as damaged: since PLUG-35 that is
every VST3 project and preset saved at the defaults. Version 4 (a Gain mode
other than Set) is read only by builds with the Gain mode. How each format stores it (the
`.vstpreset` chunk, the AU `convolver-state` key, the LV2 state property) is in
[preset files](#preset-files).

## Preset files

`convolver-preset` ([user guide](../use/presets.md)) writes the same **encoded
host state** into every format: the magic `CNVS`, state version 1, then the
sample rate, channel counts, output channel mask, delays, and every filter
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
  The encoded state is under the key `convolver-state`.
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

- Presets hold host state version 1, which records channel counts and an
  advisory output channel mask but no layout. Plug-ins must load every
  version-1 preset whose rate and counts fit; a stricter layout check never
  rejects one ([old presets](plugin-identities.md#presets-without-a-layout)).
- Host state version 2 only marks a plug-in saved after its first filter failed
  to load; it holds no filter. The tool never writes it and refuses to convert a
  `.vstpreset` that holds it. VST3 builds before VST3-7 reject version 2, so a
  fresh instance there plays as an empty instance.
- If a later version records more (for example an input layout), the tool must
  keep writing version 1 for filters that do not need it, so older plug-in
  builds can still read those presets.
- `convolver-vst3-preset` is the same tool under its old name, limited to its
  old behaviour: the same arguments, messages and output bytes (so no `Cont`
  chunk), and only `.vstpreset` output.

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
  change notification when either changes; the tail stays at the longest
  filter used until the host reinitializes the unit. Bypass is
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
  one edit per click; the report as `ConvolverReport` and the copy the processor sent;
  a chosen or dropped file as `ConvolverLoad`. A bypass change through
  the controller's `setParamNormalized` also sends `ConvolverBypass`, so the
  processor's status and report say Bypassed before the audio thread sees
  the change (PLUG-22). The controller state (`getState`, `setState`) holds
  the accepted file's name and its filter's identity (`"CVCS"`, version 1,
  each string as a little-endian 32-bit length and UTF-8); an empty or
  unknown stream reads as no name, so projects saved before PLUG-22, whose
  controller state is empty, still open. `setState` passes the name and
  identity to the processor as `ConvolverName`, and the processor names its
  report before the fields and the copied text are made from it. The view's blocks hold a
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
  Recent menu (`TrackPopupMenu`) are Windows' own modal loops, owned by the
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
  follows the newest request (3, Loading, while it is outstanding); the path
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
  for Latency: `work()` finds Estimate's figure and gives it to the playing
  filter (`update_gains()`); `activate()` finds it for a mode chosen while inactive, so a
  restored session plays the estimate from its first frame. Without a
  worker, Estimate plays the bound until the next activation. A new Latency while active is a worker
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
  freed instance. `cleanup()` therefore waits, up to 100 ms, for a queued
  Latency request to be worked (`wait_for_queued_latency` in
  `plugins/lv2/plugin.cpp`). It is narrow because file and fault requests
  are not waited for: they follow a person's choice or a rare fault, and
  waiting for them would slow every host that drops queued work on closing,
  as the specification lets it. It is not a guarantee: JUCE runs one worker
  thread for every LV2 instance in the process, so another plug-in's work
  lasting over 100 ms can still leave a queued Latency request to run after
  the instance is freed.
  It needs `urid:map`, uses `work:schedule` to load files, logs through
  `log:log` (otherwise standard error), and reads `bufsz:maxBlockLength` from
  `options` when given; without it, or for longer `run()` calls, it processes in
  pieces of at most 4096 frames with the same output. It reads filter files
  with libsndfile, whose error reporting is shared by the whole process, so
  Convolver opens one file at a time; that lock does not cover the host or
  other plug-ins using libsndfile at the same moment, which at worst report
  each other's error message.
