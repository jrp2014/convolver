<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver TODO

Open work on the active portable implementation, with a stable ID for each
item. This is the shared backlog, not a promise that every idea will be
implemented. Each item says in a line or two what remains and when it is
done; its pull requests, the plan it links and the [CHANGELOG](CHANGELOG.md)
keep the evidence. The longer text the items had until 20 November 2026 is
[archived](docs/archive/todo/2026-11-20-open-item-history.md), and finished
items are listed under [done](#done).

**In short:** jrp has published nine unsigned developer previews, the latest
[0.18.29](https://github.com/jrp2014/convolver/releases/tag/v0.18.29)
(26 November 2026). Significant features come before corner and edge cases
([decision](docs/project/decisions.md#releases)). The next milestone is an
independent user running a published preview (REL-3).
[Status](docs/project/status.md) shows what users can run now and the checks
left, and [decisions](docs/project/decisions.md) what jrp has decided. The
[limitations table](#documented-limitations) lists current constraints.

Read the [execution priorities](#execution-priorities), then search for your
item's ID. To take an item on, open a draft pull request titled with its ID;
that is the claim, and its description holds the handoff note. Record
progress as the [workflow guide](docs/develop/workflow.md#recording-progress-in-todo)
describes, and confirm the current source first: other work may already
have done part of it. Compatibility takes priority over speed. Do not change
the historical archive to implement a portable feature. External links are
starting points, not endorsements; verify the exact release again when
implementing an item.

## Execution priorities

This is the order for new work, not approval to start every item.
Correctness regressions come first whenever they are found: crashes, lost
state, wrong audio, compatibility failures and dropouts are fixed at once,
with a regression test where practical
([preview acceptance](docs/develop/releasing.md#what-blocks-a-preview)).
Each open item, or part of one, is in one table below, by what it waits for;
within a table the order is the suggested one. The runtime freeze of
9 November 2026 was lifted on 10 November, but what jrp parked by name stays
parked ([decisions](docs/project/decisions.md#releases)).

**Next milestone** (jrp's [guidance](docs/review-2026-10/release-readiness-guidance-2026-10-22.md),
22 October 2026): an independent user installs and uses an exact preview
package, following only the documentation (REL-3). The walkthrough and the
host checks left run on the published 0.18.29 files, the baseline jrp chose
on 26 November 2026. Finishing a dependable product matters more than
another substantial refactor: move code into shared layers only when two
formats need the same behaviour, and keep libsndfile at the I/O boundary and
C++20 in the core.

Brought up to date with `main` at `549ec9c` (0.16.8) on 20 November 2026.

### In progress

Check the open pull requests for an ID before starting anything.

### Ready

| Item | What to do | Note |
| --- | --- | --- |
| **PLUG-10** | The meters, starting with the output peak and a clip count | Moved to later previews on 6 November 2026 ([decision](docs/project/decisions.md#plug-in-behaviour)) |
| **REL-4** | The [steps after publishing](docs/develop/releasing.md#after-publishing-make-it-findable) for the published previews | No code |
| **PLUG-51** | Every output's routes in the window and Copy Diagnostics | From jrp's usage review of 22 November 2026 (PR #277) |
| **IO-12** | Read or refuse float samples in an extensible Wave64 file, which libsndfile decodes wrongly | Wrong audio, found 26 November 2026; jrp asked for the item |

### Depends on

| Item | What remains | Waits for |
| --- | --- | --- |
| **REL-3** | The independent walkthrough of the published 0.18.29 files | A tester, whom jrp chooses |
| **HOST-1**, **VST3-11**, **AU-4**, **AU-6**, **PERF-2**, **COMPAT-1** | [Checks left](docs/project/status.md#checks-left-before-publication) 1 to 5, 8 and 9; COMPAT-1's REAPER project saved with 0.18.29 | jrp's Mac session on 0.18.29 |
| **PERF-2**, after its session | The CPU indication beside the Latency menu, and block sizes tuned on Apple silicon (first a largest block above 8,192 frames for filters over 30 s) | Check 9 and the [workload measurements](docs/develop/fft-measurements.md#supported-workloads) run on jrp's Mac |
| **PERF-22** | Audacity's playback start timed with 30 s and 120 s filters, since VST3-20's cheaper restore digest | jrp's Mac with Audacity |
| **VST3-12**, **HOST-4** | The Windows window in hosts and a screen reader; Equalizer APO on a real device | A Windows tester or device |
| **LV2-4** | Desktop LV2 hosts and PipeWire on a real sound card | A Linux tester |
| **HOST-8** | Ardour 9.0's abort as `new_session` exits | A fixed Ardour on `ubuntu-latest` |
| **CFG-1**, then **CFG-2** | Other `.cfg` readers, then importing and exporting their formats | Access to Roon, JRiver or Audiolense workflows |
| **FFT-2**, **VST3-5**, **PLUG-11** | Optimisation | A workload that needs it; PERF-3's [measurements](docs/develop/fft-measurements.md#supported-workloads) showed none, though VST3-5 may for 60 s and large 96 kHz multichannel filters |
| **VST3-13**, **VST3-2**, **VST3-6**, **HOST-2**, **LV2-3**, **LV2-6**, **DIAG-9** | The Linux window, switching filters from the host, replacing the old VST2 plug-in, CLAP, named LV2 layouts, an LV2 window, numbers in the user's locale | A stated user need |
| **IO-1** | Large files | A reproducible defect or a user need |

### Needs a decision

| Item | What remains | The decision |
| --- | --- | --- |
| **REL-1**, **PLUG-20** | Signing and notarization, verified minimum versions and the final licence review; the VST logo in the window (optional) | jrp's signing choices in the [signing plan](docs/project/plans/2026-11-09-macos-signing.md), then his setup |
| **IO-2**, **EDGE-1** and the other **EDGE** items | First EDGE-1, with IO-2's documented route; then the other limitations users cite (IO-2's conversion, EDGE-11's other candidates, EDGE-10, EDGE-13); then EDGE-12, EDGE-9, EDGE-2, EDGE-4 and EDGE-5; then EDGE-6 to EDGE-8; EDGE-14 and EDGE-15 | Each starts with the owner's decision its row in the [edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#ranked-edges-to-build) names, and the plan's gate. Feedback that asks for one moves it up |

### Parked

Until REL-3, from jrp, 8 November 2026: "park any non-essential corner or
edge case robustness tests" ([decision](docs/project/decisions.md#releases)).
No existing test is removed, and a reproduced crash, lost state, unsafe
output or wrong audio reopens an item at once. The FFmpeg and Media
Foundation items are parked by owner decision instead.

| Items | Note |
| --- | --- |
| **ROBUST-1**, **PLUG-29**, **HOST-6** | Robustness and lifecycle corner cases |
| **CI-32** | The most extreme numerical cases less often; dormant (jrp, 12 November 2026) |
| **PERF-7**, **PERF-8**, **PERF-16** to **PERF-19** | Profiling and measurement-only optimisations |
| The rest of **PLUG-25** and **CFG-3** | Marking a host rate as not yet confirmed, and reading Windows-1252 names; each also needs jrp's decision |
| **FFMPEG-2** to **FFMPEG-5**, **HOST-3** | A C API and FFmpeg filter; a Windows Media Foundation transform. Revisit only by jrp's decision |

## User experience and distribution

- [ ] **REL-1 — Production release checklist.** The
  [production gates](docs/develop/releasing.md#production-gates) for 1.0:
  Developer ID signing and notarization on macOS
  ([signing plan](docs/project/plans/2026-11-09-macos-signing.md)), the
  Windows packages unsigned, verified minimum versions, dependency closure and
  the final licence review. The pipeline's production path exists and
  refuses while `production_enabled` is false; **remains** the signing step in
  the candidate's package phase once jrp's Apple account and secrets exist,
  and the evidence file for the candidate.
  **Done when** one candidate run gives signed, notarized macOS packages and
  the Windows and Linux packages, each gate recorded as passed in its
  evidence file, and jrp publishes them as 1.0.
- [ ] **REL-3 — Independent walkthrough of an exact preview.** Someone who
  wrote neither the code nor the guides follows the REL-2 journeys from the
  published 0.18.29 files on their own machine, using only the documentation.
  **Done when** the record gives the tester, package hash, system and host,
  and for each step whether it worked, where they hesitated and what needed
  help; and each finding has fixed the guides or the product, with the
  affected steps repeated.
- [ ] **REL-4 — Make the preview findable.** The
  [steps after publishing](docs/develop/releasing.md#after-publishing-make-it-findable)
  at each preview, and for 0.13.12, which added LV2, the community posts and
  the LV2 list proposal those steps name. Recurs with each preview.
- [ ] **HOST-1 — Named host matrix.** REAPER and Audacity on jrp's Mac
  ([checks left](docs/project/status.md#checks-left-before-publication) 1 to
  4) on the published 0.18.29 files, with REAPER step 11's check that a file
  chosen in the window adds no undo entry (from VST3-17), then Windows hosts. **Done when**
  each check passes or jrp narrows it, recorded as
  [every report](docs/develop/host-testing.md#every-report) asks.
- [ ] **HOST-2 — Additional formats.** CLAP, only on request: every CLAP
  host found also loads VST3. **Done when** a CLAP adapter passes the PLUG-2
  harness and the lifecycle tests.
- [ ] **HOST-3 — Windows Media Foundation transform (parked).** Name the
  target users first; for system-wide correction the Equalizer APO export
  is cheaper ([plan](docs/project/plans/2026-10-21-media-foundation-transform.md)).
- [ ] **HOST-4 — System-wide filtering through Equalizer APO.** The manual
  check on a real Windows device in the
  [guide](docs/develop/testing.md#equalizer-apo-export-tests). **Done when**
  its result is recorded there.
- [ ] **HOST-6 — Explain an Ardour export 16 frames early.** Seen once in
  about 300 local runs on 6 November 2026. **Done when** a kept session
  (`CONVOLVER_ARDOUR_KEEP`) shows whether Ardour or Convolver moved it.
- [ ] **HOST-8 — Ardour 9.0's abort as `new_session` exits.**
  `known_new_session_abort()` in `tests/plugins/ardour_tests.py` accepts it on
  9.0 only; jrp may report it upstream. **Done when** the Ardour on
  `ubuntu-latest` no longer aborts and the test stops accepting it.
- [ ] **HOST-13 — More of the REAPER checks from one command.** What
  HOST-11's [script](docs/develop/host-testing.md#reaper-from-one-command)
  does not cover yet: the other packaged presets, a refused preset with
  Latency and the filter kept, the wrong host rate, a Latency change with
  its render in time, and the Audio Unit in REAPER. **Worth doing when** a
  REAPER finding comes from one of these, as the refused preset that reset
  Latency to 0 in jrp's live session of 22 November 2026 did, or before a
  release whose changes touch them; one addition per run on jrp's Mac.
- [ ] **DIAG-9 — Numbers in the user's locale.** Low priority. **Done when**
  the window shows numbers in the system locale from one formatting path,
  Status and the log stay plain, and a test covers other separators.

## Plug-ins: VST3, LV2 and AU

VST3, LV2 and AU are developed in parallel, each against its own format's
headers over `HostProcessor`, following the
[shared adapter contract](docs/develop/plugin-contract.md). Shared work,
done once for all three formats:

- [ ] **PLUG-10 — Meters.** The output peak and a clipped-sample count,
  kept as audio-thread counters and shown in each window and in Copy
  Diagnostics. **Done when** each format shows them since the last filter
  change, with an oracle-backed test and nothing new allocating or locking.
- [ ] **PLUG-51 — Every output's routes.** The window's Routing line lists
  the first four paths and then how many more, so a ten-path crossover
  hides six, and nothing says which outputs no path feeds, so a crossover
  that leaves the subwoofers silent cannot be spotted there. **Done when** the window's details and Copy Diagnostics give one
  line per output with its inputs, route gains and delays, mark an output
  that nothing feeds, and say what bypass plays there (the input or
  silence), for VST3 and the Audio Unit, with window-model and report tests
  for a crossover and a matrix.
- [ ] **PLUG-11 — FFT planning in FFTW-built plug-ins (optional).** A Quick
  or Measured planning choice, only in plug-ins built with FFTW, applied
  while preparing. **Worth doing when** a workload needs it: with estimate
  plans FFTW was already cheaper than Ooura where the transforms matter
  ([measurements](docs/develop/fft-measurements.md#supported-workloads)),
  and no release ships FFTW-built plug-ins.
- [ ] **PLUG-20 — The VST logo in the window (optional).** **Done when**
  the macOS window shows the logo and notice without crowding the settings,
  and its tests cover it ([trademarks](packaging/trademarks/README.md)).
- [ ] **PLUG-25 — Say which rate the host plays at.** The rate refusal's
  advice is done (PR #210). **Remains:** whether the window marks a host
  rate as not yet confirmed (since PLUG-27 a chosen file waits instead;
  a placeholder-rate match and restored state are still judged against
  it); parked. **Done when** jrp has decided and, if
  he wants it, the marking is shown and its message tests cover it.
- [ ] **PLUG-43 — Hold a refused filter beside the one that plays.** Since
  PLUG-42, VST3 and the Audio Unit refuse a filter that does not suit the
  host only while a filter that suits it plays, and the refusal is final: a
  later start with settings that suit the refused filter keeps the playing
  one (jrp, 21 November 2026: a user who sees the refusal must be able to
  trust that restarting playback will not switch filters). Holding the
  refused filter beside the playing one, to replace it at such a start,
  keeps two filters in memory and adds a state every later change must
  remember. **Needs** a user asking for it; jrp then decides. **Done when**
  he has decided and, if he wants it, the contract and adapter tests cover
  it.
- [ ] **PLUG-45 — Built-in example presets in hosts' preset menus.**
  Identity, Echo and Repeat as Audio Unit factory presets and a VST3
  program list, generated when chosen at the host's rate and channels, so
  they always fit and need no files; Echo with three fading repeats
  0.3 s apart, as the packaged example becomes in PR #266. A chosen preset
  saves as any filter does. jrp, 21 November 2026: after the preview. **Done when** both formats offer them, host
  tests choose each and the guide describes them.
- [ ] **PLUG-29 — Keep the Recent list out of the log folder.** Save it in
  each platform's application-support folder, reading the old file once.
  **Done when** the tests cover both paths and the guide names the folder.

LV2 (Linux first; also PipeWire system audio):

- [ ] **LV2-3 — Named layouts.** **Done when** each PLUG-4 named layout has
  an LV2 URI checked by `lv2lint` and pluginval; only when a host or user
  needs named speakers.
- [ ] **LV2-4 — User guide and host matrix.** Jalv, Carla, REAPER and
  Qtractor, and PipeWire on a desktop with a real sound card. **Done when**
  those are recorded in the [host matrix](docs/develop/host-testing.md#lv2-hosts).
- [ ] **LV2-6 — Editor report and UI (optional).** The report on the notify
  port, then an LV2 UI reusing the macOS and Windows views; only if Linux
  users need it.

AU (AUv2, macOS: Logic, GarageBand, SoundSource):

- [ ] **AU-4 — User guide and host matrix.** GarageBand, checks 5 and 8 in
  [checks left](docs/project/status.md#checks-left-before-publication), on
  0.18.29's files; other hosts on request. **Done when** they pass or jrp
  narrows them.
- [ ] **AU-6 — Editor window.** VoiceOver (check 5); the host's
  responsiveness while a long chosen filter loads; a host that runs the unit
  out of process (REAPER, or Rosetta); and from the 1 November redesign, in
  GarageBand, the typed boxes, a light rendering, a manual Mix drag and
  listening to the Latency gap. **Done when** VoiceOver passes or jrp
  narrows it, and the rest are recorded.

## FFmpeg integration

Parked by jrp: a patched FFmpeg tree adds uncomfortable coupling, and the
pipe is tested (FFMPEG-1). If revisited, prefer the pipe, a filter accepted
upstream, or state support in FFmpeg's `lv2` filter
([plan](docs/project/plans/2026-10-21-ffmpeg-filter.md)).

- [ ] **FFMPEG-2 — C API.** A C99 header over `HostProcessor` and the
  configuration loader, with C-only tests matching `Engine`.
- [ ] **FFMPEG-3 — Out-of-tree `convolver` filter.** A patch series against a
  pinned FFmpeg release, with a CI job comparing it with the CLI.
- [ ] **FFMPEG-4 — Reverb support.** A latency-aligned dry/wet mix and filter
  gain in the filter.
- [ ] **FFMPEG-5 — Upstream decision.** Whether to submit the filter, once
  FFMPEG-3 has users.

## Compatibility and robustness

- [ ] **CFG-1 — Verify other readers.** Run redistributable fixtures in
  Roon, JRiver or Audiolense workflows and record the subsets they accept
  ([compatibility](docs/use/other-programs.md)).
- [ ] **CFG-2 — Configurations from and for other programs.** Import
  Equalizer APO, CamillaDSP and BruteFIR configurations, export to
  CamillaDSP, each refusing what it cannot represent; after CFG-1.
- [ ] **CFG-3 — Read configurations saved in Windows-1252.** Non-UTF-8 names
  are refused with advice (PR #211). **Remains:** jrp's decision whether to
  read them, a `.cfg` compatibility choice recorded in the decisions log
  either way. **Done when** he has decided and, if he wants it, such names
  load with a test.
- [ ] **COMPAT-1 — Files from released builds.** Five previews' files are
  kept and checked, and 0.14.0's LV2 and Audio Unit state
  ([upgrade tests](tests/upgrade/README.md)). **Remains:**
  0.18.29's files kept as the [steps after publishing](docs/develop/releasing.md#publishing-an-unsigned-preview)
  give, and a REAPER project saved with 0.18.29. **Done when** both are kept
  and checked.
- [ ] **IO-1 — Large files.** **Done when** each container's 4 GiB boundary
  and pipes beyond it have a test; only for a defect or a user need.
- [ ] **IO-2 — The rate-mismatch workflow, and offline IR resampling.** A
  documented route for an IR at one rate and a host at another. **Done
  when** that route is documented and any conversion is an explicit
  operation tested for gain, delay, length and metadata, with a mismatch
  still refused by default; jrp decides whether Convolver resamples
  ([edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#1-a-filter-for-every-sample-rate)).
- [ ] **IO-12 — Float samples in an extensible Wave64 file.** libsndfile
  1.2.2 decodes the 32-bit float subtypes of a Wave64 file with
  `WAVE_FORMAT_EXTENSIBLE` as integers, so 1.0 reads as 0.496 and the audio
  is wrong, as an input or as a filter (found with EDGE-16, PR #307; plain
  float Wave64, as Convolver writes it, and extensible PCM read correctly).
  **Done when** such a file is read correctly or refused with advice on
  every reader, with a test, and the libsndfile defect is reported upstream.
- [ ] **ROBUST-1 — Longer fuzz and transition stress.** Worst callback
  times under repeated filter changes, with found inputs kept as
  regressions.

## VST3 SDK features not yet used

Candidates from the [VST3 API audit](docs/review-2026-10/vst3-api-audit-2026-10-23.md)
(SDK 3.8.1); adopt each only with adapter, validator and pluginval tests.
MIDI, transport, track-information and side-chain interfaces are not
relevant to a convolver unless the product changes.

- [ ] **VST3-2 — Switch filters from the host without an editor.** A
  program list (`IUnitInfo`, `IProgramListData`) over a configuration
  list's entries, switched with the usual crossfade.
- [ ] **VST3-5 — Progress for slow filter preparation.** `IProgress` or a
  worker. **Worth doing when** the load-timing tests, run with 60 s or large
  96 kHz multichannel filters, show `setState` holding a host's thread over
  its limit: preparing those took up to 0.2 s on shared machines
  ([measurements](docs/develop/fft-measurements.md#supported-workloads)).
- [ ] **VST3-6 — Replacing the old VST2 plug-in.** Only with a migration
  requirement: declare `'Conv'` as compatible and convert old state and
  Gain's mapping.
- [ ] **VST3-11 — Editor window on macOS.** The window's steps in REAPER and
  Audacity, keyboard isolation and VoiceOver (checks 1 and 4). **Done
  when** they pass or jrp narrows them.
- [ ] **VST3-12 — Editor window on Windows.** **Done when** a tester has
  used it in REAPER, another Windows host and a screen reader, and the
  release notes drop the limitation.
- [ ] **VST3-13 — Editor window on Linux.** A VSTGUI window driven by the
  host's `IRunLoop`, when Linux VST3 users ask
  ([windows plan](docs/project/plans/2026-11-10-windows-linux-plugin-windows.md)).

## FFT libraries and performance

- [ ] **FFT-2 — Evaluate alternatives only against a measured need.** None
  yet: a faster library could save up to about a third on room correction,
  which needs at most 9% of a core
  ([measurements](docs/develop/fft-measurements.md#supported-workloads)). PFFFT
  first ([integration review](docs/develop/fft-backends.md#alternatives-for-linux-and-windows));
  adopt a backend only with Windows and Linux measurements, no allocation or
  locks while processing and the same oracles.
- [ ] **PERF-2 — Inverse-transform sharing or nonuniform partitions.** The
  non-uniform engine shipped in 0.13.2. **Remains:** check 9's session, the
  CPU indication beside the Latency menu and block sizes tuned on Apple
  silicon ([measurements](docs/develop/fft-measurements.md#the-non-uniform-engine)).
- [ ] **PERF-22 — VST3 restore cost at Audacity's playback start.**
  Audacity 4.0.2 hands the VST3 its own state back at every playback start
  (`getState`, `setState`, the controller's `setComponentState`, then
  `setActive`). Since PLUG-48 both halves compute a digest of the state;
  until VST3-20 that encoded the whole embedded filter again (jrp measured
  that digest alone at a median 12.9 ms for a 30 s stereo filter and
  52.0 ms for 120 s on an M5 Max, release build, 23 November 2026). Since
  VST3-20 each half hashes the bytes it read, eight at a time. That
  sequence through the production processor and controller, a Linux
  release build on a 4-core cloud machine (VST3-20's thread, 24 November
  2026, 48 kHz stereo, the middle of three runs' medians of 7, not
  committed):

  | Filter | Build | `getState` | `setState` | `setComponentState` | `setActive` | Total |
  | --- | --- | --- | --- | --- | --- | --- |
  | 30 s | 0.18.14 | 21.3 ms | 33.6 ms | 23.0 ms | 43.7 ms | 121 ms |
  | 30 s | VST3-20 | 18.5 ms | 16.6 ms | 8.0 ms | 42.3 ms | 84 ms |
  | 120 s | 0.18.14 | 105.3 ms | 209.2 ms | 187.7 ms | 204.7 ms | 708 ms |
  | 120 s | VST3-20 | 110.5 ms | 130.5 ms | 91.1 ms | 206.5 ms | 538 ms |

  VST3-18's thread measured 0.18.13 the same way (23 November 2026): 134 ms
  and 703 ms in all, the digests alone 22.2 ms and 110.6 ms. Restarting
  the processor in `setActive` is now the largest part; the remaining
  question changed from choosing a cheaper digest (VST3-20 did) to whether
  the delay is still noticeable.
  **Remains:** time the user's action itself, pressing Play in Audacity on
  jrp's Mac with 30 s and 120 s stereo filters, against the same with a
  short filter. **Done when** that delay is recorded and, if it is still
  noticeable, the larger parts (`setActive`, `getState`) are looked at,
  with `vst3.adapter` `refused_state_keeps_parameters`,
  `edits_survive_late_answer` and `edits_before_controller_first_restore`
  still passing.
- [ ] **PERF-16 — Loading stages timed inside the plug-in.** Split a load's
  hold into its stages outside the audio path, so a failed run names the
  stage, and report the time from choosing a filter until it is fully
  heard (preparation plus the crossfade) beside the host-thread limits.
- [ ] **PERF-17 — Platform audio scheduling in the measurement host.**
  Audio workgroups on macOS and MMCSS on Windows, in the test host only.
- [ ] **PERF-18 — Performance regressions measured apart from CI.**
  Interleaved base and head runs on one machine, with every result kept.
- [ ] **PERF-7 — One input check on the plug-in audio path.** A private
  planar entry point for `HostProcessor` that drops the second scan, two
  transposes and the `try`, with bit-identical output.
- [ ] **PERF-19 — Copies of whole configurations on the control side.**
  Remove a copy only where a profiler shows it in preparation time or peak
  memory.
- [ ] **PERF-8 — Split spectra straight from the FFT.** Skip the
  interleave and deinterleave kernels where the backend works in split form,
  within the divergence gates.

### Kernel rounding

Guidance, not an item (jrp, 5 November 2026): when
[`kernels.inc`](portable/kernels.inc) next changes, try whether ordinary
compiler arithmetic can replace its special rounding while staying within
the oracle and `native.kernel_divergence` bounds and the performance
targets. The AVX2 kernels stay; routing, gain, delay and scaling stay exact;
nothing changes before then. The reasoning is in the
[archived text](docs/archive/todo/2026-11-20-open-item-history.md#kernel-rounding).

### Loading responsiveness

The first preview's loading gate, met by REL-6 for the 0.12.5 source; how it
was set and the measurements behind it are in the
[archived text](docs/archive/todo/2026-11-20-open-item-history.md#loading-responsiveness),
the test in [load timing](docs/develop/load-timing.md) and the procedure in
[responsiveness qualification](docs/develop/releasing.md#responsiveness-qualification).

## Edges over other convolvers

jrp's goal: be better than the leading reverbs that play impulse responses
and the room-correction convolvers. The
[edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md)
holds the comparison, ranking, rationale and measurements. Each item starts
only after the plan's
[gate](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#before-an-item-starts)
and the owner's decision its row names, and keeps its
[non-negotiables](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#non-negotiables).

- [ ] **EDGE-1 — A filter for every sample rate in one plug-in state.** For
  a player that runs the plug-in at each track's own rate, which no offline
  conversion can serve; first in the edges since 23 November 2026.
  **Done when** a list's entries that fit are saved in the state, the
  host's rate plays and a rate change crossfades, the window and Copy
  Diagnostics name the entry that plays, other rates refused as now; needs jrp's decision on the state
  format. Effort M.
- [ ] **EDGE-2 — Show what a filter does.** **Done when** `--inspect --json`
  and the library report each path's magnitude, phase, group delay and
  decay, checked independently, and the macOS window plots them. Effort M.
- [ ] **EDGE-4 — Compare two filters, level-matched.** Suggested: prepare
  the second filter only when a comparison starts, so everyday memory stays
  as it is (a 30 s true-stereo filter took 76 MiB playing and 165 MiB while
  being replaced). **Done when** two filters are held and switched with the
  crossfade at matched loudness, within the saved-state limit if both are
  saved; needs jrp's decision on state and the
  loudness measure. Effort M.
- [ ] **EDGE-5 — Publish the workloads and the evidence.** **Done when**
  the guides state PERF-3's workloads beside other convolvers and a
  `docs/use/` page shows a `--compare` null test, with jrp's approval of the
  claims; after PERF-3. Effort S.
- [ ] **EDGE-6 — A reverb's pre-delay, start, length and fades.** **Done
  when** they shape the filter as it loads, match an independent
  computation and leave the output unchanged at their defaults; needs jrp's
  decision on parameter IDs. Effort M.
- [ ] **EDGE-7 — Decay and damping.** Per-band decay reshaping, only after
  EDGE-6 is in use and jrp decides. Effort L.
- [ ] **EDGE-8 — Immersive layouts beyond 8 channels.** **Done when**
  7.1.4, 9.1.6 or up to 16 × 16 load in the plug-ins with existing
  identities and sessions kept. Effort M to L.
- [ ] **EDGE-9 — Delays finer than a sample.** **Done when** an opt-in
  fractional delay is baked into the filter, its cost, bandwidth and error
  stated and tested independently; `.cfg` delays stay whole samples.
  Effort S to M.
- [ ] **EDGE-10 — Sound-aware and platform-aware optimisation.** A
  [catalogue](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#sound-aware-and-platform-aware-optimisation),
  investigated one candidate at a time when a measured limitation points at
  it; PERF-3's [measurements](docs/develop/fft-measurements.md#supported-workloads)
  found none in the supported workloads, only a full 7.1 matrix at 96 kHz
  beyond them. **Done when** no workload users need is out of reach, or
  each one tried is adopted as its own item or stopped with its numbers.
- [ ] **EDGE-11 — Savings in today's engine.** Idling after the tail is in
  (PR #209). **Remains:** the plan's
  [other candidates](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#2-savings-in-todays-engine),
  each only when a workload users need falls short (PERF-3 found only a full
  7.1 matrix at 96 kHz, which the shared-mix candidates target), and Apple
  silicon measurements. **Done when** no workload users need falls short,
  or each one tried is kept or stopped with its numbers, the oracle tests
  passing.
- [ ] **EDGE-12 — Filters larger than the saved-state limit.** **Done
  when** a plug-in saves and restores a filter over 64 MiB through the
  storage jrp chooses, and refuses clearly when it is missing or changed.
  Effort M.
- [ ] **EDGE-13 — Report a filter's own delay, as an option.** **Done
  when** an opt-in delay, given or verified across paths, is added to the
  reported latency with the dry signal delayed to match, tested as the
  [plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#5-report-a-filters-own-delay)
  proposes;
  needs jrp's decision. Effort S to M.
- [ ] **EDGE-14 — Match the loudness of a reverb response, as an option.**
  **Done when** an option, off by default, applies one fixed gain computed
  at load, the same at every rate, and a `.cfg` always plays as designed
  ([plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#automatic-gain));
  needs jrp's decision on its ID and saving. Effort S to M.
- [ ] **EDGE-15 — Optional output safety ceiling.** **Done when** an
  opt-in ceiling lets no sample above it leave, output below it is
  bit-identical and nothing new allocates or locks; needs jrp's decisions
  on the action, per output or global, and saving. Waits for REL-3.
  Effort S to M.

## Developer tooling

- [ ] **CI-32 — Run the most extreme numerical cases less often.** Dormant:
  moving `native.kernel_divergence`'s largest cases would shorten no job
  today. **Worth doing when** one of them sets a job's run time. **Done
  when** they run less often with their evidence kept and the everyday gates
  unchanged.

## Documented limitations

Constraints the guides state, some deliberate. Each guide linked owns the
detail; change one only for an explicit need, with compatibility tests.

| Limitation | Open item | Owning page |
| --- | --- | --- |
| A preset from `convolver-preset`, or state moved between formats, carries the filter, not the settings | None | [Settings](docs/use/plugins.md#settings) |
| Changing the Audio Unit's or LV2's Latency while playing leaves a gap and cuts a tail | None | [Latency and tail](docs/use/plugins.md#latency-and-tail) |
| Closing LV2 in a JUCE-based host can still let queued work reach the freed instance | Upstream (JUCE) | [Format notes](docs/develop/plugin-contract.md#format-notes) |
| A host rate that is not a whole number of hertz plays silence or is refused | None | [When a filter does not fit](docs/use/plugins.md#when-a-filter-does-not-fit) |
| A WAV with unfilled sizes is read to the end of the file, so one left by an interrupted write reads as complete; one of 4 GiB or more is refused | None | [Input files](docs/use/command-line.md#input-files) |
| No IR resampling or automatic channel adaptation | IO-2 | [Configuration](docs/use/configuration.md) |
| One VST3 input and output bus of 1–8 channels; no window on Linux | VST3-13, VST3-2 | [Loading a filter](docs/use/plugins.md#loading-a-filter) |
| No migration of old VST2 project state | VST3-6 | [Identities](docs/use/plugins.md#identities) |
| No device playback or clock management | None | [Concepts](docs/use/concepts.md#files-and-continuous-streams) |
| One FFT backend per binary; only FFTW has measured plans and wisdom | FFT-2 | [FFT guide](docs/develop/fft-backends.md#selection-and-contract) |
| FFTW wisdom is CPU-, build- and size-sensitive and process-global | None | [Planning](docs/use/command-line.md#fftw-plan-tuning) |
| Core processing takes interleaved float buffers that must not overlap | None | [Engine API](docs/develop/library.md#engine-api) |
| Latency and filter replacement are not instantaneous | None | [Replacement](docs/develop/plugin-contract.md#replacement-and-threads) |
| Callback safety depends on the thread contract | ROBUST-1 | [Invariants](docs/develop/architecture.md#invariants-to-preserve) |
| Resource limits cap channels, paths, delays, text, IRs and state | EDGE-12 | [Core limits](docs/develop/library.md#resource-limits) |
| The configuration format is not identical across consumers | CFG-1 | [What Convolver accepts](docs/use/other-programs.md#what-convolver-accepts) |
| Path portability checks are partial | None | [Check that a configuration will travel](docs/use/other-programs.md#check-that-a-configuration-will-travel) |
| Output formats are limited; speaker labels in WAV and RF64 only; no FLAC or Ogg in the packages | IO-1 | [Output formats](docs/use/command-line.md#output-formats) |
| Output writes are not transactional | None | [Output safety](docs/use/command-line.md#output-safety) |
| Gain estimation is not a limiter | EDGE-15 | [Gain](docs/use/command-line.md#gain-and-attenuation) |
| Dither, shaping and valid bits apply to integer file output only | None | [Integer output](docs/use/command-line.md#integer-formats-dither-and-noise-shaping) |
| C++ packages are static development libraries without a stable ABI | REL-1 | [Package guide](docs/develop/library.md) |
| Source tags and configured CI do not prove a release is qualified | None | [Versioning](docs/develop/versioning.md) |
| Subnormal numbers are flushed to zero while processing on x86-64 and AArch64 | None | [Subnormal numbers](docs/develop/architecture.md#subnormal-numbers) |
| Finite tests do not prove all-input correctness or hard real time | ROBUST-1 | [Validation limits](docs/develop/testing.md#validation-limits-and-release-work) |
| Equalizer APO 1.4.2 drops up to three bins near half the rate for blocks not a multiple of four | Upstream | [Equalizer APO tests](docs/develop/testing.md#equalizer-apo-export-tests) |
| The historical Windows applications are not built or host-tested | None | [Historical coverage](docs/develop/testing.md#historical-coverage) |

## Done

Finished items move out of this file so that it holds open work only. Each
keeps one line, with its ID, title, date, pull request and the page that now
describes it, in the [archive of finished items](docs/archive/todo/README.md):
everything finished up to 20 November 2026 is in
[that day's list](docs/archive/todo/2026-11-20-done-items.md). The
[repository checks](docs/develop/workflow.md#automated-checks) treat an ID
listed there as defined and done. What each item delivered is described on
its linked page and in the [CHANGELOG](CHANGELOG.md).

Items finished since then are listed here, newest first, one line each, until the next
move to the archive. Each line arrives in the **Done** section of its pull
request's [change fragment](changes/README.md), and the lines of items
finished since the last fold are still there
([change fragments](docs/develop/workflow.md#change-fragments)).

- [x] **VST3-29 — Keep host edits with their connection, and restores made
  off the UI thread out of the controller's way.** 25 November 2026, from
  jrp's review of `main` since `v0.18.16`
  ([adapter contract](docs/develop/plugin-contract.md#settings)).
- [x] **DIAG-11 — Word a declined channel layout as an offer.** 25 November
  2026 (jrp asked for it after the HOST-12 soak's log).
- [x] **HOST-12 — A long REAPER soak with reverb returns.** 25 November
  2026, PRs #288 and #294; jrp's run on the 0.18.23 candidate: no new
  underrun ([record](docs/archive/reviews/2026-11-25-host12-reaper-soak.md)).
- [x] **DOC-26 — Bring the host evidence on the status page up to date.**
  25 November 2026 ([status](docs/project/status.md)).
- [x] **VST3-28 — Keep a pending VST3 choice with the connection it was
  made on.** 25 November 2026, from jrp's review of `777299f3`
  ([adapter contract](docs/develop/plugin-contract.md#settings)).
- [x] **EDGE-3 — Reverb file layouts without a configuration.** 25 November
  2026, PR #292 ([configuration](docs/use/configuration.md#mono-and-true-stereo-ir-files)).
- [x] **PERF-21 — An everyday load budget.** 25 November 2026, PR #284
  ([the everyday budget](docs/develop/fft-measurements.md#the-everyday-budget)).
- [x] **DOC-25 — Correct and tighten the README and user guides.**
  25 November 2026, PR #291 ([configuration](docs/use/configuration.md#reverb-responses)).
- [x] **VST3-27 — Pair the VST3 controller only through its current
  connection.** 25 November 2026, from jrp's review of `33971caf`
  ([adapter contract](docs/develop/plugin-contract.md#status-and-log)).
- [x] **FFT-5 — Estimate's overflow decided the same in every build.**
  25 November 2026, PR #286
  ([extreme levels and the FFTW plan](docs/develop/fft-measurements.md#extreme-levels-and-the-fftw-plan)).
- [x] **VST3-26 — The VST3 controller calls its processor directly.** 24
  November 2026, from jrp's step back after VST3-18 and VST3-19
  ([adapter contract](docs/develop/plugin-contract.md#settings)).
- [x] **VST3-25 — Generated sequences for the two VST3 halves.** 24 November
  2026, from jrp's review of the shared-processor proposal
  ([VST3 tests](docs/develop/testing.md#vst3-tests)).
- [x] **VST3-19 — Let an accepted preset win over a Latency or Gain mode
  chosen before it.** 24 November 2026, PR #279, jrp's review of 0.18.14
  ([adapter contract](docs/develop/plugin-contract.md)).
- [x] **VST3-22 — A damaged report leaves Status as it was.** 24 November
  2026, jrp's review of 0.18.14
  ([adapter contract](docs/develop/plugin-contract.md)).
- [x] **VST3-21 — Windows Gain note in the notes' small type.** 24 November
  2026, jrp's review of 0.18.14
  ([settings](docs/use/plugins.md#settings)).
- [x] **VST3-20 — Restore digest without a second copy of the filter.**
  24 November 2026, jrp's review of 0.18.14
  ([adapter contract](docs/develop/plugin-contract.md)).
- [x] **VST3-18 — Keep settings changed after a refused preset when its
  answer arrives late.** 23 November 2026, PR #278, jrp's review of 0.18.12
  ([adapter contract](docs/develop/plugin-contract.md)).
- [x] **CI-53 — Later pushes carry a passed result forward.** 21 November
  2026, PR #264 ([drafts and later pushes](docs/develop/testing.md#drafts-and-later-pushes)).
- [x] **CI-54 — Drafts run Lint and the Linux jobs.** 21 November 2026,
  PR #264 ([drafts and later pushes](docs/develop/testing.md#drafts-and-later-pushes)).
- [x] **DOC-21 — Fewer pushes and review rounds for simple changes.**
  21 November 2026, PR #264 ([while working](docs/develop/workflow.md#while-working),
  [which changes need a review](docs/develop/workflow.md#which-changes-need-one)).
- [x] **DOC-24 — What to expect, for each use.** 23 November 2026, PR #277
  ([README](README.md#what-to-expect)).
- [x] **DOC-23 — Record performance then and now.** 22 November 2026,
  PR #270 ([measurements](docs/develop/fft-measurements.md#performance-then-and-now)).
- [x] **PLUG-50 — Gain mode in the Gain row; one Choose Filter menu.**
  22 November 2026, PR #273 ([Convolver's
  window](docs/use/plugins.md#convolvers-window)).
- [x] **PLUG-49 — Keep a waiting configuration list when Audacity starts
  playback.** 22 November 2026, PR #271, jrp's live session on 0.18.10
  ([loading a filter](docs/use/plugins.md#loading-a-filter)).
- [x] **PLUG-48 — Keep the shown settings when a restored preset is
  refused.** 22 November 2026, PR #271, jrp's live session on 0.18.10
  ([adapter contract](docs/develop/plugin-contract.md)).
- [x] **HOST-11 — One command for the REAPER checks.** 22 November 2026,
  PRs #245, #251, #255 and #274; jrp's run on 0.18.10 built from the
  published tag ([REAPER from one command](docs/develop/host-testing.md#reaper-from-one-command)).
- [x] **PLUG-47 — Add a list read again by itself to Recent.** 21 November
  2026, PR #269, jrp's review of the 0.18 candidate
  ([loading a filter](docs/use/plugins.md#loading-a-filter)).
- [x] **PLUG-46 — Report a stopped plug-in's tail at the filter's own
  rate.** 21 November 2026, PR #269, jrp's review of the 0.18 candidate
  ([adapter contract](docs/develop/plugin-contract.md)).
- [x] **AU-19 — Show an uninitialized Audio Unit's Latency as selected for
  the next start.** 21 November 2026, PR #269, jrp's review of the 0.18
  candidate ([Status](docs/use/plugins.md#status-and-the-convolver-log)).
- [x] **PLUG-44 — Say each fact once in the window and host views.** 21
  November 2026, PR #267, jrp's choice of items 1 to 12 from the
  interface review of the same day
  ([window](docs/use/plugins.md#convolvers-window)).
- [x] **DOC-22 — Host-testing steps that match the 0.18 defaults.**
  21 November 2026, PR #268
  ([host testing](docs/develop/host-testing.md#prepare-the-mac)).
- [x] **REL-10 — Make the example echo clearly audible.** 21 November 2026,
  PR #266 ([plug-in examples](examples/plugin-presets/README.md)).
- [x] **CI-52 — Time limits for the fuzz and version jobs.** 21 November
  2026, PR #263
  ([fuzz smoke tests](docs/develop/testing.md#fuzz-smoke-tests-and-longer-campaigns)).
- [x] **PLUG-42 — Keep a filter refused only for the host's channels or
  rate.** 21 November 2026, PR #258, jrp's decision of the same day (found
  in Ardour 8.4 by HOST-10): VST3 and the Audio Unit refuse a filter that
  does not suit the host only while one that suits it plays; otherwise
  they keep it, save it as the filter and play it at the next start that
  suits it ([contract](docs/develop/plugin-contract.md#loading-and-failure)).
- [x] **DOC-20 — Shared records without merge conflicts.** 21 November
  2026, PR #257
  ([change fragments](docs/develop/workflow.md#change-fragments)).
- [x] **DIAG-10 — Status says what the window says.** 21 November 2026,
  PR #261 ([plug-ins](docs/use/plugins.md#status-and-the-convolver-log),
  [decision](docs/project/decisions.md#diagnostics)).
- [x] **PLUG-24 — `HostProcessor` in its own source file.** 21 November
  2026, PR #260 ([architecture](docs/develop/architecture.md#components-and-source-of-truth)).
- [x] **CI-51 — One Audio Unit job instead of three.** 21 November 2026,
  PR #259 ([CI triggers](docs/develop/testing.md#ci-triggers)).
- [x] **CI-50 — The VST3 lifecycle test's time limit matches the other
  formats'.** 21 November 2026, PR #259
  ([lifecycle sequences](docs/develop/testing.md#lifecycle-sequences-plug-7)).
- [x] **CI-49 — Advisory upstream lilv off pull requests; Monday runs
  staggered.** 21 November 2026, PR #259
  ([pre-releases](docs/develop/build.md#pre-releases-and-newer-upstream-versions)).
- [x] **PLUG-39 — One consolidation of the load and restore path.** 21
  November 2026, PRs #200, #205, #213, #217, #219, #224, #226, #230 to
  #232, #236, #246, #250 and #256 ([plan](docs/archive/plans/2026-11-13-load-restore-consolidation.md),
  [adapter contract](docs/develop/plugin-contract.md#replacement-and-threads)).
- [x] **HOST-10 — Ardour's VST3 channels and settings.** 21 November 2026,
  PR #247: on Ardour's default Strict-I/O tracks mono and stereo play; a
  crossover needs the LV2 on a Flexible-I/O track, where the VST3 gets 2
  in, 2 out (a mono filter plays once its outputs are set by hand); the
  VST3's Gain mode and Latency are hidden
  ([plug-ins](docs/use/plugins.md#ardour)).
- [x] **VST3-17 — Audacity keeps a filter chosen in the window.** 21
  November 2026, PRs #177, #233 and #252: jrp's Audacity pass on 0.16.14
  ([in Audacity](docs/use/first-sound-in-a-host.md#in-audacity),
  [record](docs/archive/reviews/2026-11-21-vst3-17-audacity-pass.md));
  the drafted upstream issue stays in the project files for jrp.
- [x] **PLUG-40 — Keep a refused list's advice current.** 21 November
  2026, PR #252: a list refused before the host's settings were known is
  read again by itself once the window sees the host playing, and says the
  settings are known meanwhile
  ([plug-ins](docs/use/plugins.md#loading-a-filter)).
- [x] **PLUG-41 — A chosen file waits for audio in this start.** 21
  November 2026, PR #252, from jrp's review of the same day: VST3 no longer
  refuses a file chosen after a restart from the earlier start's audio, and
  an Audio Unit render of zero frames no longer counts as playing
  ([contract](docs/develop/plugin-contract.md#loading-and-failure)).
- [x] **PERF-3 — One measurement exercise: budgets, supported workloads and
  published evidence.** 21 November 2026, PR #248
  ([supported workloads](docs/develop/fft-measurements.md#supported-workloads),
  [limits](docs/use/limits.md#filter-length-channels-and-processor-load)).
- [x] **PLUG-30 — One rule for state a busy lock makes a call skip.**
  21 November 2026, PR #236, with PLUG-39 step 4a
  ([calls that do not wait for the control lock](docs/develop/plugin-contract.md#calls-that-do-not-wait-for-the-control-lock)).
- [x] **CI-48 — No GCC 12 warnings from our sources in the Linux package.** 21
  November 2026, PR #244
  ([compilers and CI machines](docs/develop/build.md#compilers-and-ci-machines)).
- [x] **CI-47 — Every plug-in target builds with the same warnings.** 21
  November 2026, PR #243
  ([compilers and CI machines](docs/develop/build.md#compilers-and-ci-machines)).
- [x] **AU-18 — Render the Audio Unit in the hosts' orders of events.** 21
  November 2026, PR #242 ([AVAudioEngine render](docs/develop/testing.md#avaudioengine-render)).
- [x] **HOST-9 — Play the VST3 and a saved Latency in the Ardour test.** 21
  November 2026, PR #237 ([Ardour, headless](docs/develop/testing.md#ardour-headless)).
- [x] **PLUG-27 — Read a chosen filter before the host's audio settings
  are known.** 21 November 2026, PR #233: a chosen file waits ("Filter
  selected. Waiting for the host's audio settings.") until the host plays
  ([plug-ins](docs/use/plugins.md#loading-a-filter)); checked in Audacity
  on 0.16.14 ([record](docs/archive/reviews/2026-11-21-vst3-17-audacity-pass.md)).
- [x] **CI-46 — The VST3 plug-in builds with the others' warnings.** 20
  November 2026, PR #235
  ([compilers and CI machines](docs/develop/build.md#compilers-and-ci-machines)).
- [x] **DOC-19 — Shorter project records.** 21 November 2026, PRs #238 to
  #241 ([recording progress](docs/develop/workflow.md#recording-progress-in-todo)).
