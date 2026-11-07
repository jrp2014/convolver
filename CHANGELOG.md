<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Changelog

Changes to the portable C++ core, CLI and plug-ins. The portable package is
versioned separately from the historical Windows releases (1.x to 4.4), whose
release notes are archived in [docs/historical](docs/historical).

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Until 1.0 the C++ API may change between minor versions.

## [Unreleased]

### Added

- Host checks (HOST-1): a reusable 20-second, 48 kHz stereo listening WAV
  with quiet clicks and sustained tones, a standard-library Python generator,
  and [usage notes](examples/host-audio/README.md). The REAPER smoke-test
  guide links to it. This is source audio for manual checks, not a filter
  or a numerical reference; preview package contents are unchanged.
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
  `filter.vstpreset` made from each filter. The release tests render each
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
- Inspection (ROBUST-2, jrp's answers on PR #118): when `--all-configurations`
  stops at a limit, the `limit` error now ends "The check is incomplete: this
  entry and every entry after it were not checked.", and the selection reason
  starts "incomplete:". The guides add that the sample limit is cumulative
  decoding work, not peak memory, and that the entry crossing it is checked
  in full. The decisions log records jrp's answers on the limits, CAF and
  AIFF speaker labels, the new GAIN-3 item (`no-clip` to reserve integer
  headroom, in its own pull request) and the crossover journey in HOST-1's
  REAPER session.

- Repository checks (DOC-2): `scripts/check_repository.py` now fails when a
  guide, workflow or script names a CMake `-DCONVOLVER_…` option the build
  does not define (or a value it does not allow, such as an unknown FFT
  backend), when the build guide leaves an option out, when a command shown
  with `convolver`, `convolver-preset` or `convolver-equalizer-apo` uses an
  option the tool does not parse, or when a tool's guide leaves out one it
  does. The presets and Equalizer APO guides now mention `--help` and
  `--version`.
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
- The version is now 0.10.0 (jrp, 5 November 2026), the minor step for the
  library changes in DIAG-3 and DIAG-4 below, after 0.9.0. CMake consumers
  request `find_package(Convolver 0.10)`, and the first preview is cut from
  the 0.10 series. The LV2 plug-in reports minor version 10, which LV2
  accepts as a release.
- 0.9.0 was the milestone step before jrp's host smoke tests (HOST-1), from
  which the first preview was first to be cut (jrp, 5 November 2026). Its
  CMake consumers request `find_package(Convolver 0.9)`, and its LV2 plug-in
  reports minor version 9, which LV2 reads as a development version.
- Plug-in contract (PLUG-19, from jrp's simplification review of 5 November
  2026): a setting transition (bypass, mute, gain, mix) may end one frame
  after its ramp's last frame, which is inaudible (about 5.7 µs at
  176.4 kHz). It must still end exactly at its target and not click.
  `native.host_settings` allows that frame, so a later shared ramp helper
  need not reproduce PLUG-18's frame counting. Reported latency and channel
  alignment stay exact to the sample. The sound is unchanged.
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
- The macOS window's files moved from `plugins/au/` to `plugins/editor/mac/`
  (VST3-11), shared by the Audio Unit and VST3, which each build them with
  class names of their own. The setting rows can now mark the start and end
  of a slider drag for a format that asks (VST3 does); the Audio Unit's
  window behaves as before.
- Library (VST3-11): `HostRequest` gains `files`, the files an accepted
  file's filter was read from; the report gains the fields `filter path`
  and `filter files` for the window's Reload. Code that builds a
  `HostRequest` compiles unchanged.
- Library (VST3-4): `HostState` gains `settings`, an optional
  `HostSettings` that host state version 3 records (VST3 only), and
  `HostSettings` is declared before it in `convolver/host.hpp`. Existing
  code that builds a `HostState` compiles unchanged.
- Audio Unit window (AU-10): the Gain, Mix and Latency rows and their
  handling moved out of the window's layout code into their own file
  (`plugins/au/setting_rows.mm`), so the window can gain controls without
  growing one file further. No change in behaviour or sound.
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
- Tests (CI-31): the shared double-precision oracle works out each route's
  invariant parts once, with the same arithmetic, so `native.host_settings`
  takes about 4 seconds rather than 90 under GCC's AddressSanitizer, where
  it had passed its time limit.
- Tests (CI-30): `native.engine` replaces `operator new` with the array and
  sized `operator delete` too, from one shared file, so GCC's
  AddressSanitizer build runs the oracle tests instead of stopping on
  alloc-dealloc-mismatch.
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

[Unreleased]: https://github.com/jrp2014/convolver/compare/v0.7.1...HEAD
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
