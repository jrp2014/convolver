<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in adapter contract

[Documentation index](../README.md) · [Implementation map](architecture.md) ·
[Plug-in identities and layouts](plugin-identities.md) · [Testing](testing.md#plug-in-comparisons-plug-2)

For anyone writing, changing or reviewing a plug-in adapter (VST3, Audio Unit,
LV2), and for host integrators. This page is the reference for what every
adapter must do; [how the plug-ins behave](../use/plugins.md) says how that
looks to a user. The rules were designed in the
[plug-in formats plan](../project/plans/2026-10-21-plugin-formats.md#shared-adapter-contract)
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
- Bypass ramps over 64 samples to the input delayed by the latency, or to
  silence when the counts differ.
- Gain and mix are host parameters whose defaults keep the sound unchanged
  ([settings](#settings)); the Audio Unit has them first.
- Nothing on the audio thread allocates, locks or does I/O; filters are
  prepared elsewhere and handed over.

These rules hold for VST3, LV2 and AU alike. Each adapter's tests check them
through the [PLUG-2 harness](testing.md#plug-in-comparisons-plug-2), and a
review of plug-in work checks them ([independent review](workflow.md#independent-review)).
Changing a rule is an owner's decision and goes in its own pull request.
Compatibility and numerical correctness take priority over CPU-specific
optimization in every adapter.

Contents: [loading and failure](#loading-and-failure) ·
[failure reasons](#failure-reasons) ·
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
| **Failed replacement:** a working filter is running and a new one is rejected | The working filter keeps running, unchanged | Error returned to the host, status "load failed", message in the log; saved state still describes the working filter | As specified, active or inactive once the plug-in has been activated at the current rate and buses; `setState` returns `kResultFalse` |

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
- LV2 knows the sample rate at instantiation and its ports are fixed, so its
  configuration is always known and both kinds of check run in `restore()`.
- VST3 counts its configuration as known once the plug-in has been activated
  with it, and while the rate and buses stay the same. `setupProcessing` alone
  is not binding: JUCE-based hosts, pluginval among them, call it with
  placeholder values (44.1 kHz) before restoring state made for another rate,
  which a check at that point would wrongly reject (found by
  `vst3.pluginval.acceptance` in VST3-7).

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
  failed, why, and what to change.

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
is unchanged). The speaker-layout notice stays a separate string
(`HostFilter::notice()`): it never changes what plays, and it describes the
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

- **A read-only Status parameter** whose value is a `HostStatus` (Playing, No
  filter, Load failed, Not checked yet, Fault) and whose text for the current value
  is `host_status()`'s one line of at most 127 ASCII characters
  ([`host.hpp`](../../include/convolver/host.hpp)). The status follows
  `HostFilter::failure()`, so a refused replacement shows Load failed while
  the previous filter plays, as LV2's status port does. Before the host's
  configuration is known it is Not checked yet and names the filter's rate
  and channels. Adapters with [settings](#settings) pass them, so that the
  line says what is heard when no filter plays (jrp's review of 0.5.5): the
  input at the level Gain and Mix give it when no filter is loaded, and only
  the dry signal, not "Silent", after a failed load at a Mix below 1. Before
  the host's configuration is known it cannot tell whether a dry signal will
  play (equal counts only), so it says "no filter plays". The Audio Unit
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
  parameter-change event is sent outside the control lock. Host writes to it
  are ignored, and the value `AUBase::SaveState` writes with the other
  parameters is ignored on restore. When the log file cannot be written
  (`host_log_written()` is false), the text says so instead of pointing to it.
- **The editor report** (PLUG-9, [`host_report.hpp`](../../portable/host_report.hpp)):
  for a window and its Copy diagnostics, `host_report()` gathers the status
  and its line (`host_status()`), the full explanation of what plays, why
  and what to change (the failure's message, or `playing_words()`, the same
  words the log's start line uses, with any fault explanation), and labelled
  details: host, filter, settings, latency and tail, `execution_line()` and
  the log. `report_fields()` gives it as named text values for a format's
  transport, `report_text()` as plain text, and `diagnostics_text()` adds
  this instance's recent log lines (`host_log_recent()`, at most the last
  64 KiB of the file). Built on the control side; reading the log happens
  only when diagnostics are asked for. Audio Unit (AU-6): global custom
  properties 64000 (the fields as a `CFDictionary` of `CFString`s) and 64001
  (the diagnostics text as a `CFString`), read-only, values released by the
  caller, with a property-change notification for 64000 wherever the Status
  parameter's event is sent. They are internal between the unit and its own
  window, which ship together, so they are not permanent identities.
  `kAudioUnitProperty_CocoaUI` names the window's factory class in this
  image's own component bundle; the factory and view class names carry the
  version. Unlike the editor plan's rule that the window copies reports
  pushed to it ([threads](../project/plans/2026-10-24-plugin-editor.md#threads),
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
  `read_filter_file()` ([`filter_file.hpp`](../../portable/filter_file.hpp),
  the step `convolver-preset` uses) and macOS's AudioToolbox rather than
  libsndfile ([`audio_toolbox.cpp`](../../portable/audio_toolbox.cpp)), for
  its current rate and channels, then
  installs it through the same `HostFilter::load` as a state restore, with
  `HostRequest{HostFilterSource::file, path}`, so the failure rules,
  replacement and saving are unchanged and the saved state is the one the
  tool writes. Setting it returns an error when the file was not loaded;
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
  `CONVOLVER_LOG` into the build folder.
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
  still returned by the next `observe()` (as `earlier`, logged with their own
  ending saying the status does not show them), but no longer shown as
  Fault, and the audio thread signals only faults after it
  (`faults_since_clear()`, `fault_to_signal()`, `fault_signalled()`, once per
  baseline). The audio thread never publishes Fault over Load failed: an
  accepted load sets `hold_fault_signal(failure_shown())` before the
  snapshot, and VST3 and the Audio Unit set it again after every rejection
  and activation; LV2's status port puts Load failed first itself. When an
  empty instance's first load fails, every adapter silences the processor
  that plays with `HostProcessor::silence()` (ROBUST-3) rather than
  `activate()`, which would restart the fault count; it neither allocates
  nor fails, so the silence holds even when memory has run out. The audio
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

LV2 keeps its own routes (the `status` port, whose values are Running, No
filter, Load failed, Loading and Fault, and `log:log`) and does not write
the file. An editor (VST3-3, HOST-2) would show the same text.

## Initial restoration and live replacement

- **Initial restoration** installs the filter directly: when no audio has been
  processed since activation, the adapter constructs the `HostProcessor` with
  the restored configuration. There is no warm-up or crossfade, so output
  matches the filter from the first block (after the reported latency).
- **Live replacement** uses `HostProcessor::replace`: the new filter starts with
  fresh history, warms up for 256 frames and crossfades for 256 frames, as described under
  [replacement and threads](#replacement-and-threads).
- Pending replacements coalesce to the newest, as `HostProcessor` already does.

## Bypass

Bypass ramps over 64 samples to the dry signal and back, with the dry path
delayed by the reported latency so wet and dry line up. Wet processing carries
on during bypass, so switching back does not lose history.

- **Equal input and output counts:** the dry signal is input *c* on output *c*.
- **Unequal counts:** every output ramps to silence. No output has an unfiltered counterpart: bypassing a 2→6
  crossover must not send full-range audio to a tweeter, and a 1→2 filter that
  forks a mono input to two outputs cannot tell which output the input belongs
  on. This matches the contract for an empty instance with unequal counts.

`HostProcessor` implements this, so every adapter that drives it through
`set_bypass` follows the rule; the PLUG-2 `bypass` case checks it. An adapter
must not use its format's own pass-through bypass (see the AU design in the
[plug-in formats plan](../project/plans/2026-10-21-plugin-formats.md#au-adapter-design)).

Whether saved state records bypass follows each format's convention. VST3 saves its bypass parameter with the
component state, as the SDK's examples do. In LV2 (the `enabled` port) and
Audio Units (`kAudioUnitProperty_BypassEffect`) the host saves and restores
bypass itself, so those adapters do not record it and ignore the flag in a
state from another format.

## Settings

PLUG-10, from the [editor plan](../project/plans/2026-10-24-plugin-editor.md#settings),
whose choices jrp made on 24 and 25 October 2026
([decisions](../project/decisions.md#identities-and-file-formats)). Every
adapter offers the same settings as host parameters, so they work in generic
host controls, automation and host presets without a window. The defaults
(gain 0 dB, mix 100%, latency 256 frames) leave every output sample exactly
as it was before the settings existed, which `native.host_settings` checks
bit for bit. The first version had Gain and Mix only (jrp asked to keep it
simple, 25 October 2026); Latency followed (PLUG-10, 31 October 2026), and
the plan's Gain modes and meters follow later ([TODO PLUG-10](../../TODO.md)).

- **The shared code.** `HostSettings` in
  [`host.hpp`](../../include/convolver/host.hpp), and `settings_words()` for
  the log. `HostProcessor` takes the settings at construction; `set_gain_db()` and
  `set_mix()` are audio-thread calls like `set_bypass()`, and each change
  ramps linearly over 64 frames. The latency is fixed for a processor's life.
- **Gain** applies to the filtered signal, after convolution and before the
  mix; the dry path stays the untouched input. Range −100 to +24 dB, linear
  in decibels (jrp). During a replacement both filters play at the same gain.
- **Mix** is the filtered share, 0 to 1, of the output before bypass; the
  rest is the latency-aligned dry signal that bypass uses. With unequal input
  and output counts there is no dry signal ([bypass](#bypass)), so the mix is
  always 1. Like bypass, a mix below 1 still passes its dry share while a
  failed first load plays silence (equal counts only).
- **Latency** is the block size `HostProcessor` is built with, the engine's
  uniform partition (the CLI's `--block-size`): one of `host_latencies`, 64 to
  4096 frames in powers of two, default 256 (jrp). Smaller is less delay and
  more processor load. The settings constructor refuses any other value. It
  cannot change while a processor runs, so a new choice needs a new
  processor, which each format builds at its own restart point (below), and
  the host is told the new latency only once the audio has it. Not
  automatable. The [editor plan](../project/plans/2026-10-24-plugin-editor.md#settings)
  keeps the load measurement beside it for later.
- **Saved form:** each format's own way. The Audio Unit's parameters are
  saved by `AUBase::SaveState` in `ClassInfo` ("data"); VST3 will save them
  in host state version 3, written only when a setting differs from its
  default (VST3-4); LV2 will add control ports (LV2-7). A preset from
  `convolver-preset` carries the filter only, and a state moved between
  formats carries the filter but not the settings, as
  [how the plug-ins behave](../use/plugins.md#settings) tells users.
- **Audio Unit (AU-7).** Global parameters Gain (0, decibels), Mix (1003,
  percent) and Latency (1004, an index into `host_latencies` with value names
  such as "256 frames", flagged `kAudioUnitParameterFlag_NonRealTime`),
  listed before Status. Values a host writes are stored within their ranges
  (Latency rounded to a whole index), and Render hands Gain and Mix to the
  processor on every call. `Initialize` builds the processor with the chosen
  Latency and posts the latency and tail notifications. While uninitialized,
  `kAudioUnitProperty_Latency` reports the chosen Latency, and a change posts
  the notification at once; while initialized it reports the running
  processor's, and a new choice waits for the next `Initialize` (the editor
  plan's first option: rebuilding while rendering would need a lock-free swap
  of the whole processor). The window's report then says which latency plays
  and which waits.
  `RestoreState` reads the parameter values from "data" itself, with bounds
  checks (AUBase's reader has none), and applies them only when the filter is
  accepted; it ignores Status and unknown IDs, keeps settings the state does
  not hold (a `convolver-preset` preset has an empty "data"), and keeps both
  when "data" is malformed (and logs it). The start line in the log lists the
  settings (`started_line()` with `settings_words()`), Latency included.

## Channel layouts

What a channel means (discrete or a named speaker layout), how presets with no
recorded layout are treated, and the per-format details are in
[plug-in identities and channel layouts](plugin-identities.md#channel-layouts).
In short: version-1 presets load on any arrangement with matching channel
counts, and a recorded output speaker mask only ever gives a notice.

## Replacement and threads

The host owns the audio device, clock and scheduling. Each callback supplies
the next section of one continuous stream; convolution history persists
between callbacks, and processing produces one output frame per input frame
after the reported latency (256 samples, independent of the callback length).

Filters are prepared outside processing and transferred through a bounded
single-producer/single-consumer pointer handoff. A filter that arrives before
any audio has been processed since activation or reset is installed directly.
A reset (the host stopping and restarting processing) also ends a crossfade in
progress, so the newer filter plays at once, and a filter published before
audio resumes is installed directly. Later, a replacement runs for 256 samples
to fill its latency, then crossfades for 256 samples. It starts with fresh
input history: it cannot reconstruct audio that preceded loading. Old engines
are retired on the control thread. Pending changes coalesce to the newest; at
most one transition and one pending replacement are retained, with up to two
old engines awaiting control-thread collection after the last publication.
Collection does not hold up pending changes. The tail is available before
activation and includes routed delays; it can grow conservatively during a
session, and reactivation recomputes it.

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
`mute()` is for a failure
found on the audio thread itself: the current filter ramps to silence over 64
samples (or is silent from the first sample, when nothing has been processed
since construction or reset) and stays silent until a filter published after
the mute takes over and fades in from silence; one published before it but
not yet picked up by the audio thread is silenced too. Bypass still passes the
dry signal. `silence()` is the same mute requested from the control side, for
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
`host_state_size_error()`, as a check on the state itself. A `.cfg` may hold
64 Mi samples, four times what the state holds, so a filter file can be too
large; `convolver-preset` makes the same check. Checks against the host's configuration
follow [loading and failure](#loading-and-failure), including state restored
before the host is configured. Version 1 records the filter (or an empty
instance) and the bypass setting (used by VST3 only; see [bypass](#bypass)); version 2 is written only for a saved failed
initial load, with no filter.
Decoders read both; encoders write version 1 whenever nothing new is recorded,
so older builds still read those presets. How each format stores it (the
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
  The tool's writer needs no SDK; `vst3.preset_format` checks that its output is
  byte-identical to the SDK's `PresetFile::savePreset`.
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
  old behaviour: the same arguments, messages and output bytes, and only
  `.vstpreset` output.

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
  tail plus the 256-sample latency, measured at the output as Steinberg's tail
  test does. 32- and 64-bit host buffers both use the float engine.
- **Audio Unit:** latency and tail are reported in seconds, with a property
  change notification when either changes; the tail stays at the longest
  filter used until the host reinitializes the unit. Bypass is
  `kAudioUnitProperty_BypassEffect` driving `HostProcessor::set_bypass`, not
  `AUEffectBase`'s pass-through, and is saved in `ClassInfo`. The output is
  marked silent only once the tail has decayed. Rejections are returned to the
  host as errors, shown by the Status parameter and written to the Convolver
  log and the unified log.
- **Audio Unit window in a host** (the evidence is in TODO AU-6); the VST3
  window (VST3-11) starts from these:
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
  it reports and saves follows the installed filter. The full rules are in
  the plan's
  [worker and state lifecycle](../project/plans/2026-10-21-plugin-formats.md#lv2-adapter-design).
  It needs `urid:map`, uses `work:schedule` to load files, logs through
  `log:log` (otherwise standard error), and reads `bufsz:maxBlockLength` from
  `options` when given; without it, or for longer `run()` calls, it processes in
  pieces of at most 4096 frames with the same output. It reads filter files
  with libsndfile, whose error reporting is shared by the whole process, so
  Convolver opens one file at a time; that lock does not cover the host or
  other plug-ins using libsndfile at the same moment, which at worst report
  each other's error message.
