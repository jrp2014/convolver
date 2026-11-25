<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Changelog

Changes to the portable C++ core, CLI and plug-ins. The portable package is
versioned separately from the historical Windows releases (1.x to 4.4), whose
release notes are archived in [docs/historical](docs/historical).

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
with one extra group, **For contributors**. Until 1.0 the C++ API may change
between minor versions.

Each release lists first, under **Added**, **Changed** and **Fixed**, what
users of the command-line tools and plug-ins and of the C++ library can see.
Work only contributors see (tests, CI, builds, contributor documentation and
records) follows under **For contributors**, where contributor-only TODO
items keep their entries until formal releases. Releases before 0.12.12, the
first published preview, keep their original grouping.

Pull requests add entries to this page only through their own
[change fragment](changes/README.md), and `scripts/fold_changes.py` folds the
fragments into `[Unreleased]` before each preview, and sometimes sooner
([change fragments](docs/develop/workflow.md#change-fragments)). Entries not
yet folded in are in [`changes/`](changes/README.md).

## [Unreleased]

### Added

- README (DOC-24): a short **What to expect** section for the three uses
  Convolver is built around: room correction and crossovers, music at each
  track's own sample rate, and a reverb on a send. Each says what works
  today, with the measured load, and what you do. See
  [what to expect](README.md#what-to-expect).

### Changed

- README (DOC-24): **What to expect** now says that in the VST3 plug-in the
  window, the host's controls, what plays and what the project saves end up
  agreeing in each order of presets, setting changes and reopened projects
  its automated tests try, even when the host is slow to pass a change from
  one part to the other, as VST3-26 made them; checked by automated tests but not yet in
  a host. See
  [what to expect](README.md#what-to-expect).
- VST3: the plug-in's two halves no longer pass settings and answers to
  each other in messages, which a host could deliver late or between the
  two calls of a restore. The controller now reads and asks the processor
  of its own instance directly (VST3-26, from jrp's step back on 24
  November 2026 after VST3-18, VST3-19 and the window-preset fixes), so the window,
  the host's parameters, what plays and what is saved agree in every order
  of host calls the tests try. A window preset chosen before a later edit or restore no
  longer comes back over it, a refused host preset no longer leaves the
  window showing its Bypass, and the host reads a restored latency even
  when it restores the same state twice. No change to the sound or to saved
  projects.

### Fixed

- Estimate (FFT-5): whether a filter near the largest float gets a noise
  estimate no longer depends on the build. One tap of 3e38 got no estimate
  in one Linux build and an estimate in others, because the noise could
  overflow inside the FFT; now every filter with a No clipping figure has
  an estimate too, in the plug-ins and with `--gain-db auto`, which refuses
  only a filter that `no-clip` refuses as well. Ordinary filters' figures
  are unchanged, bit for bit. The plug-ins' `Too loud to estimate: No
  clipping's gain plays.` can no longer happen and is gone. See
  [gain and attenuation](docs/use/command-line.md#gain-and-attenuation).
- VST3 (VST3-19, jrp's review of 0.18.14): a Latency or Gain mode chosen
  just before a preset is restored no longer overrides that preset when the
  host delivers the choice late. The plug-in played and saved the earlier
  Latency (256) while its window and the host showed the preset's (512),
  so a project saved then reopened with 256. The restored preset now wins,
  and the host is asked to read the latency again. A host that restores
  the window's half first, or delivers messages late, is also asked again
  once the plug-in plays the preset's Latency. See the
  [adapter contract](docs/develop/plugin-contract.md).
- VST3 (VST3-22, jrp's review of 0.18.14): a status message whose report
  arrives damaged no longer sets the Status parameter to Active. When such a
  message also answered a restored preset, bringing the settings that play,
  Status showed Active although the preset had been refused; it now keeps
  the last status a whole report gave. See the
  [adapter contract](docs/develop/plugin-contract.md).
- VST3 on Windows (VST3-21, jrp's review of 0.18.14): the note under the
  Gain row in Estimate or No clipping is drawn in the small type of the
  window's other notes. It was drawn in the larger body type in space
  measured for the small one, so its lines could be cut off.
- VST3 (VST3-20, jrp's review of 0.18.14): a valid project or preset is no
  longer refused as "the saved project or preset could not be read:
  std::bad_alloc" when memory runs short after it was read. To tell which
  state an answer is about (PLUG-48), both halves of the plug-in encoded a
  second full copy of the filter, up to 64 MiB, at every restore; they now
  hash the bytes they read, which copies nothing. Restores are quicker too,
  including the one Audacity makes at every playback start: for a 120 s
  stereo filter, the host's whole round trip took 538 ms instead of 708 ms
  in a Linux release build ([PERF-22](TODO.md)). See the
  [adapter contract](docs/develop/plugin-contract.md).
- Windows (VST3-20, seen in the VST3 plug-in's tests): when memory ran
  out while a filter was being loaded, before Convolver could record why,
  the plug-in could later stop responding instead of reporting the failure,
  which may also have held up the host. The message for that case is now
  ready before it is needed. This dates from 0.17.4.
- VST3 (VST3-18, jrp's review of 0.18.12): after the plug-in refused a
  preset, a Latency, Gain, Mix or Gain mode changed before the host
  delivered the plug-in's answer to that preset no longer jumps back. The
  late answer set the shown Latency back to the 1024 that played at the
  refusal while the plug-in played the 256 just chosen, and reading Status
  again left them apart. A setting chosen or automated since the preset
  now keeps its value, also when automation sets the value the preset
  showed; the others still show what plays. See the
  [adapter contract](docs/develop/plugin-contract.md).

### For contributors

- VST3 (VST3-27, a regression from VST3-26 that no published preview had):
  a late pairing message can no longer connect the plug-in's controls
  to another instance of Convolver. When a host that passes messages late
  moved the controller from one instance to another, the first instance's
  delayed message could pair the controller with it again: the controls
  showed that instance's settings, and a later Latency, Gain mode or chosen
  file went to it and changed what it saved, while the instance on screen
  kept its old settings (VST3-27, found by jrp's review of 25 November 2026 in the test
  harness; whether a shipping host delivers that sequence is unknown). The
  controller now pairs only with the answer to its current connection. No
  change to the sound or to saved projects. See the
  [adapter contract](docs/develop/plugin-contract.md#status-and-log).
- VST3 (VST3-27): the controller numbers each connection
  (`Bridges::stamp()`), starts it unpaired and sends the number in
  `ConvolverPairRequest`; the processor answers with its key and the number
  in `ConvolverPair`. Each time the processor connects it also sends
  `ConvolverPairAsk`, and the controller asks again, so a host that drops
  the request still pairs, on a reconnection too. The controller checks the number and
  keeps the `Bridge` under one lock, refusing an answer to any other
  connection and every answer while disconnected. `vst3.adapter`
  `pairing_follows_connection` checks jrp's two-instance sequence in 18
  variants through delaying proxies (three connection orders, whether the
  controller had paired with A, three arrival times), and that a pair the
  host disconnects and connects again, controller first with the request
  dropped, pairs again; on `main` the six in which A's answer arrives after
  B's failed ("a late pairing message from A changed the controller's
  Latency").
- Releases (REL-3): the preview notes no longer say the VST3 halves agree
  "whatever order the host delivers its calls in"; they name what the
  tests cover (host presets, window presets and later Latency, Gain mode,
  Gain and Mix changes, in every order the tests try), from jrp's review
  of 25 November 2026, and list the pairing fix that review asked for
  (VST3-27)
  ([publishing an unsigned preview](docs/develop/releasing.md#publishing-an-unsigned-preview)).
- Engine (FFT-5): `estimate_attenuation()` filters the noise through a copy
  of the filter whose input gains, taps and output gains are scaled by
  powers of two to fixed levels well inside the float range, then scales
  the peak back in double,
  caps it at the bound and reads output below the smallest normal float as
  silence; it throws `std::overflow_error` only when that peak is beyond
  the largest float. `native.engine` `attenuation` checks that the estimate
  scales exactly from 2^-100 to 2^125, by taps or by route gains, and that
  a quiet route beside cancelling 1e38 routes is kept (from Codex's review);
  `native.host_settings` `filter_estimates` and `status_names_settings`
  check that 3e38 and largest-float taps have both figures. FFTW's plan
  changed output only by rounding at every level tested, and one tap above
  2^127 overflowed in the FFT stages with every plan
  ([extreme levels and the FFTW plan](docs/develop/fft-measurements.md#extreme-levels-and-the-fftw-plan)).
- Releases (REL-3): the release notes for the preview after 0.18.12 list,
  since 0.18.12, the VST3 plug-in's halves keeping in step whatever order
  the host uses (VST3-26, with VST3-18, VST3-19 and VST3-22), restores that
  no longer copy the filter (VST3-20), the Windows Gain note's type
  (VST3-21), an estimate for every filter No clipping can play (FFT-5)
  and the README's What to expect section (DOC-24). They say
  that every VST3 host check predates VST3-26 and that the new changes are
  checked only by automated tests
  ([publishing an unsigned preview](docs/develop/releasing.md#publishing-an-unsigned-preview)).
- VST3 (VST3-26): `plugins/vst3/link.hpp`. The processor registers a
  `Bridge` under a random key and sends the key when it connects; the
  controller looks it up and calls the processor through `ProcessorLink`.
  The processor marks the `Bridge` and sends `ConvolverChanged`, which
  carries nothing, after host calls that change what the controller shows;
  the controller reads it again on the UI thread. Latency and Gain mode
  are always the processor's; Gain, Mix and Bypass are taken from the
  processor only when its generation moved. Removed: `RestoreFollow`,
  `StatusAnswer`, state digests, request numbers and eight message types
  (with `vst3.adapter` `damaged_report_message` and
  `damaged_report_keeps_status`, whose messages no longer exist). A
  configuration list waiting for the host's settings (PLUG-40) is now read
  again only by the window's report, no longer also by the report request
  that followed `setComponentState`. The controller never calls
  `restartComponent` inside another and compares Latency and Gain mode by
  their entry: Ardour reads parameters inside `restartComponent` and passes
  back float values (`convolver_vst3_sync` `float_host`). A Latency or Gain
  mode chosen before the halves pair is sent when they pair, unless the
  processor accepted a restore after it; so does Gain, Mix or Bypass the
  host set then, and a restored controller state's filter name. Bypass
  updates the Status parameter at once, without the window. `convolver_vst3_sync` passes every sequence and is
  registered as `vst3.sync`.
- Tests (VST3-25, from jrp's review of the shared-processor proposal, 24
  November 2026): `convolver_vst3_sync` generates sequences of host calls,
  window choices and message deliveries for one processor and controller
  pair, every sequence of one and two steps in every connection, delivery,
  restore-order and late-automation variant, seeded random longer ones
  and the 44 sequences jrp's reviews since VST3-19 found (pull
  request #281's `latest_action_wins`), also with a host that has no
  `setDirty` and hands edits back (Audacity), and judges each
  by one oracle: once the host has delivered everything, the controller,
  the host's view, the window's settings line, what plays, the latency the
  host last read, the save and a processor reopened from it all match the
  latest step that succeeded, and no `restartComponent` comes inside
  another. Named cases cover two instances, closing one, a processor with
  no controller, an offline restore on another thread, a save before a
  Gain edit is delivered, a choice made before the halves pair and a host
  that holds values as float (Ardour). On `main` at `3e3d5ef` 722 of 6,248
  sequences fail: a window preset whose answer a proxy delivers after
  a later edit or restore (the cases pull request #281 addresses), a
  refused restore after a Bypass change (the window and host show the
  saved Bypass while the processor keeps its own), and the same state
  restored twice controller-first with late delivery (the host is not
  asked to read the new latency), and Gain or Mix automation that reaches
  the processor after a refused restore (the window and host show the
  earlier value). It is built but not yet a CTest test;
  the change that makes it pass registers it. See
  [VST3 tests](docs/develop/testing.md#vst3-tests).
- Tests (VST3-19): `vst3.adapter` `accepted_restore_beats_queued_edit`
  replays jrp's sequence (Latency 256 and No clipping, then a preset with
  512 and Set) through forwarding and deferring connections, in the SDK's
  order and with the controller first, and checks what is shown, what
  plays, what `getState` saves and the latency the host last read, then
  restores the same preset again (no extra read in the SDK's order) and a
  1024 preset. On 0.18.14 it stops at "the host last read Latency 256
  (forwarding proxy, controller first)"; the deferring connection alone
  gives "shown 512, playing 256, saved 256".
- Tests (VST3-19, jrp's review of PR #279):
  `accepted_restore_follows_its_own_request` restores two presets with a
  Latency choice between them through a deferring connection (the request
  for the first must not make the processor forget the second), and
  restores a preset with the controller first, then a chosen file, then
  the same preset again, refused (its Latency must not come back). It also
  restores one preset twice and three times around a Latency choice
  through a deferring connection, and chooses 256, restores a 512 preset
  and then a refused one (512 still wins, and the saved filter is the 512
  preset's). Each sequence checks the filter saved as well as the Latency.
  On the first version of this PR it
  stopped at "shown 512, playing 256, saved 256, expected 512 (sequence
  1)" and "shown 512, playing 512, saved 512, expected 256 (sequence 2,
  controller first)"; on the second, at "... expected 512 (sequence 3)";
  on the third, at "shown 256, playing 256, saved 256, expected 512
  (sequence 5)".
- VST3 processor (VST3-19): what the processor knows about the restore the
  controller mirrored is one `RestoreFollow`, with one method for each
  event (a restore read, accepted or refused, any install, a report
  request, a status), so a path that changes what plays cannot forget to
  clear it.
- Tests (VST3-22): `vst3.adapter` `damaged_report_keeps_status` refuses a
  preset twice while a filter plays, the second time with every report
  damaged on its way, and checks that Latency still follows what plays and
  Status still says Load failed. It failed on 0.18.14 (Status 0, Active).
- Tests (VST3-21): `vst3.editor` on Windows checks that the Gain row's note
  uses the Mix note's font and that its wrapped text fits its height.
- Tests (VST3-20): `vst3.lifecycle` `restore_short_of_memory` restores a
  valid state with memory running out once at each allocation after the
  read, and fails if any refusal calls the state unreadable. It failed on
  0.18.14 at the first allocation after the read. On Windows it also hung
  there until CTest's timeout. The diagnostics and the fix point to
  `failure_message()`: its function-local static was first built for a
  failure recorded without a message, with no memory left, and the next
  call most likely waited on that static under the processor's lock. The
  message is now a namespace-scope constant in `portable/host.cpp`, and the
  test passes on Windows.
- Tests (VST3-18): `vst3.adapter` `edits_survive_late_answer` replays jrp's
  sequence through the deferring proxy: a refused preset, the user's
  Latency, Gain and Gain mode before the answer arrives, and a second
  refusal with only Mix changed. It failed on 0.18.12 ("the parameter shows
  Latency 1024 while the processor plays 256").
  `edits_before_controller_first_restore` covers a host that restores the
  controller first, with Latency and Gain mode chosen between the two
  calls. The status after a mirrored state carries the numbers of the last
  Latency and Gain mode messages the processor had heard (only choices
  made since the restore count, so a lost message cannot block later
  corrections), and Gain mode messages are numbered now, as Latency's
  were. `edits_survive_late_answer` also replays jrp's review of PR #278:
  automation setting Gain and Mix to the refused preset's own values.
- CI (CI-53): a later push to a pull request runs only the workflows that
  the files changed since that workflow's last full passed run need,
  counting what has landed on `main` since that run tested against it, and
  not when a newer run of that workflow failed; the `scope` job names the
  run whose result stands. On `main`, a merge that
  changes anything but documentation still runs everything; one that
  changes only documentation runs Lint. Every release and preview gets the
  full CI: the draft-release workflow refuses a tag on which the native,
  VST3, LV2, Audio Unit, fuzz and Lint workflows have not passed when
  started by hand. See
  [drafts and later pushes](docs/develop/testing.md#drafts-and-later-pushes).
- CI (CI-54): a draft pull request runs Lint and the Linux jobs only;
  marking it ready, or starting the workflows by hand on the branch, runs
  macOS and Windows.
- Process (DOC-21): fewer pushes and review rounds for simple changes.
  Merge `main` into a branch only on a conflict or when jrp asks; answer a
  review round in one push and never push only to record a pull request
  number; an independent review is needed only for code, DSP, plug-in,
  saved-state, `.cfg` and release changes, and its wording findings never
  block or start another round; a ready pull request's handoff note keeps
  only its State line; the status page changes only at milestones. See
  [while working](docs/develop/workflow.md#while-working).
- TODO (DOC-24): new items from jrp's usage review of 22 November 2026
  (summarised in PR #277): PLUG-51 (every output's routes in the window
  and Copy Diagnostics), HOST-12 (a 30-minute REAPER soak with four reverb
  returns, checking for new underruns) and PERF-21 (the everyday load budget); PERF-16 also reports
  the time until a chosen filter is fully heard, and EDGE-4 suggests
  preparing the second filter only when a comparison starts. See
  [TODO](TODO.md#execution-priorities).
- Measurements (DOC-23): [performance then and now](docs/develop/fft-measurements.md#performance-then-and-now)
  records jrp's comparison of the 2006 release with the current engine on
  his Apple M5 Max. With a 65,536-tap stereo filter in large blocks the 4.3
  and 0.18.8 engines both ran at about 1,540 times real time, against 40
  times for 2.19 on a 2006 Pentium 4; at a Latency of 256 frames (5.8 ms)
  the 0.18.8 engine was 4.5
  times as fast as the historical one with the same FFTW 3.3.11 build, and 3.4
  times as fast with Accelerate. The README's "Light on the system" point
  links it. The benchmark kit, the commands to rerun it and all 185 original
  measurements are in
  [`docs/benchmarks/2026-11-22-macos-stereo/`](docs/benchmarks/2026-11-22-macos-stereo).

## [0.18.12] - 2026-11-23

The eighth published preview, for macOS, Windows and Linux, unsigned: a
configuration list chosen before Audacity first plays now loads when it
plays with the window open, and exports hold it; a refused preset no longer
changes the settings shown; one Choose Filter menu replaces the window's
Choose…, Recent and Reload controls; and the Gain mode menu starts the Gain
row ([release](https://github.com/jrp2014/convolver/releases/tag/v0.18.12)).

### Changed

- Plug-ins (PLUG-50): in Convolver's window on macOS and Windows, the
  **Gain mode** menu now starts the Gain row, so the cut-off "Gain mo…"
  label is gone and one row says which gain plays. With Estimate or No
  clipping, the Gain slider and box are dimmed and the box shows the gain
  that mode plays; Gain keeps its value and plays again when you choose
  Set. A note under the row appears only when the box cannot say what
  plays: no filter loaded, a filter with no figure, or No clipping playing
  until the filter is estimated. See [Convolver's
  window](docs/use/plugins.md#convolvers-window).
- Plug-ins (PLUG-50): one **Choose Filter** menu replaces the Choose…,
  Recent and Reload buttons and the "Reload when the file changes" box. It
  holds Choose File…, Reload (named for the file, as `Reload room.cfg`),
  Reload When the File Changes, the recent files and Clear Recent. On
  Windows it is a split button: its main part chooses a file and its arrow
  opens the menu. It is highlighted when choosing a filter is the fix, and
  dropping a file on the window still loads it. See [Convolver's
  window](docs/use/plugins.md#convolvers-window).
- Plug-ins (PLUG-50): the Worst-case peak note says "(won't clip at this
  Gain and Mix)" instead of "(cannot clip …)".

### Fixed

- VST3 (PLUG-49, jrp's live session of 22 November 2026): in Audacity
  4.0.2, a configuration list chosen before the first playback now loads
  by itself when Audacity plays with Convolver's window open, as PLUG-40
  intended, and the export holds it. Before, Audacity handed the plug-in
  its own saved state back as playback started, which forgot the list:
  Status stayed Load failed and every export was silent until the list was
  chosen again. See [loading a filter](docs/use/plugins.md#loading-a-filter).
- VST3 (PLUG-48, jrp's live session of 22 November 2026): a preset the
  plug-in refuses, such as one for 44.1 kHz chosen from REAPER's preset
  menu while a 48 kHz filter plays, no longer changes the Gain, Gain mode,
  Mix and Latency the host and the window show. Before, they took the
  refused preset's values (Latency 0) while the plug-in went on playing its
  own (Latency 1024).

### For contributors

- Plug-ins (PLUG-50): the window report has a new `gain_played` field
  (`gain_played()`), and `gain_note` now holds only what the Gain box
  cannot say; the report's encoding number is 2. The Windows window takes
  the Choose Filter menu's commands as `WM_COMMAND` (`win::menu_reload` and
  the others), which its test sends instead of opening the menu.
- Releases (REL-3): the release notes for the follow-up preview to 0.18.10
  list, since 0.18.10, the two VST3 fixes from jrp's live session (PLUG-49
  in Audacity, PLUG-48 for a refused preset) and the window's Choose Filter
  menu and Gain row, credit what jrp's live session checked on 0.18.10 to
  0.18.10 only, and say the new changes are checked only by automated
  tests ([publishing an unsigned preview](docs/develop/releasing.md#publishing-an-unsigned-preview)).
- Tests (PLUG-49, PLUG-48): `vst3.adapter` `list_read_again` replays
  Audacity's playback start (its own state handed back, then a start)
  before the window reads the report; `refused_state_keeps_parameters`
  restores a preset for another rate while a filter plays at Latency 1024
  and checks the parameters, then an accepted preset's. Each failed before
  its fix; the second also gives the controller the state first, as a
  host against the SDK's order would, including the same preset refused
  at 48 kHz and accepted at 44.1 kHz. `HostFilter::list_waits()` and
  `Processor::unchanged_state()`; the controller's report request names
  the state it mirrored (`state_digest()`), and the status after the
  processor's outcome for it carries the settings that play
  ([adapter contract](docs/develop/plugin-contract.md)).
- Tests (HOST-11): done. jrp's second REAPER run passed all eight checks on
  0.18.10 built from the published tag, with Status read while the
  transport ran; jrp accepted that run as closing it ([record](docs/archive/reviews/2026-11-22-host11-reaper-run2.md)).
  The host-test automation plan is archived; the rest of its step 3 (the
  other presets, the refused preset, the wrong rate, Latency and the Audio
  Unit in REAPER) is HOST-13.

## [0.18.10] - 2026-11-22

The seventh published preview, for macOS, Windows and Linux, unsigned: a
filter chosen before the host plays is kept, a configuration list loads by
itself once the host plays (with the window open), a filter that does not
suit the host waits for a start that suits it, plainer wording in the window
and Status, the tail reported after a stop is the current filter's own, and
audible echo examples
([release](https://github.com/jrp2014/convolver/releases/tag/v0.18.10)).

### Added

- Documentation (PERF-3): the [limits guide](docs/use/limits.md#filter-length-channels-and-processor-load)
  states what one plug-in instance handles, from measurements of
  Convolver's processing, run as its plug-ins run it, on four shared
  machines (an Apple M1 virtual machine and Linux and Windows x86-64
  servers): room correction for up to 8 channels, stereo reverbs up to 60 s
  and true-stereo reverbs up to 60 s at 48 kHz or 30 s at 96 kHz kept up
  with room to spare, a full 8 × 8 matrix only at 48 kHz; and the memory
  and preparation time a filter takes.
- Releases: jrp published Convolver 0.14.0, the sixth unsigned developer
  preview, on 19 November 2026
  ([release](https://github.com/jrp2014/convolver/releases/tag/v0.14.0)).
  After publishing it he asked to move the baseline for the checks left,
  REL-3's walkthrough and COMPAT-1's REAPER project from 0.13.24 to 0.14.0
  ([decision](docs/project/decisions.md#releases)).

### Changed

- Plug-ins (PLUG-44): a filter kept because it does not suit the host's
  sample rate or channels now shows the Status **Waiting**, not Load failed,
  with what it waits for: `Silent, waiting for 44100 Hz: the host runs at
  48000 Hz.` (or `Only the dry signal plays, waiting for …` where Mix lets
  the input through). Nothing was refused, and it plays once the host starts
  the plug-in with settings that suit it. Load failed now always means a
  request was refused or a filter could not start; after a refusal it stays
  until a load is accepted, as before. The window shows Waiting in plain
  grey rather than red. LV2's `status` port keeps Load failed for it. See
  [status](docs/use/plugins.md#status-and-the-convolver-log).
- Plug-ins (PLUG-44): the window says each fact once. When the host's rate
  and channels suit the filter, the Audio section shows one line, `Host
  48000 Hz, 2 in → 2 out ✓ suits the filter`, instead of two; before the
  host has sent its settings it says `not known yet` once; Routing shows
  only with a filter; Ring-out shows only when it
  says more than the filter's Length (delays, a Latency, another rate);
  channels read `2 in → 2 out`
  everywhere. The reminder that a file can be dropped on the window is
  gone from under the name (the Fix and Choose…'s tip still say it). When
  a file is refused, the message now explains that file first and says
  once what becomes of the filter in the heading, by name: `room.cfg keeps
  playing.` while the host plays it, `room.cfg is still selected.` after
  Stop, and, for a kept filter that does not suit the host, `room.cfg is
  still selected and will play at 48000 Hz.` instead of telling you to
  change the project's rate (jrp's review). The note under the name says
  only `broken.wav was refused.`, and now also shows when a file is
  refused while a kept filter waits (before, only the log and the host's
  beep said so); the message no longer repeats the Latency row's "not in use yet"; the
  title gives the release number without the build's source revision,
  which the Software line keeps. Loudest peak is renamed **Worst-case
  peak**, so that it cannot be confused with the level meters planned
  under PLUG-10. See [Convolver's
  window](docs/use/plugins.md#convolvers-window).
- Plug-ins (PLUG-44): for hosts that show only their own controls, Status
  ends with the gain that Estimate or No clipping plays (`Estimate plays
  -7.3 dB.`); Latency values read `0 frames (none)` for 0 in every format
  and, in VST3 and the Audio Unit once the host has started the plug-in,
  `256 frames (5.3 ms)`, and a VST3 host that types a
  Latency must give one of the listed frame counts; the VST3 parameters
  have short names (Gain, Mode, Mix, Lat, Bypass, Status); and LV2's
  latency output is named Reported latency, so hosts no longer show two
  controls called Latency. See [settings](docs/use/plugins.md#settings).
- Packages (REL-10): the `echo` example presets and filters now sound like an
  echo: each sound comes back three times, 0.3 s apart, each repeat 6 dB
  quieter, where before a single copy 5 ms later only made the tone hollow.
  The three examples (identity, echo and repeat) now sound clearly different
  and show differently on a level meter. See
  [the plug-in examples](examples/plugin-presets/README.md).
- Plug-ins (DIAG-10): the VST3 and Audio Unit Status text also says when a
  setting does not play as chosen: a Latency not in use yet (`Latency 1024
  frames not in use yet.`), Estimate without an estimate for the filter
  (`No estimate: No clipping's gain plays.`, or `Too loud to estimate: …`)
  and a filter with no gain figure (`Silent: no gain figure for this
  filter.`, or `… only the dry signal plays.` where Mix lets it through),
  as the window's report does. LV2's `status` port keeps its
  five values (jrp, 21 November 2026): while a new Latency is applied or
  Estimate's figure is found the filter plays on and the port stays at
  Active, and the host's log now says so before saying what happened.

- Plug-ins (PLUG-39 step 6, which finishes PLUG-39): the latency and tail
  the host reads are written in one place in each of VST3 and the Audio
  Unit, from the newest processor or, with none, from the accepted filter
  (`HostFilter::filter_tail()`, which replaces `HostChange::inactive_tail`).
  One reading changes, while no audio runs: once the host has stopped the
  plug-in, the tail the host reads and the window's Ring-out row are the
  accepted filter's, where they stayed at the longest filter the last
  processor had played (or at 0 for a filter that did not suit the host).
  `vst3.adapter`'s `edge_cases` and `au.adapter`'s `timing_in_seconds`
  check it. Audio, saved state and every other message are unchanged.
- Library (PLUG-39 step 6), in 0.18.0: `HostChange::inactive_tail` is
  removed; `HostFilter::filter_tail()` gives the accepted filter's tail
  instead, found when the filter is accepted. An incompatible API change,
  so a minor version step. The
  [plan](docs/archive/plans/2026-11-13-load-restore-consolidation.md) is
  archived with its recount.
- Documentation (HOST-10): the plug-in guide has an Ardour section, by the
  track's Pin Mode (tried with Ardour 8.4 and 9.0). On a Strict-I/O track,
  Ardour's default, a mono or stereo filter plays in the VST3 and the LV2
  plug-in, but a crossover cannot (the track keeps its channel count). On a
  Flexible-I/O track, the LV2 plug-in plays its mono, stereo and 2→6
  layouts, while the VST3 plug-in gets 2 inputs and 2 outputs whatever the
  track has, so a mono or crossover filter plays silence; the guide gives
  Pin Connections steps that should let a mono filter play there (the same
  settings, made from a script, played it; a filter chosen before the
  channels change plays once they do, since PLUG-42), and a crossover
  cannot be set up that way. Ardour also leaves Gain mode and Latency out of its controls
  for the VST3 plug-in, and names all four of the LV2 plug-in's settings.
  The LV2 ports table now lists `gain_mode`.
- Plug-ins (PLUG-40, from jrp's Audacity pass of 21 November 2026): a
  configuration list chosen in Convolver's window before the host has
  played is no longer something to choose again. With the window open, it
  loads by itself, as if chosen, when the host plays, and the project is
  marked changed (it is not added to Recent). Checked by automated tests,
  not yet in a host. Once Convolver has seen the host play, Status and the
  window say "The host's settings are now known (48000 Hz, 2 in, 2 out):
  choose the configuration list again." (Status in short) instead of still
  saying the settings are unknown; a brief start without playing, such as
  Audacity's after a parameter edit, does not count. The window no longer
  suggests dropping a file beside any refusal. VST3 and the Audio Unit; the
  Audio Unit's part is checked only on macOS CI
  ([when a chosen file is checked](docs/use/plugins.md#loading-a-filter)).
- Documentation (VST3-17): in Audacity, Convolver's **Choose…** and
  **Recent** are now checked routes alongside Audacity's preset menu. jrp
  checked the 0.16.14 candidate in Audacity 4.0.2 on macOS on 21 November
  2026: a filter chosen before playing was in the export with the window
  open and, after saving, quitting and reopening, in the next export; a
  configuration list chosen after one playback took its 48 kHz entry
  ([record](docs/archive/reviews/2026-11-21-vst3-17-audacity-pass.md)).
  The guides, status and the release notes' text say so; **Reload**,
  reloading when the file changes and a `.vstpreset` chosen in the window
  remain checked only by automated tests.
- Library and tools (PLUG-39 step 5b): exceptions keep their message's
  parts (problem, cause, fix, note) typed beside their text, and every catch
  block reads the parts rather than parsing the "Cause: ", "Fix: " and
  "Note: " lines back. The exception types and their `what()` text are
  unchanged, so nothing a user reads changes. `HostFilter::fail_activation()`
  gains an overload taking the exception, which keeps only its problem as
  the reason, and the string form is documented to take the reason as a
  phrase. One LV2 log line differs, and only when memory runs out twice:
  when recording an activation failure itself runs out of memory and an
  earlier load failure had been recorded without its message, the log now
  says that memory ran out before Convolver could record why, where it
  logged nothing.
- Plug-ins (PLUG-39 step 5a): the window's report travels as one typed
  value. `encode_report()` and `decode_report()` in
  `portable/host_report.hpp` encode it once for the VST3 message between the
  processor and the controller and for the Audio Unit's report property
  (64000, now a `CFData` instead of a dictionary of strings), and read it
  back whole or not at all; the windows no longer parse tab-separated
  lines back. The VST3 controller derives the Status text and Copy
  diagnostics from the report it holds rather than keeping copies.
  `report_fields()` is removed, and `HostStatusLine` gains `operator==`.
  Nothing a user sees or hears changes.
- Library (PLUG-39 step 4a), incompatible, hence 0.17.0: the accepted
  filter is shared rather than copied, as jrp chose on 20 November 2026.
  `HostFilter::filter()` returns a `std::shared_ptr<const Configuration>`
  (null while no filter is accepted) instead of a
  `std::optional<Configuration>`, and `HostPlayback::filter` is the same
  shared, immutable filter, so activation and a copy of the `HostFilter`
  no longer copy the filter's samples; `filter_playback()` also takes the
  shared filter. Output and saved state are unchanged.
- Audio Unit (PLUG-39 step 4a, PLUG-30): the Status and the window's report
  are built from one diagnostics snapshot that every control change
  publishes whole, together with the Gain, Mix, bypass, latency and tail
  set when they are read. The report cache and the copies of the status
  are gone, so the window no longer waits for a load or the Latency work
  when it had no earlier report, and the report follows a bypass switched
  while that work holds the control lock. The contract lists every call
  that does not wait for the control lock, with a held-lock test for each.
- Plug-ins (PLUG-39 step 1b): in LV2, choosing the file of the filter
  already playing again keeps it and what it is playing, so its tail rings
  on, as choosing it again in the VST3 and Audio Unit windows already did
  (jrp's choice, 13 November 2026). The decoded filter decides, never the
  file name: a file of the same name whose contents changed loads as before.
- Library (PLUG-39 step 1b), incompatible, hence 0.16.0: `HostFilter::load()` decides
  what an accepted request changes besides the filter and returns it as a
  `HostChange` (the settings to apply, restored state's bypass, and the
  filter's tail while no processor plays), from the request's new `kind`
  (`HostRequestKind`: restored state, a session the host is opening, which
  replaces even the filter already playing, or a file chosen in the
  plug-in). `set_estimating()` is removed: a load whose state holds settings
  finds the estimate their Gain mode needs, and a refused one leaves
  `estimating()` as it was. `fail_request()` takes the processor that plays
  and silences it after a failed first load, and `HostProcessor::play()`
  keeps the processor or replaces it, as an install callback needs.
- Library (ROBUST-8), incompatible, hence 0.15.0: `HostFilter::activate(const
  HostLayout&)` is split in two. `activation()` returns a `HostActivation`
  (what to play, as `playback`, and the failure and notice it would record)
  and changes nothing; `activate(HostActivation&&)` records it and cannot
  fail. Build the processor from `activation().playback`, then call
  `activate()` once nothing can stop that processor from playing.

- Builds (CI-43): the Mac preview packages are built with the newest Xcode
  on GitHub's macOS 26 images (Xcode 26.6 today, was 16.4 on macOS 15), still
  for macOS 14 and later, and the Linux packages with GCC 12 (was 11), still
  on Ubuntu 22.04 for glibc 2.35. CI tests with the newest GCC and Xcode the
  images have, the newest LLVM release branch (Clang 23 today, was 21) and the newest
  stable Python (3.15, was 3.12); the Audio Unit job runs on macOS 26, and the
  LV2 tests run on the newest Ubuntu image with its lilv (the Ardour test
  stays on Ubuntu 24.04 until it works with Ardour 9).
  Output may differ within the tested tolerance. Contributor guidance now
  says to keep every external dependency, compilers and CI machines
  included, on its newest formal release
  ([compilers and CI machines](docs/develop/build.md#compilers-and-ci-machines)).
- Plug-ins (PLUG-39 step 4): with bypass on, the window's report and the
  Convolver log say what bypass plays instead of saying that the filter
  plays: a fault reads "Bypass is on: the input plays unfiltered." instead
  of "The filter keeps playing.", a refused replacement "The previous
  filter is kept. Bypass is on: the input plays unfiltered.", and the start
  log line "Bypass is on: the input plays unfiltered. Loaded: …", as Status
  has done since PLUG-36. VST3's log line for restored state kept until the
  host suits it says what plays meanwhile (silence, Mix's dry share or
  bypass) instead of always "plays silence", and the log says a list entry
  chosen for a wider bus "is used" rather than "plays". A fault counted
  before the host stopped, with bypass off, reads "The filter is kept and
  plays when the host starts again." instead of "The filter keeps
  playing.", since no processor runs then, even when the host's rate and
  channels stay known after it stopped (`HostReportFacts::running`, from
  jrp's review). The audio is unchanged.
- Plug-ins and Library (PLUG-39 step 4b): one rule in the library shows a
  filter's saved name only while that filter plays (`HostFilter::offer_name()`).
  VST3 offers the name from its controller state there instead of keeping
  two name pairs in the processor and two in the controller, and the report
  gains `filter saved name`, which the VST3 controller state saves. A
  refused preset still never renames the playing filter. The Audio Unit
  now names restored state that brings no name of its own (an "Untitled"
  preset) after the same filter's earlier file or preset name instead of
  "Restored filter", as VST3 already did. One rare VST3 order shows
  "Restored filter" where it showed the name before: a refused preset, then
  another preset, then the first filter's component state alone with no
  controller state, since only the last offered name is kept.
- Plug-ins and Library (PLUG-39 step 4c): one function,
  `host_status_value()`, orders a plug-in's status (a load failure, then a
  fault, then Bypassed, then the filter, then a failed load, then no
  filter). `host_status()`, the window's report and LV2's status port all
  follow it instead of ordering the record again. Nothing shown changes.
- Audio Unit (PLUG-39 step 3c): as in VST3, a new Latency and
  uninitializing no longer wait for `Render` to let go of the processor
  they replace; the next control call frees it. Nothing heard or reported
  changes.
- VST3 plug-in (PLUG-39 step 3b): choosing a new Latency while audio plays
  no longer holds the host's control call until the audio thread finishes
  its block. The new processor is offered and reported at once, as before;
  the one it replaces is freed by the plug-in's next control call once the
  audio thread has let it go. What plays, the latency reported and the
  pause while the new processor fills are unchanged.

### Fixed

- Plug-ins (PLUG-47, jrp's reviews of 21 and 22 November 2026): a
  configuration list chosen in Convolver's window before the host had
  played, which Convolver then loads by itself once the host plays with the
  window open (PLUG-40), now joins Recent when it loads, as a chosen file
  does, also when the window was closed and opened again meanwhile.
  Before, it was never added. See
  [loading a filter](docs/use/plugins.md#loading-a-filter).
- Plug-ins (PLUG-46, jrp's review of 21 November 2026): while the host has
  the plug-in stopped, the tail it reports and the window's Ring-out are the
  filter's own length when the filter's sample rate differs from the host's.
  Before, the filter's frames were counted at the host's rate: a 48 kHz
  filter with a 1 s tail, after the host started the Audio Unit at 96 kHz
  and stopped it, reported a 0.5 s tail and the window showed 500.0 ms. The
  VST3 plug-in now gives that tail in the host's samples at its last rate,
  rounded up. See the
  [adapter contract](docs/develop/plugin-contract.md).
- Audio Unit (AU-19, jrp's reviews of 21 and 22 November 2026): a Latency
  changed while the host has the plug-in uninitialized no longer leaves
  Status and the window saying it is "not in use yet". Uninitialized, the
  choice is selected for the next start, as the VST3 plug-in already said,
  and the window is told once the change is applied. Until then, for a
  moment, the latency the host reads can still be the previous one. In
  both formats, the window's Latency row now says "selected for the next
  start" while the host has the plug-in stopped. See
  [Convolver's window](docs/use/plugins.md#convolvers-window).
- Plug-ins (PLUG-42, jrp's decision of 21 November 2026): in the VST3 and
  Audio Unit plug-ins, a filter that does not suit the host's sample rate or
  channels, as a preset, an IR file or a `.cfg`, is refused only while
  another filter that suits the host is playing. With nothing playing it is
  kept and saved with the project, plays silence with the reason in Status,
  and plays when the host next starts the plug-in at its rate and with its
  channels; the log says it waits for audio settings that suit it. Until
  then Mix's dry share and bypass pass the input where the host's and the
  filter's counts are all equal, as for any filter kept that way, where a
  refused first filter played silence. A filter or the input that played
  before it mutes within 1.3 ms; a filter kept that way before is replaced
  without a gap in the dry share or bypass where the new one passes them
  too. Before, it was refused and
  the project saved the failure: in Ardour 8.4, a filter chosen just
  before Ardour restarted the plug-in with the outputs set in Pin
  Connections was refused and the export was silent. A configuration list with no entry for the host is still
  refused. A filter refused while another plays stays refused, and the
  refusal now says the filter Convolver has stays selected when the host
  restarts the plug-in, even with settings that suit the refused one. The
  LV2 plug-in, whose settings never change, refuses as before.
- Plug-ins (PLUG-41, from jrp's review of 21 November 2026): a filter file
  chosen in Convolver's window waits for audio in the current start in both
  formats. The VST3 plug-in refused a file chosen just after the host
  stopped and started again with the same settings, before any audio of
  that start, from audio played in the earlier start. The Audio Unit
  counted a render of zero frames as the host playing, so after one it
  refused a chosen file that should wait, and refused restoring a 48 kHz
  project after a 44.1 kHz probe (error -10851). No change to the sound.
- Plug-ins (PLUG-27): a filter file chosen in Convolver's window before the
  host plays is kept and saved with the project, and checked when the host
  plays audio through the plug-in; until then Status says "Filter selected.
  Waiting for the host's audio settings." rather than judging it against a
  brief start at a placeholder rate. In Audacity 4.0.2, which sets up the
  plug-in at 44.1 kHz until it first plays whatever the project's rate, a
  48 kHz filter chosen before playback was refused, and the project then
  saved that failure, so exports were dry or silent (jrp's run of 0.14.0,
  20 November 2026, [record](docs/archive/reviews/2026-11-20-vst3-17-audacity.md)).
  A configuration list needs the host's rate and channels to choose its
  entry: before the host has started the plug-in it is not loaded, and the
  window says to play audio once, then choose the list again; a list with
  no entry for the rate the host last ran at is refused naming it, with
  the same step added to the advice while the host is not playing. While the
  host plays, a file that does not suit it is still refused and the working
  filter keeps playing. VST3 and the Audio Unit
  ([when a chosen file is checked](docs/use/plugins.md#loading-a-filter));
  checked in Audacity 4.0.2 on macOS with the 0.16.14 candidate (jrp, 21
  November 2026, [record](docs/archive/reviews/2026-11-21-vst3-17-audacity-pass.md)):
  Status said it was waiting, both exports had the filter, and a list
  chosen after one playback took its 48 kHz entry. The Audio Unit's part
  is checked only by automated tests.
- Documentation (DOC-18, from an outside audit of 20 November 2026): the
  user guides no longer contradict the plug-ins. **Choose…** opens a
  Convolver `.vstpreset` (the first-sound guide said it read no presets);
  the concepts page describes the non-uniform engine of 0.13.0 and lists
  Gain mode among the settings; the pages that named only macOS for
  Convolver's window include the VST3 window on Windows, with its Copy
  Diagnostics for a problem report; the package list names the Linux LV2
  plug-in, and presets and the plug-ins table name Gain mode; and TODO IDs
  no longer appear in the user guides.
- Plug-ins (PLUG-36): while bypassed, Status and the window's report say
  what bypass plays when no filter is heard. A kept filter that does not
  suit the host (another sample rate, say) showed "Silent:" while bypass
  passed the input; it now shows "Load failed. Bypass is on: only the dry
  signal plays." and the reason. With no filter loaded and Gain or Mix
  changed, Status gave the level they set, though bypass plays the input
  unchanged; it now says "No filter. Bypass is on: audio passes
  unchanged." Where bypass plays silence (after a failed load with no
  filter kept, or with unequal counts) both still say so. The audio is
  unchanged. The plug-in contract now lists each
  loading state against every request and host event, with its tests.

- Plug-ins (ROBUST-8): when a new Latency cannot be applied because memory
  runs out, the status and the window's report stay as they were. Before,
  Status could show Fault again for faults a later filter load had cleared,
  and, if memory ran out while the filter was checked against the host,
  read Active over a filter playing silence. Now a new start is recorded
  only once its processor plays. What plays is unchanged.

### For contributors

- Tests (PLUG-47): `vst3.adapter` `list_read_again` and `au.editor`
  `list_read_again` check Recent before and after the list is read again;
  the Audio Unit test chooses the list in a window that then closes.
- Tests (PLUG-46): `au.adapter` `timing_in_seconds` follows jrp's sequence
  (a 48 kHz filter, the host's rate changed to 96 kHz, a start and a stop);
  `vst3.adapter` `edge_cases` checks the inactive tail at 44.1 and 96 kHz;
  `native.host_report` checks Ring-out at the filter's rate.
- Tests (AU-19): `au.adapter` `timing_in_seconds` changes the Latency while
  uninitialized and checks the Status text before and after the Latency
  work, and that the window is told; `native.host_report` and
  `native.window_model` check the report's `latency_next_start` and the
  Latency row's note.
- Host layer (PLUG-44): `HostFilter::waits_for_settings()` (Kept, unsuited
  with no refused request) gives the Waiting value; `latency_value_text()`
  in `host_report.hpp` names a Latency for every format. Tests in
  `native.host`, `native.host_report`, `native.window_model`,
  `vst3.adapter`, `au.adapter` and the macOS window tests follow the new
  text ([contract: status and log](docs/develop/plugin-contract.md#status-and-log)).
- Host testing (DOC-22): the Mac walkthrough no longer assumes Latency
  starts at 256. GarageBand's settings step sets 256 and checks it is in use
  before the change to 1024, and REAPER's log line reads `from 0 to 1024`.
  Audacity's configuration-list check loops for at least two seconds, so
  the 85 ms example cannot end before the window sees the host's settings,
  and keeps the list selected for the export. The GarageBand preset steps
  delete each old preset first, so one left from an earlier preview cannot
  stand in under the same name, and the Latency checks name the menu value,
  not the detail beside it, which shows the time added. jrp found all three in his 21 November
  review.
- Tests (REL-10): the package probe checks every tap of each example against
  its own independent list, so it follows the echo's four taps.
- Packages (REL-10): `examples.json` gives each example's latency as 0, the
  default since PERF-2, where it said 256. The echo examples are longer
  (0.9 s), so the generated `presets/` folder grows from about 1.2 MB to
  5.2 MB, and each future upgrade capture (`tests/upgrade/X.Y.Z/`) to about
  13 to 15 MB, from 4.4 MB for 0.14.0.
- CI (CI-52): the fuzz and version jobs have time limits, so a hang stops
  instead of running for GitHub's six hours. The fuzz job stops after 45
  minutes (the Monday run's four 600-second campaigns, two at a time, take
  about 20 after the build); a manual fuzz run sets its own length, so it
  keeps the six hours. The version job stops after 10 minutes.
- Releases (REL-3): the draft release's notes describe the seventh
  unsigned developer preview, made for jrp's live host testing and the
  independent walkthrough: `PREVIOUS_PREVIEW` is 0.14.0, and `CHANGES` in
  `scripts/release/prepare_release.py` lists in users' words what changed
  since (a filter chosen before the host plays is kept, a configuration
  list loads by itself once the host plays, a filter that does not suit the
  host waits for a start that suits it, Status names a setting not in use,
  the tail after a stop, bypass wording, and the new Ardour and limits
  guides). The known limitations add the host checks not repeated on this
  version, Ardour's VST3 channels, and the features new since 0.14.0 that
  only automated tests have checked
  ([releasing](docs/develop/releasing.md#publishing-an-unsigned-preview)).
- Records (DOC-20): pull requests no longer edit the top of `CHANGELOG.md`,
  TODO's done list, the decisions log or `VERSION`, the places where two
  open pull requests used to conflict whenever one merged. Each adds its
  own file, `changes/<ID>.md`, with its changelog entries, its finished
  item's done line and any decision of jrp's
  ([form](changes/README.md#the-form)), and `scripts/fold_changes.py` folds
  the fragments into those pages, in the order they merged, and deletes
  them: in the release-notes pull request before each preview, or sooner
  in a pull request of its own. Pre-commit refuses a fragment that could
  not be folded, and the repository checks count an ID ticked in one as
  done. A minor or major version step is now a `version:minor` or
  `version:major` label on the pull request, which the version workflow
  reads through the GitHub CLI on merge; an explicit `VERSION` edit is
  still honoured. A new rule clears whatever a change has processed:
  folded fragments and scratch files are deleted, finished plans and dated
  evidence archived
  ([change fragments](docs/develop/workflow.md#change-fragments),
  [clearing processed records](docs/develop/workflow.md#clearing-processed-records),
  [source versioning](docs/develop/versioning.md#minor-and-major-versions)).
  Pre-commit now also runs the versioning tests when the version helper
  or its tests change, as CTest's `versioning` test already did. A pull
  request that changes only the record checks, the fragment folder or their
  tests now runs Lint alone, and one that changes the version helper adds
  only the native workflow, instead of every plug-in workflow
  ([CI triggers](docs/develop/testing.md#ci-triggers)).
- Code (PLUG-24): `HostProcessor`, the plug-ins' audio path, moved out of
  `portable/host.cpp` into `portable/host_processor.cpp` unchanged; the
  gain, Mix and dry-signal rules it shares with `HostFilter` are in
  `portable/host_common.hpp`. Public APIs, saved state, messages and audio
  are the same.
- CI (CI-51): the Audio Unit workflow's two
  sanitizer jobs, the AU tests with ASan and UBSan and the fallbacks, and
  `au.adapter` under ThreadSanitizer, run as steps of its `au` job after the
  Release build and tests, each in its own build folder and even when an
  earlier part failed. A pull request that changes the core takes three of
  the account's five macOS runners instead of five, or five instead of seven
  with the candidates, and every test still runs
  ([CI triggers](docs/develop/testing.md#ci-triggers)).
- Tests (CI-50): `vst3.lifecycle` has a 600-second CTest limit, as
  `au.lifecycle` and `lv2.lifecycle` have, instead of 300. The Intel macOS
  candidate took 124 to 221 seconds in ten passing runs on 20 and 21
  November 2026 and timed out at 300 on a slow runner (run 44934007168).
- CI (CI-49, from an outside CI analysis jrp passed on on 21 November
  2026): pull requests no longer run the advisory **Newest upstream lilv**
  job, which builds the newest lilv, zix, serd, sord and sratom from source;
  like the advisory pluginval pre-release (CI-38) it still runs on pushes to
  `main`, the weekly and manual runs
  ([pre-releases](docs/develop/build.md#pre-releases-and-newer-upstream-versions));
  `tests/ci_scope_tests.py` accepts that extra condition only for the
  advisory jobs it lists, and requires it of them. The Monday runs with macOS jobs now start half an hour apart
  (candidates 06:23, Audio Unit 06:53, VST3 07:23 UTC) instead of together,
  so they no longer queue for the same five macOS runners. The analysis's other
  proposals were not taken: the Accelerate and Ooura builds keep the
  package consumer tests, because the installed package's configuration
  differs for each FFT backend and those builds are the only check of the
  Accelerate and Ooura packages; the Ardour and LV2 tools jobs stay without
  a compiler cache (builds of 17 to 35 s, never the last job, and the
  repository's cache space is shared).
- Tests (HOST-10): the headless Ardour test plays the mono and stereo
  examples on Strict-I/O tracks too, as Ardour's window adds by default
  (`CONVOLVER_STRICT_IO`), and the mono example in the VST3 plug-in on a
  Flexible-I/O track with its channels set by hand, as the Pin Connections
  window's Manual Config does: outputs only (all Ardour 8's window can set
  for a VST3) and outputs and input pins (Ardour 9's). It prints the
  track's Pin Mode, Ardour's
  natural streams, the insert's channels and the name Ardour gives each
  input parameter, the layout lines the Convolver log gained in each case,
  and the whole log when an export does not match the oracle. After
  setting the channels it waits until the log says the plug-in started
  with them: Ardour 8.4 restarts the plug-in a little later, and a preset
  loaded before that was refused and saved as a failure (PLUG-42).
- Benchmarks (PERF-3): `tests/native/benchmarks/workloads.py` measures
  callback times, peak memory and preparation for long and multichannel
  filters through `HostProcessor`, and the manual Workload measurements
  workflow runs it on GitHub's Linux, macOS arm64 and Windows runners. The
  first runs (21 November 2026) are in [FFT measurements](docs/develop/fft-measurements.md#supported-workloads):
  no supported workload needs FFT-2, PLUG-11 or EDGE-11's other candidates;
  cost grows in proportion to length beyond about 30 s, the first target for
  PERF-2's tuning; VST3-5 may be needed for 60 s and large 96 kHz
  multichannel filters. No shipped code changed.
- Tests (COMPAT-1, in progress): LV2 state saved by the published 0.14.0's
  own LV2 plug-in is kept in `tests/upgrade/0.14.0/lv2-state/`: for each of
  the 21 cases, a session lilv saved after the plug-in chose the case's
  filter through its filter parameter, and two more with Gain, Mix, Latency
  and Gain mode away from their defaults. The new `lv2.upgrade` restores
  each through lilv, ports included, in today's bundle and checks the
  reported latency and the render against the release's own (within the
  `upgrade.cli` bound; identical where built as the release was). The
  release's plug-in rendered every default-settings case exactly as its CLI
  did. `capture_lv2.py` captures later releases. The 0.14.0 macOS arm64
  Audio Unit's state is kept too (`au-state/`): for the same cases and
  settings, the `ClassInfo` the release's unit gave after restoring an
  `.aupreset` from the release's `convolver-preset` and taking the
  settings. The Audio Unit workflow restores each in today's unit through
  AVAudioEngine (`run_upgrade.py --au-render`) and checks the latency and
  the render within the bound; `capture_au.py` captures later releases,
  and the AVAudioEngine renderer gains `--parameter` and `--save` for it.
  Changes to `tests/upgrade/` now run the LV2 and Audio Unit workflows too.
  The Audio Unit check requires every sample before the reported latency
  to be finite; it had checked only their largest value, which a NaN
  passes, before dropping them. `tests/upgrade/harness_tests.py` (in
  pre-commit) feeds it a NaN, an infinity and sound there. `capture_au.py`
  no longer uses `str.removesuffix`, which needs Python 3.9.
- Tests (HOST-11): `scripts/reaper_check.py` accepts a Status reading only
  when it is exactly the stereo example's Active text, `Filter loaded: 48000
  Hz, 2 in, 2 out.`. From jrp's review of 21 November 2026: the waiting text
  ("; waiting for audio settings from the host.") and the Mix 0% text start
  the same way, and passed checks 3 and 7 before.
- Tests (HOST-11): `scripts/reaper_check.py` repeats the project's first
  seconds while REAPER plays, so that Status is read with the transport
  running, and counts that reading only when REAPER reports it running; in jrp's first run, on 21 November 2026, REAPER had
  stopped at the end of the 85 ms example before the reading. Checks 3
  and 7 now need that reading too, and track and master pan laws are set to
  0 dB. The first run passed all eight checks on the 0.16.14 candidate.
- Tests (HOST-11, first version): `scripts/reaper_check.py` runs part of
  the REAPER host check from one command on a Mac. REAPER renders the
  stereo example through its `.vstpreset`, saves, quits, reopens the
  project in a new process and renders again; both renders are compared
  with `convolver --compare`, and one `result.md` holds what a host report
  needs. It needs REAPER, so no workflow runs it; its tests run in
  pre-commit against a stand-in REAPER.
- Tests (HOST-9): the headless Ardour test plays the VST3 plug-in too, in
  the VST3 workflow, on Ardour's own VST3 host, and saves sessions at a
  larger Latency (LV2 at 256 and 1024, VST3 at 1024): Ardour must be told
  the new latency without reopening the session, report it again after
  reopening, and export in time, within 1e-5 of the oracle. The test moved
  to `tests/plugins/`.
- Tests (AU-18): the AVAudioEngine render of the Audio Unit also follows
  the hosts' other orders of events: GarageBand's, which starts the unit at
  its defaults and restores the project while it runs (at 44.1 kHz on a
  mono track, and with a Latency chosen after it); a Latency chosen while
  the engine runs, which the host's property listener must hear of; and a
  project's round trip into a second unit through `ClassInfo`. Each must
  report the expected latency and match the oracle within 1e-5.
- Builds (CI-48): the Linux package, built with GCC 12.3 on Ubuntu 22.04,
  builds without warnings from Convolver's own sources (libsndfile's and the
  VST3 SDK's remain theirs). That compiler wrongly warned `-Wrestrict` at nine
  places in the core and the tests that put text in front of a temporary
  string; they now build the string front to back. No output changes.
- Builds (CI-47): the Audio Unit component's own sources (its factory and
  the window it shares with the VST3 plug-in) and the Audio Unit and LV2
  tests build with `-Wall -Wextra -Wpedantic`, as everything else now does;
  what they found is fixed in the code. No output changes.
- Builds (CI-46, from an outside audit of 20 November 2026): the VST3
  plug-in and the programs that compile its sources build with the warnings
  the core, the Audio Unit and the LV2 plug-in already had
  (`-Wall -Wextra -Wpedantic`, or `/W4 /permissive- /utf-8` with MSVC); the
  SDK's headers are system headers. Everything they found is fixed in the
  code, none silenced: on Windows, a bounded copy of the window's font name,
  the accessibility IDs passed as the type the interface takes, a copied
  environment variable in the tests, shadowed names, and compile-time
  branches for `convolver-vst3-preset`'s differences; on macOS, standard
  `std::min`/`std::max` and a full conditional in the window instead of GNU
  extensions; on Linux, an unused variable in `host_report.cpp`, a
  signed/unsigned comparison and two partly initialised timing results in
  the VST3 tests, and an unused test helper. No output changes.
- Records (DOC-19, from an outside audit jrp accepted): `TODO.md`
  holds open work only, each item in a line or two, and its documented
  limitations one line each, pointing at the page that owns the detail; the
  48 finished items and the items' earlier text moved unchanged to
  `docs/archive/todo/`, and PLUG-39's step 1b is no longer listed as both in
  progress and done. The [status page](docs/project/status.md) is rewritten
  for 0.14.0, with the first preview's gates in
  `docs/archive/releases/`; the [decisions log](docs/project/decisions.md)
  is a list of dated entries, with those later decisions wholly replaced
  archived beside it; and each published release in this file lists what
  users see first and contributor-only entries under **For contributors**,
  none removed.
- Tests (COMPAT-1, in progress): the example presets of the published
  0.14.0 and the audio its own CLI and VST3 plug-in rendered from them are
  kept in `tests/upgrade/0.14.0/`, beside those of 0.13.2, 0.13.12,
  0.13.20 and 0.13.24; `upgrade.cli` and `vst3.upgrade` check later builds against all
  five. Every preset, filter, input and expected render matches 0.13.24's
  byte for byte: EDGE-11's change of a -0 sample to +0 did not occur in
  these renders.
- Tests (HOST-7): the headless Ardour test runs Ardour 9 on the newest
  Ubuntu image instead of Ardour 8.4 on Ubuntu 24.04. Its session script
  gives Ardour 9 the empty route group it expects (`nil` crashed it), and
  the test accepts Ardour 9.0's known abort as `new_session` exits once the
  session is written (that version and message only). Every first-use
  example matches the oracle (worst error 3.42e-8).
- Builds (CI-45): Convolver's own sources build without the warnings GCC 12
  to 15, Xcode 26.6 and Visual Studio 2022 gave in CI. No output changes.
- Releases (CI-44): the Linux candidate's check refuses libstdc++ symbol
  versions newer than GCC 12's (GLIBCXX_3.4.30, CXXABI_1.3.13), as it
  already refused glibc ones newer than 2.35: a newer GCC on the build
  image now fails the candidate instead of shipping a package that does not
  load on Ubuntu 22.04 or Debian 12.
- Plug-ins (PLUG-39 step 2): VST3 and the Audio Unit decide when the
  host has settled its sample rate and channels in one shared record
  (`HostSettling`), which each feeds with the host's starts, stops and
  played audio, instead of in fields of their own. Each format keeps its
  rule: VST3 binds a start and refuses restored state only once audio
  has played; the Audio Unit binds an initialization only once it has
  rendered. Nothing heard, saved or reported changes.
- CI (CI-42, from jrp's audit of v0.14.0): a pull request that changes
  only `tests/upgrade/`, the files kept from published releases and their
  checks, runs the native and VST3 workflows, whose `upgrade.cli` and
  `vst3.upgrade` read them, instead of every workflow: the Audio Unit and
  LV2 builds no longer run for it. The Audio Unit job's run of
  `upgrade.cli`, the only Release Accelerate one with the Xcode of
  `macos-15` (the macOS candidates' image), is left to `main`; the VST3
  macOS job (newer Xcode) and the native macOS builds still run it against
  the same bound. The saving was not measured
  ([CI triggers](docs/develop/testing.md#pull-requests-for-one-plug-in-format)).

- Benchmarks (PERF-20, from jrp's audit of v0.14.0):
  `tests/native/benchmarks/compare_engines.py` refused to compare two
  revisions whose `portable/engine.cpp` was the same, so a change confined to
  the partitioner, the kernels or an FFT backend could not be measured
  against its baseline. It now compares any two commits, warns when nothing
  under `portable/` or `include/`, nor the CMake files or the benchmark's
  source, differs between them (the warning asks to check the latency
  settings and benchmark changes when reading the timings), and refuses
  the same commit before building either revision instead of after
  building both.

## [0.14.0] - 2026-11-19

The sixth published preview, for macOS, Windows and Linux, unsigned:
quicker restores of the playing filter, less CPU once the input has been
silent past the tail, the command-line tool's clipping report and clearer
messages for a sample-rate refusal and a `.cfg` file name that is not UTF-8
([release](https://github.com/jrp2014/convolver/releases/tag/v0.14.0)).

### Added

- Engine (EDGE-11, first candidate): once the input has been exactly
  silent for longer than anything can stay in the engine (the latency, the
  filter and its delays, plus an additional interval determined by the
  partition layout; approximately half a second or less in the measured
  48 kHz workloads), each further silent callback writes zeros without
  convolving. The output stays bit for
  bit what processing the silence gives, and the next sound resumes at
  once. The saving applies only to silence after the tail has ended, not
  to normal playback: on the Linux cloud VM such silence costs 93 to 99%
  less CPU (11 s stereo reverb: 173 to 2.2 ns per frame), and music costs
  the same as before within the measurements' spread
  ([measurements](docs/develop/fft-measurements.md#idle-after-the-tail)).
  Only Linux was measured; Apple silicon and Windows are not yet.
  `convolver_benchmark` gained `--silent` to time it. So that this holds
  exactly, the engine never outputs -0: where sound cancelled to a value
  too small for a normal float, the frame used to come out as -0 and is now
  +0. The two are numerically equal and inaudible, but those output bits
  differ from earlier versions.
- CLI (DIAG-5, from the BruteFIR comparison): after a render in which any
  sample went beyond full scale, `convolver` warns on stderr with each
  affected channel's clipped-sample count and peak in dBFS, and suggests
  `--gain-db no-clip` or how far to lower `--gain-db`. Before, `--gain-db 20`
  could clip most of a 24-bit output without a word. Float output, which
  keeps such samples, gets a note that a later conversion to integer samples
  will clip them. A clean render prints nothing, and the samples and exit
  status are unchanged. The C++ API gains `Quantizer::clipped()` and
  `AudioWriter::clipped_samples()`, which count the clipped samples of each
  channel.
- Releases: jrp published Convolver 0.13.24, the fifth unsigned developer
  preview and a patch of 0.13.20 with LV2-9's fix, on 16 November 2026
  ([release](https://github.com/jrp2014/convolver/releases/tag/v0.13.24)).
  Before publishing it he asked to move the baseline for the checks left,
  REL-3's walkthrough and COMPAT-1's REAPER project from 0.13.20 to 0.13.24
  ([decision](docs/project/decisions.md#releases)).

### Changed

- Releases: version 0.14.0, a minor step in place of the next 0.13.x
  patch, for the sixth unsigned developer preview, which jrp asked for on
  19 November 2026 ([decision](docs/project/decisions.md#releases)); C++
  consumers request `find_package(Convolver 0.14)`. Its draft notes list
  what changed for users since 0.13.24: quicker restores of a filter that
  is already playing, less CPU once the sound has stopped (measured on
  Linux only, with -0 output samples now +0), the command-line tool's
  clipping report, the Fix line of a sample-rate refusal, the error and
  inspection code for a `.cfg` file name that is not UTF-8, the plug-ins
  guide on Reload when the file changes, the wider static analysis and the
  longer weekly fuzz run. The known limitations say that the quicker
  restores and the saving once the sound has stopped are checked only by
  automated tests. After jrp's review of 19 November 2026, they also say
  that LV2-9's 100 ms wait on closing mitigates the JUCE host crash rather
  than ruling it out (also now in TODO's documented limitations), and that
  the idle starts after an interval set by the partition layout, about
  half a second or less in the measured 48 kHz workloads. No change to
  what the packages run or hold beyond the version number.
- Documentation (DOC-16, from jrp's review of 18 November 2026): the
  plug-ins guide no longer promises that Reload when the file changes never
  reads a file part-way: waiting until a change has held for a second makes
  it less likely, and the guide recommends that tools writing filters
  write each file under a temporary name and rename it into place. For
  contributors, TODO's execution priorities sort open work as in progress,
  ready, depends on, needs a decision or parked, in place of
  columns written for the freeze lifted on 10 November, with jrp's parking
  decisions kept and the items he brought forward that day shown in
  progress until they are done. jrp
  decided that day that lifting the freeze also ended the hold of
  6 November on optional features
  ([decision](docs/project/decisions.md#releases)). The
  workflow guide's verification table decides what to run when starting,
  picking up and finishing work, with results for an exact commit carried
  forward; a review's later rounds read the commits since the reviewed one;
  older CI failures that a later commit explains need only a line. The
  status page and TODO point to the changelog and decisions log instead of
  retelling each preview's publication, CONTRIBUTING no longer calls
  bundled dependencies future work, and the documentation guide says what
  a source comment should hold.
- Configurations (CFG-3, first part): a file name in a `.cfg` or list that
  is not UTF-8 text, as in a file saved in Windows-1252 (Notepad's old
  "ANSI"), now stops with an error that says so: its line, the byte that is
  wrong, the likely cause and how to save the file as UTF-8. Before, on
  Linux, it stopped with "IR file not found" and a garbled name. Its
  inspection code is the new `not-utf8`, where it was `missing-asset` (or
  `unreadable` for a list entry). On Linux a file with exactly those bytes
  in its name still loads. Convolver still reads only UTF-8; whether it should
  also read Windows-1252 is open for jrp.
- Plug-ins (PLUG-25, first part): a refusal because the filter's rate
  differs from the host's ends its Fix with the sentence jrp agreed on 8
  November 2026, "The host may play at a different rate from the one it
  exports at.", in every format and wherever the Fix is shown: the window,
  Status details and the Convolver log, or LV2's host log. Audacity, for
  one, plays at its Default sample rate while it exports at the project's.
- Plug-ins (PLUG-39 step 1a, from jrp's review of 0.13.28): a restored
  project, preset or undo step that holds the filter already accepted no
  longer analyses that filter again. Its peak bound and, under Estimate, its
  noise estimate are kept from when it was accepted, so a restore that
  changes only the settings no longer filters ten filter lengths of noise
  again: with a 10-second stereo filter under Estimate, the shared load step
  took about 590 ms before and 2 to 5 ms after (Linux, optimised build). An
  estimate is found only when Estimate is chosen and the filter has none
  yet. What plays, what is saved and the figures shown are unchanged.
- Documentation (DOC-15): the README's opening says what sets Convolver
  apart, in a "What sets it apart" section under jrp's four emphases:
  accurate (checked against an independent double-precision convolution),
  light on the system (no added delay at a measured cost), robust (the
  shared lifecycle tests, safe failure and upgrade checks) and behaves as
  you'd expect (smoothed changes, exact latency, Status and log); then
  routing, the Gain mode, measured dither defaults and free source for
  macOS, Windows and Linux. Each claim links to the guide, test or
  measurement behind it; the preview's limitations follow in their own
  section.

### Fixed

- CLI (DIAG-8, from jrp's review of 19 November 2026): the clipping report
  after a render said that a sample of +1.0 always clips, which is not so
  with dither: dither can take such a sample down within range, and jrp's
  render of 4,096 samples at +1.0 with triangular dither counted 3,556
  clipped yet still printed that sentence. It now says that +1.0 is above the
  largest representable positive integer sample, so leave headroom for
  quantization. Only the wording changed; the counts, samples and exit
  status are as before
  ([file rendering](docs/use/command-line.md#file-rendering)).

### For contributors

- Tests (COMPAT-1, in progress): the example presets of the published
  0.13.24 and the audio its own CLI and VST3 plug-in rendered from them are
  kept in `tests/upgrade/0.13.24/`, beside those of 0.13.2, 0.13.12 and
  0.13.20; `upgrade.cli` and `vst3.upgrade` check later builds against all
  four. Every preset, filter, input and expected render matches 0.13.20's
  byte for byte, as expected of a patch that changed only the LV2 plug-in.
- Documentation, for contributors (DOC-17, from jrp's edits of
  19 November 2026): the documentation guide's
  [current claims](docs/develop/documentation.md#current-claims-and-dated-evidence)
  section says how claims in the README, user guides and release notes stay
  current. The README and release notes cite a guide, test or dated
  measurement; user guides do so only for quantitative performance,
  accuracy or comparative claims. Each claim keeps its evidence's scope, is
  updated in the change that affects it and is narrowed when the evidence
  no longer supports it. The workflow's while-working
  list, the definition of done, the review checklist, the pull request
  template and AGENTS.md link to it, and the release steps check a draft's
  notes and download links against its tag and files before publishing.
- CI (CI-34, finished; chosen by jrp on 18 November 2026): a pull request
  runs the fuzz workflow only when it changes what the four fuzz targets are
  built from: their harnesses, seeds and regression inputs, the parsers,
  audio reader and state decoder with the code compiled in with them, the
  build configuration or the workflow. Pushes to `main` and manual runs
  still run it, a new Monday run gives each target 600 seconds, and every
  native build still replays the committed fuzz inputs. Each run checks
  that the list names every repository file compiled into the targets
  ([when pull requests run the fuzz targets](docs/develop/testing.md#when-pull-requests-run-the-fuzz-targets)).
- CI (CI-38, jrp's request of 16 November 2026 for safe speedups without
  restructuring): `native.engine_stress`, `native.engine` and `native.host`,
  the longest core tests under sanitizers, carry a CTest `COST`, so they
  start first
  ([test order](docs/develop/testing.md#build-targets-and-test-order)).
  Pull requests no longer run the advisory newest-pluginval pre-release in
  the VST3, Audio Unit and LV2 workflows, nor with it the macOS real-time
  check (`--rtcheck relaxed`); pushes to `main`, the weekly and manual runs
  still do, and a VST3 pull request still runs the required acceptance test
  with it ([pre-releases](docs/develop/build.md#pre-releases-and-newer-upstream-versions)).
  Parallel MSVC compiles (`CL=/MP`) were tried and dropped: no measured gain.
- CI (CI-41, from jrp's review of 19 November 2026): the load-timing
  harness's self-test, `native.load_timing_harness`, which no FFT backend
  or core-only shape changes, runs once for each compiler and build type
  in a native job instead of in every build: no longer in the Accelerate
  and Ooura builds, nor in the core-only builds of g++ and MSVC, whose
  full builds are already Release. The Clang jobs keep it in their
  core-only Release build. On `main`'s run of 19 November it ran four
  times in the macOS job (20.7 to 23.8 s each) and now runs twice, saving
  about 45 s of test time there; how much of it shortens the job depends
  on the tests running beside it. Every FFT-dependent test, backend
  comparison and plug-in timing test still runs where it did
  ([which configurations run which tests](docs/develop/testing.md#which-configurations-run-which-tests)).

- CI (CI-40, from jrp's review of 18 November 2026): clang-tidy ran on a
  hand-kept list of portable sources, repeated in CONTRIBUTING, that had
  missed `portable/partitions.cpp`. CI and the documented command now
  analyse every portable source the build compiles, chosen from its
  `compile_commands.json` with the vendored `portable/third_party/` left
  out, so a new file is covered without editing a list
  ([lint checks](CONTRIBUTING.md#formatting-and-analysis)).
- CI (CI-39): the native Windows job's two core-only steps run configure,
  build and CTest in one PowerShell script, which went on after a failed
  command, so a failed configure (leaving no tests) or a failed build of a
  target no test runs still passed the step. A failed command now fails it.
  Found by CI-38's independent review.

## [0.13.24] - 2026-11-16

The fifth published preview, a patch of 0.13.20 for macOS, Windows and
Linux, unsigned: it mitigates a host crash in the LV2 plug-in after a Gain
mode change; the VST3 plug-in, the Audio Unit and the tools are unchanged apart
from the version number ([release](https://github.com/jrp2014/convolver/releases/tag/v0.13.24)).

### Added

- Releases: jrp published Convolver 0.13.20, the fourth unsigned developer
  preview, on 16 November 2026
  ([release](https://github.com/jrp2014/convolver/releases/tag/v0.13.20)).

### Changed

- Releases: the patch preview's draft notes list what changed since 0.13.20,
  LV2-9's fix alone, and say that the VST3 plug-in, the Audio Unit and the
  tools are unchanged from 0.13.20 apart from the version number. The known
  limitations now say that the features checked only by automated tests are
  new in 0.13.20, and that it is the previews before 0.13.20 that refuse or
  play louder a project saved with Gain mode Estimate or No clipping. jrp
  asked on 16 November 2026 for this patch preview so that 0.13.20's Linux
  LV2 users get the fix ([decision](docs/project/decisions.md#releases)).
  No change to what the packages run or hold.

### Fixed

- LV2 plug-in (LV2-9): in hosts built on JUCE, pluginval among them,
  changing the Gain mode just before the host closed the plug-in (which
  JUCE's LV2 host does whenever the sample rate or block size is set) could
  crash the host. Closing now waits briefly (up to 100 ms) for a queued
  Gain mode change or fault report, as it already did for a Latency
  change; this mitigates the crash rather than ruling out every late
  call. Present since the Gain mode arrived in 0.13.20. No
  change to the sound or to saved state.

### For contributors

- Tests (COMPAT-1, in progress): the example presets of the published
  0.13.20 and the audio its own CLI and VST3 plug-in rendered from them are
  kept in `tests/upgrade/0.13.20/`, beside 0.13.2's and 0.13.12's;
  `upgrade.cli` and `vst3.upgrade` check later builds against all three.
  Every preset, filter, input and expected render matches 0.13.12's byte
  for byte, so 0.13.20 renders those presets exactly as the two earlier
  previews did.

## [0.13.20] - 2026-11-16

The fourth published preview: macOS, Windows and Linux, unsigned, adding
the Gain mode, opening a Convolver preset from the window, **Choose…** on
wider input buses and a preset or undo that keeps the playing filter's tail,
published before any of them was tried in a host
([release](https://github.com/jrp2014/convolver/releases/tag/v0.13.20)).

### Added

- Plug-ins (PLUG-10, AU-7): a **Gain mode** setting in every format.
  **Set** plays Gain as before and is the default. **Estimate** plays the
  filter's own estimate of how far to turn it down, from noise played
  through it, as `convolver --gain-db auto` does, and **No clipping**
  plays the gain at which no input within full scale can clip (up to float
  rounding), as `--gain-db no-clip` does; it is not a limiter. Each
  filter plays at its own figure, so loading another one changes the level
  to suit it, and Gain keeps its value for Set. The macOS and Windows
  windows have a Gain mode menu with the gain it plays beside it, and the
  Convolver log names the mode and the gain. Estimate takes a moment for a
  long filter (about 0.6 s for a 10-second stereo filter); until it is
  ready, the No clipping figure plays. Saved as VST3 parameter 1002 (host
  state version 4, written only when the mode is not Set, so builds before
  this one refuse only such a project), Audio Unit parameter 1002 and the
  LV2 control `gain_mode`. Set mode plays every sample exactly as before;
  the other modes match the double-precision oracle within 1.3e-7 of the
  output peak. Not yet tried in a host.
- Releases: jrp published Convolver 0.13.12, the third unsigned developer
  preview, on 11 November 2026
  ([release](https://github.com/jrp2014/convolver/releases/tag/v0.13.12)).
- Plug-ins (PLUG-26): **Choose…** and dropping a file in Convolver's window
  (the Audio Unit, and the VST3 plug-in on macOS and Windows) also open a
  Convolver `.vstpreset` from any folder, so a preset no longer has to be
  installed or imported through the host first (Audacity 4's preset menu
  cannot import one). Its filter loads as from the host's preset menu, with
  the Gain, Mix and Latency saved in it; the host's controls and saved
  project show them. A preset from `convolver-preset`, which holds only a
  filter, leaves them as they are. Another plug-in's preset or a damaged
  file is refused with a message saying what to choose instead. Not yet
  tried in a host
  ([a preset in the window](docs/use/plugins.md#a-preset-in-the-window)).
- Documentation: [system-wide filtering with PipeWire](docs/use/pipewire.md)
  sets up a Room correction device on Linux that runs the LV2 plug-in in
  PipeWire's `filter-chain`, with a filter per sample rate from a
  configuration list, and says what Convolver adds over PipeWire's own
  convolver. It needs PipeWire 1.6.2 or later: versions before 1.6 start
  the plug-in without its filter (LV2-4, in part).

### Changed

- Releases: the next preview's draft notes list what changed since 0.13.12
  (the Gain mode, a `.vstpreset` chosen in the window, settings saved at
  their defaults, **Choose…** on wider input buses, a preset or undo that
  keeps the playing filter (PLUG-38) and the PipeWire guide) and add two
  known limitations: those features are checked only by automated tests;
  and what earlier previews refuse (0.12.12 a
  VST3 project saved at Latency 0, every earlier preview one saved with
  Gain mode Estimate or No clipping). The LV2 limitation names the PipeWire
  trial. No change to what the packages run or hold.

### Fixed

- Plug-ins (PLUG-38): during playback, a preset or undo that holds the
  filter already playing, with Latency unchanged, and changes only Gain,
  Mix or the Gain mode now keeps the filter and its history in every format: the settings ramp over
  1.3 ms and a sounding tail rings on. Before, the filter restarted and
  the tail fell silent after the 5.3 ms crossfade. The filter must be the
  same in every value, not just by name. A different Latency, reopening a
  VST3 project, a reset or the host restarting the plug-in still starts
  afresh. A Gain change on
  the same filter matches the uninterrupted tail within 2.1e-7 of the
  output peak in `vst3.adapter`; the plug-in harness's `same-filter-restore`
  case, a known deviation until now, passes in every format. Not yet tried
  in a host.
- Plug-ins (PLUG-35): the VST3 plug-in now saves Gain, Mix and Latency in
  projects and presets even at their defaults, so a preset a host saved at
  0 dB, 100% and 0 frames sets them back to the defaults when chosen in the
  window, as it does from the host's preset menu; before, it left the
  current settings as a filter-only preset does. Presets saved at the
  defaults by earlier versions still leave the current settings, and
  presets `convolver-preset` makes from a filter still hold only the
  filter. Projects and presets saved now open in every published preview
  from 0.13 on; the first preview, 0.12.12, refuses those at Latency 0 (the
  default) as damaged.
- Plug-ins (PLUG-33): a file chosen with **Choose…** (or dropped on the
  window, or set through LV2's file parameter) follows the same channel rule
  as a preset: the host's outputs exactly and at least the filter's inputs.
  On REAPER's six-channel track, which gives the VST3 plug-in 6 inputs,
  choosing the 2→6 crossover's `filter.cfg` was refused with a Fix asking
  for 2 inputs while its preset played; it now plays, reading inputs 1 and
  2. From a configuration list, an entry with exactly the host's inputs
  comes first, otherwise the entry for the host's rate and outputs with the
  most inputs below the host's (jrp's
  [decision](docs/project/decisions.md#plug-in-behaviour)); the Convolver
  log (for LV2, the host's log) and the window's File, software and log
  lines name that entry and say why. With no filter, Status says "Choose a filter
  or load a Convolver preset." where the plug-in has a window (macOS, and
  VST3 on Windows); on Linux, where the VST3 plug-in has no window, the
  Convolver log's advice no longer tells users to choose a file or drop one
  on the window, and names the host's preset menu. The guides no longer send
  REAPER users to the preset for the crossover. Filters that loaded before
  load and sound the same.

### For contributors

- Tests (COMPAT-1, in progress): the example presets of the published
  0.13.12 and the audio its own CLI and VST3 plug-in rendered from them are
  kept in `tests/upgrade/0.13.12/`, beside 0.13.2's; `upgrade.cli` and
  `vst3.upgrade` check later builds against both. Every preset, filter,
  input and expected render matches 0.13.2's byte for byte, so 0.13.12 renders those presets exactly as
  0.13.2 did.
- Backlog (DOC-14, jrp's review of 12 November 2026): one consolidation
  pass over [TODO](TODO.md). Open items now say what remains, what would
  justify doing it and what counts as finished, and the progress
  narratives moved, word for word, to
  [`docs/archive/todo/`](docs/archive/todo/2026-11-12-open-item-history.md).
  REL-1 is the one production-release checklist (PLUG-5 and AU-5 are done
  for the previews); PLUG-10 is the meters, starting with the output peak
  and clipping (PLUG-9 and AU-7 are done); LV2-5 is done for Linux; PERF-3
  is one measurement exercise (PERF-1 joined it, EDGE-5 publishes it);
  EDGE-10 and EDGE-11 investigate a candidate only when a measured
  limitation justifies it; IO-2 starts from the rate-mismatch workflow
  before EDGE-1; CI-32 is dormant. Typed `Advice` errors, VST3's
  `"refused"` throw and typed report facts are notes for when that code
  next changes. The workflow guide's rule for recording progress follows.

- Plug-in contract (PLUG-37, jrp's simplification review of 12 November
  2026): a new section, [scope of each change](docs/develop/plugin-contract.md#scope-of-each-change),
  states that each change does only the work it needs. Gain, Mix, the Gain
  mode and bypass ramp on the playing filter and keep its history; names and
  diagnostics change only what is shown; only a different filter is prepared
  and crossfaded; only a different Latency or host configuration builds new
  processing; saving leaves playback alone. `native.host_settings` now
  checks that a Gain and Mix change keeps a long filter's history, latency
  and tail (`settings_keep_history`). History is kept for changes during
  playback; a session restored after a host reset starts clean. The one
  place that did not follow the rule, restored state holding the filter
  already playing, is fixed by PLUG-38 (below); the plug-in harness's new
  `same-filter-restore` case checks it in every format.

## [0.13.12] - 2026-11-11

The third published preview: macOS, Windows and Linux, unsigned, adding the
VST3 window on Windows and the LV2 plug-in in the Linux package, published
before either was tried in a host and before VST3-17's Audacity fix was
checked in Audacity ([release](https://github.com/jrp2014/convolver/releases/tag/v0.13.12)).

### Added

- Releases: the draft release's notes list what changed for users since
  the previous preview, 0.13.2 for the next one, before the link to this
  changelog; each preview's release-notes change updates the list
  ([publishing](docs/develop/releasing.md#publishing-an-unsigned-preview)).
  jrp decided on 10 November 2026 to publish a third preview, a 0.13.x tag,
  without the checks left, once the changes in flight are in, VST3-17's
  Audacity fix among them without a check in Audacity
  ([decision](docs/project/decisions.md#releases)).
- LV2 plug-in (LV2-7): Gain, Mix and Latency, as the control inputs `gain`
  (dB), `mix` (%) and `latency_frames` (frames), after the existing ports so
  those keep their numbers. They work as in the other formats; a new Latency
  plays once the host's worker thread has built it, and the `latency` output
  reports it from then on (its maximum is now 4096). Defaults leave the
  sound unchanged. Sessions saved before open at the defaults.
- Releases (LV2-5): the Linux preview package holds the LV2 plug-in,
  `LV2/convolver.lv2`, built against the lv2 headers in the corresponding
  source (new CMake option `CONVOLVER_LV2_SDK`) with its debugging symbols
  kept. The candidate runs the LV2 tests, and the package checks, on CI and
  on the clean machines, load its library and check its plug-ins against its
  manifest. Linux only; no change to the plug-in itself.
- VST3 plug-in on Windows (VST3-12): Convolver's window, the same as on
  macOS, built from Windows' own controls: the status, why it is silent and
  what to change, Choose…, Recent and Reload, a dropped file, Gain, Mix and
  Latency, the Audio facts, Copy Diagnostics and Show Log. It follows the
  screen's scaling and high-contrast mode, moves between controls with Tab,
  and names its controls for screen readers. `vst3.editor` opens it on
  Windows CI and keeps a picture of it, and pluginval's editor tests now run
  there. Not yet tried in a host or with a screen reader. The Windows and
  Linux plug-in reads filter files with libsndfile (WAV, RF64, W64, AIFF,
  CAF and headerless files), and on Linux exports only its entry points. No
  change to the sound.
- Releases: jrp published Convolver 0.13.2, the second unsigned developer
  preview and the first for Windows and Linux, on 9 November 2026
  ([release](https://github.com/jrp2014/convolver/releases/tag/v0.13.2)).
- Problem reports: a new page, [report a problem](docs/use/report-a-problem.md),
  says what to collect on macOS, Windows and Linux (Copy Diagnostics, or
  where the Convolver log is and how to attach or copy it) and how to keep
  private paths out. A **Preview feedback** issue form asks what worked,
  what was hard to follow and what is missing; the **Problem report** form
  asks for the system and points Windows and Linux users to the log, and the
  release notes link both. No change to the plug-ins or tools.

### Changed

- Source (PLUG-34): what Convolver's window does, apart from drawing it,
  moved from the macOS window's Objective-C into shared C++
  (`portable/window_model.hpp`), so the coming Windows window behaves the
  same; `native.window_model` tests it on every system. The macOS window
  looks and behaves as before, except in typed values nobody would type:
  `0x10` now reads as 0 rather than 16, and `5e` is refused rather than
  read as 5.
- Command line and plug-ins (DIAG-6, from the whole-project review of 9
  November 2026): a sound file refused as a filter is reported with the
  reader's reason, for example "truncated or invalid RIFF/WAVE size" or
  "channels 1..256", where Convolver used to say only that the file was
  neither a configuration nor a sound file it can read. This applies to
  files that start like a WAV, RF64, BW64, W64, RIFX, AIFF, CAF, AU, FLAC or
  Ogg file.
- Command line (DIAG-6): a new `--inspect` code, `unreadable-asset`, for a
  sound file that is there but cannot be read, also with `--syntax-only`.
  A file refused as the filter itself used to get `syntax`, and an IR file
  named by a configuration used to get `missing-asset`, as if it were
  absent ([decision](docs/project/decisions.md#diagnostics)). The
  [command-line guide](docs/use/command-line.md#inspecting-and-validating-configurations)
  now lists every code.

### Fixed

- WAV files (IO-10, IO-11): two kinds of WAV file that other programs read
  were refused as cut short, by the command line and by the plug-ins, and as
  a filter were reported as "neither a configuration nor a sound file". Both
  are now read. One has a last chunk with an odd number of bytes and no
  padding byte after it, as Python's `wave` module writes mono 24-bit files
  with an odd number of frames; a file whose header counts the missing byte
  is still refused. The other has sizes that were never filled in, as FFmpeg
  writes a WAV to a pipe, and is read to the end of the file. FFmpeg stopped
  half way through writing a file leaves the same header, so that file is
  now read as if it were complete, unless it is PCM or float audio that
  stops inside a frame. A file without sizes of 4 GiB or more is still
  refused, because nothing says where its audio ends, with a message that
  says why and what to do. On a Mac the plug-ins read a file without sizes
  as the command line does: macOS's own reader, left to open one, read some
  of them a frame short, so it is shown the file with its sizes filled in
  ([input files](docs/use/command-line.md#input-files)). Found by the
  project review of 9 November 2026; jrp chose to read files without sizes
  on 10 November 2026.
- Windows (DIAG-7, from the whole-project review of 9 November 2026): a
  file whose name holds characters outside the Windows ANSI code page is
  named correctly in messages, where Convolver used to report a text
  conversion error in place of the reason; a configuration naming an IR
  whose extension holds such characters now loads; and the plug-ins read
  the UTF-8 path a window passes as UTF-8. One shared helper replaces the
  copies that did these conversions.

- VST3 (VST3-17): in a host that cannot be told the project changed
  (no `IComponentHandler2`, such as Audacity 4.0.2), a filter loaded from
  Convolver's window (Choose…, Recent, Reload or reloading when the file
  changes) is now reported as an edit of Latency that keeps its value, so
  that such a host stores it for exports and saved projects. The sound,
  Gain, Mix and automation do not change, and hosts that can be told see no
  edit.
  Checked by automated tests only, not yet in Audacity, so the guides keep
  Audacity's preset menu as the checked route.

### For contributors

- Tests (COMPAT-1, in progress): the example presets of the published 0.13.2,
  with the audio its own CLI and VST3 plug-in rendered from them, are kept in
  `tests/upgrade/0.13.2/`. `upgrade.cli` and `vst3.upgrade` fail if a later
  build stops loading them or renders different audio (beyond 1e-5, or by any
  amount where it builds and runs as the capture did: a newer compiler may
  change the samples within 1e-5).
- Benchmark documentation (PERF-1): distinguish the older FFTW/Accelerate
  comparison from the Accelerate-only PERF-2 engine results. Add a matched
  new-engine comparison of Accelerate, native NEON FFTW estimation and
  patient wisdom, with raw timings and practical CPU/tuning trade-offs;
  explain that explicit CLI wisdom tuning does not configure the Mac plug-in.
  Add paired Ooura measurements and review portable FFT alternatives (FFT-2),
  including the real-time integration limits of PocketFFT and an isolated
  ordered-PFFFT Mac experiment. No production backend or default changes.
- Tests (CI-36): the VST3 workflow runs its tests under sanitizers, as the
  Audio Unit and LV2 workflows already did. The Linux FFTW job is now a
  Debug build with ASan and UBSan running every `vst3.*` test it can (the
  SDK validator and pluginval cannot load an instrumented bundle and keep
  running on the Release jobs, so no job now loads an FFTW bundle in them),
  and a new Debug build runs `vst3.adapter` under ThreadSanitizer with the
  shipped Ooura backend; a step checks that the test is instrumented.
  `vst3.preset` gets 300 s in sanitizer builds. Both passed on their first
  run, with no reports. From the whole-project review of 9 November 2026.
  No change to the plug-in
  ([VST3 tests](docs/develop/testing.md#vst3-tests)).
- Source (TIDY-1, from the whole-project review of 9 November 2026): small
  helpers that existed in two or three copies now have one: the SplitMix64
  generator and the little-endian readers and writers in `bytes.hpp`, and
  "2 in, 6 out" in `host_log.hpp`. The macOS window names its status
  values from `HostStatus`, and the FFT backends lose `fft_size()`, unused
  since the PERF-2 engine. No change in behaviour or output.
- Tests (CI-37, from the whole-project review of 9 November 2026): ten C++
  test programs, among them `native.engine`, `native.host` and the VST3
  and Audio Unit adapter tests, run every case and report each failure by
  name, where the first failed check used to end the program and hide the
  rest. The shared runner is `tests/native/cases.hpp`.

## [0.13.2] - 2026-11-09

The second published preview: macOS, Windows and Linux, unsigned, with the
new audio engine (PERF-2), published before anyone listened to that engine
in a host ([release](https://github.com/jrp2014/convolver/releases/tag/v0.13.2)).

### Added

- Releases: the notes of the second preview say that the new engine has
  not been heard in a host yet, that the earlier host results were from
  0.12.12, and that some 0.12.12 projects open at Latency 0, and link the
  changelog; jrp decided to publish the next 0.13.x tag for macOS, Windows and Linux
  without PERF-2's host session or the checks left from the first preview
  ([decision](docs/project/decisions.md#releases)).
- Windows and Linux previews (REL-9): from the second preview, the draft
  release attaches the Windows and Linux packages with the macOS ones: the
  VST3 plug-in (no window yet on those systems; filters load from the
  host's preset menu) and the tools, with a Windows installer. Its notes
  give each system's checksum command, what Windows and Linux lack and how
  to report a problem without the window. Before the draft is made, the
  exact files run on machines that never built Convolver: stock Ubuntu
  22.04, Ubuntu 24.04, Debian 12 and Fedora images and a fresh Windows
  runner, where the tools render the examples, the plug-in's library loads
  and lists its classes, and the installer installs and removes itself
  ([publishing](docs/develop/releasing.md#publishing-an-unsigned-preview)).
- Releases (REL-7): jrp decided to publish the first preview without the
  remaining host checks and the independent walkthrough; they stay open
  as follow-up, and the release notes list what was not checked
  ([decision](docs/project/decisions.md#releases)).
- Releases (REL-7): jrp published Convolver 0.12.12 as the first unsigned
  developer preview for macOS on 8 November 2026
  ([release](https://github.com/jrp2014/convolver/releases/tag/v0.12.12)).

### Changed

- Version 0.13.0 for PERF-2 (jrp, 8 November 2026): the next minor-version
  pre-release after the published `v0.12.12`. C++ consumers request
  `find_package(Convolver 0.13)`.
- Engine (PERF-2): non-uniformly
  partitioned convolution that adds no processing delay at Latency 0. A filter's first taps are
  summed directly and the rest by stages of blocks growing from 64 to 8192
  frames, whose larger blocks spread their work over the following
  callbacks. Long filters need far less processor time (a 480,000-tap
  stereo filter about 11 times less, a 30 s crossover 38 times less, on
  the Linux cloud VM; 10 to 17 times less at 10 and 30 s on an Apple M5
  Max at the same latency; see the
  [measurements](docs/develop/fft-measurements.md#the-non-uniform-engine))
  and the worst callback falls with them. At the same latency short filters
  cost no more than before; zero latency costs more for them (about 60 to
  85% more than the old default for 4096 taps, under 1% of a core). The
  Latency setting gains 0 frames, first in the list and the default, and the command line's `--block-size` and inspection default to
  0 to match, so default renders still equal the plug-ins' output.
  `--block-size 0` is accepted everywhere, and `--tune-fft` plans every
  transform size the engine uses, whatever the latency. Output differs
  from before by rounding only (worst oracle error 5.2e-7 of the peak).
  Projects keep the latency they were saved with, except some at the old
  default, 256, which open at zero: in VST3, a project that saved no
  settings (all at their defaults); in the Audio Unit, every project saved
  at 256. The C++ `Engine`, `analyze_configuration`,
  `validate_configuration` and `configuration_tail_frames` default to
  latency 0.

### For contributors

- Long-reverb experiment (PERF-2): `convolver_long_reverb_benchmark`
  compares today's engine with prototype non-uniform partitions (zero
  latency, large blocks' work spread over callbacks, or on a worker thread)
  and with KlangFalter's engine, checks each against the double-precision
  oracle, and `auto_gain.py` measures what automatic gain would do to
  reverb and room-correction filters. Results are in the
  [measurements](docs/develop/fft-measurements.md#long-reverbs-and-zero-latency)
  and the [edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#automatic-gain),
  with a review of eleven other open-source convolvers and of algorithms
  beyond partitioned convolution
  ([other convolvers](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#other-convolvers-and-algorithms));
  the product is unchanged.

- CI (CI-35): a pull request that edits the README, the first-use
  examples' README or `docs/use/first-filter.md` without changing a
  command marked `<!-- first-use-example -->` now runs Lint (and, for the
  packaged guide, only what a guide edit runs) instead of every workflow;
  a change to those commands still runs everything
  ([CI triggers](docs/develop/testing.md#documentation-only-pull-requests)).

## [0.12.12] - 2026-11-08

The first published preview: macOS only, unsigned, published before its
last host checks ([release](https://github.com/jrp2014/convolver/releases/tag/v0.12.12)).

### Added

- Host checks (HOST-1, AU-4): jrp's 8 November 2026 checks of the v0.12.8
  candidate are recorded
  ([record](docs/archive/reviews/2026-11-08-v0128-host-checks.md)). In
  REAPER a render, save and reopen, a refused preset and a change of host
  rate passed against the CLI; in GarageBand a packaged preset with save
  and reopen, Recent and Reload passed. In Audacity 4 a filter chosen with
  Choose… was missing from the export, because Audacity stores a plug-in's
  state only after a parameter edit (VST3-17, open).
  For the first preview, Audacity is supported through its own preset
  menu only, and the release notes say so.
- User guides (REL-2): [check a crossover in a host](docs/use/check-a-crossover-in-a-host.md),
  the 2→6 crossover journey in REAPER with the VST3 plug-in or the Audio
  Unit, and [check file rendering with the command line](docs/use/check-files-with-the-command-line.md),
  the CLI journey from download to the three examples checked with
  `--compare` and the messages for a filter at another rate or channel
  count. With the stereo journey, the three first-preview journeys are
  written; none has yet been followed in a host. The release tests run the
  new guides' commands from the package and check that each refusal they
  quote (`<!-- refused-example -->`) fails with that message and writes no
  file; the package must hold the crossover's preset, input and expected
  output.
- CLI (IO-5): `--speaker-map config` also labels `rf64-` output, with any
  mask, in RF64's WAVE_FORMAT_EXTENSIBLE header (so `5.1(side)` and `7.1`
  files larger than 4 GiB can be labelled). W64, FLAC, CAF and AIFF still
  refuse a map before the output is created, now saying why: libsndfile
  cannot write labels in W64 or FLAC, and CAF and AIFF layouts would change
  the labels' meaning. The library's `AudioWriter` accepts a mask for
  `Container::rf64` the same way.
- VST3 (VST3-9): host rates that are not a whole number of hertz, such as a
  pull-down 48000 ÷ 1.001 ≈ 47952.04795 Hz, are accepted rather than refused
  by `setupProcessing` (which made hosts report the plug-in unusable). They
  are matched strictly, as jrp decided: no filter suits such a rate, so the
  plug-in plays silence (bypass still plays the input), keeps its filter,
  and Status and the log give both rates; it plays the filter again when the
  host returns to its rate. Only floating-point rounding of a whole rate
  (within 1e-12 of it) counts as that rate. Nothing is resampled.
- C++ API (VST3-9): `HostLayout::fractional_rate` (the host's exact rate
  when it is not whole hertz, else 0), `set_host_rate()`, `host_rate_text()`
  and `same_rate()` in `convolver/host.hpp`. Added, so existing code
  compiles unchanged.

- macOS preview packages (AU-5, PLUG-5) hold the Audio Unit,
  `Components/Convolver.component`, beside the VST3 plug-in, with its
  licence notices in `licenses/` (`Convolver-AU.txt`, `GPL-3.0.txt`,
  `audiounitsdk.txt`). The macOS installer has a third package that puts it
  in `/Library/Audio/Plug-Ins/Components` (receipt
  `io.github.jrp2014.convolver.pkg.au`), and `uninstall.sh` removes it. The
  candidates keep its debugging symbols as `Convolver-au.dSYM`, require it
  in `verify_artifact.py`, `release.stage` and the promotion gate, and on
  each macOS runner install the `.pkg`, check every installed path and
  signature, and run `auval -strict` on the installed component.
- The VST trademark notice (PLUG-5), in the wording jrp approved: ® on the
  first mention of VST, "Convolver supports VST® 3." and "VST is a
  registered trademark of Steinberg Media Technologies GmbH.", with
  Steinberg's unmodified VST Compatible logo, in the README, every user guide
  that mentions VST, the VST3 bundle's new `Contents/Resources/NOTICE.txt`
  (its credits) and the macOS installer's text. One source,
  `packaging/trademarks/vst.json`; `scripts/trademarks.py` checks it in
  pre-commit and writes the guides' closing block with `--fix`. Packages ship
  `docs/use/images/`, and `package_docs.py` refuses an image a packaged
  guide could not show offline.

- `convolver --compare OUTPUT EXPECTED` (REL-5) checks a render, such as a
  plug-in host's, against the file it should match, sample for sample. It
  prints the largest difference relative to the expected peak, matches within
  -80 dB of it or two 16-bit steps, whichever is larger (rounding and 16-bit
  dither pass), and when the files differ says whether the output is silent,
  late (a host that did not make up for the plug-in's latency), ends before
  the filter's tail, is at another level or has its polarity reversed.
  Exit status 0 match, 1 different or unreadable, 2 invalid usage.
- Preview packages (PLUG-5, REL-2) ship the `convolver` command-line tool in
  `bin/`, and the first-use examples in `examples/first-use/` (mono, stereo
  and the 2→6 crossover: filter, `input.wav`, `expected.wav`) with a
  preset made from each filter. The release tests render each
  example with the packaged tool and compare it with `expected.wav`.
- User guide: [check a stereo filter in a host](docs/use/check-a-stereo-filter.md),
  the stereo journey from download and checksum to a host render checked
  with `--compare`, saving and reopening, and presets that do not fit (REL-2).
- VST3: **Gain**, **Mix** and **Latency** settings (VST3-4), with the
  Audio Unit's ranges, defaults and parameter IDs: Gain 0 (−100 to +24 dB),
  Mix 1003 (0 to 100 %, which hosts can show as the plug-in's dry/wet
  control through `kDryWetMix`) and Latency 1004 (64 to 4096 frames, not
  automatable). A new Latency asks the host to restart the plug-in
  (`restartComponent(kLatencyChanged)`) and plays from then on. The
  settings are saved in host state version 3, written only when one
  differs from its default, so projects at the defaults save the same
  bytes as before and older builds still open them; older builds refuse a
  version 3 state. The Audio Unit and LV2 read a version 3 state's filter
  and ignore its settings. The defaults leave the sound unchanged; Gain
  mode waits with PLUG-10's other deferred parts.
- VST3: the controller and processor exchange what a window needs
  (VST3-3). The processor sends the PLUG-9 report with each Status update
  and a fresh one when the controller asks; the controller builds Copy
  Diagnostics from it. A `ConvolverLoad` message carries a chosen file's
  path to the processor, which reads it as `convolver-preset` does (on
  macOS with AudioToolbox, so the bundle still needs nothing beyond macOS)
  and installs or refuses it as for a preset; an accepted file marks the
  project changed (`setDirty`). Other systems refuse the file with a
  reason until their windows exist.
- VST3 on macOS: Convolver's window (VST3-11, in progress), the same view as
  the Audio Unit's: the status and what to change, the filter with Choose…
  and drag and drop, Gain, Mix and Latency, the Audio facts, Copy
  Diagnostics and Show Log, and a Bypass checkbox (VST3 only; Audio Unit
  hosts show their own). It is the SDK's `EditorView` for an NSView, as
  the VST3 documentation describes an editor without VSTGUI; settings change
  through `beginEdit`, `performEdit` and `endEdit` (a slider drag is one
  edit), and the window asks the host to resize it when its folded lines
  open. Checked off screen by `vst3.editor`, and opened by pluginval's
  editor tests on macOS CI; not yet tried in REAPER or Audacity. Windows
  and Linux have no window yet (VST3-12, VST3-13).
- Convolver's window on macOS (VST3-11, and the Audio Unit's, AU-6):
  **Recent** lists the last ten filter files loaded in any Convolver window
  (kept in `recent-filters.txt` beside the Convolver log, never in the
  project), **Reload** reads the file again, and **Reload when the file
  changes** (ticked when the window opens) does so by itself while the
  window is open, a second after the file, or a file a `.cfg` or the list
  entry that plays names, has changed. Not yet tried in GarageBand, REAPER
  or Audacity.
- Audio Unit: a **Latency** setting (PLUG-10, AU-7), parameter 1004 and a
  menu in Convolver's window: 64, 128, 256, 512, 1024, 2048 or 4096 frames,
  default 256 (the old Convolver's partitions setting in modern form, and the
  command line's `--block-size`). Smaller is less delay and more processor
  load. It is not automatable and is saved with Gain and Mix. A new choice
  plays at once, also when GarageBand restores a project after starting the
  plug-in: the unit's own worker thread builds a new processor and hands it
  over without locking, with a gap in the sound (the new latency plus the
  filter's own delay), and the host is told the new latency. The log
  records each change and its settings name the latency. The default
  leaves the sound unchanged; the oracle tests cover gain and mix at each of
  the seven block sizes.
- Library (ROBUST-6): `convolver/engine.hpp` names the limits every part of
  Convolver checks: `max_sample_rate` (768000), `max_channel_count` (256),
  `max_block_frames` (2^20), `max_impulse_frames` and `max_engine_delay_frames`
  (2^26). The engine, the sound-file and configuration readers, saved state,
  the tools and the plug-ins use them in place of their own copies of the
  numbers; the limits and messages are unchanged.

### Changed

- Documentation (DOC-10, from jrp's file-loading review of 7 November 2026
  and his release-direction review of 8 November): [your first sound in a
  host](docs/use/first-sound-in-a-host.md) now runs choose a filter, check
  its rate and channels, play, save the project, with **Choose…** in
  Convolver's window and the package's `filter.cfg` files; host presets and
  `convolver-preset` are an optional section for plug-ins without a window
  and for preset libraries. It says that Choose… reads IR files, `.cfg`
  files and lists from any folder but not preset files. One table in [how
  the plug-ins behave](docs/use/plugins.md#loading-a-filter) lists what each
  way of loading a filter reads, and one table in [plug-in
  presets](docs/use/presets.md#preset-folders) gives the host preset
  folders. The stereo and crossover checks choose `filter.cfg` in the
  window, with the `.vstpreset` as the route without one. A file chosen in
  the window must have exactly as many inputs as the host gives, unlike a
  preset (TODO PLUG-33), so the crossover check loads the preset where the
  host gives more inputs, as REAPER does. A new [In
  Audacity](docs/use/first-sound-in-a-host.md#in-audacity) section gives the
  route the first preview supports there, Audacity's own preset menu, since
  Audacity 4.0.2 does not keep a filter loaded from the window for exports
  or saved projects. The first-use guide gains a short "Before you start"
  list, and [report a problem](docs/use/troubleshooting.md#report-a-problem)
  and the GitHub problem form ask for Copy Diagnostics, the loading method
  and whether playback, an export or reopening is affected. No behaviour
  change.
- Plug-in presets (PLUG-31, from jrp's REAPER check of VST3-16, 8 November
  2026): a `.vstpreset` made by `convolver-preset` also holds the name of
  the file it was made from, with its extension and without its folder, and
  that filter's identity, as the VST3 controller state that a file chosen
  in the window saves (PLUG-22). Loaded through Steinberg's preset code, as
  the tests do, the VST3 window's heading then names the file
  (`impulse.wav`, `filter.cfg` for the packaged presets) while that filter
  plays. jrp checked this in REAPER 7.85 on 8 November 2026
  ([record](docs/archive/reviews/2026-11-08-plug31-reaper.md)). A restored filter with no name (an
  older project or preset, or another filter than the name's) is headed
  "Restored filter" in every format, in place of "From the saved project
  or preset", which named an origin the plug-in cannot know (jrp's choice). Converting a `.vstpreset` keeps its name only while it still
  matches the filter. The audio state is unchanged, so presets sound the
  same; VST3 builds from 0.5.0 on load them as before.
  `convolver-vst3-preset` still writes its original bytes, without the
  name. The controller state's reader and writer moved to
  `portable/vst3_controller_state.hpp` so the tool, which needs no SDK,
  shares them.
- Packages (PLUG-28, from jrp's file-loading review; jrp chose to do it
  before the first preview): the 18 example presets in `presets/` are named
  after their folder, such as `presets/repeat-48000-stereo/repeat-48000-stereo.vstpreset`,
  instead of each being `filter.vstpreset`, so hosts that list presets by
  file name, such as Audacity, show what each one is and copying them into
  a preset folder needs no renaming. The three first-use presets in
  `examples/first-use/` are likewise `first-use-mono.vstpreset`,
  `first-use-stereo.vstpreset` and `first-use-crossover.vstpreset`, and the
  stereo and crossover guides load them by those names. The presets'
  contents are unchanged.
- Documentation (DOC-10, from jrp's file-loading review of 7 November 2026,
  [recorded](docs/archive/reviews/2026-11-07-file-loading-review.md)):
  [plug-in presets](docs/use/presets.md) no longer says the plug-ins have no
  window or that the LV2 plug-in has no file chooser; it now starts from
  choosing the filter file itself, with presets as the other way, and the
  [plug-ins guide](docs/use/plugins.md)'s summary names **Choose…** beside
  presets. The
  Audacity preparation in [host testing](docs/develop/host-testing.md#prepare-the-mac)
  removes only the presets it copied into the system folder, not the whole
  folder. The review's other points are TODO items PLUG-26 (choose a
  `.vstpreset` in the window), PLUG-27 (read a chosen filter before the
  host's audio settings are known), PLUG-28 (packaged presets named after
  their filter) and PLUG-29 (the Recent list out of the log folder), and
  PLUG-25 records jrp's agreed wording for a rate refusal.
- Host checks (HOST-1): jrp's Audacity 4.0.2 session with the VST3
  plug-in (7 November 2026, candidate 0.10.13) is
  [recorded](docs/archive/reviews/2026-11-07-host1-audacity.md): the seven
  48 kHz stereo exports with a filter or bypass matched the CLI with
  `convolver --compare`, including after a refused preset and after
  restarting Audacity, and the export with no filter matched the original
  file. The guides now say
  what it found: Audacity plays real-time effects at its Default sample
  rate, which can differ from the rate an export uses, so set it, and the
  project's rate, to the filter's rate; Convolver's window shows with *Preset options ›
  Use vendor UI*; and, as Audacity's preset menu does not import
  `.vstpreset`, a filter can be chosen with **Choose…** in the window on
  macOS. [Your first sound in a host](docs/use/first-sound-in-a-host.md) no
  longer says the plug-in has no window or that preview packages lack the
  Audio Unit. PLUG-25 records the clearer playback-rate wording still to
  decide. No code or sound changes.
- Wording (DOC-9, from jrp's review of 7 November 2026): one set of words for
  filters, IR files, configurations, presets and projects, listed in
  [words Convolver uses](docs/use/concepts.md#words-convolver-uses) and used
  by the window, the Status text, the log, the tools' help and messages, and
  the documentation. A filter given as a sound file is an "IR file" (the
  window, `--inspect` and list messages said "sound file"; one reader message
  said "audio file"); a `.cfg` is a "configuration", never a "config"; a list
  is a "configuration list". `convolver --help` and `--inspect --help` name
  the filter argument `FILTER` (was `IR_OR_CFG` and `CONFIG`). Messages about
  unreadable saved data say "the saved project or preset" rather than "the
  saved state". Channel mismatches name the host's channels, as the window's
  "Host channels" does: "the filter has 2 in, 6 out, the host 2 in, 2 out"
  (was "the plug-in 2 in, 2 out"). The Status value "Not checked yet" is now
  **Waiting** ("Filter loaded: 48000 Hz, 2 in, 2 out; waiting for audio
  settings from the host."; was "checked when the host starts it", wording
  from jrp's Audacity walkthrough), with the same value and icon, and the
  window says "Waiting for audio settings from the host. The filter then
  plays if it fits the host's sample rate and channels." The LV2
  status port's first label is Active, as in the VST3 and Audio Unit (was
  Running); its value is unchanged. No sound changes.
- Plug-ins (PLUG-22, from jrp's review of 6 and 7 November 2026): the macOS
  window (Audio Unit and VST3) names the filter at the top, with its
  extension, and keeps the name after the project is reopened: the Audio
  Unit as its preset name, VST3 in its controller state, shown only while
  the filter saved with it plays. Beside the name is the status, now
  **Active** in place of Playing, plus a new **Bypassed**; the host's Status
  parameter reads the same. Under the name, the filter's own sample rate,
  channels, length and speakers as labelled columns; a problem and its fix
  sit in a box under them, without a second Choose Filter… button: the
  window's Choose… (or Copy Diagnostics) is highlighted instead. The Audio section gives the host's values with
  short labels (Host sample rate, Host channels, Host block size), says
  which inputs a wider bus leaves unused, and drops Filter length and
  Added delay, which the facts and the Latency row show. With no dry
  signal (a crossover, say) the Mix slider dims with a note saying why it
  has no effect, and Bypassed says the outputs are silent. VST3 shows
  Bypassed as soon as the host or the window changes Bypass. Projects saved
  by earlier versions open as before, without a name. No audible effect.
- Library (PLUG-22): `HostStatus::playing` is renamed `HostStatus::active`,
  and `HostStatus::bypassed` is added after `fault`; `host_status()` takes
  whether the instance is bypassed. Hence version 0.12.0.
- Plug-ins (PLUG-23): a filter now plays when the host gives more inputs than
  it has, reading the first inputs and ignoring the rest; its outputs must
  still match exactly. REAPER gives the VST3 plug-in six inputs on a
  six-channel track, so the 2→6 crossover was refused there (HOST-1). Status
  ends "plug-in inputs 3-6 ignored.", and the log and the window's report
  say which inputs the filter reads. Whether there is a dry signal now
  follows the filter's own counts, so a crossover still bypasses to silence
  and stays fully filtered at any Mix, whatever the host's counts. Each filter
  applies Mix by its own rule, so a replacement that changes whether there
  is a dry signal crossfades without a gap or a jump, playing or bypassed.
  The same in VST3, the Audio Unit and LV2 (an LV2 layout with more inputs
  than the filter now plays it too). The audio of a filter on exactly its
  own counts is unchanged.
- Plug-ins (PLUG-23): bypass and Mix pass nothing when no filter plays and
  passing the input could send full-range audio to a driver. A kept filter
  that does not suit the host (another rate, say) has a dry signal only if
  its own input and output counts are equal, so a 2→6 crossover kept at the
  wrong rate stays silent bypassed. After a failed load with no filter kept
  (a missing or unreadable file or preset, a first filter that does not fit,
  or a project saved after such a failure) bypass and Mix pass nothing at
  all, where they used to pass the input on a track with as many inputs as
  outputs, a mono instance included (jrp). An instance with no filter passes
  the input until a load fails, and again once a filter loads or an empty
  state is restored. A failed first load
  fades its dry signal out with it. Status, the log's settings and the
  window's report give the reason; Status no longer says "Load failed, no
  filter plays" before the host has started the plug-in, as nothing plays.
- Library (PLUG-23, jrp's review), incompatible: a prepared filter carries
  whether it has a dry signal. `HostFilter::activate()` returns a
  `HostPlayback` instead of a `Configuration`; the `HostFilter::load()`
  install callback takes `const HostPlayback&`; its last parameter is a
  `HostLoadCheck` (whether a filter that does not suit the host is refused
  or kept until it does) instead of a `bool`; and the three-argument
  `unfiltered_configuration()` is gone (use `unfiltered_playback()`).
  `HostProcessor`'s constructor, `prepare()` and `replace()` gain
  `HostPlayback` overloads beside the `Configuration` ones, and
  `same_configuration()` compares two layouts' rates and channels.
- CLI (GAIN-3): `--gain-db no-clip` now also reserves the headroom that
  integer output needs, so input within full scale cannot clip even after
  rounding, dither and noise shaping. **Integer output with `no-clip` is
  slightly quieter**: by at most 0.0002 dB at 24 and 32 bits, 0.0007 dB at 16 bits
  with the default dither (0.0097 dB with ninth-order shaping), and 0.14 dB
  at 8 bits with dither (2.86 dB with ninth-order shaping); this includes
  0.00013 dB for the convolution's float rounding. Float output and numeric
  gains are unchanged. `no-clip` and `auto` now print the applied gain in
  full (for example `-3.5314949508258078 dB`), so the figure can be given to
  `--gain-db` to make the same file, and `no-clip` says how much of it is
  headroom. The library adds `quantization_headroom()`.
- Inspection (ROBUST-2, jrp's answers on PR #118): when `--all-configurations`
  stops at a limit, the `limit` error now ends "The check is incomplete: this
  entry and every entry after it were not checked.", and the selection reason
  starts "incomplete:". The guides add that the sample limit is cumulative
  decoding work, not peak memory, and that the entry crossing it is checked
  in full. The decisions log records jrp's answers on the limits, CAF and
  AIFF speaker labels, the new GAIN-3 item (`no-clip` to reserve integer
  headroom, in its own pull request) and the crossover journey in HOST-1's
  REAPER session.

- Documentation (GAIN-1): the command-line guide states a safe bound for the
  headroom integer output needs after `--gain-db no-clip`: none for float
  output, beyond float rounding; for B-bit integers, H = (1 + S)(½ + D) + ½
  steps for the dither and noise shaping chosen, from 0.0003 dB (16 bits,
  no dither) to 0.0095 dB (16 bits, triangular dither and ninth-order
  shaping) and 2.9 dB at 8 bits, with half a step of margin; lower the
  reported gain by that and 0.01 dB more, as the report is rounded.
  `native.io` checks the bound for 8, 16
  and 24 bits with every dither and shaper, and the exact undithered
  endpoints; `native.engine` checks that the no-clip gain keeps the input
  that reaches the bound within full scale. The sound is unchanged.
- Processing (PERF-15, jrp's decision of 5 November 2026): numbers below the
  smallest normal float (about 1.2e-38, -758 dBFS) are flushed to zero
  while Convolver processes, in the plug-ins (the whole
  `HostProcessor::process` call, including conversion, gain and mixing),
  `Engine::process`, the CLI's render and while an engine is prepared. A
  long filter or a tail fading to silence produces such numbers, and
  arithmetic on them is much slower. On the Linux cloud VM (Release, Ooura,
  the LV2 plug-in, 512-frame callbacks), with a 30 s filter at full history
  and a host thread that does not flush, the longest callbacks took 37 to
  43 ms before and 8 to 12 ms after, as with a host that flushes, and a
  load held the plug-in's worker for 170 to 200 ms before and 63 to 93 ms
  after. The
  caller's floating-point mode is restored after every call, and a host
  that already flushes keeps flushing. x86-64 (MXCSR FTZ and DAZ) and
  AArch64, including Apple Silicon (FPCR.FZ); other processors are
  unchanged. Results differ from IEEE gradual underflow only within a few
  hundred `FLT_MIN` of zero, and a signal wholly below `FLT_MIN` becomes
  silence; the double-precision oracle tests pass unchanged. Tests:
  `native.denormals` (restoration on return, early return, exception and
  nested scopes; subnormal input; a decaying tail), and
  `native.kernel_divergence`'s 1e-35 case is held to an absolute limit
  (1000 times `FLT_MIN`, 1.2e-35) where flushing applies. `convolver_callback_benchmark` gains
  `--filter decaying` and `--thread-denormals gradual|flush`.
- The version is now 0.12.0, the minor step for PLUG-22's incompatible
  library change below, after PLUG-23's 0.11.0 (jrp, 7 November 2026) for
  its own, after 0.10.13. CMake consumers request
  `find_package(Convolver 0.12)`, and the first preview is cut from the 0.12
  series. The LV2 plug-in reports minor version 12, which LV2 accepts as a
  release; LV2 is not in the first preview.
- 0.10.0 (jrp, 5 November 2026) was the minor step for the library changes
  in DIAG-3 and DIAG-4 below, after 0.9.0; its LV2 plug-in reported minor
  version 10, which LV2 accepts as a release.
- 0.9.0 was the milestone step before jrp's host smoke tests (HOST-1), from
  which the first preview was first to be cut (jrp, 5 November 2026). Its
  CMake consumers request `find_package(Convolver 0.9)`, and its LV2 plug-in
  reports minor version 9, which LV2 reads as a development version.
- Library and plug-ins (DIAG-3, from jrp's simplification review): faults
  met while a replacement filter was being prepared are logged together with
  any met just after it, as "around a filter change", instead of in a
  sentence of their own. The status still shows only those after the new
  filter took over. `HostFaultReport` holds `faults`, `changed` and `held`
  instead of `current`, `earlier` and `held`.
- Library (DIAG-4, from jrp's simplification review): failure records keep
  a message in its parts instead of as text. `HostFailure::message`,
  `HostFilter::notice()`, `speaker_notice()` and `host_state_size_error()`
  hold a `convolver::Advice` (problem, cause, fix, note, in the new header
  `convolver/advice.hpp`), and `HostFilter::reject()` takes one.
  `HostFilter::error()` returns the failure's text by value. The log, the
  window and the tools format the parts where they leave the library, so
  the window's report no longer parses text it has just written; exception
  messages, and the text an adapter hands the log, are still read back. Every message's wording is unchanged.
- CLI (GAIN-2, from an external review of `91fcf1f`): `--gain-db no-clip`
  takes the largest possible peak directly instead of also filtering ten
  filter lengths of noise, so choosing it costs well under a millisecond
  rather than tens of milliseconds for long filters. The gain it chooses is
  unchanged; `--gain-db auto` still filters noise.
- Diagnostics (DIAG-2): every message a user can meet states the problem,
  its cause and the fix, with a note where an environment, host or design
  limit is the reason. `convolver`, `convolver-preset` and the other tools
  print `program: error: problem` and then labelled `Cause:`, `Fix:` and
  `Note:` lines; the Convolver log keeps one line per message. Messages that
  had no fix (a syntax error, a bad option value, a missing output folder)
  now have one. In the macOS window the problem is in bold, followed by what
  plays, Cause and Fix rows, a button that carries out the fix when the
  window can (Choose Filter… or Copy Diagnostics) and a Note row; Bypass
  moved to the end of the title row, and Copy Diagnostics and Show Log to the
  "File, software and log" row. The shared report (`portable/host_report`)
  replaces `headline` with `problem`, `plays`, `cause`, `fix`, `note` and
  `action`, which `report_fields()` also gives. Scripts that matched the old one-paragraph
  wording must match the new lines; exit codes are unchanged.
- Library (VST3-11): `HostRequest` gains `files`, the files an accepted
  file's filter was read from; the report gains the fields `filter path`
  and `filter files` for the window's Reload. Code that builds a
  `HostRequest` compiles unchanged.
- Library (VST3-4): `HostState` gains `settings`, an optional
  `HostSettings` that host state version 3 records (VST3 only), and
  `HostSettings` is declared before it in `convolver/host.hpp`. Existing
  code that builds a `HostState` compiles unchanged.
- The version is now 0.8.0, the minor step for the library change below
  (jrp, 1 November 2026: an incompatible API change before 1.0 needs one).
  CMake consumers now request `find_package(Convolver 0.8)`, and the first
  preview is cut from the 0.8 series (now the 0.9 series, above). The LV2 plug-in reports minor version
  8, an even (release) minor again, though odd patch numbers still give a
  micro version LV2 treats as a development one (LV2-5).
- Library (PLUG-10): `HostSettings` gains `latency` (one of the new
  `host_latencies`, default `default_host_latency`, 256), and
  `HostProcessor`'s settings constructor takes the block size from it,
  refusing any other value with `std::invalid_argument`; its separate block
  argument is gone. `settings_words()` adds the latency, and
  `HostReportFacts::latency_frames` is the running latency, so a report
  explains a chosen one that could not be applied.
- Audio Unit: Convolver's window is redesigned (AU-6, as jrp approved on 1
  November 2026 from a mockup). The status has a symbol, and what plays, why
  and what to change sits under it. A form follows: **Filter** with the
  file's name and **Choose…** beside it; **Gain** and **Mix** with a box
  for typing the value exactly (a value beyond the range stops at its end)
  and Option-click on a slider to reset it, Mix labelled Dry and Wet; and
  **Latency** with the delay it adds. An always-shown **Audio** section
  gives what plays in plain words, with the technical term in brackets and
  a tooltip for each line: sample rate, channels and speakers, routing,
  filter length, added delay, ring-out (the tail), the loudest possible
  peak and the Gain that rules out clipping (new), and the host's block
  size; a line that does not match (a filter for another rate) is marked
  with a warning sign and says what differs. The file path, software and log
  lines are folded away; Copy Diagnostics and Show Log use title case. The
  layout uses stack views (auto layout) and still fits its height to the
  content. Copy Diagnostics uses the same plain labels. A typed value
  accepts a decimal comma. A box sets a value only when you changed its
  text: leaving it untouched no longer sets the rounded value it shows, or
  one older than a change the host made meanwhile. Sound unchanged.
- Library: `worst_case_peak()` (engine.hpp) gives the guaranteed peak
  bound behind `--gain-db no-clip` without running the convolution, and
  now clears only the part of its work buffer each output and input use;
  `HostFilter::filter_peak()` holds it for the accepted filter, found when
  the filter is accepted. The editor report (`host_report.hpp`) replaces its
  `details` with `headline`, `filter_name`, `filter_note`, `audio`
  (`HostFact`), `settings`, `reference`, `sample_rate` and
  `latency_waits` (pre-1.0 internal API).
- Plug-ins (PLUG-17): the Gain, Mix, bypass and mute ramps last 1.33 ms and
  a replacement filter's crossfade 5.33 ms at every sample rate, rather
  than 64 and 256 frames. At 48 kHz the sound is unchanged (the oracle tests
  pass as before); at 44.1 kHz the ramps are 59 and 235 frames, and at
  96 kHz 128 and 512, so fades no longer get shorter as the rate rises.
- Command line (IO-9): `convolver-equalizer-apo` formats gains with
  `std::to_chars` instead of a search with `snprintf` and `strtof`. Every
  gain reads back as the same number; the text is the same for every float
  but ±2^-96 (about −578 dB), which now takes one digit fewer. Building it now needs macOS 13.3 or later (the
  default target is 14.0); the build guide says what building for an older
  macOS would need.

### Fixed

- Audio Unit (AU-17, a ThreadSanitizer report on `main` at `8af5efd`): the
  unit told the host's property listeners about a new latency, tail or
  window report from its own Latency queue, while hosts add and remove
  listeners on their main thread; the Audio Unit SDK keeps them in a list
  without a lock, so the two could collide and, at worst, crash the host
  just after a Latency change or a restored project. No host showed it.
  These notifications now reach listeners on the main thread, as the JUCE
  Audio Unit wrapper sends them; one still pending when the unit closes is
  dropped. No change to the audio. A test adds and removes listeners while
  Latency changes are applied, under ThreadSanitizer in CI.
- Releases (REL-7, from jrp's review of 8 November 2026): the draft
  pre-release notes say how to report a problem: Copy Diagnostics, the
  host and its version, how the filter was loaded, and whether playback,
  export or reopening is affected.
- VST3 (HOST-1, from jrp's v0.12.8 REAPER check): after a refused preset,
  the window's heading changed from the playing filter's name to
  "Restored filter", and a project saved then kept the refused preset's
  name, because the host sends that preset's controller state too. The
  processor and the controller now keep the pair that named the playing
  filter and use whichever pair matches it, in either order of the two
  states; audio was not affected.
- Guides (REL-8): the install guide now says how to open the unsigned macOS
  installer (**Open Anyway** in Privacy & Security); it described only
  clearing the quarantine of bundles copied from the ZIP archive. The limits
  page no longer says the plug-ins have no window: the VST3 plug-in and the
  Audio Unit have one on macOS.
- VST3 (VST3-16, from jrp's HOST-1 REAPER run and his trace with PR #134's
  trace build): in REAPER 7.85 a new Latency stayed unused until a render,
  FX Offline then Online, or reopening the project. Convolver asked REAPER
  to restart the plug-in (`restartComponent(kLatencyChanged)`), as the VST3
  SDK describes, but REAPER only reads the latency then and does not
  restart it, so it read the old one. While the host runs the plug-in, a new
  Latency now plays at once, as in the Audio Unit: a processor with the new
  block size is built off the audio thread and replaces the running one
  before the host is asked, so the latency the host reads is always the one
  that plays. A change during playback leaves a gap in the sound, as
  before ([latency and tail](docs/use/plugins.md#latency-and-tail)).
  If the new processor cannot be built, the old Latency keeps playing and
  the log and window say so. jrp's check in REAPER 7.85 with the candidate
  at `7d6a8b4` passed: stopped and while playing, 256 to 1024 frames and
  back, the window and REAPER's delay compensation showed the new latency
  at once, and every impulse in the later renders lined up with the
  reference track
  ([record](docs/archive/reviews/2026-11-08-vst3-16-reaper-acceptance.md)).
- Audio Unit (AU-16, an intermittent au.adapter failure in CI on `2f3cb80`):
  switching the host's bypass could leave Status at Active while the audio
  was bypassed, and the reverse. The bypass setter does not wait for the
  control lock, and when a load or the Latency work held it, the setter
  left the Status to that call, which might already have set it or never
  set it. The Latency work's serial queue now updates the Status once the
  lock is free. No change to the audio. A test holds the Latency work
  while bypass is switched both ways; TODO PLUG-30 records the pattern and
  the other places that skip a busy lock.
- VST3 (PLUG-23, from jrp's REAPER crossover retest at `5d188a7`): a project
  saved with the 2→6 crossover on a six-channel track played silence after
  reopening, and after FX Offline then Online. REAPER starts the plug-in on
  stereo buses, restores the saved state, and only then gives it the track's
  six inputs and outputs; the plug-in refused the crossover against the
  stereo buses, so nothing was left to play on six. Restored state is now
  refused for not suiting the host's rate or channels only once the host has
  played audio with them, and never when the host says it is restoring a
  project (VST 3.6 `IStreamAttributes`). Until then a filter that does not
  suit is kept, plays silence with the reason in Status and the log, and
  plays as soon as the host starts the plug-in on buses that suit it. A
  preset that does not suit an instance that has played audio is still
  refused, and the previous filter keeps playing, also after the host
  restarts the plug-in on the same rate and buses. A preset loaded into a
  new instance before the host has played any audio, or just after the host
  changed its rate or buses, is now kept silent rather than refused, and
  replaces the filter that was loaded; saving then saves it. The log says
  whether the host had played audio and whether it said it was restoring a
  project. jrp's retest in REAPER 7.85 at `c3dbf0d` passed: the crossover
  came back after reopening and after FX Online, and survived a save, quit
  and reopen with the new build (REAPER does not say it is restoring a
  project).
- Plug-ins (PLUG-23): after a refused load, if the host later changes to a
  rate or channels the refused filter would suit, Status no longer shows
  counts that look as if they conflict ("the filter has 2 in, 6 out, the
  plug-in 6 in, 6 out"); it says "the filter has 2 in, 6 out; the channels
  have changed since." (or "the rate has changed since."), and the log keeps
  the counts the load was refused against.
- Inspection (ROBUST-2): `--inspect` and `--validate` with
  `--all-configurations` decode and prepare every entry of a list, so a
  small list naming one long filter thousands of times kept them busy for
  many minutes, and a list of configurations with thousands of paths made
  a report of hundreds of megabytes. One check now stops, with a `limit`
  error, before the next entry once it has decoded 1 Gi IR samples (16
  renders of the largest filter), or before an entry that would take it past
  16,384 reported filter paths or 131,072 reported routes (a path can have
  8192, so 128 one-path entries made a 37 MB report; now 4.6 MB). A
  message saying no list entry has the wanted sample rate now names at
  most ten of the list's rates, as it already did entries; it had named
  every rate, which a plug-in's log and window then showed. The `limit`
  error is kept even when the report's 256 diagnostics are already used.
- Plug-ins (ROBUST-7, from an external review of `91fcf1f`): when memory ran
  out while a refused first load was being recorded, VST3, the Audio Unit
  and LV2 could go on passing the unfiltered input, and saved state did not
  record the failure. The failure is now marked before anything is
  allocated, so the instance plays silence and saves the failure whenever
  its first load is refused; the log and window then say memory ran out if
  the reason itself could not be kept. An accepted filter still keeps
  playing when a replacement fails.
- Plug-ins (PLUG-18, from the same review): bypass and mute now end exactly
  at the dry signal or silence on their last frame. Since PLUG-17 they
  could leave about 2.4e-6 of the signal (-112 dB) for one frame at some
  rates (176.4 kHz, for example). At 48 kHz and 96 kHz the output is
  unchanged sample for sample; elsewhere it differs only by float rounding
  during the 1.33 ms ramp.
- Audio Unit (AU-13, from the same review): if the Latency work's
  semaphore could not be created when a unit opened, the unit released a
  dispatch source that had never been activated, which Apple documents as
  undefined. The source is now created last, so a failure there leaves
  nothing to undo, and opening the unit fails cleanly.
- VST3 (VST3-15, from jrp's review of `276c7dc`): with a host that puts a
  proxy between the plug-in's two halves and delivers their messages later,
  a new Latency asked the host to restart before the processor had it, so
  the old latency stayed in use, and a filter file refused in the window
  still marked the project changed. The controller now waits for the
  processor's answer: the restart is asked for once the processor has the
  Latency, and only an accepted file marks the project changed. Hosts that
  connect the halves directly, or forward messages at once, behave as
  before. Not seen in a host; found by new tests that connect through such
  proxies.
- Plug-ins (PLUG-13): a filter that filled the saved state at default
  settings could no longer be saved once a VST3 setting changed (jrp's review
  of `276c7dc`: Gain to −6 dB, then `getState()` failed), because settings
  add 20 bytes. The size check now keeps 256 bytes free for the settings
  whether or not they are in use (`host_settings_room_bytes`, room for
  settings added later too), so what loads still saves after any setting
  changes. The largest accepted filter is 64 samples shorter in every
  format, still just under 16,777,216 samples; `convolver-preset` refuses
  the same filters, and a state saved by an earlier build within 256 bytes
  of the limit is refused when restored, with the size explanation (no
  release has published such a state).
- Audio Unit window: a report read just after a load could show the state
  from before it, when the Latency worker happened to hold the unit's lock
  at that moment. The window's last report is now forgotten whenever what
  the unit plays changes, so it is shown only while it is still current
  (during a load or a Latency change, as before). Seen as an intermittent
  `au.editor` failure in the sanitizer job.

### For contributors

- Release evidence (REL-6, REL-7): jrp's responsiveness qualification of
  the 0.12.5 source on his MacBook Pro is recorded: all 24 combinations
  passed on the first run, with the longest hold of the host's thread
  43.2 ms and the longest judged callback 2.95 ms
  ([record](docs/archive/reviews/2026-11-08-rel6-qualification.md)). The
  status page now lists, once, the checks left before the first preview
  is published, keeping the host results jrp accepted
  ([checks left](docs/project/status.md#checks-left-before-publication)).
- Releases (REL-8): a "Draft preview release" workflow turns a passed
  candidate run on a `vX.Y.Z` tag into a draft GitHub pre-release. It refuses
  a run that was not started by hand on the tag or did not pass, or whose
  tag holds another commit or version, checks every file against
  `SHA256SUMS` and the candidate record, attaches only the macOS packages
  and the corresponding source, writes the release notes (each file's
  SHA-256, the run, the install guide, known limitations and the
  responsiveness result) and downloads the attached files again to compare
  them. Nothing is public until jrp publishes the draft. Pull requests that
  change it run a dry run on the newest tag candidate. jrp rehearsed it on
  8 November 2026 with the v0.12.5 candidate: the draft was right and was
  deleted. REL-8 is done.
- Tests (AU-3): the Audio Unit workflow runs `au.adapter` under
  ThreadSanitizer, in a Debug build with the defaults the packages use, on
  the runs where that workflow already runs. It switches bypass from another
  thread while rendering, sets the Latency from audio threads and holds the
  Latency work while bypass changes; a step checks that the test is
  instrumented. Its first runs reported four races, all in the test's own
  setup, which relied on a dispatch source's wake-up that ThreadSanitizer
  does not see as ordering; the test now sets its hooks before any unit
  exists and publishes each hold atomically. No plug-in code changed. AU-3
  is done, and with it first-preview gate 4.
- Host checks (HOST-1, AU-4): jrp's 8 November 2026 checks with the 0.12.5
  candidate are recorded. In REAPER, part C on the redesigned window
  passed its controls and state, including save, quit and reopen, and the
  latency renders lined up again
  ([record](docs/archive/reviews/2026-11-08-host1-reaper-window.md)). In
  GarageBand, the Audio Unit's presets, **Choose…**, a refused preset,
  settings kept after reopening and two exports matching the CLI passed
  ([record](docs/archive/reviews/2026-11-08-au4-garageband.md)). Listening
  was not checked in either. jrp decided these results stand for the final
  candidate after PLUG-31, where only what it touches and the unchecked
  steps are run.
- Preview qualification (REL-7): an acceptance plan with a fixed-candidate
  stop rule, required gates, blocker classification and deferred work.
  CI-34 proposes scheduling improvements separately; workflows and useful
  test coverage are unchanged.

- Host checks (HOST-1): a reusable 20-second, 48 kHz stereo listening WAV
  with quiet clicks and sustained tones, a standard-library Python generator,
  and [usage notes](examples/host-audio/README.md). The REAPER smoke-test
  guide links to it. This is source audio for manual checks, not a filter
  or a numerical reference; preview package contents are unchanged.
- Tests (LV2-3, AU-3): pluginval, at the VST3 job's strictness (10, the
  highest), validates each of the eight LV2 plug-ins in the Release bundle
  and the installed Audio Unit, its editor tests opening the window, in the
  LV2 and Audio Unit workflows. The newest full release decides; a newer
  pre-release runs afterwards as a warning only. Both formats share the
  command line with the VST3 tests in `tests/plugins/pluginval.py`. Given
  a bundle, pluginval tests only its first LV2 plug-in, so each is copied
  into a bundle of its own first.
- Tests (AU-15): the Audio Unit workflow plays the three first-use
  fixtures through the installed unit with Apple's AVAudioEngine (offline
  manual rendering, the filter loaded as an `.aupreset` through
  `ClassInfo`, block sizes 61, 300 and 1000) and checks the 256-frame
  latency and the output against the oracle within 1e-5.
- Tests (HOST-5): the LV2 workflow plays the three first-use fixtures in
  headless Ardour 8 sessions on the Dummy backend: the preset loaded from
  Ardour's preset list, white noise recorded, the session saved, then
  restored and exported by `ardour8-export` in a second process. Each
  export must match the oracle applied to the recorded take within 1e-5,
  with Ardour's latency compensation leaving no offset; the worst error was
  4.7e-8.
- Tests (FFMPEG-1): `native.ffmpeg_pipe` runs the
  [FFmpeg pipe](docs/use/command-line.md#with-ffmpeg) as the guide writes
  it, on a short film with two audio tracks: the second track is chosen,
  the 2-in/6-out crossover's six channels keep every frame and the filter's
  tail and match the expected output after FLAC, the video is copied, and
  audio that started late starts at time zero, as the guide warns. It runs
  where CMake finds `ffmpeg` and `ffprobe`; the Linux g++ Release CI job
  installs them.
- Tests (PERF-9): `vst3.load_timing`, `au.load_timing` and `lv2.load_timing`
  time each plug-in format's own load entry points (VST3 `setState` and the
  window's file, the Audio Unit's `RestoreState` and load property, LV2's
  `restore()` and worker) with filters of 1 to 30 seconds while another
  thread renders at the host's pace, and report how long the caller was held
  and how late audio callbacks were. In optimised builds they fail above
  jrp's limits for the loading-responsiveness gate: no late callback, and
  the host's thread held at most 100 ms with filters up to 10 s and 250 ms
  with 30 s; an entry that does not pass runs up to three times (how runs
  are judged: PERF-10, under Fixed). Tests only: no change to the plug-ins or the sound.

- CI (CI-34, first step, approved by jrp on 8 November 2026): a pull request
  builds the plug-in distribution candidates, whose macOS builds are the
  slowest jobs, only when it changes release files, packaged pages, the
  macOS window, the build configuration or a shipped dependency; for
  anything else, start the workflow by hand on the branch. A change only to
  `scripts/release/`, `tests/release/` or `packaging/` now runs only the
  candidates, and a change to a guide in `docs/use/` builds them only when
  the page has a command they run or shows an image. Pushes to `main`, the weekly run and
  manual runs still build them every time, and no test was removed.
- Documentation (DOC-13, from jrp's review of `main` at `21711c5`, 8
  November 2026): [TODO](TODO.md) holds open work only. The 122 finished
  items' one-line entries moved, with their IDs and links, to
  [`docs/archive/todo/`](docs/archive/todo/2026-11-08-done-items.md), whose
  IDs the repository checks (`scripts/check_repository.py`) now count as
  defined and done; the execution priorities no longer describe VST3-16 as an
  open draft. The plug-in formats and editor plans, whose state paragraphs
  dated from October, are [archived as superseded](docs/archive/plans/README.md);
  the risks, the PipeWire comparison and one owner's question they still
  held moved to HOST-2, VST3-6, AU-6, LV2-4 and LV2-5. Dated dependency versions, fixed intermittent failures and old
  timings moved from the [testing guide](docs/develop/testing.md) to an
  [evidence record](docs/archive/reviews/2026-11-08-testing-guide-evidence.md),
  leaving the two failure causes that still apply to new work.
- Documentation (DOC-11, from jrp's review of `main` at `bf6a207`, 8
  November 2026): the [status page](docs/project/status.md) agrees with the
  dated host records. It is dated 8 November 2026 against 0.12.1, no longer
  says there are no open fixes while the VST3 Latency finding in REAPER
  (VST3-16) is open, says which window each host run used (none yet the
  0.12 window), and gives the crossover's passing REAPER retest without the
  older refusal as a current result. [What still blocks the first
  preview](docs/project/status.md#what-still-blocks-the-first-preview) is now
  one table listing each remaining gate once, with its status and evidence:
  VST3-16, the VST3 smoke tests on the current window (HOST-1), the Audio
  Unit in GarageBand (AU-4), the Audio Unit's ThreadSanitizer build (AU-3),
  responsiveness qualified on a named machine (REL-6), one exact candidate
  (PLUG-5) and the independent walkthrough (REL-3). Every host
  step, those that passed on earlier candidates included, is run again on
  the preview candidate, as jrp decided on 8 November 2026. TODO's
  execution priorities now list VST3-16 as an open fix and PLUG-28 before
  the first preview. The dated records are unchanged. No code or sound
  changes.
- Development (DOC-8, from jrp's review of the agent guidance at `f6895f3`):
  the [workflow guide](docs/develop/workflow.md#verification-for-each-kind-of-change)
  says what each kind of change must run before a push (a prose-only change
  needs pre-commit, not a native build), and that a review is read-only
  unless asked otherwise, host testing records what was observed, and only a
  change gets a branch. The `todo-item` and `review-change` skills link to
  their sections and the link check now reads `.claude/skills/`; `AGENTS.md`
  lists the same entry points and a task-to-section index. The dated
  benchmarks move to [FFT and engine measurements](docs/develop/fft-measurements.md)
  and the load-timing method to [load timing](docs/develop/load-timing.md),
  with the old links kept. jrp's HOST-1 REAPER session is archived, and its
  two open findings (a new VST3 Latency not used until the project is
  reopened; no REAPER route for the 2→6 crossover) are listed in
  [host testing](docs/develop/host-testing.md#unresolved-host-findings)
  instead of the guides promising those results. Merged branches are kept
  for a while and deleted periodically by jrp from a list agents prepare.
- Documentation (DOC-7): finished items in [TODO](TODO.md#done) are one line
  each again, as the workflow guide asks: ID, title, date, pull request and
  the page that describes the result. Their earlier full entries, with the
  evidence they recorded, are kept unchanged apart from link paths in
  [`docs/archive/todo/`](docs/archive/todo/2026-11-06-done-entries.md).
  `TODO.md` is about 500 lines shorter.
- Development (DOC-7): `scripts/check_format.sh`, a wrapper kept only for old
  habits, is removed; run `python3 scripts/check_format.py` (or pre-commit).
- LV2 (LV2-8): closed without a change. Jalv, Ardour, Carla and PipeWire,
  read in their current source and in their normal configuration, do not
  drop a queued worker request and then reactivate the plug-in, so the
  status cannot be left at Loading in them.
- Planning (jrp's review of 0.10.8): the first preview is macOS-only, with
  the VST3 plug-in and the Audio Unit in the existing shared window and
  their Gain, Mix and Latency settings; the Windows and Linux windows, the
  other gain modes and the meters move to later previews. Its
  loading-responsiveness gate names a finite set of workloads, the stereo 1,
  10 and 30 s filters its test already runs; PERF-3's wider supported
  workloads come after the preview. TODO, the status page (whose summary
  now describes 0.10.8), the release steps and the decisions log say so.
  The Audio Unit's ThreadSanitizer build (AU-3) and the named-machine
  responsiveness qualification (REL-6) stay first-preview gates.
- Repository checks (DOC-2): `scripts/check_repository.py` now fails when a
  guide, workflow or script names a CMake `-DCONVOLVER_…` option the build
  does not define (or a value it does not allow, such as an unknown FFT
  backend), when the build guide leaves an option out, when a command shown
  with `convolver`, `convolver-preset` or `convolver-equalizer-apo` uses an
  option the tool does not parse, or when a tool's guide leaves out one it
  does. The presets and Equalizer APO guides now mention `--help` and
  `--version`.
- Tests (PERF-12, from jrp's review of `db54160`): the load-timing tests
  record each audio callback (when it was due, when the thread woke, when
  the plug-in was entered and returned, and the thread's CPU time where the
  platform gives it) and print the latest wake and the longest call into the
  plug-in. A callback late only because its thread woke late is not judged
  and leaves the run not qualified rather than failed; a slow call with
  little CPU time is judged, since a call blocked on a lock looks the same
  as a descheduled one. `native.load_timing_harness` tests the verdicts on made-up records
  instead of sleeping, in about 9 seconds rather than 40 to 60. Tests only:
  no change to the plug-ins or the sound.
- Tests (AU-14, from jrp's review of `db54160`): `au.adapter` waits for the
  Audio Unit's Latency work to finish, through a completion witness, instead
  of sleeping 100 ms and 20 ms before checking its notification and log
  line. Each unit counts its Latency requests with a lock-free increment
  for this, so one unit's finished work never satisfies another's check;
  no change to the sound.
- Tests (PERF-13, from jrp's review of `db54160`): each load-timing test
  writes every callback's record, each phase's summary and each run's
  verdict, with the Convolver version, build, compiler, FFT backend and
  machine, to `FORMAT-load-timing.json` in its work folder; the VST3 and
  Audio Unit Release jobs keep them as an artifact. Tests only: no change to
  the plug-ins or the sound.
- Releasing (REL-6, from jrp's review of `db54160`): a responsiveness
  qualification before each release, on a named machine with the
  candidate's source and an optimised build, cold and `--warm`; the release
  notes state whether it qualified
  ([releasing](docs/develop/releasing.md#responsiveness-qualification)).
- Tests (PERF-14, from jrp's review of `db54160`): the load-timing tests
  take `--warm`, which fills the filter's input history faster than real
  time before the replacement and the reset, so they start from a long
  filter's full cost, and shows half a second of each filter at that cost
  (reported only). Not in CI (filling takes minutes). Tests only; no change
  to the sound.
- Tests (PLUG-21, from jrp's review of `db54160`): teardown is tested at
  chosen stages instead of by repetition. `lv2.adapter` holds the worker's
  `work()` after reading the file, after preparation and after its last
  check, deactivates or closes the instance from another thread, and
  releases it once deactivation signals from inside that it is waiting for
  the held work; `au.adapter` holds a run of the Latency work while the
  unit closes, releasing it once closing has cancelled the work. With LV2's
  `deactivate()` lock removed, the staged case fails while the earlier
  repeated loop passes. Both plug-ins gain test-only hooks, null in the
  components; no change to the sound.
- Plug-in contract (PLUG-19, from jrp's simplification review of 5 November
  2026): a setting transition (bypass, mute, gain, mix) may end one frame
  after its ramp's last frame, which is inaudible (about 5.7 µs at
  176.4 kHz). It must still end exactly at its target and not click.
  `native.host_settings` allows that frame, so a later shared ramp helper
  need not reproduce PLUG-18's frame counting. Reported latency and channel
  alignment stay exact to the sample. The sound is unchanged.
- The macOS window's files moved from `plugins/au/` to `plugins/editor/mac/`
  (VST3-11), shared by the Audio Unit and VST3, which each build them with
  class names of their own. The setting rows can now mark the start and end
  of a slider drag for a format that asks (VST3 does); the Audio Unit's
  window behaves as before.
- Audio Unit window (AU-10): the Gain, Mix and Latency rows and their
  handling moved out of the window's layout code into their own file
  (`plugins/au/setting_rows.mm`), so the window can gain controls without
  growing one file further. No change in behaviour or sound.
- REL-5, the early macOS walkthrough, ran as a package and command-line
  check of the stereo journey on a macOS arm64 candidate, assisted for jrp
  rather than by an independent tester: the checksums,
  the package contents and the command-line render all matched, with no
  defect found. By jrp's choice its host steps moved to the REAPER smoke
  test (HOST-1) and the preview walkthrough (REL-3).
- The [status page](docs/project/status.md) is checked against `main` at
  0.8.6 (DOC-6, from jrp's review of `276c7dc`). It no longer calls the CLI
  and the journey presets missing, the journeys not started or the VST3 work
  unclaimed, and it now names only what remains, linking each TODO item;
  progress detail lives only in TODO's `Progress` lines
  ([recording progress](docs/develop/workflow.md#recording-progress-in-todo)).
  TODO gains VST3-14, VST3-15 and AU-11 from the same review.
- The [status page](docs/project/status.md) is checked again against `main`,
  now at 0.8.2 (DOC-5, from jrp's review of PR #98). It names the commit
  and the CI runs it relies on, the Audio Unit's Latency setting, window
  redesign and editor tests, jrp's GarageBand run of the redesign and the
  typed-box defects it led to, and what remains of the settings and windows.
- Continuous integration (CI-21): the one third-party action,
  `hendrikmuhs/ccache-action`, is pinned to the commit of its v1.2.24 release
  instead of the tag, so a moved tag cannot run other code in CI. Dependabot
  still proposes each new release. GitHub's own actions keep their major tags.
- Continuous integration (CI-26): a pull request that changes only the macOS
  window that VST3 and the Audio Unit share (`plugins/editor/mac/`) runs Lint,
  the VST3, Audio Unit and candidate workflows, and skips the native, LV2
  and fuzz builds, as a change to one format's own files does (CI-13).
  Anything else in the same pull request still runs everything.
- Continuous integration (CI-27, from jrp's review of `f2dc691`): the Audio
  Unit adapter and window tests write their Convolver logs to files in the
  build folder from the start, rather than private files copied there or
  deleted at exit, so the logs survive a sanitizer abort, crash or forced
  end. CI evidence now includes the window test's log, which was missing,
  and the `.old` file each test log rotates to. `au.log_evidence` checks
  collection after an abort and a kill.
- VST3 adapter (VST3-14, from jrp's review of `276c7dc`): the edit
  controller moved from `plugins/vst3/processor.cpp` to its own file,
  `plugins/vst3/controller.cpp`, unchanged, before the window gains more
  controls.
- macOS window (AU-11, from jrp's review of `276c7dc`): Reload When the File
  Changes, following the playing filter's files, moved from the window's
  layout (`plugins/editor/mac/diagnostic_view.mm`) to its own file
  (`plugins/editor/mac/file_watch.mm`), unchanged, before the window gains
  more controls.
- Continuous integration (CI-28, from CI-27's review): `vst3.adapter` and
  `vst3.preset`, which runs the same tests, write their Convolver logs to
  files of their own in the build folder (`convolver-adapter-tests.log`,
  `convolver-preset-tests.log`), set once at start, and the Status and log
  case reads what it added from there, rather than switching to a temporary file for that case and
  deleting it, so a crash in the case keeps its log. The Audio Unit and VST3
  tests share the log helper (`tests/plugins/test_log.hpp`).
- Build (PLUG-15): a new macOS build directory targets macOS 14.0 unless the
  first configure sets `CMAKE_OSX_DEPLOYMENT_TARGET` or
  `MACOSX_DEPLOYMENT_TARGET` says otherwise, the same for every target and
  the preview packages; an existing directory keeps the target in its cache.
  The VST3 and Audio Unit folders used to force 11.0 for their own targets
  after `project()`, so a developer's plug-in build targeted another macOS
  than the packages and the core it links.
- Build (FFT-4): the Apple vDSP kernel experiment
  (`CONVOLVER_EXPERIMENT_VDSP_MAC`, `portable/kernels_vdsp.cpp` and its two
  scripts) is removed; its timing program is now `convolver_kernel_benchmark`.
  It was off by default and its 24 October results kept the existing
  kernels; no build that ships changes.
- Plug-in contract (PLUG-16): a replacement filter plays for one Latency
  block, not always 256 samples, before its 256-sample crossfade, as it has
  since the Latency setting arrived; the page now says so. In
  `HostProcessor` the 64-frame ramps and the crossfade are named constants;
  the sound is unchanged.
- Continuous integration (CI-7): the VST3 and Audio Unit workflows resolve
  the newest SDK release once per run, in a first `sdk` job that uploads a
  lock file, and every build job of the run builds exactly that release
  (`sources.py --lock`), so one run can no longer mix two releases. Each
  job's summary still lists what it built with.
- Continuous integration (CI-8): the advisory checks (the newest pluginval
  pre-release in the VST3 jobs, the newest upstream lilv in the LV2
  workflow) keep CTest's output as an `advisory-evidence-…` artifact when
  they report a problem, as failed jobs keep theirs; the job still passes.
  `scripts/ci_evidence.py --advisory` collects it and lists the tests in the
  job summary as an advisory problem.
- Continuous integration (CI-29): static analysis runs LLVM's
  `run-clang-tidy.py`, installed with clang-tidy, in place of
  `scripts/clang_tidy.py` (removed with its tests and pre-commit hook). The
  checks and the file list are unchanged; a listed file the build does not
  compile is now skipped rather than reported.
- Audio Unit (AU-12): a new Latency is applied on a serial dispatch queue
  woken by a `DISPATCH_SOURCE_TYPE_DATA_OR` source, Apple's coalescing
  wake-up, in place of Convolver's own thread (`portable/request_worker.hpp`).
  Behaviour is unchanged: the choice still plays at once, wakes that arrive
  together run the work once, and closing the unit waits for work in
  progress. The worker's tests (`native.request_worker` and its watchdog
  check), `.github/workflows/watchdog.yml` and `scripts/ci_watchdog_probe.py`
  are removed, the Linux CI jobs no longer install gdb, and CI-24 and CI-25
  close without a cause; their evidence is archived.
- Tests (CI-33): `native.cli`'s no-clip case compared each pair of renders
  byte for byte, so its float case failed when the two files' `PEAK` chunks,
  in which libsndfile records the second a float WAV file was written,
  differed. It now checks the format, rate, channel count and frame count
  one by one and every sample exactly, with an explicit dither seed, and
  reports the first differing sample and the largest difference. The
  [command-line reference](docs/use/command-line.md) now promises
  byte-identical samples, not byte-identical files, for a given seed (with
  the default FFT planning or a wisdom file), as jrp
  [decided](docs/project/decisions.md#configuration-and-sound). The review
  checklist says assertions test contracts and invariants, not incidental
  metadata.
- Tests (PERF-10, from jrp's review of `59ac8e0`): the load-timing tests
  passed when they could not judge a run: a late baseline made every
  violation, an over-long load included, "inconclusive", which counted as
  a pass. The hold of the host's thread is now judged whatever the audio
  thread saw; a run whose late callbacks come with a late baseline (half a
  second idle before the phases and another after them), or that ran in a
  build that does not enforce the limits, is "not qualified", and the
  test then exits 77, which CTest reports as skipped, not passed. An entry
  no limit applies to (LV2's `restore()`) is "reported only" rather than
  passed. The summary lists every run that did not pass, also when a later
  run did, and counts entries that passed only after a run that did not.
  An entry passes only if one run met both limits; limits met in different
  runs leave it not qualified (or failed, when every run broke one), and the
  summary names the run that met each. A test in which nothing was judged is skipped, not passed.
  `native.load_timing_harness` checks these verdicts with a mock adapter.
  Tests only: no change to the plug-ins or the sound.
- Tests (PERF-11, from jrp's review of `59ac8e0`): the load-timing tests
  played input whose negative half wrapped round in unsigned arithmetic to
  about 1.8e15, instead of quiet noise from -0.1 to 0.1. The generator now
  subtracts in signed arithmetic, and `native.load_timing_harness` checks
  the input's range and sign. Tests only: no change to the plug-ins or the
  sound.
- Tests (CI-31): the shared double-precision oracle works out each route's
  invariant parts once, with the same arithmetic, so `native.host_settings`
  takes about 4 seconds rather than 90 under GCC's AddressSanitizer, where
  it had passed its time limit.
- Tests (CI-30): `native.engine` replaces `operator new` with the array and
  sized `operator delete` too, from one shared file, so GCC's
  AddressSanitizer build runs the oracle tests instead of stopping on
  alloc-dealloc-mismatch.
- Release scripts (CI-20): unpacking a ZIP (the downloaded NSIS release on
  Windows, and each candidate's own ZIP) now checks that every file lands
  inside the destination. Before, on Windows a member name starting with `/`
  passed the name check and could be written at the root of the drive, and a
  link already in the destination could lead a file outside it. Found in a
  Skylos scan (1 November 2026). Packages are unchanged.
- Continuous integration (CI-22): intermittent failures leave evidence and
  two causes are removed. Windows jobs set `VCPKG_DISABLE_METRICS=1`: an
  exiting vcpkg copied `vcpkg.exe` for its metrics upload with no sharing,
  which matches the "being used by another process" (exit 32) failures of
  parallel app-local DLL steps
  ([vcpkg-tool#2163](https://github.com/microsoft/vcpkg-tool/issues/2163)).
  `native.request_worker`'s watchdog still allows 10 s, now without
  progress rather than in all, and reports a timeout with the workers done,
  where the test's thread and the current worker were, and every thread's
  stack from cdb, gdb or lldb (the Linux jobs install gdb; cdb reads only
  local symbols);
  `native.request_worker.watchdog` passes only when that report comes with
  the stacks; the test is resumed (SIGCONT) however the debugger ends,
  until nothing traces it, since a stop from lldb's attach could outlast
  the debugger; a debugger that overruns is killed with its process group
  and, on macOS, lldb's `debugserver`; the package candidates leave that
  check out; and the build and test jobs have time limits, after the check
  ran on past CTest's timeout on the macOS arm64 candidate. Its waits are
  timed by the clock. The package consumer tests
  count two CPUs each in CTest (`PROCESSORS 2`). A failed test job lists its
  failures with the run attempt in its summary, its evidence artifact's
  name carries the attempt, and a job that passes on a re-run says "Passed
  on retry" with a link to the earlier attempt. `au.adapter` sets
  `CONVOLVER_LOG` once instead of per case, which raced with the Latency
  worker's read of it (ASan). `au.editor` reads the report
  after a held load until it says Playing, as the Latency worker the restore
  wakes may hold the lock a moment. The cause of the one Windows `request_worker`
  timeout is still open (CI-24). See
  [intermittent failures](docs/develop/testing.md#intermittent-failures).
  Tests and CI only: no change to the plug-ins, the CLI or the sound.
- Continuous integration (CI-23): `vst3.fft_symbols` now runs on Windows
  instead of always skipping, finding `dumpbin` through `vswhere` when it
  is not on PATH.

## [0.7.1] - 2026-11-01

### Changed

- Clearer failure messages everywhere (DIAG-1, from jrp's choices on
  31 October 2026). The CLI names the file, time, frame and channel of an
  invalid input sample (not a number, or infinite) and says to export the file
  again; an overflow names its time, frame and channel and says to lower
  `--gain-db` or use `no-clip`; and after any failure once the output is open
  it says the output is incomplete. Damaged, truncated or unrecognised sound
  files, incomplete RAW frames and binary files given as a filter now say what
  to do, in all three tools and in the Audio Unit. `convolver-preset` starts
  its messages with its name, names a file that is not a VST3 preset, points
  to the CLI for filters over 8 channels, describes a list that matches
  nothing in terms of the options given, and accepts options after the file
  names, `--help` included (an option written there without a value used to
  be taken as an output name). The VST3 plug-in logs a sample rate, block size, sample type
  or (once per instance) channel layout it refuses, LV2 logs why it refused
  to start, and all three formats log a failed save. Each message has a test.
  The CLI still replaces an existing output, now documented as deliberate.

## [0.7.0] - 2026-11-01

### Changed

- The version is now 0.7.0, a deliberate minor step at a milestone: the
  Audio Unit's window (AU-6) and the pre-preview fixes (PLUG-12, ROBUST-3)
  are in (jrp, 31 October 2026). The first preview is cut from the 0.7
  series. CMake consumers now request `find_package(Convolver 0.7)`; the
  package keeps its same-minor compatibility policy, so a consumer asking for
  0.6 no longer finds it. The LV2 plug-in now reports minor version 7,
  which LV2 treats as a development version until jrp decides the mapping
  (LV2-5). Nothing else changes: the library, plug-ins and their saved state
  are as in 0.6.10.

## [0.6.10] - 2026-10-31

The 0.6 series, from 0.6.0 (25 October 2026) to 0.6.10; its patch versions
have no entries of their own.

### Added

- Audio Unit: a small window of its own (AU-6, first version), so a user can
  see why the plug-in is silent or refused a preset without finding the log.
  It shows the status, what plays, why and what to change in full (a stereo
  preset on a mono track now also says to use a stereo track, as the log's
  start line does), the host,
  filter, settings and software details, and has Copy diagnostics (the report
  and this instance's recent log lines, as text) and Show log (the log file
  in the Finder), with Gain and Mix sliders, because hosts such as
  GarageBand show it instead of their generic controls. **Choose Filter…**
  (or dropping a file on the window) loads a `.cfg`, configuration list or
  sound file directly, as the old Convolver did, read as `convolver-preset`
  reads it, so the project then holds the same state a preset would, and
  the chosen file becomes the current preset, named after it, so hosts can
  mark the project changed. The
  Audio Unit reads sound files with macOS's own AudioToolbox, so the
  component still links nothing beyond macOS (jrp, 30 October 2026);
  `native.audio_toolbox` checks that it reads every format the tests write
  to the same samples as libsndfile. A build with
  `-DCONVOLVER_AU_FILE_READER=LIBSNDFILE` reads them with libsndfile instead,
  as a fallback; the window's Software detail names the reader. A refused
  file's short status names its own channels or rate (jrp's GarageBand run
  showed "the filter has 0 in, 0 out"); for a configuration list, whose
  entries differ, it names the host's channels or rate instead. Plug-ins
  explain a file that does not fit in terms of the host's settings and
  loading a file in Convolver, not command-line options (Audio Unit and LV2). While the host restores a filter on
  another thread the window shows the last report rather than waiting. No
  sound changes.
- Library (internal to the adapters): the editor report (PLUG-9) in
  `portable/host_report.hpp`, built from the Status line and the log's
  wording, and `playing_words()`, the part of the log's start line that says
  what plays, which the report shares; and `portable/filter_file.hpp`
  (PLUG-9), the one step from a filter file to plug-in state, now shared by
  `convolver-preset`, the LV2 plug-in and the Audio Unit. The `.cfg` loader
  moved into a private `convolver_configuration` library with no sound-file
  library of its own (`portable/sound_files.hpp`): `Convolver::io` gives it
  libsndfile, and the public `load_configuration()` reads as before.
  Installed packages gain `libconvolver_configuration.a`, which
  `Convolver::io` links.

### Changed

- The version is now 0.6.0, a deliberate minor step for the library changes
  below (jrp, 25 October 2026: old interfaces are not kept while the API
  settles). CMake consumers now request `find_package(Convolver 0.6)`; the
  package keeps its same-minor compatibility policy, so a consumer asking for
  0.5 no longer finds it.
- Library: `HostFaultReport` is again a plain struct of `current`, `earlier`
  and `held`. The 0.5.5 bridge to 0.5.0, where `HostFilter::observe()`
  returned `HostFaults`, is gone: read `current` and `earlier` instead.
- Library: `host_status()` takes the instance's `HostSettings` after
  `log_written`; without them it describes the default Gain and Mix.
- The log and the plug-in window give Gain and Mix to one decimal ("gain
  -76 dB, mix 56.1% wet"), not as the host's slider set them ("-75.967 dB",
  "56.1413%"); a value too small for one decimal keeps two significant
  digits, and a value near the end of its range keeps the decimals that
  show it is not the end ("99.96% wet", not "100% wet"). The window's
  sliders show the same text.
- Audio Unit: a failure identical to the line just logged is not logged
  again under a new request number (GarageBand restored one preset five
  times in a second on a mono track); accepted loads are always logged.
- Library (PLUG-12, ROBUST-3): new `encoded_host_state_size()`,
  `host_state_size_error()`, `HostFailureKind::too_large` and
  `HostProcessor::silence()`. `encode_host_state()` refuses an oversized state
  before allocating it and reserves the exact size otherwise.
- Continuous integration (CI-9): the Linux VST3 jobs and the Audio Unit
  sanitizer job build only what their tests run (the new
  `convolver_vst3_suite` and `convolver_au_suite` targets), the long tests
  carry a CTest `COST` so they start first, and `scripts/clang_tidy.py`
  analyses the portable files side by side. No test was dropped; on 25 October
  2026 the Linux VST3 jobs went from 76 s and 67 s to 39 s and 52 s and
  clang-tidy from 70 s to 49 s. See
  [build targets and test order](docs/develop/testing.md#build-targets-and-test-order).
- Continuous integration (CI-10): the Linux release candidate is built from
  the corresponding-source archive with networking disabled and CMake's
  default generator (Unix Makefiles), replacing the second, offline build of
  the same archive; the macOS candidates still use Ninja. Candidate builds
  also set `FETCHCONTENT_FULLY_DISCONNECTED`. See
  [candidate checks](docs/develop/testing.md#plug-in-candidate-checks).
- Continuous integration (CI-11): the native macOS job keeps its FFT
  backends in one job instead of splitting them, since the macOS runner limit
  would make the release candidates wait longer. See
  [CI triggers](docs/develop/testing.md#ci-triggers).
- Continuous integration (CI-14): builds and tests use every CPU of the
  runner (4 on the Linux and Windows runners, which ran 2 at a time before),
  through the new `use-every-cpu` action; the release scripts use the
  machine's CPU count. No test was dropped. On 27 October 2026 the macOS
  Accelerate and Ooura builds took 65 to 82 seconds instead of about 120;
  whole workflows vary too much from run to run to show a firm gain. See
  [build targets and test order](docs/develop/testing.md#build-targets-and-test-order).
- Continuous integration (CI-12): the four 60-second fuzz campaigns run two at
  a time, one per core of the Linux runner, so the step takes about 2 minutes
  instead of 4 and each campaign keeps about its usual number of executions.
  See [fuzz smoke tests](docs/develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).
- Continuous integration (CI-13): a pull request that changes only one plug-in
  format's adapter and tests (`plugins/au/` and `tests/au/`, for example), or
  only one workflow file, runs Lint and that format's workflows and skips the
  others; VST3 changes also run the candidates. Pushes to `main` still run
  everything. See
  [pull requests for one plug-in format](docs/develop/testing.md#pull-requests-for-one-plug-in-format).
- Continuous integration (CI-15): the fuzz workflow's scheduler moved from the
  workflow into `scripts/fuzz_campaigns.sh`, and `tests/fuzz/scheduler_tests.py`
  (in pre-commit) runs it with stand-in fuzzers to check that a failing
  campaign, one that leaves no exit status, or a missing fuzzer fails the step,
  and that no more campaigns run at once than the limit. A status file left by
  an earlier local run can no longer stand in for a campaign that recorded
  none. From the external review of PR #86. See
  [fuzz smoke tests](docs/develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).
- Release scripts (CI-16): `candidate.py`, `build_dependencies.py` and
  `tests/release/source_build.py` take `--jobs N`, which limits their builds
  and tests to N jobs at once for a machine with less memory; without it they
  still use every CPU. From the external review of PR #86. See
  [preview recipe](docs/develop/build.md#preview-recipe).
- Python tools (CI-17): removed 11 unused imports from the release scripts
  (`build_sources.py`, `candidate.py`, `symbols.py`, `verify_artifact.py`) and
  four test scripts, found by a Skylos scan on 29 October 2026 and confirmed
  with ruff. No behaviour changes. See
  [Python formatting and lint](CONTRIBUTING.md#python-formatting-and-lint).
- Python tools (CI-18): pre-commit, and so lint CI, now formats the maintained
  Python scripts and tests with the latest stable ruff (120 columns, single
  quotes) and lints them with every stable ruff rule except a short, explained
  list in `ruff.toml`. All 77 files were reformatted once, in a layout-only
  commit that `.git-blame-ignore-revs` lets `git blame` skip; the code then
  changed to meet the rules: f-strings instead of `%` formatting, explicit
  `check=False` on `subprocess.run`, keyword-only boolean options in the
  release and version scripts (`fetch=`, `push=`, `production=` and others), `Path.open`,
  comprehensions, sorted imports, and executable scripts that have a `#!`
  line. Like the Clang tools, ruff is not pinned, so a new release can turn
  Lint red. No behaviour changes. See
  [Python formatting and lint](CONTRIBUTING.md#python-formatting-and-lint).
- Build (FFT-3): on macOS the default FFT backend (`AUTO`) is now Apple's
  Accelerate for every build, the command-line tools and libraries included,
  as it already was for the plug-ins ([decision](docs/project/decisions.md#releases)). A Mac build
  therefore needs no FFTW installed. FFTW remains available with
  `-DCONVOLVER_FFT_BACKEND=FFTW`, and is needed for `--tune-fft`, FFTW wisdom
  and measured plans, which Accelerate builds reject. Linux and Windows are
  unchanged. Output differs from an FFTW build by float rounding only: at
  most 8.3e-7 of full scale in the recorded comparisons (about -122 dBFS),
  with no audible effect. The tools keep
  libsndfile for sound files on every platform. The native macOS CI job now
  builds FFTW explicitly and checks that the default build chooses
  Accelerate without FFTW. See
  [which libraries each build uses](docs/develop/build.md#which-libraries-each-build-uses).
- Continuous integration (CI-19): the Apple silicon (`macos-arm64`) release
  candidate is built on GitHub's `macos-15` runner instead of `macos-14`.
  GitHub retires the macOS 14 image on 2 November 2026; on 30 October its
  scheduled brownouts cancelled the candidate job before it ran
  ([runner-images#13518](https://github.com/actions/runner-images/issues/13518)),
  so pull requests showed the candidate workflow as cancelled. The package is
  still built for macOS 14 (`CMAKE_OSX_DEPLOYMENT_TARGET=14.0`) and the
  proposed minimum system is unchanged, but no CI job now runs a candidate on
  macOS 14 itself. See
  [candidate checks](docs/develop/testing.md#plug-in-candidate-checks).
- Audio Unit source (AU-8): `plugins/au/effect.cpp` is split by
  responsibility into `effect.cpp` (initialization, cleanup, stream formats,
  rendering, latency and tail), `effect_state.cpp` (preset checks, saved
  parameters, save and restore, a chosen file, installing or refusing a
  filter) and `effect_properties.cpp` (parameters, properties, the window's
  report, notifications and logging), still one `Effect` class (jrp's review,
  30 October 2026). The repeated "a failed first load plays silence" step
  and the refusals' logging are now one function each, and Core Foundation
  values are released by `cf_owned` (`plugins/au/core_foundation.hpp`)
  rather than by hand. No behaviour changes, except that the preset checks
  no longer create a string per key, which could crash if memory ran out.
  `au.adapter` now also checks that a state refused before it reaches the
  filter is logged.
- macOS plug-in source (AU-9): small simplifications with no change in
  behaviour. The AudioToolbox reader checks for a regular file in one place,
  the window builds its detail styles once, and the Audio Unit's property
  calls hand other scopes to `AUBase` first instead of testing the scope on
  every branch.

### Fixed

- Audio Unit (AU-9): the Status parameter's value names are refused,
  rather than crashing the host, if memory runs out while they are made.
- Plug-ins (PLUG-12): a filter too large for the saved state is refused when
  it is loaded, instead of loading and then failing every save. A `.cfg` may
  hold 64 Mi samples, but plug-in state holds at most 64 MiB (just under
  16,777,216 samples, all paths together), so a 16,777,216-sample WAV loaded
  in the Audio Unit and then its `ClassInfo` could not be saved (`-10851`);
  LV2's filter files had the same gap. `HostFilter::load` now checks the
  encoded size before installing anything, so the previous filter, or the
  silence of a failed first load, keeps playing and saving, and the failure
  (new kind `HostFailureKind::too_large`) explains the limit and what to
  change. `convolver-preset` refuses to write such a preset with the same
  explanation. Filters within the limit sound and save as before. Found in
  jrp's review of `6920a45`.
- Plug-ins (ROBUST-3): when an active instance's first load failed, VST3, the
  Audio Unit and LV2 built a silent filter to replace the unfiltered input; if
  memory ran out at that moment, the input kept playing while the status said
  the load had failed. They now call the new `HostProcessor::silence()`,
  which neither allocates nor fails, and the audio thread mutes at its next
  callback. Before any audio has played the output is silent from the first
  sample (the 64-sample ramp of `mute()` used to let the start of the input
  through there; LV2's run() refusing a first file met this); while audio
  plays it ramps to silence over 64 samples, as before. Otherwise the sound
  is unchanged. VST3's `setState` also no longer lets an out-of-memory
  exception escape while writing the log line. Found in jrp's review of
  `6920a45`; the lifecycle sequence `state-malformed-starved` reproduces it.
- Plug-ins: Status and the log's `Started at` line now say what Gain and Mix
  let through when no filter is heard. With no filter at Gain −20 dB the
  Audio Unit's Status said "audio passes unchanged" while the input played
  20 dB lower; it now says `No filter: audio passes at -20.0 dB (Gain and
  Mix)`. After a refused first load at Mix 50% it said "Silent" while the dry
  half played; it now says `Load failed, only the dry signal plays: ...`, and
  the start line says the dry signal still plays and at what level. A filter
  playing at Mix 0% adds `Mix 0%: only the dry signal plays`. The sound is
  unchanged. Found in jrp's review of 0.5.5.
- Builds: `CONVOLVER_SANITIZERS=ON` now compiles the Audio Unit adapter,
  AudioUnitSDK and their tests with ASan and UBSan, as it does the core;
  before, they were only linked with the sanitizer runtimes, so the adapter's
  parameter-state parser ran uninstrumented. The VST3 adapter's tests are
  instrumented too, and a VST3 sanitizer build no longer runs the SDK tools
  that load the bundle (as with `CONVOLVER_VST3_RUNTIME_VALIDATION=OFF`),
  since they cannot load an instrumented one: such a build used to stop at
  `moduleinfotool`. Found in
  jrp's review of 0.5.5.

## [0.5.6] - 2026-10-25

### Added

- Example presets: `repeat`, half the input at once and a quarter of it a
  quarter of a second later, so the filter is easy to hear (the `echo`
  example's 5 ms tap only colours the tone). The package now has 18 examples.

## [0.5.5] - 2026-10-25

### Fixed

- Supporting-library downloads (CI-6): a cached archive is reused only while
  it has the checksum of its first download. Before, `--latest` recomputed the
  checksum of whatever was in the cache, so an archive changed there was
  recorded as trusted under the original upstream commit; it is now refused,
  with how to download it again. Replaying a lock file already checked its
  recorded checksum. Found in jrp's review of 0.5.2.
- Supporting libraries and tools (CI-6): pre-releases are ordered by stage
  (alpha, beta, then release candidate), so a newer `2.0.0-rc1` is tried
  instead of `2.0.0-beta3`, which used to rank above it. Found in jrp's review
  of 0.5.2.
- Library: `HostFilter::observe()` compiles again in code written for 0.5.0,
  where it returned `HostFaults`. Its `HostFaultReport` now derives from
  `HostFaults`, counting every fault the call returns (current plus earlier),
  and a constructor takes `current`, `earlier` and `held` in that order, so
  code written for 0.5.1 to 0.5.4 compiles too, braces included. Two uses
  still do not: a pointer to `observe` as a member function returning
  `HostFaults`, and designated initialisers of `HostFaultReport`. Found in
  jrp's review of 0.5.2.
- Plug-ins: after a rejected replacement, a fault in the audio was logged
  saying that the status shows Fault, while the status rightly kept showing
  Load failed. The log now says that Load failed shows instead. Found in
  jrp's review of 0.5.2.

## [0.5.4] - 2026-10-25

### Added

- Audio Unit: Gain (−100 to +24 dB) and Mix (0 to 100% wet) parameters,
  saved with the project. Their defaults (0 dB, 100%) sound exactly as
  before.

## [0.5.3] - 2026-10-25

### Changed

- CLI: integer output below 20 significant bits (pcm8, pcm16, `--valid-bits`
  17 to 19) now gets triangular (TPDF) dither unless `--dither none` is given,
  so fades and quiet passages turn into steady hiss rather than distortion.
  This changes the bytes of such files. From 20 bits output is still plainly
  rounded, since plain rounding is already inaudible there. A note on stderr
  says when dither was added.
- CLI: Convolver now warns when dither or noise shaping is pointless (from 20
  bits) or when another choice keeps the noise at least 3 dB less audible at
  the output sample rate, and names that choice.

### Removed

- CLI: `--dither rectangular`, `--noise-shaping second-order` and
  `--noise-shaping sony-sbm`, which another choice always beats; naming them is
  an error that says why and what to use. The library's `Quantizer` keeps them.

## [0.5.2] - 2026-10-25

### Changed

- Builds use the newest release of every supporting library and tool instead
  of pinned versions (CI-6), so security and bug fixes arrive without a pin
  being moved: the VST3 SDK, `lv2`, AudioUnitSDK, FFTW, libsndfile, NSIS for
  the Windows installer (now 3.13, was 3.11), pluginval, `lv2lint` and the
  `lilv` packages. Each build records exactly what it used (version, commit
  and archive checksum) in its job summary, the source bundle's
  `source-manifest.json` and the package's `artifact-manifest.json` and
  `licenses/DEPENDENCIES.json`, and a recorded build can be repeated with
  `sources.py --lock`. The VST3 and Audio Unit plug-ins name their SDK in the
  Convolver log. The VST3, LV2, Audio Unit and candidate workflows also run
  every Monday, replacing the weekly Dependency updates check. Formal tests
  use full releases only: pluginval moves from 2.0.0-beta3 to 1.0.4, and the
  beta runs as an advisory check whose findings are warnings (the acceptance
  test, which needs pluginval 2, still runs on it). A new advisory LV2 job
  tests against the newest upstream lilv and its libraries. See
  [supporting libraries](docs/develop/build.md#supporting-libraries).

## [0.5.1] - 2026-10-25

### Changed

- Library: `HostFilter::observe()` returns a `HostFaultReport` (`current` and
  `earlier` faults) instead of `HostFaults`, and `HostFilter::load()` takes
  the `HostProcessor` that plays (optional), for the fixes below. Code that used the
  result as `HostFaults` no longer compiled.

### Fixed

- Plug-ins: after a filter loaded, a processing fault from before it (for
  example from the audio played while the new filter was being prepared) could
  turn the Status back to Fault at the next status check, though the new
  filter played cleanly. An accepted load now takes one snapshot of the fault
  counts for both the audio thread and the control side: earlier faults are
  still logged, saying that the status does not show them, but no longer
  shown or signalled. Found in jrp's review of
  0.4.43.
- Audio Unit and VST3: the audio thread could set Status to Fault while a
  load failure was showing, hiding Load failed until the next status check.
  Load failed now stays ahead of Fault on the audio thread too, as LV2's
  status port already did. Found in jrp's review of 0.4.43.
- VST3 and Audio Unit: when an empty instance's first load failed, faults
  counted while that filter was prepared were left out of the log. They are
  now explained like any other. Found in the independent review of the fixes
  above.
- The plug-in and troubleshooting guides said the VST3 cannot show why a load
  failed and that the speaker check's note cannot be shown. Both now point to
  the Status parameter and the Convolver log. Found in jrp's review of 0.4.43.

## [0.5.0] - 2026-10-24

The series the first preview comes from. It includes the changes tagged
automatically as 0.4.1 to 0.4.46.

### Added

- Cheaper startup, reset and filter replacement (PERF-5): until the input
  history has filled, the engine multiplies only the filter partitions that
  can meet audio, and a reset marks the history empty instead of clearing it.
  Output is bit-identical (for filters whose spectra are finite, which is
  every filter that can produce finite output). With 65,536-tap filters in a 6×6 matrix and
  64-frame host calls, the calls after a start, reset or replacement cost
  about half as much on average while the history refills, and a reset itself
  takes microseconds instead of a time that grew with the filter
  ([measurements](docs/develop/fft-backends.md#startup-reset-and-filter-loading)).
  Steady processing is unchanged.

- Faster filter loading (PERF-6): a configuration that names one file in
  several paths, such as a crossover with one multichannel IR file, opens and
  decodes the file once instead of once per path. Diagnostics and limits are
  unchanged.

- The VST3 marks an output channel silent in each call that leaves it all
  zero (VST3-10), so hosts that honour the flags can skip work after it. A
  filter's tail that is still sounding is never marked, however silent the
  input. Bypass keeps the convolution running, as before.

- Faster processing on x86-64 (PERF-4): the partition multiply-accumulate,
  which takes 85-99% of the time for room-correction filters, is also compiled
  for AVX2 with fused multiply-add (FMA) and used on CPUs that have both (most
  Intel and AMD CPUs since 2013-2015). Release binaries still run on any x86-64
  CPU. Measured about 1.3 (Clang) to 1.4 (GCC) times faster on average for
  32k-128k-tap stereo and 2→6 crossover filters at B = 256, 1.1-1.6 per
  workload, with the Ooura FFT the plug-ins use; FMA gives 3-6% of that ([measurements](docs/develop/fft-backends.md#compiler-flags-and-avx2-kernels)).
  **Numerical effect:** output on CPUs with AVX2+FMA differs from other CPUs by
  float rounding only: at most 7.5e-7 of the peak (-122.6 dBFS) on 64k-512k
  filters, so 24-bit renders may differ in their lowest bits, with no change in
  accuracy against the double-precision reference (owner's decision,
  24 October 2026). A new test, `native.kernel_divergence`, fails on anything
  larger. `-DCONVOLVER_AVX2_KERNELS=OFF` leaves the AVX2 kernels out.
  `convolver_benchmark` gained room-correction cases and reports which kernels
  ran.

- The VST3 and the Audio Unit say why they are silent or refused a preset
  (DIAG-1): a read-only **Status** parameter, which hosts can list with the
  plug-in's controls, shows one line such as "Silent: the filter is for 44100
  Hz, the host runs at 48000 Hz", and the new Convolver log
  (`~/Library/Logs/Convolver/Convolver.log`, `%LOCALAPPDATA%\Convolver\Convolver.log`
  or `~/.local/state/convolver/convolver.log`; `CONVOLVER_LOG` overrides)
  records every load, start and failure with the full explanation, ready to
  send with a report. See [status and the Convolver log](docs/use/plugins.md#status-and-the-convolver-log).

- The plug-ins report problems met while playing (DIAG-1, jrp's review of
  24 October 2026): output too large to represent (extreme input or filter
  gain), invalid input samples, and (Audio Unit) buffers that do not match
  the host's setup. The Status parameter (and LV2's status port) shows a new value,
  **Fault**, instead of "Filter loaded" or "No filter", and the log explains what happened,
  how often and what to change. The audio is silenced as before; nothing is
  logged from the audio thread. The Convolver log also names the process and
  numbers each load and refusal, its first line in each process says what ran
  (version, FFT library, CPU kernels), and when the file cannot be written
  the Status text says so and the lines go to standard error.
  `convolver --version` and the JSON inspection report (`cpu_kernels`) name
  the CPU kernels.

- More output formats (IO-3): `--output-format` takes `CONTAINER-TYPE`, with
  `wav`, `rf64`, `w64`, `aiff`, `caf`, `flac` and `raw` containers and `pcm8`,
  `pcm16`, `pcm24`, `pcm32`, `float` and `double` samples (FLAC holds 8 to 24
  bits; raw has no 8-bit type). RF64, W64, CAF, FLAC and raw have no 4 GiB
  limit. The existing names, such as `wav-pcm24` and `raw-float`, are
  unchanged. In the library, `AudioWriter` also takes an `OutputEncoding`.
  FLAC needs a libsndfile built with FLAC. The preview packages stay without
  one (IO-6, decided): `--version` says whether it is available,
  `available(Container)` tells the library, and without it a `flac-` format is
  refused before any file exists; opening a FLAC or Ogg input or IR says the
  codec is missing and how to convert or rebuild. The command-line guide's
  **FLAC and Ogg** section describes both workarounds.
- Raw sample types (IO-4): `--raw-format` reads `--raw-input` streams as
  `s16`, `s24`, `s32`, `f32` or `f64`, little- or big-endian (`AudioReader`'s
  new `RawEncoding` argument), and `raw-pcm16`, `raw-pcm24`, `raw-pcm32` and
  `raw-double` write them.
- Raw filters (IO-4): a configuration's filter ending in `.pcm` or `.raw`, and
  not starting with a sound-file header, is read as mono at the configuration's
  sample rate, as little-endian float32 (`f32le`) by default, the type DRC and
  BruteFIR write. Rendering prints which type each raw filter was read as, and
  `--inspect` shows it (`raw_format` in JSON). `--raw-filter-format` (render
  and inspect) chooses another type with the `--raw-format` names; in the
  library, `ConfigurationSelection::raw_filter_encoding` and
  `InspectionOptions::raw_filter_encoding`, with `parse_raw_encoding()` and
  `raw_encoding_name()`. `--profile portable-paths` warns that other `.cfg`
  readers expect sound files (`raw-filter`).
- An LV2 plug-in for Linux (LV2-1), built with `-DCONVOLVER_BUILD_LV2=ON`: one
  bundle, `convolver.lv2`, with a plug-in for each fixed layout (1→1, 2→2, 1→2,
  2→4, 2→6, 2→8, 6→6 and 8→8). Filters load from presets written by
  `convolver-preset` and from saved sessions; bypass, 256-frame latency
  reporting and a status port; rejected loads are explained in the host's log.
  See [how the plug-ins behave](docs/use/plugins.md).
- LV2 filter files (LV2-2): a **Filter** parameter that hosts such as Ardour
  and Jalv show as a file chooser, loaded on the host's worker thread with a
  Loading status while it is prepared; rapid choices load only the newest,
  and an overtaken load is never heard. Sessions save the file's path
  (mapped with `state:mapPath`) beside the embedded filter, and restoring
  falls back on the file when the embedded copy cannot be used; a path alone
  restores too, as PipeWire's `filter-chain` `config` passes it. The plug-in
  now links libsndfile. For adapters, `HostProcessor::prepare()` and
  `publish()` split `replace()` in two, and `SupersededRequest` abandons an
  overtaken request.
- `HostProcessor::mute()`, an audio-thread call: the current filter ramps to
  silence over 64 samples until the next published filter fades in. The LV2
  adapter uses it when `run()` refuses a first filter file.
  `HostProcessor::publications()` and `mute(through)` let an adapter mute
  only what was published before it checked its own state.
- Lifecycle sequence tests (PLUG-7): `native.host_lifecycle`,
  `vst3.lifecycle`, `lv2.lifecycle` and `au.lifecycle` run every short
  sequence of host operations, and long seeded ones, against a reference
  model of the shared adapter contract, checking the saved filter, the failure
  record, what is audible and that audio-thread calls neither allocate nor
  lock ([testing](docs/develop/testing.md#lifecycle-sequences-plug-7)).
- An Audio Unit (AUv2 effect) adapter for macOS, `Convolver.component`
  (AU-1, AU-2): build with `-DCONVOLVER_BUILD_AU=ON` and the pinned
  AudioUnitSDK, then load `.aupreset` files from `convolver-preset` through the
  host's preset menu. 1 to 8 channels per side (counts may differ), latency and
  tail reported in seconds, bypass with the same 64-sample ramp and delayed dry
  signal as VST3 (left to the host to save, as is usual for Audio Units), and
  the shared rules for presets that do not fit. The component is distributed
  under GPL-3.0-or-later because it includes
  AudioUnitSDK (Apache-2.0). See the [AU guide](docs/use/plugins.md). Not yet tested in
  named hosts.
- `HostFilter`, `HostLayout` and `unfiltered_configuration` in
  `convolver/host.hpp`: the control side of the shared plug-in adapter
  contract (which filter a load replaces, what is saved and what plays), for
  every format adapter to reuse. `HostState::failed` marks a saved failed
  first load (encoded as host state version 2). `HostProcessor::replace` now installs a filter
  directly, without warm-up or crossfade, when no audio has been processed
  since construction or `reset()`.
- `HostFailure` and `HostFilter::failure()` (PLUG-8): every failed load, and
  every reason a plug-in plays silence, is kept as a value (what failed, the
  request it belongs to, whether a working filter still plays, and the
  explanation), not just a string. The VST3, LV2 and AU adapters record it
  on every failure path, including activation failures VST3 used to drop.
  Where it is shown is DIAG-1's decision. `decode_host_state` throws
  `NewerHostState` for state from a newer Convolver, and the message says to
  update Convolver rather than calling the state damaged.
- `speaker_notice` and `HostFilter::notice()` (VST3-1) compare the speakers a
  filter's outputs are labelled for (the `.cfg` channel mask) with the host's
  output speakers, now carried in `HostLayout`, and explain which outputs
  land on which speakers. Advice only: nothing is rejected, and channels stay
  routed by bus index. The VST3 adapter passes its speaker arrangements to it;
  it has no way to show the notice yet.
- `convolver-equalizer-apo` (HOST-4) exports an IR, `.cfg` or configuration
  list as an [Equalizer APO](docs/use/equalizer-apo.md) configuration, so Windows
  can apply the filter to everything a device plays. The device's sample rate
  and channel count choose the list entry (on a device with more channels,
  the largest entry that fits); a device that no entry fits, and device
  channels a filter does not use, are silenced rather than played unfiltered. Checked against a model of Equalizer APO and, on Windows CI, in
  Equalizer APO 1.4.2's own filter code through its `Benchmark.exe`.

- `convolver-preset` (PLUG-3) makes self-contained presets for VST3
  (`.vstpreset`), Audio Units (`.aupreset`) and LV2 (`NAME.preset.lv2`
  bundles) from one IR, `.cfg` or configuration list, several formats in one
  run, and converts an existing `.vstpreset` to the other formats. All carry the
  same encoded filter. It needs no plug-in SDK, so AU and LV2 builds can ship
  it. `convolver-vst3-preset` is now the same tool under its old name, with the
  same arguments and byte-identical output. See [plug-in presets](docs/use/presets.md).

- Pinned plug-in format dependencies (PLUG-6): AudioUnitSDK 1.4.0 and the
  `lv2` 1.18.10 headers by commit and checksum in the release policy, and the
  test tools `lilv` 0.24.22 and `lv2lint` 0.16.2 in new LV2 and Audio Unit CI
  workflows. A weekly Dependency updates workflow reports newer upstream
  releases of every pin and keeps one tracking issue current. See
  [pinned format dependencies](https://github.com/jrp2014/convolver/blob/v0.5.0/docs/develop/build.md#pinned-format-dependencies)
  (0.5.2 replaced the pins, see above).

- First-use examples in `examples/first-use/`: a mono IR, a stereo
  configuration and a stereo-to-six-channel crossover, each with an input and
  the expected output. CI runs the documented commands as written and checks
  the results.
- A shared test harness for plug-in adapters (PLUG-2): every format is checked
  against a direct double-precision convolution and the CLI's renders of the
  same examples, with the lifecycle cases of the shared adapter contract. The
  VST3 adapter runs it as `vst3.harness`.
- One registry of permanent plug-in identities and channel layouts,
  `plugins/identities.json` (PLUG-4): VST3 class IDs, AU codes `aufx`/`Conv`/`CONV`,
  LV2 URIs and fixed layouts, macOS bundle and installer identifiers, Windows
  installer keys and the speaker meaning of each WAV channel-mask bit in VST3, AU
  and LV2. The build generates `convolver_identities.hpp` from it, and new tests
  (`identities`, `vst3.identities`) stop identities from changing or being copied
  elsewhere. See [plug-in identities](docs/develop/plugin-identities.md); modified
  builds should change the product identities it lists.
- VST3 CI loads the plug-in in an independent host as well as Steinberg's SDK:
  [pluginval](https://github.com/Tracktion/pluginval) at strictness 10 on
  Linux, macOS (with real-time-safety checks) and Windows, plus acceptance
  renders that load real filters through the host and match a direct
  convolution. Optional locally via `-DCONVOLVER_PLUGINVAL`.
- The SDK validator also runs in extensive mode (`vst3.validator.extensive`).
- Unsigned VST3 preview distributions: pinned dependencies and corresponding
  source, relocatable archives with synthetic presets, unsigned installer
  templates, and a read-only candidate workflow on Windows, Linux and both macOS
  architectures (`-DCONVOLVER_PACKAGE_PLUGINS=ON`, preset `release-vst3`).
  Production qualification (signing, clean machines, named hosts) remains open.
- macOS VST3 bundles are re-sealed with an ad-hoc signature after the SDK adds
  `moduleinfo.json`, and `vst3.signature` verifies the seal.

- `sound_file_header()` in `convolver/io.hpp`: whether a file's content is a
  sound file (libsndfile recognises it, or it starts with a container marker),
  opening regular files only.

### Changed

- The version is now 0.5.0. CMake consumers now request
  `find_package(Convolver 0.5)`; the package keeps its same-minor
  compatibility policy, so a consumer asking for 0.4 no longer finds it.

- Host testing names jrp's first hosts on macOS: REAPER and Audacity for the
  VST3 plug-in and GarageBand for the Audio Unit, each with a step-by-step
  [smoke test](docs/develop/host-testing.md#smoke-test-in-each-first-host)
  (HOST-1, AU-4); they replace REAPER and Carla. The
  [presets guide](docs/use/presets.md#load-it-in-a-host) now says where
  Audacity 4 finds `.vstpreset` files, since its preset menu cannot import
  them.
- Documentation for contributors and coding agents moved into `docs/develop/`
  (DOC-4, first step): one [build page](docs/develop/build.md) for every build,
  the [plug-in adapter contract](docs/develop/plugin-contract.md) taken out of
  the plug-in formats plan, and writing rules for each reader and kind of page.
  The `todo-item` and `review-change` skills now only point to the workflow
  guide, which holds the full procedures. The
  [index](docs/README.md#moved-pages) lists the old and new paths.
- User documentation moved into `docs/use/` (DOC-4, second step), written for
  people applying filters: a [start page](docs/use/README.md), two tutorials
  ([first filter](docs/use/first-filter.md), whose commands the
  `examples.first_use` test now runs, and
  [first sound in a host](docs/use/first-sound-in-a-host.md)), one
  [install page](docs/use/install.md) and one
  [plug-in page](docs/use/plugins.md) for VST3, AU and LV2,
  [troubleshooting](docs/use/troubleshooting.md) and [limits](docs/use/limits.md).
  The CLI guide is now the [command-line reference](docs/use/command-line.md);
  its engine API and limits moved to the [library guide](docs/develop/library.md),
  and test evidence from every user page to [testing](docs/develop/testing.md).
  Preview packages ship all of `docs/use/` instead of `docs/plugins/` and two
  copied guides. The tools' messages point at the new paths.
- Status, direction and decisions moved into `docs/project/` (DOC-4, third
  step). One [status page](docs/project/status.md) replaces `REVIEW.md`,
  release readiness and the modernization page; the dependency choices are in
  the [implementation map](docs/develop/architecture.md#dependency-choices). A
  new [decisions log](docs/project/decisions.md) lists jrp's decisions with the
  page that explains each. Releasing is a [developer guide](docs/develop/releasing.md),
  and the 18 October release record and the old review are archived. History,
  the convolverPipe page and open plans are in `docs/project/`.
- Shorter front pages (DOC-4, last step): the [README](README.md) is a start
  page with one example, `AGENTS.md` links to the guides instead of repeating
  them, and [TODO](TODO.md) holds open work, with finished items reduced to one
  line each under "Done". `check_repository.py` now requires every maintained
  page to sit in a reader folder, and fails when a page whose paths it checks
  is missing.
- Preview packages (PLUG-5) ship `convolver-preset` and
  `convolver-equalizer-apo` in `bin/`, with the presets and Equalizer APO
  guides in `docs/`, instead of `convolver-vst3-preset` (still built from
  source, for existing scripts). The example presets are made with
  `convolver-preset` (byte-identical). Release checks run both tools from a
  relocated package.
- The VST3 plug-in follows the shared adapter contract (VST3-7). With no preset
  loaded and different input and output counts (for example a 2→6 crossover)
  it plays silence instead of passing input *c* to output *c*. Once the plug-in has
  been activated, a preset at the wrong sample rate or channel counts is
  rejected while it is inactive too, keeping the last valid one; if none was loaded, it plays
  silence. A project restored at the wrong rate or layout now activates and
  plays silence, keeping its filter, instead of failing to activate. A preset
  loaded after activation but before any audio plays from the first sample
  without a crossfade. Saving after a failed first load records the failure,
  so reopening stays silent: host state version 2, used only for that state
  (other states are still written as version 1). The shared decisions are in
  the host layer (`HostFilter`), for LV2 and AU to reuse.
- Bypass with unequal input and output counts (for example a stereo-to-six
  crossover) now ramps every output to silence instead of passing input *c* to
  output *c*, so bypassing a crossover never sends full-range audio to a driver.
  Filters with equal counts bypass as before.
- The macOS VST3 bundle identifier is now `io.github.jrp2014.convolver.vst3` and the installer package
  identifiers are `io.github.jrp2014.convolver.pkg.vst3` and `.pkg.tools` (they were
  `org.convolver.*`, a domain the project does not control). No release used
  the old identifiers.
- The preserved 2005–2006 Windows source moved from `convolver/` to
  `historical/`, so the top-level folder named after the project is no longer
  the old code. Open plans moved from `docs/superpowers/` to `docs/plans/`.
  Nothing in the portable build, its targets or installed files changed.
- Plug-in builds no longer use FFTW by default. `CONVOLVER_FFT_BACKEND` now
  defaults to `AUTO`: FFTW for ordinary builds, and the built-in Ooura FFT
  (Accelerate on macOS) when `CONVOLVER_BUILD_VST3` is ON, because another
  plug-in or the host may use the same FFTW and its planner is not thread-safe.
  `-DCONVOLVER_FFT_BACKEND=FFTW` still selects FFTW (CMake warns in a plug-in
  build). A new `vst3.fft_symbols` test checks the plug-in binary, and the
  release candidates for Linux and Windows use Ooura. Output differs from FFTW
  builds only by floating-point rounding (worst error against the double oracle
  2.6e-7 with Ooura on Linux x86-64).
- `convolver --help` describes `--gain-db no-clip` as the filter's worst-case
  peak bound rather than a gain that can never clip, matching the CLI reference:
  it assumes input within full scale and leaves no dither or shaping headroom.
- When no filter matches the input (for example a mono IR given for stereo
  audio, a configuration for another sample rate, or a list with no matching
  entry), the CLI, inspection and the VST3 preset tool now say what each
  candidate offers, what differs and what to change. For a mono IR used with
  multichannel input the message includes a ready-to-save configuration that
  applies the IR to every channel. Missing IRs, out-of-range IR or route
  channels, IR sample-rate mismatches and `-` without RAW options also get
  fuller explanations.

### Fixed

- Out of memory while creating the integer-sample quantizer (every PCM output
  of the CLI) ended the program instead of reporting the error, because
  `Quantizer::reset()`, which cannot throw, allocated. The constructor now
  allocates everything. Found in jrp's review of 0.4.43.
- `--gain-db auto` reported 0 dB for a filter whose coefficients are so large
  that convolving full-scale noise overflows single precision, because the
  peak search ignored the resulting NaN. `estimate_attenuation` now refuses
  such a filter with an explanation (`std::overflow_error`). Found in jrp's
  review of 0.4.43.
- Release candidates checked only the names of the kept debugging symbols, so
  an empty `.debug` file or a `.dSYM` holding only `Info.plist` passed. Each
  symbol file must now hold debugging information and match its packaged
  binary by GNU build ID, Mach-O UUID or PDB signature and age. Found in jrp's
  review of 0.4.43.
- A filter named `.pcm` or `.raw` that is a sound file with a header (Sun/NeXT
  AU, for example) was read as raw samples, header included, because only some
  headers were recognised. Now such a filter is read through its header when
  libsndfile recognises its content or it starts like a WAV, RF64, BW64, AIFF,
  CAF, AU, FLAC or Ogg file, and a header that cannot be read is an error that
  says so, not raw samples. Found in jrp's review of 0.4.37.
- `--inspect --syntax-only --profile portable-paths` opened `.pcm` and `.raw`
  filters to look for a header, so a named pipe with such a name blocked it.
  Syntax-only inspection now opens no filter file; its `raw-filter` warning
  goes by the name and says it did not check. Found in jrp's review of 0.4.37.
- LV2: after an empty state (or an empty state map) was restored while the
  plug-in was running, a filter file that `run()` refused in the same cycle (no
  worker, or a full worker queue) let the unfiltered input play until the
  next save or other control call, though the status said "load failed".
  `HostProcessor::mute()` now also silences a filter published before it that
  the audio thread had not yet picked up; only a filter published after it
  ends the silence. Found by the PLUG-7 lifecycle sequences.
- VST3 and AU: a filter state restored while inactive could be kept although
  `setState` or `RestoreState` reported a failure, when memory ran out while
  working out its tail after the load. The tail is now worked out first, so
  the request is rejected whole. Found by the PLUG-7 lifecycle sequences.
- VST3 (VST3-8, from the [VST3 API audit](docs/review-2026-10/vst3-api-audit-2026-10-23.md)):
  the reported tail now includes the 256-sample latency, so a host that stops
  or suspends the plug-in after its tail no longer cuts the last 256 samples
  (an instance with no filter used to report 0). Input that the host flags as
  silent is still read, so a host that flags audio by mistake no longer
  silences it. The Bypass parameter accepts its own "Off" and "On" text (and 0
  or 1) and refuses other text on every platform, and
  the controller's state calls succeed instead of reporting "not
  implemented". The filtered sound is unchanged.
- A filter loaded while the host had stopped processing in the middle of a
  crossfade now plays from the first resumed sample. Before, `HostProcessor`
  kept the interrupted transition after `reset()`, so the old filter went on
  playing until that crossfade finished (jrp's review of 0.4.19 measured 1.004
  where 3 was expected). Any reset now ends the transition and the newer filter
  plays at once; that includes the internal recovery reset after non-finite
  output, which used to bring the old filter back and repeat the crossfade.
- LV2: choosing a filter file in a host without a worker thread, or when the
  host could not queue the load, left an instance with no filter passing its
  input through until the next save or restore. It now falls silent within
  64 samples, as for any first filter that cannot be used; a filter that was
  already playing keeps playing. `HostProcessor::mute()` is the new
  audio-thread call that does this.
- LV2: restoring a state or preset with none of Convolver's properties now
  resets the plug-in to an empty instance (no filter, failure or path; a file
  still loading is abandoned), as the LV2 state specification requires. It
  used to keep the previous filter.
- `HostFilter::load` builds everything it keeps before its install callback
  publishes the new filter, so running out of memory can no longer leave one
  filter playing while another is saved (jrp reproduced a restored failed
  state saving the old filter while the output was silent).

## [0.4.0] - 2026-10-19

### Added

Features that only the historical engine had:

- Dither on every integer output format (PCM8, PCM16, PCM24, PCM32), with the
  historical engine's five error-feedback noise shapers: `--noise-shaping
  simple|second-order|third-order|ninth-order|sony-sbm`. Dither stays off by
  default. `convolver::Quantizer` generalizes `Pcm16Quantizer`, which remains.
- `wav-pcm8` output and `--valid-bits N`, which writes WAVE_FORMAT_EXTENSIBLE
  with fewer valid bits than the container (for example 20 bits in 24).
- `--gain-db auto` (the historical white-noise optimum-attenuation estimate,
  seeded so it is repeatable) and `--gain-db no-clip` (an exact bound that
  cannot clip), from `convolver::estimate_attenuation`.
- `--output-channels N` and `ConfigurationSelection` choose a list entry by
  output channel count as well as rate and inputs; inspection reports it.
- `--fft-plan exhaustive`, `--fft-time-limit SECONDS`, `FftPlanning::exhaustive`,
  `FftOptions::time_limit_seconds` and `tune_fft`.
- `-DCONVOLVER_FFT_BACKEND=OOURA`: Ooura's FFT built in (permissive licence),
  for a core, CLI and plug-in with no FFT library dependency. FFTW stays the
  default.
- VST3 buses of 1 to 8 channels each, with unequal input and output counts.
  `HostProcessor` accepts unequal counts. `convolver-vst3-preset` gains
  `--sample-rate`, `--input-channels` and `--output-channels`.

### Changed

- PCM24 output is now quantized by Convolver (round to nearest, ties up) instead
  of libsndfile's float conversion; samples can differ by at most 1 LSB.
- CMake consumers now request `find_package(Convolver 0.4)`; the package keeps
  its same-minor compatibility policy.

## [0.3.1] - 2026-10-18

### Fixed

- Inspection reports direct sound-file decoding and filesystem resolution failures
  as located diagnostics, and continues with later entries in `--all-configurations`.
- Invalid UTF-8 warnings now affect `--warnings-as-errors`, JSON validity, stderr
  and the exit status, including invalid bytes in diagnostic messages.
- Triangular PCM16 dither explicitly sequences random draws for reproducible output
  across compilers. Its noise distribution is unchanged; a compiler that previously
  evaluated the right operand first will produce a different seeded noise sequence.

## [0.3.0] - 2026-10-18

### Added

- Configuration compatibility corpus (`tests/compatibility/`): generated
  fixtures rendered through the CLI and checked against an independent
  direct-convolution reference at several block sizes and both latency modes.
- `convolver --inspect` and `convolver --validate`, with `--json` (report
  schema 1), `--syntax-only`, `--all-configurations`, `--sample-rate`,
  `--input-channels` and `--warnings-as-errors`.
- `convolver::inspect_configuration` (`convolver/inspection.hpp`, in
  `Convolver::io`) and `convolver::analyze_configuration` (core).
- `--profile portable-paths` (`InspectionProfile::portable_paths`): warnings
  for absolute references, references leaving the configuration's folder,
  letter-case differences from the names on disk, and names Windows cannot
  store. `docs/configuration-compatibility.md` records the evidence for each
  accepted feature and what is known about other readers of the format.
- A diagnostic for configurations in the format before 3.0 (no delay lines)
  that says which lines to add.
- Boundary tests for every configuration, list, IR and host-state limit
  (`native.limits`), and libFuzzer harnesses for configuration text, lists,
  host state and IR files (`tests/fuzz/`, `-DCONVOLVER_BUILD_FUZZERS=ON` with
  Clang). Committed seeds and regression inputs replay on every compiler as
  `fuzz.replay.*`; CI fuzzes each target for 60 seconds.
- `--speaker-map config` labels WAV output channels with the configuration's
  hex channel mask (WAVE_FORMAT_EXTENSIBLE), and `OutputMetadata` is an optional
  fifth `AudioWriter` argument, after `QuantizationOptions`. Masks must name
  one known speaker per output channel; samples keep their order. Without the
  flag output is unchanged.
- `AudioReader::declared_frames()`: the frame count a file header declares.
- Two new resolution limits: 16 MiB of configuration text read per load or
  inspection, and 256 inspection diagnostics followed by one
  `diagnostics-truncated` warning.

### Changed

- Configuration errors report the file, physical line and column, and the
  chain of enclosing lists. Rejected Windows drive, UNC and rooted paths name
  an existing relative replacement when one is found beside the configuration.
- `Convolver::io` now links `Convolver::core`. The package version is 0.3.0;
  consumers ask for `find_package(Convolver 0.3 ...)`.
- The configuration parser is separated from list resolution and IR decoding
  (`portable/config_parse.*`, private). Selection and rendered audio are
  unchanged, and so are accepted configurations, except that lists reading more
  than 16 MiB of configuration text in one load are now rejected (see Added).

### Fixed

- An IR whose header declares more than 64 Mi samples is rejected before any
  sample is read, instead of after decoding up to the limit.
- An audio file with an out-of-range sample rate or channel count in its header
  is reported as an unreadable file. Previously the exception escaped
  inspection and list traversal (found by fuzzing).

## [0.2.2] - 2026-10-18

### Added

- PCM16 dither. `Pcm16Quantizer` and `QuantizationOptions` define a deterministic,
  seeded policy with `none`, `rectangular`, `triangular` (TPDF) and
  `triangular_highpass` (the historical engine's "Triangular") modes.
  CLI options `--dither` and `--dither-seed`; dither is off unless requested.
- The benchmark reports p50, p99 and maximum single-block times and peak memory,
  and has two 6×6 matrix cases.

### Changed

- Paths with identical ordered input routes share one forward FFT and spectrum
  history. Output is bit-identical; dense routing matrices run 1.2 to 1.4 times
  faster and use less memory.
- PCM16 output rounds to nearest. libsndfile 1.2.2's float-to-PCM16 conversion
  with clipping enabled rounds down, which added a -0.5 LSB bias.

## [0.2.1] - 2026-10-18

### Added

- Automatic patch versions and annotated source tags on `main`, with one
  `VERSION` file shared by the CMake package, CLI and VST3 bundle.
- CLI and VST3 factory build identification including the Git revision and a
  marker for uncommitted changes. See [source versioning](docs/develop/versioning.md).

## [0.2.0] - 2026-10-18

### Added

- Portable host lifecycle (`Convolver::host`) and a VST3 adapter built against
  VST3 SDK 3.8.1, with adapter, preset and SDK validator tests in CI.
- Documented streaming contract for hosts.
- FFT backend notes and benchmarks (`docs/fft-backends.md`, `docs/benchmarks/`):
  FFTW build flags, planner modes, and packaging gaps on Apple Silicon and vcpkg.

### Changed

- Filter and history spectra are stored split per partition (real parts, then
  imaginary parts) so the multiply-accumulate vectorises. Output is unchanged;
  long filters run 1.15 to 1.3 times faster on x86-64.
- Windows CI builds FFTW with SSE2, AVX and AVX2.
- Windows VST3 builds link FFTW statically so hosts can load the plug-in.
- VST3 factory and macOS bundle versions now come from the CMake project version.
- The package version check accepts only matching minor versions, so consumers
  must now ask for `find_package(Convolver 0.2 ...)`.

## [0.1.0] - 2026-10-18

### Added

- Portable C++20 convolution core with FFTW and Apple Accelerate backends,
  FFTW wisdom support, and a libsndfile command-line tool.
- `.cfg` reader compatible with the historical Convolver format.
- CMake package exports (`Convolver::core`, `Convolver::io`), presets, and
  native oracle, sanitizer and historical regression tests on Linux, macOS and
  Windows.

### Fixed

- Windows FFTW DLL data imports, RAW pipe end-of-file handling, and portable
  documentation and config paths.

[Unreleased]: https://github.com/jrp2014/convolver/compare/v0.18.12...HEAD
[0.18.12]: https://github.com/jrp2014/convolver/releases/tag/v0.18.12
[0.18.10]: https://github.com/jrp2014/convolver/releases/tag/v0.18.10
[0.14.0]: https://github.com/jrp2014/convolver/releases/tag/v0.14.0
[0.13.24]: https://github.com/jrp2014/convolver/releases/tag/v0.13.24
[0.13.20]: https://github.com/jrp2014/convolver/releases/tag/v0.13.20
[0.13.12]: https://github.com/jrp2014/convolver/releases/tag/v0.13.12
[0.13.2]: https://github.com/jrp2014/convolver/releases/tag/v0.13.2
[0.12.12]: https://github.com/jrp2014/convolver/releases/tag/v0.12.12
[0.7.1]: https://github.com/jrp2014/convolver/compare/v0.7.0...v0.7.1
[0.7.0]: https://github.com/jrp2014/convolver/compare/v0.6.10...v0.7.0
[0.6.10]: https://github.com/jrp2014/convolver/compare/v0.5.6...v0.6.10
[0.5.6]: https://github.com/jrp2014/convolver/compare/v0.5.5...v0.5.6
[0.5.5]: https://github.com/jrp2014/convolver/compare/v0.5.4...v0.5.5
[0.5.4]: https://github.com/jrp2014/convolver/compare/v0.5.3...v0.5.4
[0.5.3]: https://github.com/jrp2014/convolver/compare/v0.5.2...v0.5.3
[0.5.2]: https://github.com/jrp2014/convolver/compare/v0.5.1...v0.5.2
[0.5.1]: https://github.com/jrp2014/convolver/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/jrp2014/convolver/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/jrp2014/convolver/compare/v0.3.1...v0.4.0
[0.3.1]: https://github.com/jrp2014/convolver/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/jrp2014/convolver/compare/v0.2.2...v0.3.0
[0.2.2]: https://github.com/jrp2014/convolver/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/jrp2014/convolver/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/jrp2014/convolver/compare/82cf94b...v0.2.0
[0.1.0]: https://github.com/jrp2014/convolver/commit/82cf94b
