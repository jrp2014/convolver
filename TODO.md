<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver TODO

Open work on the active portable implementation, with a stable ID for each
item. This is the shared backlog, not a promise that every idea will be
implemented. Finished items are listed briefly under [done](#done); what they
delivered is described in the guides and the [CHANGELOG](CHANGELOG.md).

**In short:** jrp has published three unsigned previews: 0.12.12 for macOS
(8 November 2026), 0.13.2 for macOS, Windows and Linux, with the new audio
engine (9 November 2026), and [0.13.12](https://github.com/jrp2014/convolver/releases/tag/v0.13.12), which adds the
Windows VST3 window and the LV2 plug-in (11 November 2026). The runtime
freeze of 9 November was lifted on 10 November; significant features come
before corner and edge cases
([decision](docs/project/decisions.md#releases)). The
[execution priorities](#execution-priorities) sort every open item into three
groups: checks and known defects in the published previews, work decided
or likely now the freeze has lifted, and parked or later work. The next
milestone is still an independent user running a published preview (REL-3).
[Status](docs/project/status.md) shows what users can run now and the checks
left, and [decisions](docs/project/decisions.md) what jrp has decided. Open
themes beyond the preview are the Windows and Linux windows and LV2, signing
and notarization, diagnostics (DIAG), files kept from releases (COMPAT),
other configuration readers (CFG), large files and resampling (IO), stress
testing (ROBUST), a parked FFmpeg filter (FFMPEG), FFT or performance work
only where measurements justify it (FFT, PERF), and the edges over the
leading convolution plug-ins (EDGE). The
[limitations table](#documented-limitations) lists current constraints.

You need not read this file whole: read the
[execution priorities](#execution-priorities), then search for your item's
ID. To take an item on, open a draft pull request titled with its ID; that is the
claim, and its description holds the handoff note. Record progress here as the
[workflow guide](docs/develop/workflow.md#recording-progress-in-todo) describes,
and confirm the current source first: other work may already have done part of
it. Compatibility takes priority over speed. Do not change the historical
archive to implement a portable feature. External links identify starting
points, not endorsements or a completed licence or performance assessment;
verify the exact release again when implementing an item.

## Execution priorities

This is the order for new work, not approval to start every item. Correctness
regressions take priority whenever discovered. Claim an item with a draft pull
request before starting so independent tasks do not duplicate changes.

**The freeze is lifted** (jrp, 10 November 2026,
[decision](docs/project/decisions.md#releases)): runtime changes, frozen
after the second preview on 9 November, can go ahead again now that
previews are out, with significant features before corner and edge cases.
Crashes, lost state, wrong audio, compatibility failures and dropouts are
still fixed at once, with a regression test where practical
([preview acceptance](docs/develop/releasing.md#what-blocks-a-preview)).
The sections below were ordered under the freeze, and their order stands
until the next triage.

**Next milestone** (jrp's [guidance](docs/review-2026-10/release-readiness-guidance-2026-10-22.md),
22 October 2026): an independent user installs and uses an exact preview
package, following only the documentation (REL-3). jrp published all three
previews before it ([decisions](docs/project/decisions.md#releases)), so the
walkthrough uses published files, and its findings join the other preview
feedback. The decision of 8 November kept the remaining host checks and
REL-3 as gates for later previews; jrp waived them for the second preview
too, and on 10 November 2026 for a third. Finishing a
dependable product matters more than another substantial refactor: move
code into shared layers only when two formats need the same behaviour (as
with `HostFilter`), do not generalise host lifecycle or release machinery
beyond the requirements in hand, and keep libsndfile at the I/O boundary and
C++20 in the core.

Triaged on 9 November 2026 against `main` at `b96b98e` (0.13.2). Each open
item is in one group below; the item itself holds the detail and its
progress.

### 1. The published previews: checks and known defects

Checks of what the previews ship, which change no shipped code and so go
ahead under the freeze, and the known defects users meet.
[Checks left](docs/project/status.md#checks-left-before-publication) on the
status page is the one list of the host steps that remain. They and REL-3
run on the published 0.13.12 files, the baseline jrp chose on 11 November
2026 in place of 0.13.2; the results recorded on 0.12.x builds are records of those builds, not
passes for 0.13, since PERF-2 changed the engine
([decision](docs/project/decisions.md#releases)).

| Item | What remains | Under the freeze |
| --- | --- | --- |
| **REL-3** | The independent walkthrough of the published 0.13.12 files; jrp chooses the tester | Goes ahead. A finding in the freeze's exceptions is fixed at once; the rest joins the preview feedback |
| **HOST-1**, **VST3-11** | REAPER and Audacity, status checks 1 to 4 | Host checks on jrp's Mac |
| **AU-4**, **AU-6** | GarageBand, status checks 5 and 8 | Host checks on jrp's Mac |
| **PERF-2** | Its focused REAPER and GarageBand session, status check 9; then the CPU indication beside the Latency menu and block sizes tuned on Apple silicon | The session goes ahead. The CPU indication changes the window and the tuning is optimisation, so both wait for the freeze to lift |
| **COMPAT-1** | Keep presets, a REAPER project and expected audio from the published previews (0.13.2's and 0.13.12's presets and audio are kept; the REAPER project is owed from 0.13.12, the baseline), with their own runner and the capture step in the publication steps | Tests and files only |
| **REL-4** | The [steps after publishing](docs/develop/releasing.md#after-publishing-make-it-findable) for the published previews | No code |
| **HOST-4** | The check on a real Windows device, now that the Windows package ships `convolver-equalizer-apo` | A check; needs a Windows computer |
| **VST3-17** | In 0.13.2 and earlier, in Audacity a filter chosen in the window plays but is missing from the export and the saved project. The fix (PR #177) is published in 0.13.12 and checked only by `vst3.adapter`; what remains is jrp's Audacity check on 0.13.12 (status check 10), and until then the guides keep the preset menu as the checked route | Lost state, one of the freeze's exceptions: brought forward and merged without an Audacity check ([decisions](docs/project/decisions.md#plug-in-behaviour)) |

### 2. Decided or likely, after the freeze

Runtime changes, so each waits until preview feedback arrives or jrp lifts
the freeze; feedback may change their order. Within each row the order is
the suggested one.

| Group | Items | Note |
| --- | --- | --- |
| Later previews, as decided | **VST3-12** (the Windows window, built: host and screen-reader checks left); **VST3-13** (the Linux window) only once LV2 is in a Linux package and users ask; **PLUG-10** (the meters, starting with the output peak and clipping; the gain modes are built, PR #188) | Moved to later previews on 6 November 2026 ([decision](docs/project/decisions.md#plug-in-behaviour)); the windows' order is [decided](docs/project/decisions.md#direction) in the [windows plan](docs/project/plans/2026-11-10-windows-linux-plugin-windows.md). The Linux packages use the host's own controls meanwhile; the Windows window shipped in 0.13.12, not yet tried in a host |
| LV2 in a preview | **LV2-3** and **LV2-4** (LV2-5 and LV2-7 are done) | LV2 is in the Linux package since 0.13.12; a source build was tried in PipeWire 1.6.8 on virtual devices, not yet in a desktop host ([decision](docs/project/decisions.md#releases)) |
| Other configuration readers | **CFG-1**, then **CFG-2** | CFG-2 waits for CFG-1 to show which readers matter |
| Only on request | **VST3-2** (switch filters from the host), **VST3-6** (replacing the old VST2 plug-in), **HOST-2** (CLAP), **LV2-6** (an LV2 window); distribution packages and macOS or Windows LV2 bundles (after LV2-5) | Each needs a stated user need and acceptance tests first |
| Consolidation | **PLUG-39**, the load and restore path, in the [plan](docs/project/plans/2026-11-13-load-restore-consolidation.md#steps)'s order | After the next preview is published, so the host sessions test code it has not changed; its targets and order wait for jrp's approval of the plan |
| Measurements, then optimisation | **PERF-3**, one measurement exercise (PERF-1's reruns joined it; EDGE-5 publishes its results); then, only where it shows a need, **FFT-2**, **VST3-5** and **PLUG-11** | PERF-3's resource budgets (callback tail latency, peak memory and preparation time while loading, replacing and resetting) decide whether further optimisation is worth doing. Its measurements change no shipped code; the optimisation they might justify waits |

### 3. Parked and later

| Group | Items | Note |
| --- | --- | --- |
| Parked by jrp until after REL-3 | **ROBUST-1**, **ROBUST-8**, **PLUG-25**, **PLUG-27**, **PLUG-29**, **PLUG-30**, **PLUG-36**, **HOST-6**, **CFG-3**, **DIAG-5**, **CI-32**; **PERF-7**, **PERF-8**, **PERF-16** to **PERF-19**; and **CI-34** apart from its first step (done) | jrp, 8 November 2026: "park any non-essential corner or edge case robustness tests", and "Park both"; PLUG-36 on 12 November 2026 ("park until after REL-3") for the items and CI-34's narrowing ([decision](docs/project/decisions.md#releases)); CI-34's remaining steps wait for his go-ahead. No existing test is removed, and a reproduced crash, lost state, unsafe output or wrong audio reopens one at once |
| The production gate | **REL-1**, the one production checklist (PLUG-5 and AU-5 joined it): Developer ID signing and notarization on macOS, Authenticode on Windows, verified minimum OS and CPU versions, the final licence and corresponding-source review; **PLUG-20** (the VST logo in the window, optional) | The previews are unsigned by decision ([releasing](docs/develop/releasing.md#production-gates)). Planning it changes nothing shipped |
| Edges over other convolvers | First the limitations users cite: **IO-2** (the rate-mismatch workflow; **EDGE-1** only if offline conversion proves inadequate), **EDGE-11** and **EDGE-10** (a candidate only when a measured limitation justifies it), then **EDGE-3** and **EDGE-13**; then **EDGE-12**, **EDGE-9**, **EDGE-2**, **EDGE-4** and **EDGE-5**; then **EDGE-6** to **EDGE-8**; **EDGE-14** and **EDGE-15** | jrp's goal (6 November 2026): be better than the most popular reverb plug-ins, in the ranked order of the [edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#ranked-edges-to-build), keeping its non-negotiables; each starts with the owner's decision its plan row names. Waits for REL-3 unless jrp lifts the freeze; feedback that asks for one of them (sample rates, true stereo, a filter's own delay, an output ceiling) moves it up |
| Supporting work | **IO-1** (large files) | Take it up when a reproducible defect or a user need calls for it |
| Code organisation | **PLUG-24** (`HostProcessor` in its own source file) | When `host.cpp` next changes for another reason; a runtime change, so not during the freeze |
| Parked by owner decision | **FFMPEG-2** to **FFMPEG-5**, **HOST-3** | Revisit only by an owner decision |

The ordering of the first preview's steps that this section held until
9 November 2026 is in the history of this file; the dated
[21 October review order](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md#recommended-order)
is superseded.

## User experience and distribution

- [ ] **REL-1 — Production release checklist.** One checklist for a signed
  production release of every format and tool, extending the candidate
  pipeline that already builds the unsigned previews for macOS, Windows and
  Linux (`packaging/release-policy.json`, `scripts/release/`,
  `.github/workflows/release.yml`; [releasing](docs/develop/releasing.md)).
  PLUG-5 (that pipeline) and AU-5 (the Audio Unit in it) finished for the
  previews and joined this item on 12 November 2026. **Remains**, at the
  [production gates](docs/develop/releasing.md#production-gates): verified
  minimum OS and CPU versions; dependency closure; the final licence and
  corresponding-source review; installation on clean machines; immutable
  release assets; and per platform, Developer ID signing and notarization
  on macOS, shared by the VST3 plug-in and the Audio Unit
  ([signing plan](docs/project/plans/2026-11-09-macos-signing.md), with four
  choices for jrp), and Authenticode on Windows. The draft signing scripts
  are at `f4519022c` (`git show f4519022c:scripts/release/sign-macos.sh` and
  `sign-windows.ps1`). **Worth doing when** jrp moves from unsigned
  previews to a production release. **Done when** one candidate run gives
  signed, verified packages for every shipped format, each gate recorded as
  passed, and jrp publishes it.
- [ ] **REL-3 — Independent walkthrough of an exact preview.** Someone who
  did not write the code or the guides follows the REL-2 journeys from the
  published 0.13.12 files (identified by their SHA-256 sums), on their own
  machine, using only the documentation; first, as jrp asked on
  8 November 2026, "installing, loading their own filter, saving and
  reopening with only the supplied instructions". jrp chooses the tester.
  **Done when** the record gives who (a name or "independent tester"), the
  package hash, OS, host and version, and for each step whether it worked,
  where they hesitated and what they could not do without help, with the
  evidence HOST-1 lists for each journey; each finding has fixed the guides
  or the product, with the affected steps repeated. This is the exit
  criterion of the [next milestone](#execution-priorities); jrp published
  the previews before it ([decisions](docs/project/decisions.md#releases)).
- [ ] **REL-4 — Make the preview findable.** Room-correction users and the
  `.cfg` format's existing users should find Convolver from search, the old
  SourceForge site, KVR and their forums. Done on the repository side: a README
  line for users of the old Windows Convolver, issue forms, the
  [social preview source](.github/social-preview.html) and the
  [steps after publishing](docs/develop/releasing.md#after-publishing-make-it-findable)
  (PR #64). jrp set the repository description, topics, website and social
  preview, and the SourceForge project summary now points here (24 October
  2026); the old SourceForge website stays as it is. Remaining, at each
  preview: the steps after publishing; for 0.13.12, which adds a format
  (LV2), the community posts and the LV2 list proposal those steps name.
- [ ] **HOST-1 — Named host matrix.** Versions and results for the named
  hosts: scan, load, preset and state restore, silence and tails, seek,
  variable callbacks, bypass, filter changes, multiple instances and
  unequal channel counts. SDK validation and pluginval alone do not
  complete it; Steinberg's VST3PluginTestHost is a convenient manual check
  ([host qualification](docs/develop/host-testing.md#steinbergs-sdk-hosts)).
  **Remains:** REAPER and Audacity on jrp's Mac, checks 1 to 4 and 10 in
  [checks left](docs/project/status.md#checks-left-before-publication), on
  the published 0.13.12 files; then Windows hosts. **Worth doing because**
  a preview's notes can claim only what a host has shown. **Done when**
  each check passes or jrp narrows it, each record keeping what
  [every report](docs/develop/host-testing.md#every-report) lists (package
  SHA-256, host and system versions, the expected output and its
  `convolver --compare` result, recovery from a refused filter). Results
  on earlier builds (REAPER 7.85 and Audacity 4.0.2, 6 to 8 November 2026)
  are records of those builds, linked from
  [host testing](docs/develop/host-testing.md#reaper-vst3).
- [ ] **HOST-2 — Additional formats.** CLAP only on request: every CLAP host
  found also loads VST3. Adapters are written against each format's own
  headers, not a framework. The editor this item once also held is built
  (VST3-11, VST3-12, AU-6); LV2's window is LV2-6. **Worth doing when** a
  user names a host that needs CLAP. **Done when** a CLAP adapter passes the
  PLUG-2 harness and the lifecycle tests.
- [ ] **HOST-3 — Windows Media Foundation transform (parked).** Needs more
  thought before any work starts. Wrap the engine as a
  synchronous dual-mode audio effect (MFT plus DMO) for Windows Media Player
  Legacy, DirectShow players and applications that insert effects through Media
  Foundation or WinRT. First name the target users. For system-wide room
  correction, the Equalizer APO export (HOST-4) is cheaper than an MFT or our
  own APO. See the
  [MFT plan](docs/project/plans/2026-10-21-media-foundation-transform.md).
- [ ] **HOST-4 — System-wide filtering through Equalizer APO.** Progress
  22 October 2026: `convolver-equalizer-apo` exports an IR, `.cfg` or list as
  an Equalizer APO configuration ([guide](docs/use/equalizer-apo.md)), chosen by
  the device's sample rate and channel count, silent when nothing fits.
  `export.equalizer_apo` checks it against a model of Equalizer APO 1.4.2 and
  the double-precision oracle. On Windows CI, `export.equalizer_apo.runtime`
  runs the same cases in Equalizer APO 1.4.2's `Benchmark.exe` (its own filter
  code, without the audio service). Still open: the manual check on a real
  Windows device in the [guide](docs/develop/testing.md#equalizer-apo-export-tests)
  (device rate and channel count reach the configuration; exclusive mode
  bypasses it), done by anyone with a Windows computer, with the result recorded there.
- [ ] **HOST-6 — Explain an Ardour export 16 frames early.** On 6 November
  2026, 1 of about 300 local mono exports in the
  [Ardour test](docs/develop/testing.md#ardour-headless) (HOST-5) came out
  16 frames earlier than the recorded take plus the reported 256 frames of
  latency; it was not reproduced in 180 more runs, some under full CPU load.
  It is not yet known whether Ardour 8.4 or Convolver moved it. The test
  fails on such an export, never retries it, and prints the shift that
  matches best and the region's position. Next time: keep the session
  (`CONVOLVER_ARDOUR_KEEP`), check the region position and whether the
  plug-in's filter loaded during the export, and repeat with a plug-in
  that reports latency and has no worker.

## Plug-ins: VST3, LV2 and AU

VST3, LV2 and AU are developed in parallel. Design, host evidence and framework choice:
[plug-in formats plan](docs/archive/plans/2026-10-21-plugin-formats.md). Each
adapter is written against its format's own headers over `HostProcessor` and
follows the [shared adapter contract](docs/develop/plugin-contract.md). VST3
work continues under HOST-1, REL-1 and the VST3 items
[below](#vst3-sdk-features-not-yet-used).

Shared work, done once for all three formats:

- [ ] **PLUG-10 — Meters.** Gain, Mix, Latency and the Gain mode are built
  in every format (PRs #80, #95, #188;
  [settings](docs/develop/plugin-contract.md#settings)); their host checks
  are in [checks left](docs/project/status.md#checks-left-before-publication)
  with HOST-1's and AU-4's, except the Gain mode, which no published
  preview has yet and which is not yet tried in a host: its check joins
  them with the next preview. **Remains:** meters, starting with the output
  peak and a count of clipped samples, kept as audio-thread counters and
  shown in each window and in Copy Diagnostics; a callback-load meter only
  if hosts' own CPU displays prove insufficient. **Worth doing when** users
  or a host check need to see clipping: Estimate and No clipping make it
  rarer, not impossible. **Done when** each window and the report show the
  peak and the clip count since the last filter change or reset, with an
  oracle-backed test per format and nothing new allocating or locking on
  the audio thread.
- [ ] **PLUG-11 — FFT planning in FFTW-built plug-ins (optional).** From the
  [editor plan](docs/archive/plans/2026-10-24-plugin-editor.md#settings), as jrp asked:
  only when a plug-in is built with `-DCONVOLVER_FFT_BACKEND=FFTW` (released
  plug-ins use Ooura or Accelerate, which have no planner), a Quick or
  Measured planning choice, Quick by default, passed through `HostProcessor`
  and applied while preparing, never on the audio thread. Its tooltip states
  the measured gain (about 2–6% in the 18 October whole-engine tests on Apple
  Silicon, least for long filters; short filters at small blocks may gain
  more, so measure them on x86-64 first) and the longer loads. Saved with PLUG-10's settings only when Measured; not
  automatable.
  If FFTW-built plug-ins go ahead, FFTW 3.3.5's
  `fftwf_make_planner_thread_safe` is FFTW's answer for plug-ins in one
  process that cannot share a lock (its manual calls it a last resort).

LV2 (Linux first; also PipeWire system audio):

- [ ] **PLUG-20 — The VST logo in the window (optional).** Steinberg's usage
  guidelines say the VST Compatible logo *should* appear in an About box or
  equivalent; where there is no room, the attribution notice is enough.
  Convolver's window has no About box, so the preview carries the notice in
  the VST3 bundle's credits (`NOTICE.txt`) and the macOS installer text, and
  the logo in every packaged guide (PLUG-5,
  [trademarks](packaging/trademarks/README.md)). jrp, 5 November 2026: keep
  it as follow-up work after the preview. Done when the macOS window (shared
  by VST3 and the Audio Unit) shows the logo and notice somewhere a user can
  find them, without crowding the settings, and its tests cover it.
- [ ] **PLUG-25 — Say which rate the host plays at.** From jrp's Audacity
  session (HOST-1, 7 November 2026): Audacity 4.0.2 played real-time
  effects at 44100 Hz, its Default sample rate, while the export of the same
  project ran at 48000 Hz, so a 48 kHz `.cfg` was refused during playback
  and the exports passed. Refusing the filter is correct; what was hard was
  telling the rate the playing copy runs at from the export's rate. The
  guides now say so ([plug-ins](docs/use/plugins.md#status-and-the-convolver-log),
  [Audacity steps](docs/develop/host-testing.md#audacity-vst3)). A refusal
  for the rate adds one line to its Fix, that the host may play at a
  different rate from the one it exports at
  ([decided](docs/project/decisions.md#diagnostics)). Still
  to decide with jrp: whether the window should mark a host rate as not
  yet confirmed when it comes from a copy that has not processed audio
  (the copy in Audacity's effect window, which a host may start briefly at
  another rate); PLUG-27 overlaps it. Done when every format's rate
  refusal carries the agreed line, in the window, Status details and the
  log, and the message tests cover it. Diagnostic text only, no sound
  change; waits for the freeze unless jrp brings it forward.
- [ ] **PLUG-27 — Read a chosen filter before the host's audio settings
  are known.** From the same review: before the host has started audio,
  the VST3 plug-in reads a chosen file for the rate and channels the host
  has only proposed (`known_layout().value_or(layout())` in
  `Processor::load_file`), so a single IR file or `.cfg` can be refused
  against a rate the host never plays at, while a preset or restored
  project arriving at the same moment is kept and checked when audio
  starts. Read a single IR file or `.cfg` without assuming a rate and check
  it at the same point as a preset; a configuration list still needs the
  rate and channels to choose its entry, so say plainly that the choice
  waits for the host's audio settings rather than pick an entry for a
  proposed rate. Keep the protection for an unsuitable replacement during
  playback; no resampling. The Audio Unit reads a chosen file the same way
  (`effect_state.cpp`), so it is in scope too. Done when tests cover
  choosing before activation, a refusal during playback, a changed host
  rate and list selection, through the existing acceptance code rather
  than a second state machine, and the
  [plug-in contract](docs/develop/plugin-contract.md#loading-and-failure)
  and [plug-ins guide](docs/use/plugins.md#loading-a-filter) say when a
  chosen file is checked. Waits for the freeze unless jrp
  brings it forward.
- [ ] **PLUG-29 — Keep the Recent list out of the log folder.** From the
  same review: the window's Recent list is saved beside the log
  (`recent-filters.txt` in `~/Library/Logs/Convolver/`,
  `portable/recent_files.cpp`), which mixes lasting settings with
  diagnostics a user may delete. Save it under
  `~/Library/Application Support/Convolver/` (each platform's equivalent,
  and the sandboxed path for GarageBand), reading the old file once if the
  new one is missing. Done when the Recent-list tests cover the new path
  and the one-time read of the old file, and the plug-ins guide says where
  the list is kept. Lower priority than PLUG-26 and PLUG-27; waits for
  the freeze.
- [ ] **PLUG-30 — One rule for state a busy lock makes a call skip.**
  From AU-16 (8 November 2026; jrp asked for fixes like it to be recorded,
  as stepping back may give a more general fix). The pattern: a call that
  must not wait changes shared state, finds the control lock taken, skips
  the update that lock guards and relies on the holder to make it; but the
  holder may already have made it, or may never make it, so what the host
  sees stays stale with no later update. Fixed where it was seen: the Audio
  Unit's bypass setter (`SetProperty`, `effect_properties.cpp`) left Status
  at Active while the Latency work held the lock with the Latency
  unchanged (au.adapter, CI run 42418181089 on `2f3cb80`). The narrow fix
  hands the update to the Latency work's serial queue, which may wait for
  the lock and updates the Status when the bypass it shows differs
  (`status_bypassed_`). Other places that skip or work around a busy lock,
  each correct today for its own reason: the Audio Unit's report read
  (`Effect::report()`) returns its last copy, safe only because every
  change goes through `update_status()`, which forgets the copy, and then
  notifies the window; LV2's `deactivate()` tries the lock first but then
  waits. VST3 takes its locks without trying. A general fix would make
  "changed while the lock was held" a rule rather than a per-call
  decision: for example, a pending-update flag that whoever releases the
  control lock acts on, or all Audio Unit control work on the one serial
  queue. Done when each adapter's skip paths either follow that rule or
  are listed with why they are safe, with a held-lock test for each, as
  AU-16's. Waits for the freeze.
- [ ] **PLUG-39 — One consolidation of the load and restore path.** From
  jrp's review of 13 November 2026: individually reasonable fixes have left
  each adapter keeping its own copies of the accepted filter's
  consequences, the playing processor, latency and tail, whether the host
  has settled, the status and report, and the filter's name, and deciding
  for itself what an accepted request requires. **Remains:** the
  [consolidation plan](docs/project/plans/2026-11-13-load-restore-consolidation.md)'s
  steps 0 to 6: protection first, then one shared decision for each
  accepted request (building on PLUG-38's `HostPlayback::playing`), one rule
  for when the host has settled, one lifetime mechanism for the playing
  processor in which the audio thread adopts and the control side never
  waits, diagnostics as a view of state, and
  typed reports and advice. **Worth doing when** the next preview is
  published; step 0, tests only, could start earlier if jrp wants. **Done
  when** the plan's counted states reach their targets (VST3 22 to 5, the
  Audio Unit 12 to 3, LV2 5 to 2) or a step records with jrp why one stays, no adapter
  decides what an accepted request requires, and every test of audio,
  saved state and host behaviour passes with no expected value changed
  except in a commit that names the defect or decided difference.
- [ ] **PLUG-36 — The loading-and-failure table covers each transition.**
  Left open by the first-preview acceptance plan (section 5, archived
  12 November 2026; jrp's 8 November priority: extend the contract and the
  existing lifecycle tests, no new framework). The
  [table](docs/develop/plugin-contract.md#loading-and-failure) names three
  situations; extend it to the empty, active, kept-but-unsuited and failed
  states against load, restore, rate and bus changes, bypass, Mix and
  processing faults, saying for each what plays, what is saved and what
  Status reports, with channel safety kept apart from which message wins.
  Map each row to its test in `tests/plugins/lifecycle.cpp` or a format's
  adapter test, and add a test only for a missing rule or a reproduced
  defect; the expected result must not come from production decision
  functions. Any unclear rule goes to jrp. Parked until after REL-3 (jrp,
  12 November 2026).
- [ ] **LV2-3 — Named layouts.** The eight discrete plug-in URIs, `lv2lint`
  and pluginval at strictness 10 run in CI (PR #43;
  [pluginval](docs/develop/testing.md#pluginval)). **Remains:** URIs for the
  PLUG-4 named layouts. **Worth doing when** an LV2 host or user needs
  named speakers. **Done when** each named layout has a URI checked by the
  same tests.
- [ ] **LV2-4 — User guide and host matrix.** The guides are written:
  LV2 in `docs/use/plugins.md` and `docs/use/install.md`, and
  [system-wide filtering with PipeWire](docs/use/pipewire.md), with what
  Convolver adds over PipeWire's own convolver; PipeWire 1.6.8 results are
  in the [LV2 host matrix](docs/develop/host-testing.md#lv2-hosts).
  **Remains:** Jalv, Carla, REAPER and Qtractor results, and PipeWire on a
  desktop with a real sound card through its systemd user service (the
  guide's `systemctl` restart, `journalctl` log and `~/.lv2` lookup were not
  exercised). **Done when** those are recorded in the host matrix.
- [ ] **LV2-6 — Editor report and UI (optional).** Only if Linux users show a
  need: generic UIs already offer the `filter` file chooser and the status
  port. The report as `patch:Set` atoms on the notify port, prepared in
  `work()` and written allocation-free by `run()` (new property URIs for jrp
  to name), then an LV2 UI reusing the macOS and Windows views
  ([editor plan](docs/archive/plans/2026-10-24-plugin-editor.md#per-format)).

AU (AUv2, macOS: Logic, GarageBand, SoundSource):

- [ ] **AU-4 — User guide and host matrix.** The Audio Unit's guides are
  written (`docs/use/plugins.md`, `docs/use/install.md`). **Remains:**
  GarageBand, checks 5 and 8 in
  [checks left](docs/project/status.md#checks-left-before-publication), on
  the published 0.13.12 files: listening to the presets, Gain and Mix, a
  click on bypass, the track in time after a Latency change, Latency with
  the window open, and VoiceOver; Logic, SoundSource, Live, REAPER and FL
  Studio (with an `.aupreset` from each host's preset menu) only on
  request. **Done when** GarageBand's checks pass or jrp narrows them,
  recorded as HOST-1's are. Results on earlier builds (GarageBand 10.4.14,
  the 0.12.5 and v0.12.8 candidates) are in the
  [GarageBand record](docs/archive/reviews/2026-11-08-au4-garageband.md)
  and the [v0.12.8 record](docs/archive/reviews/2026-11-08-v0128-host-checks.md).
- [ ] **AU-6 — Editor window.** Built and shared with VST3-11: the report and
  Copy Diagnostics properties (64000, 64001), Choose Filter and dropping a
  file (64002), Recent, Reload and reloading when the file changes, and the
  settings, checked by `au.editor` and `auval`; jrp's GarageBand gates of
  30 October 2026 passed, apart from those below. **Remains:** VoiceOver
  reading the status, the sliders and the buttons (check 5); whether the
  host stays responsive and the audio uninterrupted while a long chosen
  filter loads on the host's main thread (about 1.4 s for a 30 s stereo
  filter; load-performance work is deferred,
  [decisions](docs/project/decisions.md#plug-in-behaviour)); and, untried,
  the file sheet and the custom properties in a host that runs the unit in
  another process (REAPER, or Rosetta). From the 1 November 2026 redesign,
  still to check in GarageBand: PR #98's typed boxes (a box sets only text
  that was changed), a light rendering (in a host with a light window), a
  manual Mix drag, listening to the Latency gap and the kept echo, and
  VoiceOver reading the Audio facts and typed fields. **Done when**
  VoiceOver passes or jrp narrows it, and the rest are recorded as
  observed.

## FFmpeg integration

Parked: maintaining a patched FFmpeg source tree adds
uncomfortable coupling. The pipe is tested (FFMPEG-1, done). If this is revisited,
prefer the pipe, a filter accepted upstream, or upstream state support in
FFmpeg's generic `lv2` filter (it cannot pass a file path today), which would let
the LV2 plug-in serve FFmpeg.

Design and evidence: [FFmpeg filter plan](docs/project/plans/2026-10-21-ffmpeg-filter.md)
(21 October 2026). FFmpeg cannot load filters at run time, so a built-in filter
means a patched FFmpeg build with `--enable-gpl`.

- [ ] **FFMPEG-2 — C API.** A C99 header over `HostProcessor` and the
  configuration loader, a `Convolver::c` target and `convolver.pc`, with C-only
  tests and the same output as `Engine`. First add tested host FFT-option
  propagation for initial/replacement engines; reject unsupported backend
  options, keep preparation off the audio path, and query actual latency.
- [ ] **FFMPEG-3 — Out-of-tree `convolver` filter.** Patch series against a
  pinned FFmpeg release, build script, CI job comparing filter output with the
  CLI, and a user guide once it works.
- [ ] **FFMPEG-4 — Reverb support.** Latency-aligned dry/wet mix in the host
  layer (shared with PLUG-10), filter gain, and a faster long-IR choice (larger
  offline block or PERF-2).
- [ ] **FFMPEG-5 — Upstream decision.** Decide whether to submit the filter to
  FFmpeg after FFMPEG-3 has users; it overlaps `afir`, so the case rests on
  `.cfg` compatibility and routing.

## Compatibility and robustness

- [ ] **CFG-1 — Verify other readers.** Run redistributable fixtures in named
  Roon/JRiver/Audiolense-related workflows; record versions and exact supported
  subsets before adding a consumer-specific profile. See
  [configuration compatibility](docs/use/other-programs.md).
- [ ] **CFG-2 — Configurations from and for other programs.** Import Equalizer
  APO `Convolution:` files, CamillaDSP YAML and BruteFIR configurations into a
  `.cfg` with its filters, so an existing setup can be brought over; and export
  to CamillaDSP, which would do for Linux and Raspberry Pi players what HOST-4
  does for Windows. Each converter needs fixtures from the program's own
  documentation or sources, states what it cannot represent (per-channel EQ,
  mixers with gains above unity, resampling) and fails rather than guess.
  Optional; after CFG-1 shows which readers matter.
- [ ] **CFG-3 — Say when a configuration is not UTF-8.** From the BruteFIR
  comparison (8 November 2026; BruteFIR fixed non-ASCII configuration text
  in 0.99g). The historical Convolver read `.cfg` text in the Windows ANSI
  code page (`channelpaths.cpp` reads it through `basic_ifstream<TCHAR>`
  and `wavefile.h` opens `CT2CA(path)`); the portable parser reads UTF-8
  only. A `.cfg` saved in Windows-1252 that names `ir_ü.wav` stops with "IR
  file not found" and a garbled name, and nothing mentions the encoding
  (tried on Linux at `8ba79fb`; not checked on Windows). Done when a filename
  line that is not valid UTF-8 is reported as such, with the line, the
  likely cause (saved in a Windows code page) and how to save it as UTF-8,
  and a test covers it. Whether Convolver should also read Windows-1252
  names is a `.cfg` compatibility decision for jrp, recorded in the
  decisions log either way. Which encoding Roon, JRiver and Audiolense write
  is unknown (CFG-1). A compatibility failure, so jrp decides whether it
  waits for the freeze.
- [ ] **COMPAT-1 — Files from released builds.** Round-trip tests with
  today's writer and reader can agree with each other while both break
  yesterday's files, so each published preview leaves a small collection
  made by that exact release in `tests/upgrade/VERSION/`, with its own
  runner: its example presets, a saved REAPER project, inputs and the audio
  that release rendered, with the package SHA-256 and how each file was
  made ([upgrade tests](tests/upgrade/README.md)). Later builds must load
  every file and reproduce the audio after the reported latency:
  bit-identical when the FFT backend, block size, kernel set, compiler and
  its version, configuration and flags are unchanged, otherwise within the
  PLUG-2 tolerance (1e-5, scaled by the peak of a file louder than full
  scale; [decision](docs/project/decisions.md#releases)). Never regenerate a
  kept file; a deliberate incompatibility is jrp's decision. 0.13.2 and
  0.13.12 are kept (PRs #170, #187), checked by `upgrade.cli` and
  `vst3.upgrade`, and the capture step is in the
  [publication steps](docs/develop/releasing.md#publishing-an-unsigned-preview).
  **Remains:** a REAPER project saved with 0.13.12 (jrp's Mac session),
  with its state chunk restored through each adapter, and Audio Unit state
  from a release. **Done when** both are kept and checked. The guarantees
  it protects are in [status](docs/project/status.md#compatibility-guarantees).
- [ ] **DIAG-5 — Report clipping after a command-line render.** From the
  BruteFIR comparison (8 November 2026): BruteFIR has printed each output's
  count of overflowed samples and its peak by default since 0.99
  (`overflow_warnings`). The `convolver` CLI clips integer output silently,
  as [file rendering](docs/use/command-line.md#file-rendering) states,
  and stops only for samples that are not finite: at `8ba79fb`, `--gain-db 20`
  with `wav-pcm24` clipped 4,800 of 4,863 samples, printed nothing and
  exited 0. Done when, after a render to integer output in which any sample
  clipped, stderr gives the clipped-sample count and the peak in dBFS for
  each affected output and suggests `--gain-db no-clip`; float output whose
  peak exceeds full scale gets a note, since a later integer conversion
  will clip it; nothing is printed when nothing clipped; the exit status is
  unchanged; and CLI tests cover each case. The plug-ins' clip counts stay
  with PLUG-10's meters.
- [ ] **IO-1 — Large files.** RF64, W64, CAF, FLAC and raw output have no
  4 GiB limit, and WAV and AIFF stop with an error at it (PR #60).
  **Remains:** tests at the boundary sizes and of pipe behaviour beyond
  4 GiB; input decoder support is not a promise of strict validation for
  every container. **Worth doing when** a reproducible defect or a user
  need calls for it. **Done when** each container's boundary has a test.
- [ ] **IO-2 — The rate-mismatch workflow, and offline IR resampling.**
  Users cite missing rates, and resampled responses that changed level or
  clipped elsewhere (rank 1 of the
  [edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#1-a-filter-for-every-sample-rate)).
  **Remains:** start from the actual workflow: a user with an IR at one rate
  and a host at another. An explicit offline conversion (a tool or option
  that writes a filter at the host's rate) may solve it; EDGE-1's
  multi-rate saved state is a separate and much larger capability, taken
  up only if conversion proves inadequate. jrp decides whether Convolver
  resamples at all; compare [libsamplerate](https://libsndfile.github.io/libsamplerate/)
  and other implementations before choosing a dependency. **Done when** the
  workflow has a documented route, and any conversion is an explicit
  operation with tests for gain, delay, length and reported metadata, while
  a mismatch is still refused by default.
- [ ] **ROBUST-1 — Longer fuzz and transition stress.** Keep discovered inputs as
  regressions. Measure worst callback times and exercise repeated filter changes;
  allocation guards and average throughput are not a deadline guarantee.
- [ ] **ROBUST-8 — Fault record after a failed Latency change.** From the
  independent review of VST3-16 (PR #134, 8 November 2026). When a new
  Latency's processor cannot be built (out of memory), the Audio Unit and
  VST3 keep the running processor, but `HostFilter::activate()` has already
  cleared the fault counts, the activation failure and the notice. Status
  can then show Fault again for faults a later filter load had cleared, and
  if `activate()` itself throws, a filter that plays silence for not suiting
  the host may read Active. Split `activate()` into a step that may throw
  and a commit that cannot, called only once the new processor plays, in
  both adapters, with a test of each case. Only reachable when memory runs
  out; waits for the freeze.

## VST3 SDK features not yet used

Checked against VST SDK 3.8.1 (`pluginterfaces/`, the developer portal's 3.8.0
and 3.8.1 change notes) on 21 October 2026. The whole developer portal and the
SDK's validator suite were compared with the adapter on 23 October 2026
([VST3 API audit](docs/review-2026-10/vst3-api-audit-2026-10-23.md)). The adapter already uses one main
input/output bus, 32/64-bit processing, sample-accurate bypass
(`kIsBypass`), latency/tail reporting, component state, generated
`moduleinfo.json` and the default (empty) process-context requirements. Items
below are candidates; adopt each only with adapter, validator and pluginval tests.

- [ ] **VST3-2 — Switch filters from the host without an editor.** Expose a set of
  filters as a program list (`IUnitInfo`, `IProgramListData`, a `kIsProgramChange`
  parameter), for example the entries of a configuration list, possibly chosen
  automatically by host rate and layout. Hosts can then switch filters in generic
  UIs and automation. Changes reuse the existing crossfading replacement.
- [ ] **VST3-5 — Progress for slow filter preparation.** `setState` prepares the
  filter synchronously on the host's thread. Measure the worst case (PERF-3) for long,
  multichannel filters (including FFTW planning) and, if hosts stall noticeably,
  report it with `IProgress` (`AsyncStateRestoration`) or move preparation to a
  worker.
- [ ] **VST3-6 — Replacing the old VST2 plug-in.** If a migration requirement is
  defined (see limitations), declare the VST2 ID `'Conv'` in `moduleinfo.json`'s
  `Compatibility` section or `IPluginCompatibility`, and map old parameters with
  `IRemapParamID`. Old VST2 chunk state would still need a converter.
  Gain uses the old attenuation's parameter ID 0 but not its mapping, so a
  declared compatibility must convert that value in the state converter or
  leave parameter values out of the migration (from the archived editor
  plan's [risks](docs/archive/plans/2026-10-24-plugin-editor.md#risks-and-open-questions)).
- [ ] **VST3-11 — Editor window on macOS.** Built and shared with AU-6
  (PR #103), checked by `vst3.editor` and pluginval's editor tests
  ([format notes](docs/develop/plugin-contract.md#format-notes)).
  **Remains:** the window's steps in REAPER and Audacity, including that
  typing in the window does not trigger the host's shortcuts nor the host's
  keys reach the window, and VoiceOver (checks 1 and 4 in
  [checks left](docs/project/status.md#checks-left-before-publication)).
  **Done when** those pass or jrp narrows them. The meters come with
  PLUG-10.
- [ ] **VST3-17 — Audacity keeps a filter chosen in the window.** In
  Audacity 4.0.2 with 0.13.2 and earlier, a filter chosen in the window
  (Choose, Recent, Reload) played but was missing from the export and the
  saved project, because Audacity stores a VST3 plug-in's state only after
  a parameter edit. The fix, in 0.13.12 (PR #177, merged untested in
  Audacity by jrp's choice): after an accepted file, a controller whose
  `setDirty` does not succeed reports one edit of Latency that keeps its
  value; `vst3.adapter` checks it
  ([decisions](docs/project/decisions.md#plug-in-behaviour)). **Remains:**
  jrp's Audacity check (check 10; steps in project files
  `vst3-17-audacity/`: Choose, then export with the window open and
  without playing, save and reopen, with 0.13.2 as a control that fails),
  including whether undoing a later action brings back the previous
  filter; a REAPER check that a chosen file adds no Latency entry to its
  undo history; and the drafted upstream issue (project files
  `audacity-upstream/`) for jrp to post. **Done when** the Audacity check
  passes and the guides drop "not yet checked in Audacity".
- [ ] **VST3-12 — Editor window on Windows.** The Win32 window
  (`plugins/vst3/editor_win.cpp`, with `IPlugViewContentScaleSupport` and
  keyboard navigation) and the libsndfile file reader are built and shipped
  in 0.13.12 (PR #181), checked by `vst3.editor` and pluginval's editor
  tests on Windows CI. **Remains:** a tester in REAPER and one other
  Windows host, and a screen reader (NVDA or Narrator). **Done when** those
  are recorded; the release notes' Windows-window limitation goes with the
  next preview's notes.
- [ ] **VST3-13 — Editor window on Linux.** The window in VSTGUI, for the
  Linux VST3 build only, as a stop-gap, driven by the host's `IRunLoop`;
  VSTGUI (BSD-3-Clause) and the libraries it links added to the package
  checks and licence notices; opened by `editorhost` under `xvfb-run`. Not
  reachable by screen readers, which the guide says. A native X11 view can
  replace it later
  ([editor plan](docs/archive/plans/2026-10-24-plugin-editor.md#framework-choice)).
  **Worth doing when** Linux VST3 users ask for a window, now that LV2 is in
  the Linux package
  ([decided](docs/project/decisions.md#direction); [windows plan](docs/project/plans/2026-11-10-windows-linux-plugin-windows.md)).
Not relevant to a convolver unless the product changes: MIDI and note
interfaces (`IMidiMapping2`, `IMidiLearn2`, note expression, MIDI 2.0
articulations), `ITransportControl`, `IPrefetchableSupport`,
`IAudioPresentationLatency`, `IInfoListener` (track name/colour),
`IAutomationState`, `IXmlRepresentationController`,
`IEditControllerHostEditing`, `IParameterFinder`, side-chain or auxiliary buses,
`IComponent::setIoMode`, and `IStreamAttributes` (presets embed their filters,
so the preset file path is not needed).

## FFT libraries and performance

- [ ] **FFT-2 — Evaluate alternatives only against a measured need.** These are
  candidates, not supported backends or automatic fallbacks. The
  [8 November integration review](docs/develop/fft-backends.md#alternatives-for-linux-and-windows)
  prioritizes an ordered PFFFT adapter. The
  [isolated Mac trial](docs/develop/fft-measurements.md#pffft-ordered-transform-experiment)
  passed the FFT/engine oracles and saved 12–36% processing time versus Ooura,
  including reordering; no shipped backend changed. PocketFFT currently allocates during
  execution and its optional plan cache locks. Windows/MSVC and Linux/x86
  measurements remain required before selecting a replacement.

  | Candidate / primary reference | Question to answer |
  | --- | --- |
  | [PFFFT](https://github.com/marton78/pffft) | Does its SIMD implementation improve the target audio workloads after layout conversion? Check supported lengths, alignment and workspace requirements. |
  | [PocketFFT](https://github.com/mreineck/pocketfft) | Current real-transform execution allocates; optional plan caching locks. Revisit only with a suitable prepared workspace interface or an offline use case. |
  | [KissFFT](https://github.com/mborgerding/kissfft) | Is a small alternative useful on constrained platforms compared with the existing Ooura backend? Measure the whole engine. |
  | [Intel oneMKL DFT](https://www.intel.com/content/www/us/en/docs/onemkl/developer-reference-c/2025-0/fourier-transform-functions.html) | Is there a demonstrated Intel workload benefit worth another platform dependency and redistribution review? |

  Before adopting one: pin a revision, check licence and supported CPUs, document
  transform sizes/scaling/layout, ensure no processing allocation or locks, and
  pass the same direct DFT and convolution oracles. Compare setup, memory,
  p50/p99/max callback times and throughput against all relevant existing backends.
  Keep Convolver's C++20 and platform baseline unless deliberately changed.
- [ ] **PERF-2 — Inverse-transform sharing or nonuniform partitions.** Prototype
  only for a demonstrated long-filter/latency bottleneck. If PERF-3 shows the
  plug-ins need less CPU, first point users to a larger block with the
  Latency setting (PLUG-10). Nonuniform partitions are
  the route to low latency and low CPU together. Preserve routing, tail,
  delay and reset behavior; account for changed floating-point accumulation order.
  Shared forward transforms are already implemented. A user need is now
  stated: long reverbs at low CPU with lower and finer latency, rank 3 of
  the [edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#3-long-reverbs-at-low-cpu-and-latency), which sets out the
  techniques to consider and what is still a hypothesis. PERF-3's
  long-reverb measurements and EDGE-11 come first. Work moved off the
  calling thread must be correct under realtime, prefetch and offline
  scheduling and the switches between them: results ready when their
  samples are needed, never assumed from wall-clock time (the plan's
  [scheduling rule](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#3-long-reverbs-at-low-cpu-and-latency)).
  Progress: a prototype experiment (7 November 2026, at jrp's request;
  `convolver_long_reverb_benchmark`, not in the product) measured
  non-uniform partitions with a direct head for zero latency and the large
  blocks' work spread over the callbacks of their slack: about 15 to 35
  times less CPU than the engine at Latency 256 for 30 and 60 s reverbs, no
  extra cost at zero latency, worst 64-frame callback near 0.2 ms, oracle
  worst −131 dB; a worker thread did not help
  ([measurements](docs/develop/fft-measurements.md#long-reverbs-and-zero-latency)).
  Decided (jrp, 7 November 2026): a working release comes first; after it,
  move the spread non-uniform scheme into `Engine`, keeping routing,
  delays, tail, reset and shared forward transforms, with block sizes tuned
  on each platform. Zero latency then becomes the plug-ins' default, the
  Latency setting stays and shows the CPU a larger value saves, and a new
  filter still plays as soon as possible (the plan's
  [rank 3](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#3-long-reverbs-at-low-cpu-and-latency)).
  Built at jrp's request (8 November 2026) on draft PR #139, unmerged until
  after the release: the non-uniform engine in `Engine` (routing, delays,
  tail, reset and shared transforms kept; worst oracle error 5.2e-7 of the
  peak), zero first in the Latency list and the default (jrp's choice), the
  CLI and inspection defaulting to 0 to match. In the engine zero latency is
  not free, unlike the prototype's single path: the direct sum costs about
  10 to 15 ns per frame for stereo, and short filters (4096 taps) cost about
  a fifth more than today's engine at Latency 256, against several times
  less for long ones ([measurements](docs/develop/fft-measurements.md#the-non-uniform-engine),
  `compare_engines.py`, which builds the two revisions separately). Still
  open: the CPU indication beside the Latency menu, the cost at latency 0
  varying by up to about 30% between nearby filter lengths on the Linux VM
  (both revisions alike, most likely buffer placement; measure several
  lengths on the Mac before tuning it), block sizes tuned on
  Apple silicon, and PERF-3's measurements on jrp's Mac with both engines.
  When tuning: every spread stage's last unit (the last path's inverse
  transform) waits for that stage's last boundary, and all stages' last
  boundaries fall together, at frames 8064 mod 8192, where the review saw
  the worst ordinary callbacks (about 100 µs against a 13 µs mean on the
  Linux VM). The independent review of `6056ef3` found two blocking
  problems, both fixed on the PR: Audio Unit projects reopened at a shifted
  Latency (the AU now also saves frames, and older state is read through
  the earlier menu), and no test checked spreading (`native.engine_stress`
  now does). The branch carries the minor version step, 0.13.0 (the C++
  defaults change). jrp (8 November): it merges for the 0.13.0 pre-release
  after the Mac benchmarks, stress and soak, a focused REAPER and
  GarageBand session, and CI, compared against the published `v0.12.12`.
  The Mac benchmarks, stress and soak passed on jrp's M5 Max at `6cf7260`
  ([measurements](docs/develop/fft-measurements.md#on-apple-silicon)):
  3 to 17 times faster than `v0.12.12` at the same Latency from 65,536
  taps, and Latency 0 costing 16 to 96% more than 256, most for short
  filters. The REAPER and GarageBand session is still open: jrp chose to
  merge first and to publish the second preview without it (9 November), so it is
  check 9 on the status page. jrp decided no
  separate zero-latency switch is needed: the Latency menu is the choice,
  with 0 the default, and the user guide explains the trade-off.
- [ ] **PERF-3 — One measurement exercise: budgets, supported workloads and
  published evidence.** One set of dated runs on named machines serves
  three uses: reproducible benchmarks (PERF-1 joined it on 12 November
  2026), the supported workloads users read, and EDGE-5's published
  comparison. **Remains:** on named machines (Apple silicon with
  Accelerate, Windows with MSVC and Ooura, Linux), record commit, compiler,
  dependency revisions, block size and numerical error with: callback
  durations (median, 99th and 99.9th percentiles, maximum), peak memory and
  preparation time, while playing, replacing and resetting, from a full
  history (`--warm`, PERF-14); for long filters (10, 30 and 60 s, stereo
  and true stereo), 6- and 8-channel routing, 48 and 96 kHz and the host
  block sizes users meet; with the encoded saved-state size beside them (a
  60 s, 96 kHz true-stereo filter needs about 88 MiB, over the 64 MiB
  limit, so the plug-ins refuse it: EDGE-12). Rerun the room-correction
  cases of `convolver_benchmark` (10 to 18) on Apple silicon and Windows
  before choosing compiler flags there (PERF-4 measured x86-64 Linux only).
  **Worth doing when** a speed or capacity claim is to be made, or before
  the next substantial DSP change: only these results decide whether
  PERF-2, PERF-7, PERF-8, FFT-2, VST3-5 or an EDGE-10 or EDGE-11 candidate
  deserves work. **Done when** the results are dated records in
  [FFT measurements](docs/develop/fft-measurements.md) and the user guides
  state the supported workloads (for example "8 channels of 65536 taps at
  96 kHz use under N% of one core and M MB"). `convolver_callback_benchmark`
  (PR #70) and the load-timing tests already measure part of this
  ([startup, reset and loading](docs/develop/fft-backends.md#startup-reset-and-filter-loading)).
- [ ] **PERF-16 — Loading stages timed inside the plug-in.** From jrp's
  review of `db54160`: the load-timing records give a load's whole hold;
  split it into reading and decoding, validation, FFT preparation,
  publication and notification, recorded outside the audio path (no
  logging, allocation or locks there) and written with the PERF-13 records.
  A failed run should name the stage that did not progress.
- [ ] **PERF-17 — Platform audio scheduling in the measurement host.** From
  the same review: the load-timing renderer is an ordinary thread that
  sleeps and yields, unlike a host's audio callback. For final deadline
  measurements, a small platform layer in the test host only (Apple audio
  workgroups on macOS, MMCSS on Windows), never priority changes inside the
  plug-in. The portable renderer stays for concurrency tests.
- [ ] **PERF-18 — Performance regressions measured apart from CI.** From
  the same review: correctness, ordering, audio output and allocation and
  locking contracts block every pull request; performance regressions are
  measured on optimised builds by base and head runs interleaved on one
  machine, with a sampling policy fixed in advance and every result kept,
  as an explicit report rather than "retry until one passes"; responsiveness
  is qualified before a release (REL-6). Background preparation (VST3-5,
  and the Audio Unit's equivalent) only if qualified measurements show
  loading pauses, keeping coherent state restoration, request order,
  cancellation, the previous filter and shutdown ownership.
- [ ] **PERF-7 — One input check on the plug-in audio path.** From the
  project review of 5 November 2026, after PERF-3: each `HostProcessor`
  call copies planar host buffers into an interleaved buffer and replaces
  non-finite input, then `Engine::process` checks the spans for overlap,
  scans the input again and transposes it through its delay lines, and the
  output goes back the same way inside a `try` block. A private planar,
  `noexcept` entry point for `HostProcessor` would drop the second scan, two
  transposes and the `try`; the public `Engine::process` keeps its checks.
  The output must stay bit-identical. jrp's simplification review of the same
  day agrees, measurement first: remove such redundant work (as GAIN-2 did
  for `--gain-db no-clip`) before any new planar engine or more elaborate
  partitioning.
- [ ] **PERF-19 — Copies of whole configurations on the control side.**
  From jrp's review of PLUG-23 (7 November 2026): a filter's configuration,
  every tap included, is copied where the control side hands it on:
  `HostFilter::activate()` returns a copy in a `HostPlayback` at each
  activation, the install callback's filter is copied into the prepared
  engine, and saving copies it into `HostState`. For a 30-second stereo
  reverb at 48 kHz each copy is at least 11 MB. Profiling targets only: measure the preview loading
  workloads with a profiler first, and remove a copy only where it shows in
  preparation time or peak memory, with saved state, diagnostics and audio
  unchanged. Waits for the freeze.
- [ ] **PERF-8 — Split spectra straight from the FFT.** From the project
  review of 5 November 2026, after PERF-3: the engine stores spectra split
  (real parts, then imaginary), but the backends hand them over interleaved
  and the `deinterleave` and `interleave` kernels convert them, although
  vDSP's real FFT already works in split form and FFTW can read and write
  split arrays through its guru interface. Measure the saving, and keep
  within the kernel divergence gates.

### Kernel rounding

Guidance, not an item (jrp's simplification review, 5 November 2026):
[`kernels.inc`](portable/kernels.inc) controls rounding closely. It turns
implicit contraction off, writes each fused multiply-add explicitly, keeps
the accumulation a separate rounding and makes the vector loop and the
scalar remainder compute each bin the same way. Some of that guards
differences far below audibility. The AVX2 kernels have measured benefits
(PERF-4), so they stay. When these kernels next change (PERF-8, say), try
whether ordinary compiler-generated arithmetic removes special handling
while keeping within the oracle tolerance, `native.kernel_divergence`'s
limits and the performance targets. Require identical rounding only where
there is a concrete compatibility or reproducibility need. Routing, gain,
delay and scaling stay exact. Nothing changes until then, because
loosening rules without a kernel change saves no code.

### Loading responsiveness

An acceptance gate for the first preview (jrp's review of `6920a45`, 31
October 2026), not an optimisation item; met for the first preview by
REL-6 (below), and kept here as the record of how it was set. Every adapter except LV2 reads and
prepares a filter on the thread that asks for it: VST3's `setState`, the
Audio Unit's `RestoreState` and, since PR #84 (merged 31 October 2026), the
Audio Unit window's Choose Filter property call. Cached reports keep other readers from waiting,
but not the calling thread, which may be the host's interface thread.
Measure, on the preview workloads (below), how long that thread is held
and whether audio drops out while a long filter loads, replaces another and
is reset; if loads cause noticeable pauses, move preparation to the
background before the preview (VST3-5 is the VST3 part). Measure with
automated tests and command-line benchmarks rather than by hand in a host's
window (jrp, 31 October 2026: test through the CLI where that makes sense):
time each adapter's own load call (`setState`, `RestoreState`, the window's
load property) from the test hosts, with `convolver_callback_benchmark`
timing callbacks while it runs; a named host only confirms the result. LV2's request
numbers and prepare-then-publish (`HostFilter::load` with
`SupersededRequest`) are the pattern; the adapter, not its window, owns the
request, overtaken requests and shutdown, whether or not a window is open.
First evidence (jrp's GarageBand run of PR #84's `b0f2552`, 31 October
2026, [recorded on the pull request](https://github.com/jrp2014/convolver/pull/84#issuecomment-6548846745)):
stereo 44.1 kHz filters of 30 s (1,323,000 taps a path) and 120 s
(5,292,000 taps) loaded during playback, the new report appearing after
about 1.4 s and 1.7 s including the observation's own overhead. Those are
not isolated preparation timings, and whether the interface stayed
responsive and the audio free of dropouts was not verified. jrp deferred
load-performance work for now; that is not a qualification of either.

The external review of `91fcf1f` (0.8.13, 5 November 2026) asks for this to
become an executable gate before the preview. Acceptance: an automated test
times each adapter's own load and restore entry point (VST3 `setState` and
the window's load, the Audio Unit's `RestoreState` and load property, LV2's
`restore` and worker) while a test host renders audio on another thread, on
the preview workloads, and records how long the caller is blocked and how
late each callback is. jrp sets the threshold for both; the gate passes when
every adapter meets it. If one does not, preparation moves to a worker, with
the adapter (as LV2 does) owning request order, overtaken requests,
publication and shutdown.

PERF-9 (done, PR #111) built that test on interim workloads, with jrp's
limits (5 November 2026): in optimised builds, no late callback and none
over half its period while a load runs on another thread, and the host's
thread held at most 100 ms with filters up to 10 s and 250 ms with 30 s
([load timing](docs/develop/load-timing.md)).

**Preview workloads.** The gate is finite: its workloads are the ones that
test already runs, a stereo 48 kHz filter of 1, 10 and 30 s with 512-frame
callbacks, loaded into an empty instance, replaced by another as long and
reset, through each adapter's load and restore entry points. PERF-3's wider
measurements (6- and 8-channel routing, 96 kHz, other block sizes, memory)
become stated supported workloads after the first preview; they are not a
preview gate. The release qualification on the intended machine
([REL-6](docs/develop/releasing.md#responsiveness-qualification), a
first-preview gate) passed for the 0.12.5 source on jrp's MacBook Pro
(8 November 2026, longest hold 43.2 ms;
[record](docs/archive/reviews/2026-11-08-rel6-qualification.md)), so
background preparation (VST3-5) is not needed for the preview. jrp
decided the result stands for the final candidate while no engine,
loading or plug-in code changes
([decisions](docs/project/decisions.md#releases)).
First optimised-build figures (Linux cloud VM, 4 cores, clang Release with
Ooura, 5 November 2026; stereo 48 kHz, 512-frame callbacks): VST3 `setState`
held the host's thread for 4 ms with a 1 s filter, 43 to 61 ms with 10 s,
about 235 ms with 30 s and about 600 ms with 120 s, the window's file about
the same; no callback was late. LV2's worker took the same time off the
audio thread, with no late callbacks; LV2's `restore()` held back `run()`
for as long (about 260 ms with 30 s), and its reset (deactivate, activate)
for about 200 ms, as the specification has the host stop audio for both.
CI on `d1e1443` (5 November 2026, the "Load timings" steps) held the host's
thread far less with the 30 s filter: 45 to 65 ms for VST3 on Linux, about
55 ms on Windows, 51 to 154 ms for VST3 on macOS and 28 to 51 ms for the
Audio Unit (macos-15); the cloud VM above is the slow outlier. Every
callback on Linux CI was on time. On the macOS and Windows runners even the
baselines were late, by 3 to 57 ms with callbacks taking under 1 ms, so the
callback checks there were inconclusive: in practice only Linux CI enforces
them. On `1290b81` the Windows runner held the host's thread for about
265 ms with the 30 s filter (a clean baseline, then late callbacks during
the load), so an entry that does not pass now runs up to three times.

jrp's review of `59ac8e0` (0.10.0) found that a late baseline made every
violation inconclusive, the hold included, and that inconclusive counted
as a pass: a mock adapter taking over 400 ms a load passed. PERF-10 judges
the hold whatever the audio thread saw, reports a run whose callbacks
cannot be judged as not qualified (CTest: skipped) rather than passed, and
keeps every failed run in the summary; so a test that passes on a CI runner
is a qualifying measurement, and one skipped there needs a quieter machine.
The same review found the input was not the intended quiet noise (PERF-11).
Measured again with the corrected input (same cloud VM and build, 5
November 2026), the figures above hold: VST3 `setState` 4 to 6 ms with
1 s, 47 to 60 ms with 10 s, 238 to 254 ms with 30 s (one run of three over
250 ms, the next under) and 644 to 700 ms with 120 s; the window's file
4 to 7, 34 to 38, 227 to 234 and 546 to 556 ms; LV2's worker 3 to 4, 37 to
47 and 203 to 229 ms, its `restore()` holding back `run()` for 4, 39 and
256 to 264 ms. No VST3 callback was late. Loading time does not depend on
the input, which only the callbacks play.

jrp's review of `db54160` (5 November 2026) separates correctness tests,
performance measurement and release qualification: PERF-12 and PERF-13 (records
and verdicts), AU-14 and PLUG-21 (completion witnesses and staged
teardown), PERF-14 (warm workloads), REL-6 (the qualification procedure),
PERF-15 (subnormal flushing, done) and the open PERF-16 to PERF-18. The 100 ms and 250 ms limits and the
callback limits stay. With `--warm` (PERF-14, same cloud VM and build), the
10 s filter at full history took about 3 ms a callback and the 30 s filter
25 to 43 ms, every callback late, so its replacement and reset failed on
callbacks while the hold (167 to 192 ms) met its limit; with flush-to-zero
on the audio thread, 6 to 7 ms and no late callback (PERF-15). The
archived request worker's Windows timeout
([archived](docs/archive/reviews/2026-11-05-request-worker.md)) was never
explained; AU-12 removed that worker, which is not a diagnosis.

### Keeping `host.cpp` manageable

From the external review of `91fcf1f` (0.8.13, 5 November 2026):
`portable/host.cpp` combines filter acceptance, failure policy, diagnostic
wording, status text and the real-time `HostProcessor`. Diagnostics already
stay in their structured parts (problem, cause, fix) inside and are formatted
only where they leave the library (DIAG-4).

When this code next changes for another reason, three narrow
simplifications from jrp's review of 12 November 2026 go with it; none is a
project of its own, and none changes a message a user reads. PLUG-39's
[steps](docs/project/plans/2026-11-13-load-restore-consolidation.md#steps)
1 and 5 carry them out:

- Exceptions carry `Advice` as a typed error rather than English-labelled
  text ("Cause: ", "Fix: ", "Note: ") that `parse_advice()`
  (`portable/advice.hpp`) reads back, keeping the public exception types
  and their `what()` text.
- The VST3 processor's `load_file()` returns a refusal it has already
  recorded directly, rather than throwing `"refused"` or `"no reader"` only
  to reach its catch block.
- The window report keeps its audio facts and reference lines as typed
  lists until the transport boundary (the VST3 message, the Audio Unit
  property), encoded there once by one codec, rather than tab- and
  newline-separated fields that `window_model.cpp` parses back
  (`report_fields()` in `portable/host_report.cpp`).

- [ ] **PLUG-24 — `HostProcessor` in its own source file.** From jrp's review
  of PLUG-23 (7 November 2026), which grew `host.cpp` again: move
  `HostProcessor`'s audio code into its own source file, apart from the
  control-side filter policy, as VST3-14 and AU-11 moved code out of the
  adapters, with public APIs and ownership unchanged. PLUG-23 already
  gathered the rate and channel comparison (`same_configuration()`) and the
  dry-signal answer (`HostPlayback`) in one place each. No new general
  framework. The move keeps saved state, diagnostic text, allocation guards
  and numerical results unchanged, and never shares a pull request with a
  DSP change. Waits for the freeze.

## Edges over other convolvers

jrp's goal: be better than the leading reverbs that play impulse responses
and the room-correction convolvers. The [edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md) holds the
comparison, the ranking, the rationale and the measurements; the items below
hold only what each must deliver. They wait for REL-3 unless jrp lifts the
freeze on optional features; platform- and chip-level micro-optimisation
waits for the walkthroughs (jrp, 7 November 2026). Order: first the
limitations users cite (EDGE-1 with IO-2, PERF-3 with EDGE-11, PERF-2 with
EDGE-10, EDGE-3, EDGE-13), then edges nobody was found citing (EDGE-12,
then the easy room-correction wins EDGE-9, EDGE-2, EDGE-4, EDGE-5), then
the reverb features (EDGE-6 to EDGE-8). EDGE-15, from the BruteFIR
comparison, is not ranked yet. An item starts only after the
plan's [gate](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#before-an-item-starts): a reproducible Convolver
limitation, an affected intended workflow and a measurable acceptance
criterion. Optimisations are experiments with a stated gain and permission
to stop. Every item
keeps the plan's [non-negotiables](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#non-negotiables).

- [ ] **EDGE-1 — A filter for every sample rate in one plug-in state.** A
  larger capability than IO-2's offline conversion, taken up only if that
  proves inadequate for the rate-mismatch workflow. **Done when** a list's
  entries that fit the host's channels are kept in the saved state, the
  entry for the host's rate plays and a rate change switches with the usual
  crossfade; a rate no entry has is refused as now; states over the
  saved-state limit are refused with a clear message. Needs jrp's decision
  on the state format and the preset tool's options.
  [Plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#1-a-filter-for-every-sample-rate). Effort M.
- [ ] **EDGE-2 — Show what a filter does.** Done when `--inspect --json`
  and the library report each path's magnitude, phase, group delay and
  energy decay, checked against an independent computation, and the macOS
  window plots them (layout approved by jrp as an addition to the
  [editor plan](docs/archive/plans/2026-10-24-plugin-editor.md)). Effort M.
- [ ] **EDGE-3 — Reverb file layouts without a configuration.** Done when a
  mono response on a stereo instance and true stereo (one four-channel file
  or two stereo files) load without a `.cfg`, giving the same routes and
  output as the equivalent `.cfg`, and an ambiguous file is never guessed
  silently. Needs jrp's decision on the four-channel order and how the
  layout is chosen. Effort S to M.
- [ ] **EDGE-4 — Compare two filters, level-matched.** Done when two filters
  can be held and switched with the replacement crossfade at matched
  loudness, within the saved-state limit for both together. Needs jrp's
  decision on the saved state and the loudness measure. Effort M.
- [ ] **EDGE-5 — Publish the workloads and the evidence.** The user-facing
  use of PERF-3's measurements, not a benchmark of its own. **Remains:** the
  user guides state PERF-3's supported plug-in workloads (CPU, memory and
  saved-state size) beside the open convolvers measured in the same runs,
  and a `docs/use/` page shows a null test with `--compare` against a
  shipped reference render. **Worth doing when** PERF-3 has results. **Done
  when** both are published and jrp approves the claims. Effort S for the
  page.
- [ ] **EDGE-6 — A reverb's pre-delay, start, length and fades.** Done when
  these settings shape the filter when it loads (never on the audio
  thread), match the shaped response computed independently, and leave the
  output unchanged at their defaults. Needs jrp's decision on parameter IDs
  and saving. Effort M.
- [ ] **EDGE-7 — Decay and damping.** Per-band decay reshaping when the
  filter loads. Only after EDGE-6 is in use and reverb users ask; jrp
  decides whether to do it. Effort L.
- [ ] **EDGE-8 — Immersive layouts beyond 8 channels.** Done when 7.1.4,
  9.1.6 or up to 16 × 16 load in the plug-ins, with existing identities and
  saved sessions preserved; additional identities only where a separate
  variant requires them, such as further fixed-layout LV2 variants. Changes
  the recorded [channel layouts](docs/develop/plugin-identities.md#channel-layouts)
  only if jrp decides so. Effort M to L.
- [ ] **EDGE-9 — Delays finer than a sample.** Done when an opt-in
  fractional delay is baked into the prepared filter, with any added taps,
  processing cost and integer delay measured, and its supported bandwidth
  and approximation error stated and tested against an independent
  computation. `.cfg` delays stay truncated to whole samples; jrp decides
  how the finer delay is given. Effort S to M.
- [ ] **EDGE-10 — Sound-aware and platform-aware optimisation.** A catalogue
  of [candidates](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#sound-aware-and-platform-aware-optimisation),
  not an obligation to build or benchmark each. First reconcile the plan's
  older proposals with the PERF-2 engine. **Worth doing when** a measured
  limitation (PERF-3, or a user's report) points at one candidate; that one
  is then investigated against the gain the plan states for it. **Done
  when** PERF-3's results show no candidate is justified; each one
  investigated is adopted as its own item or stopped with its numbers.
  Exact and rounding-only changes keep the oracle and divergence bounds; a
  bounded approximation needs jrp's decision on its bound. OpenMP only where
  it causes no cross-platform incompatibility (offline paths, optional,
  serial build byte-identical); `libomp` ships on macOS only for a
  meaningful gain there. Explicit NEON or vDSP kernels only with a new
  workload, hardware target or hypothesis, measured against the
  [24 October 2026 vDSP experiment](docs/archive/reviews/2026-10-24-apple-vdsp.md).
- [ ] **EDGE-11 — Savings in today's engine.** Candidates, not obligations:
  exact, with bit-identical output, idling once silent input has outlasted
  the tail, skipping all-zero filter blocks, holding identical responses
  once and reading each input history once for all its paths; rounding
  only, within `native.kernel_divergence`'s bound, spreading the older
  filter blocks' multiply-adds over the callbacks between engine blocks and
  one inverse transform per output where outputs are fewer than paths.
  **Worth doing when** a measured limitation on PERF-3's long-reverb
  workloads points at one; it is then measured and kept only if it reaches
  the gain the [plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#2-savings-in-todays-engine)
  states. **Done when** PERF-3's results show no candidate is justified;
  each one tried is kept or
  stopped with its numbers, with the oracle tests passing, the worst error
  reported and nothing new allocating or locking on the audio thread.
- [ ] **EDGE-12 — Filters larger than the saved-state limit.** Done when a
  plug-in accepts a filter whose samples exceed 64 MiB (such as a 60 s
  true-stereo filter at 96 kHz, about 88 MiB) through the storage jrp
  chooses, saves and restores it, and refuses clearly when what it needs is
  missing or changed. Needs jrp's decision on the storage. Effort M.
- [ ] **EDGE-13 — Report a filter's own delay, as an option.** Done when an
  opt-in setting adds a delay given by the user, or a common linear-phase
  delay verified across the paths concerned, to the reported latency, and
  tests show: each path's delay is its centre of symmetry (never its
  largest peak) plus its configured delays; a path outside the symmetry
  tolerance gets no verified delay; differing path delays add nothing and
  the Status says why; a half-sample centre is rounded down and the
  remainder reported (the [plan's proposed handling](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#5-report-a-filters-own-delay),
  as jrp confirms it); the wet convolution is unchanged; Mix and bypass
  delay the dry signal by the reported latency (a buffer as long as the
  delay, prepared off the audio thread); and a host's latency compensation
  is shown to line it up.
  Needs jrp's decision on whether to offer it, how the delay is found and
  what a replacement with a different delay does, since the latency is
  fixed for a processor's life. Effort S to M.
- [ ] **EDGE-14 — Match the loudness of a reverb response, as an option.**
  Done when a "Match loudness" checkbox, off by default (jrp, 7 November
  2026), matches the wet signal's K-weighted loudness for pink noise to the
  dry signal, computed when the filter loads and applied as one fixed gain;
  off plays every filter as designed, as today; a `.cfg` configuration
  always plays as designed; and the gain is the same at every sample rate
  for the same response. Evidence and the methods compared: the plan's
  [automatic gain](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#automatic-gain).
  Still needs jrp's decision on its parameter ID, how it is saved, and the
  proposed Status warning when a filter plays more than 12 dB louder than
  its input at 100% Mix. Effort S to M.
- [ ] **EDGE-15 — Optional output safety ceiling.** From the BruteFIR
  comparison (8 November 2026): BruteFIR's `safety_limit` (1.0m, 2013)
  checks every output sample and stops before one above the set level
  reaches the device, "to protect your ears and expensive speakers", and
  [protect-bflogic](https://github.com/MRinus/protect-bflogic) (2026) adds
  per-channel limits because BruteFIR has none. Convolver has no output
  ceiling, so a wrong filter or route gain reaches an amplifier or a
  crossover's tweeter at full level. Done when an opt-in ceiling, off by
  default, acts on any output sample above it as jrp decides (for example,
  mute that output with Fault and the sample's position in the plug-ins,
  stop the render in the CLI), and tests show that no sample above the
  ceiling leaves, that output below it is bit-identical, and that the check
  allocates and locks nothing on the audio thread. Needs jrp's decisions on
  the action (mute, limit or stop), per output or global, and whether it is
  saved in state. Waits for REL-3. Effort S to M.

## Developer tooling

- [ ] **CI-34 — Reduce duplicate CI scheduling, preserving coverage.**
  Following the [CI plan](docs/project/plans/2026-11-08-ci-scheduling.md).
  The first step is done: a pull request builds the candidates only when it
  touches what they ship, or when started by hand
  ([CI triggers](docs/develop/testing.md#when-pull-requests-build-the-candidates)).
  **Remains**, waiting for jrp's go-ahead: the fuzz triggers, the coverage
  table and matched runs. **Done when** scope tests and affected CI pass,
  every configuration has a retained trigger, and matched runs show the
  waiting-time savings, with oracle, regression, validator, platform and
  release checks retained.

- [ ] **CI-32 — Run the most extreme numerical cases less often.** Dormant.
  From jrp's review of 5 November 2026: the largest and most extreme cases
  (`native.kernel_divergence`'s 128k to 512k filters and its noise near
  1e-35) could move to scheduled or DSP-focused runs. Its measurements of
  5 November 2026 show that would shorten no job (17.6 s under sanitizers,
  10.5 s in Release, while `native.fft_planning` alone took 35 s of a 55 s
  run). **Worth doing when** one of these cases sets a job's or a
  contributor's run time; a DSP-focused run would then need a `dsp` answer
  from `scripts/ci_scope.py`. **Done when** they run less often with their
  evidence kept and the everyday gates unchanged.

## Documented limitations

This inventory gathers constraints stated across the guides. Some are deliberate
API or safety policies, not defects. Change them only for an explicit need and
with compatibility tests; linked IDs point to open work above.

| Limitation | Current handling / follow-up | Authoritative detail |
| --- | --- | --- |
| A preset from `convolver-preset` carries the filter only, not the plug-in settings (gain, mix, latency), and a state moved between formats keeps the filter, not the settings | The same project can play at another gain, mix or latency in another format. | [Settings](docs/use/plugins.md#settings) |
| Changing the Audio Unit's or the LV2 plug-in's Latency during playback leaves a gap (the new latency plus the filter's own delay) and cuts a sounding tail | The new processor starts with no history; a crossfade would need both sizes running at once. Not automatable for this reason (PLUG-10), though JUCE-based LV2 hosts automate it anyway. | [Latency and tail](docs/use/plugins.md#latency-and-tail) |
| A host rate that is not a whole number of hertz (such as 48000 ÷ 1.001) plays silence in VST3 and is refused by the Audio Unit and LV2 | No filter is made for such a rate and nothing is resampled (VST3-9, decided by jrp). The Audio Unit and LV2 could accept it the same way when a host needs it. | [When a filter does not fit](docs/use/plugins.md#when-a-filter-does-not-fit) |
| A WAV whose sizes were never filled in (`0xFFFFFFFF`, as FFmpeg writes to a pipe) is read to the end of the file, so one left by an interrupted write is read as if it were complete; only PCM or float audio that stops inside a frame is refused. Such a file of 4 GiB or more is refused | jrp's decision of 10 November 2026 (IO-11). A file of 4 GiB or more has no size a reader could use; RF64, W64 or a RAW stream carry long audio. | [Input files](docs/use/command-line.md#input-files) |
| No IR resampling or automatic channel adaptation for direct sound-file filters | Rate/count mismatches fail; use an explicit configuration. IO-2 covers resampling. | [Configuration](docs/use/configuration.md) |
| One VST3 input bus and one output bus, each 1–8 channels; a window on macOS and Windows only | Presets and generic host controls on Linux; layouts are checked with a notice only; the Linux window is VST3-13, host switching VST3-2. | [VST3](docs/use/plugins.md#loading-a-filter) |
| No promised migration of old VST2 project state | New VST3 identity; define a migration requirement before implementing one (VST3-6). | [VST3](docs/use/plugins.md#identities) |
| No built-in device application or clock management | CLI pipes need a producer/consumer; VST3 relies on its host. Standalone device support requires a separate design. | [Concepts](docs/use/concepts.md#files-and-continuous-streams) |
| One FFT backend per binary; Ooura/Accelerate have no FFTW-style measured plans or wisdom | Intentional deployment choice. Assess alternatives under FFT-2; do not imply runtime fallback. | [FFT guide](docs/develop/fft-backends.md#selection-and-contract) |
| FFTW wisdom is CPU/build/size-sensitive and process-global | Retune when relevant inputs change; external planner users must coordinate. A successful import does not prove optimality. | [Planning](docs/use/command-line.md#fftw-plan-tuning) |
| Core processing uses interleaved float buffers that must not overlap | Use `HostProcessor` for planar/in-place float/double host buffers; internal DSP remains float. | [Engine API](docs/develop/library.md#engine-api) |
| Latency and filter replacement are not instantaneous | Core latency is B; the plug-ins' default is 0 frames (PERF-2); a larger Latency adds its delay. Replacement starts with fresh history and warms/crossfades; reset work scales with the block buffers and delay lines, not the filter length (PERF-5). | [Replacement](docs/develop/plugin-contract.md#replacement-and-threads) |
| Callback safety has a defined thread contract | One audio thread per instance; serialized control calls; teardown requires stopped audio. Stress/deadline evidence remains ROBUST-1. | [Implementation invariants](docs/develop/architecture.md#invariants-to-preserve) |
| Resource limits cap channels, paths, delays, text, IRs and state | Intentional bounds; assess any increase with memory/overflow tests. | [Core limits](docs/develop/library.md#resource-limits), [tests](docs/develop/testing.md) |
| The configuration format is not identical across consumers | No external consumer has verified this corpus yet; CFG-1. Older bundled configs may need delay lines and portable paths. | [Compatibility matrix](docs/use/other-programs.md#what-convolver-accepts) |
| Path portability checks are partial | No drive/share remapping; a clean profile is not certification for Windows or another reader. Extend checks only with fixtures. | [Profiles](docs/use/other-programs.md#check-that-a-configuration-will-travel) |
| Output is WAV, RF64, W64, AIFF, CAF, FLAC or headerless raw; speaker labels are WAV and RF64 only, reduced valid bits WAV only | Input codecs and FLAC output depend on the libsndfile build; the preview packages have no FLAC or Ogg, by decision ([workarounds](docs/use/command-line.md#flac-and-ogg)). Boundary-size tests remain IO-1; libsndfile cannot write labels in the other containers (IO-5). RAW has no embedded rate/channel metadata. | [Output formats](docs/use/command-line.md#output-formats) |
| Output writes are not transactional | An I/O or numerical failure leaves a partial file, which the CLI says is incomplete; deleting it or publishing through a temporary file was not chosen ([decisions](docs/project/decisions.md#diagnostics)). Never promise rollback for pipes. | [Output safety](docs/use/command-line.md#output-safety) |
| Gain estimation is not a limiter; float samples can exceed full scale | Input beyond full scale can clip integer output; `no-clip` keeps full-scale input from clipping it, quantizer headroom included (GAIN-3). | [Gain](docs/use/command-line.md#gain-and-attenuation) |
| Dither/shaping/valid bits apply to integer file output only | Float/RAW output rejects those options; the plug-ins exchange floats, so the host does any final conversion. | [Integer output](docs/use/command-line.md#integer-formats-dither-and-noise-shaping) |
| C++ packages are static development libraries, not self-contained installers or a stable binary ABI | Rebuild with compatible toolchains; shared dependencies must be deployed. REL-1 covers end-user bundles. | [Package guide](docs/develop/library.md) |
| Source tags and configured CI do not prove a release is qualified | Tie results to exact commits; named hosts, clean machines and signatures remain release evidence. | [Versioning](docs/develop/versioning.md), [testing](docs/develop/testing.md) |
| Subnormal numbers (below about 1.2e-38, -758 dBFS) are flushed to zero while processing, on x86-64 and AArch64 | Deliberate (PERF-15): results are not bit-identical to IEEE gradual underflow, and a signal wholly below that level is silence; the caller's floating-point mode is restored after each call. Other processors keep gradual underflow. | [Subnormal numbers](docs/develop/architecture.md#subnormal-numbers) |
| Finite oracle, fuzz and heap-guard tests do not prove all-input correctness or hard realtime | Keep explicit numerical tolerances and test boundaries; ROBUST-1. | [Testing](docs/develop/testing.md#validation-limits-and-release-work) |
| Equalizer APO 1.4.2 (x86/x64) convolution drops up to three bins just below half the sample rate when a block is not a multiple of four frames | Upstream behaviour, inaudible (about 22 kHz at 44.1 kHz); the export does not work around it. The runtime test uses 480-frame blocks. | [Equalizer APO guide](docs/develop/testing.md#equalizer-apo-export-tests) |
| Historical Windows applications are not built or host-tested by portable CI | Old PCM32, shaping, CMD tail and host-negotiation gaps remain historical; fix only proven defects, without implying modern host support. | [Historical coverage](docs/develop/testing.md#historical-coverage) |

## Done

Finished items move out of this file so that it holds open work only. Each
keeps one line, with its ID, title, date, pull request and the page that now
describes it, in the [archive of finished items](docs/archive/todo/README.md):
everything finished up to 8 November 2026 is in
[that day's list](docs/archive/todo/2026-11-08-done-items.md). The
[repository checks](docs/develop/workflow.md#automated-checks) treat an ID
listed there as defined and done. What each item delivered is described on
its linked page and in the [CHANGELOG](CHANGELOG.md).

Items finished since then are listed here, one line each, until the next
move to the archive.

- [x] **PLUG-5 — One release pipeline.** Done for the previews, 12 November
  2026 (DOC-14): one candidate pipeline packages the VST3 plug-in, the Audio
  Unit, LV2 on Linux, the CLI, the tools and the examples for macOS,
  Windows and Linux (PRs #38, #101, #115, #163, #179;
  [what a package holds](docs/develop/releasing.md#what-a-package-holds)).
  Signing and the other production gates continue in REL-1.
- [x] **PLUG-9 — Editor report and loading from a file.** 12 November 2026
  (DOC-14): the report, `report_text()` and Copy Diagnostics, and the
  shared file-to-state step (PR #84), with the computed gains since PR
  #188 ([status and log](docs/develop/plugin-contract.md#status-and-log)).
  The meters go into the report with PLUG-10.
- [x] **LV2-5 — Release bundles.** Done for Linux, 12 November 2026
  (DOC-14): the Linux package holds `LV2/convolver.lv2` since 0.13.12, with
  `VERSION` mapped to LV2's minor and micro versions as jrp decided on
  10 November 2026 (PR #179; [LV2 versions](docs/develop/versioning.md#lv2-versions)).
  Distribution packages and macOS or Windows LV2 bundles are separate
  possibilities, taken up only on request.
- [x] **AU-5 — Release.** Done for the previews, 12 November 2026 (DOC-14):
  the unsigned Audio Unit component, with its installer package, symbols
  and notices, ships in the macOS previews since 0.12.12 and `auval -strict`
  checks the installed component (PR #115;
  [what a package holds](docs/develop/releasing.md#what-a-package-holds)).
  Signing and notarization continue in REL-1.
- [x] **AU-7 — Settings as Audio Unit parameters.** 12 November 2026
  (DOC-14): Gain (0), Mix (1003), Latency (1004) and the Gain mode (1002)
  as global parameters, Latency with its change notification (PRs #80, #95,
  #188; [settings](docs/develop/plugin-contract.md#settings)). Their
  GarageBand checks are AU-4's; the meters come with PLUG-10.
- [x] **PERF-1 — Reproducible current benchmarks.** Joined PERF-3,
  12 November 2026 (DOC-14): reruns on named machines are part of PERF-3's
  one measurement exercise. The records so far are in
  [FFT measurements](docs/develop/fft-measurements.md).
- [x] **PLUG-38 — Restoring the playing filter keeps its history.**
  13 November 2026, PR #193: restored state or a preset holding the filter
  already playing keeps it and its history, and the settings ramp; checked by
  the plug-in harness in every format and `vst3.adapter`
  ([scope of each change](docs/develop/plugin-contract.md#scope-of-each-change)).
- [x] **DOC-14 — One backlog consolidation pass.** 12 November 2026, PR #191: open
  items say what remains, what would justify it and when it is done; their
  earlier text is in the
  [archive](docs/archive/todo/2026-11-12-open-item-history.md)
  ([recording progress](docs/develop/workflow.md#recording-progress-in-todo)).
- [x] **PLUG-37 — The scope of each change.** 12 November 2026, PR #191:
  jrp's rule
  that each change does only the work it needs, with the history check in
  `native.host_settings`
  ([scope of each change](docs/develop/plugin-contract.md#scope-of-each-change)).
- [x] **PLUG-35 — Saved state records the settings at their defaults.**
  12 November 2026, PR #189: the VST3 plug-in's component state records
  Gain, Mix and Latency whenever it saves them, defaults included (host
  state version 3), so a
  `.vstpreset` a host saves at 0 dB, 100% and 0 frames resets them when
  chosen in the window, as the host's preset menu does
  ([decision](docs/project/decisions.md#plug-in-behaviour)).
  Presets `convolver-preset` makes from a filter, and the Audio Unit's and
  LV2's states, still write version 1, and older presets and projects load
  as before; one saved at the defaults by an earlier version still leaves
  the settings. Cost: 0.12.12, the first preview, refuses a project saved
  at Latency 0 (now every one at the defaults) as damaged. Checked by `native.state`
  and `vst3.adapter`; not yet tried in a host.
- [x] **PLUG-26 — Choose a `.vstpreset` in the window.** 12 November
  2026, PR #185: Choose… and dropping a file in the VST3 and Audio Unit
  windows also open a Convolver `.vstpreset`, read as `convolver-preset`
  reads it, with the Gain, Mix and Latency it holds reported to the host
  ([decision](docs/project/decisions.md#plug-in-behaviour)); another
  plug-in's preset or a damaged one is refused with a fix. Checked by
  `vst3.adapter` and, on macOS, `au.adapter`; not yet tried in a host
  ([a preset in the window](docs/use/plugins.md#a-preset-in-the-window)).
  A preset a host saved at the default settings left them, until
  PLUG-35.
- [x] **PLUG-33 — A chosen file follows the same rules as a preset.**
  12 November 2026, PR #184: a file chosen in a window or through LV2's
  file parameter needs the host's outputs exactly and at least its inputs,
  as a preset does; from a list, the exact entry first, then the one with
  the most inputs below the host's, named in the log and the window
  ([decision](docs/project/decisions.md#plug-in-behaviour)); with no
  filter, Status names choosing a filter where there is a window
  ([channels](docs/use/plugins.md#channels),
  [channel layouts](docs/develop/plugin-contract.md#channel-layouts)).
- [x] **LV2-7 — Settings as control ports.** 10 November 2026, PR #182:
  Gain, Mix and Latency as the control inputs `gain`, `mix` and
  `latency_frames` after the atom ports; a new Latency while active is built
  on the worker thread. Checked by `lv2.adapter`; older sessions in named
  hosts were not opened, as jrp's decision for LV2 in the previews allows
  ([LV2 ports](docs/use/plugins.md#lv2-ports),
  [adapter contract](docs/develop/plugin-contract.md#format-notes)).
- [x] **PLUG-34 — The window's behaviour in shared code.** 10 November
  2026, PR #181: what the window shows and what a user's action means, in
  `portable/window_model.hpp`, used by the macOS window with no visible
  change and by the Windows one
  ([implementation map](docs/develop/architecture.md#the-shared-macos-window)).

- [x] **IO-11 — WAV files with unfilled sizes are read.** 10 November
  2026, PR #176. jrp chose to read them everywhere: a WAV whose sizes were
  left at `0xFFFFFFFF` (FFmpeg writing to a pipe) is read to its end,
  knowing that FFmpeg stopped half way leaves the same header and that its
  file is read as if complete. One of 4 GiB or more is still refused.
  macOS's reader is shown such a file with its sizes filled in, because
  left to open one it read 15 bytes of mono 24-bit audio as four frames
  and 18 as five
  ([input files](docs/use/command-line.md#input-files),
  [decision](docs/project/decisions.md#identities-and-file-formats)).
- [x] **IO-10 — WAV files that other programs read are refused.** 9
  November 2026, PR #176, from that day's project review: a WAV whose last
  odd chunk lacks its padding byte (Python's `wave` module) is read whole
  ([input files](docs/use/command-line.md#input-files),
  [decision](docs/project/decisions.md#identities-and-file-formats)).
- [x] **CI-36 — VST3 tests under sanitizers in CI.** 9 November 2026,
  from the whole-project review: ASan and UBSan for the `vst3.*` tests,
  ThreadSanitizer for `vst3.adapter`
  ([VST3 tests](docs/develop/testing.md#vst3-tests)).
- [x] **TIDY-1 — Remove small duplicated helpers.** 9 November 2026, from
  the whole-project review of that day (M2; its `utf8_path` part is
  DIAG-7): one copy each of `splitmix64`, `ports()` and the little-endian
  helpers, the window's status numbers named, and the unused `fft_size()`
  removed ([implementation map](docs/develop/architecture.md#components-and-source-of-truth)).
- [x] **CI-37 — Test programs report every failing case.** 9 November
  2026, from the whole-project review of that day: ten programs run their
  cases through one shared runner
  ([native production tests](docs/develop/testing.md#native-production-tests)).
- [x] **DIAG-6 — Show the reader's reason when a sound file is refused as
  a filter.** 9 November 2026, from the whole-project review of that day
  ([filter files](docs/use/configuration.md#filter-files)).
- [x] **DIAG-7 — File names as UTF-8 in messages and paths on Windows.** 9
  November 2026, from the whole-project review of that day: one shared
  helper, and a non-ANSI name loaded and refused on every system
  ([invariants](docs/develop/architecture.md#invariants-to-preserve)).
- [x] **CI-35 — Prose edits to the first-use pages run Lint only.** 8
  November 2026, from jrp's question why a README link ran every workflow;
  [CI triggers](docs/develop/testing.md#documentation-only-pull-requests).
- [x] **REL-9 — Windows and Linux in previews.** 8 November 2026,
  PR #163: from the second preview the draft release attaches the Windows
  and Linux packages, after they run on clean machines
  ([publishing](docs/develop/releasing.md#publishing-an-unsigned-preview),
  [decision](docs/project/decisions.md#releases)). jrp published the first
  such preview, [0.13.2](https://github.com/jrp2014/convolver/releases/tag/v0.13.2), on 9 November 2026, and the
  next, [0.13.12](https://github.com/jrp2014/convolver/releases/tag/v0.13.12), on 11 November 2026.
- [x] **REL-7 — Apply bounded preview acceptance.** 8 November 2026,
  PRs #156 and #159; jrp published [0.12.12](https://github.com/jrp2014/convolver/releases/tag/v0.12.12) without the remaining
  host checks and REL-3, which stay open under HOST-1, AU-4 and REL-3
  ([checks left](docs/project/status.md#checks-left-before-publication)).
- [x] **AU-17 — Audio Unit listeners told on the main thread.** 8
  November 2026, PR #158, from a ThreadSanitizer report on `main`
  ([run 42512164144](https://github.com/jrp2014/convolver/actions/runs/42512164144));
  [status and log](docs/develop/plugin-contract.md#status-and-log).
- [x] **REL-8 — Rehearse publishing before the preview.** 8 November
  2026, PR #152; jrp's rehearsal from `main` on the v0.12.5 candidate
  ([run 42505146594](https://github.com/jrp2014/convolver/actions/runs/42505146594))
  made the draft, which he read ("That looked fine to me") and deleted;
  [publishing](docs/develop/releasing.md#publishing-an-unsigned-preview).
- [x] **DOC-13 — Shorter backlog, superseded plans and evidence archived.** 8
  November 2026; [archived TODO entries](docs/archive/todo/README.md),
  [archived plans](docs/archive/plans/README.md).
- [x] **DOC-10 — One short route to load a filter.** 8 November 2026,
  PR #157, from jrp's file-loading review; [your first sound in a
  host](docs/use/first-sound-in-a-host.md), [inputs](docs/use/plugins.md#loading-a-filter),
  [preset folders](docs/use/presets.md#preset-folders).
- [x] **PLUG-31 — Generated VST3 presets carry the filter's name.** 8
  November 2026, PR #148, checked in REAPER;
  [presets](docs/use/presets.md),
  [record](docs/archive/reviews/2026-11-08-plug31-reaper.md).
