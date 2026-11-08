<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver TODO

Open work on the active portable implementation, with a stable ID for each
item. This is the shared backlog, not a promise that every idea will be
implemented. Finished items are listed briefly under [done](#done); what they
delivered is described in the guides and the [CHANGELOG](CHANGELOG.md).

**In short:** the next milestone is end-to-end testing: an independent user
running an exact preview package from the 0.12 series. The
[order of work](#execution-priorities) gets there in four steps: finish the
open fixes (done), run jrp's host smoke tests (HOST-1), finish the package
and the user journeys (PLUG-5, REL-2; the journeys are written, and the CLI,
the journey presets, the Audio Unit and the VST trademark notice are in the
package), then the independent
walkthrough (REL-3).
[Status](docs/project/status.md) shows what users can run now and what still
blocks the preview, and [decisions](docs/project/decisions.md) what jrp has
decided. Other open themes are the later LV2 and AU previews, diagnostics
(DIAG), documentation checks (DOC), files kept from releases (COMPAT), other configuration readers (CFG),
large files and resampling (IO), no-clip headroom (GAIN), stress testing
(ROBUST), a parked FFmpeg filter (FFMPEG), FFT or performance work only
where measurements justify it (FFT, PERF), and the edges over the leading
convolution plug-ins to build after the first walkthrough (EDGE). The
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

**Next milestone** (jrp's [guidance](docs/review-2026-10/release-readiness-guidance-2026-10-22.md),
22 October 2026): an independent user installs and uses an exact preview
package, following only the documentation. This is the end-to-end test. "Ready" means the
REL-2 journeys work, not that a set of items is ticked. The first preview is
cut from the 0.12 series and is macOS-only
([decided](docs/project/decisions.md#plug-in-behaviour)): the VST3 plug-in and the
Audio Unit with the existing shared window and their Gain, Mix and Latency
settings, the `convolver` CLI and the preset tool. The Windows and Linux
windows, the other gain modes and the meters, and PERF-3's wider workloads
come in later previews; LV2 joins a later preview once its own journeys are
demonstrated. What still blocks it, with the evidence, is in
[status](docs/project/status.md#what-still-blocks-the-first-preview).
The [bounded qualification plan](docs/project/plans/2026-11-08-preview-acceptance.md)
defines acceptance, retesting and deferred work (REL-7).
Finishing a dependable product matters more than another substantial
refactor: move code into shared layers only when two formats need the same
behaviour (as with `HostFilter`), do not generalise host lifecycle or release
machinery beyond the requirements in hand, and keep libsndfile at the I/O
boundary and C++20 in the core.

| Order | Work | Exit criterion / dependency |
| --- | --- | --- |
| 1. Finish the open fixes | **DIAG-1** fault state after reloads and Load failed before Fault ([PR #73](https://github.com/jrp2014/convolver/pull/73)); from jrp's review of `6920a45` (31 October 2026), **PLUG-12** (a filter that loads but cannot be saved) and **ROBUST-3** (silence after a failed first load that cannot run out of memory); from his review of `276c7dc` (1 November 2026), **PLUG-13** (room for the settings in saved state, [PR #105](https://github.com/jrp2014/convolver/pull/105)); from jrp's HOST-1 REAPER run (6 November 2026), **VST3-16** (a new VST3 Latency comes into use in REAPER) | DIAG-1 done: merged on 25 October 2026 (`7ed7e38`), and the "Plug-in distribution candidates" run on that commit passed ([run](https://github.com/jrp2014/convolver/actions/runs/40145929436)). PLUG-12 and ROBUST-3 done: merged on 31 October 2026 ([PR #91](https://github.com/jrp2014/convolver/pull/91), `7542576`), each with a test of the sequence that failed. VST3-16 open: its fix is in draft [PR #134](https://github.com/jrp2014/convolver/pull/134). |
| 2. Host smoke tests | **HOST-1** (REAPER and Audacity, VST3), on jrp's Mac, from the latest candidate; **AU-4** (GarageBand, the Audio Unit from the same candidate, which holds it since PR #115) at the same sitting | Each [smoke test](docs/develop/host-testing.md#smoke-test-in-each-first-host) recorded with the package SHA-256, host and macOS versions, and every finding fixed or listed. HOST-1 and AU-4 both gate the first preview, which holds the Audio Unit too. Needs no CLI in the package, so it can start now that step 1 is merged. Its notes (where each host shows the Status parameter and presets) feed REL-2. |
| 3. Alongside step 2 | **PLUG-5** first-preview contents: in the candidates since PR #115 (the Audio Unit on macOS, the VST trademark notice; the `convolver` CLI and the first-use presets since PR #101); what remains is qualifying one exact candidate (HOST-1, AU-4, REL-3) and jrp's publication. **REL-2** (done): the stereo, 2→6 crossover and CLI journeys are written in `docs/use/`. **PLUG-28** (done, PR #141): the packaged presets named after their folder, before the first preview | The package holds everything the journeys use, and the release tests run it; each journey step states its expected result and how to check the output. |
| Early walkthrough (macOS) (done) | **REL-5**: an explicitly limited walkthrough of an exact candidate on macOS, the stereo journey only | Done on 1 November 2026 as a package and command-line check only, by jrp's choice ([done](#done)); its host steps moved to HOST-1's REAPER smoke test and REL-3. It did not change the first preview's contents and does not replace REL-3. |
| 4. Settings, windows and the Audio Unit | The **HOST-2** editor as the macOS-only first preview has it: **PLUG-9** (the report and loading from a file) and **PLUG-10** (Gain, Mix and Latency), **VST3-3**, **VST3-4**, **AU-7**, and the window **VST3-11** shared with the Audio Unit in **AU-6**; and the Audio Unit's own path into the package, **AU-3** (validation), **AU-4** (guide and hosts) and **AU-5** (the component in the preview, in the macOS candidates since PR #115). For later previews: **VST3-12** (Windows), **VST3-13** (Linux), and PLUG-9, PLUG-10 and AU-7's remaining gain modes and meters | The [editor plan's phases](docs/project/plans/2026-10-24-plugin-editor.md#phases-and-todo-items) the first preview needs, each merged with its tests; the REL-2 journeys and the host smoke tests include the window. **Acceptance gate:** loading a long filter from a window or a preset, on the [preview workloads](#loading-responsiveness), causes no noticeable pause in the host's interface or audio, measured by automated tests rather than by hand; if it does, background preparation comes first ([loading responsiveness](#loading-responsiveness)). |
| 5. The milestone | A candidate from a 0.12.x tag; **REL-3**, the independent walkthrough of its exact files; fixes for what the tester finds | Recorded walkthrough. jrp then publishes the unsigned preview ([releasing](docs/develop/releasing.md#publishing-an-unsigned-preview)), with **COMPAT-1**'s capture and **REL-4**'s steps after publishing. |
| 6. Later previews | Windows and Linux (**VST3-12**, **VST3-13**), the remaining gain modes and meters (**PLUG-9**, **PLUG-10**, **AU-7**), PERF-3's supported workloads; **LV2-3** to **LV2-5**, with the editor's **LV2-7**, **REL-1** for all formats; **COMPAT-1** at each publication | Each format's REL-2 journeys demonstrated before it joins a preview. Each preview repeats the REL-2 journeys; from the second, the COMPAT-1 upgrade tests too. Developer ID signing, notarization and Authenticode remain the later production gate. |
| Before further substantial DSP changes, and any optimisation | **PERF-3:** resource budgets | Callback tail latency, peak memory and preparation time while loading, replacing and resetting, across realistic routing and callback sizes, define the supported workloads (jrp's review of `6920a45`: keep the core architecture and finish this first). PERF-2, FFT-2 and VST3-5, and any more complex partitioning, start only if PERF-3 shows a need. Not a first-preview gate: the loading-responsiveness gate uses the finite preview workloads instead. |
| Supporting, cheap | **PERF-1**, **HOST-4** | HOST-4's remaining check needs a Windows computer. PERF-1 reruns the room-correction benchmarks on Apple Silicon and Windows. |
| After the milestone: edges over other convolvers | First the limitations users cite: **EDGE-1** with **IO-2**, **PERF-3**'s long-reverb workloads with **EDGE-11**, then **PERF-2** with **EDGE-10**, **EDGE-3** and **EDGE-13**; then **EDGE-12**, **EDGE-9**, **EDGE-2**, **EDGE-4** and **EDGE-5**; then **EDGE-6** to **EDGE-8**, in the ranked order of the [edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#ranked-edges-to-build) | jrp's goal (6 November 2026): be better than the most popular reverb plug-ins. Waits for REL-3 unless jrp lifts the freeze; each item starts with the owner's decision its plan row names, and keeps the plan's [non-negotiables](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#non-negotiables). PERF-2 still waits for PERF-3's budgets. |
| Parked | **FFMPEG-2 to FFMPEG-5**, **HOST-3** | Revisit only by an owner decision. |
| Optional editor work | **PLUG-11** (FFT planning in FFTW-built plug-ins), **LV2-6** (an LV2 window, only if Linux users ask) | Not needed for any preview. |
| On request or measured need | **HOST-2** (CLAP), **VST3-2**, **VST3-5**, **VST3-6**, **FFT-2**, **CFG-2** | Define the user need or measured bottleneck and acceptance tests first. CFG-2 waits for CFG-1. |

The CFG-1, IO-1, IO-2 and ROBUST-1 items
remain open supporting work; take them up when a reproducible defect or the milestone
needs them. This table owns the current ordering; the dated
[21 October review order](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md#recommended-order)
is superseded.

## User experience and distribution

- [ ] **REL-7 — Apply bounded preview acceptance.** Follow the
  [qualification plan](docs/project/plans/2026-11-08-preview-acceptance.md):
  hold one candidate still, complete the existing required host and release
  gates, classify blockers by consequence, and record lower-risk work for
  later. Done when the final candidate meets the stop rule and its evidence
  and deferred limitations are linked from the gate summary; jrp publishes.
  The plan is written; qualification is not claimed complete. CI-34 is
  separate and does not gate the walkthrough.

- [ ] **REL-1 — Qualify downloadable plug-ins.** The candidate workflow
  builds unsigned packages with pinned dependencies, corresponding source,
  staging, installers, artifact verification and promotion gates
  ([releasing](docs/develop/releasing.md)); PLUG-5 extends it to the Audio
  Unit (done for the preview, PR #115) and LV2 (a later preview).
  Still open, for the production gate: verified minimum OS and CPU versions,
  dependency closure, the final licence and corresponding-source review,
  signing and notarization, installation on clean machines, and immutable
  release assets ([production gates](docs/develop/releasing.md#production-gates)).
  The 18 October [distribution plan](docs/archive/plans/2026-10-18-plugin-distribution.md)
  is archived as superseded.
- [ ] **REL-3 — Independent walkthrough of an exact preview.** Someone who
  did not write the code or the guides follows the REL-2 journeys from the
  exact preview files (identified by their SHA-256 sums), on their own
  machine, using only the documentation. The tester uses the chosen
  candidate run's files before they are uploaded, so the pre-release can
  describe what was demonstrated; the
  tester is still to be chosen. Record who (a name or "independent
  tester"), the package hash, OS, host and version, and for each step whether
  it worked, where they hesitated and what they could not do without help.
  Fix the guides or the product for each finding and repeat the affected
  steps. This is the exit criterion of the
  [next milestone](#execution-priorities). jrp chooses the tester. Record
  the evidence HOST-1 lists (package hash, host and OS versions, expected
  outputs, recovery from a refused filter) for each journey.
- [ ] **REL-4 — Make the preview findable.** Room-correction users and the
  `.cfg` format's existing users should find Convolver from search, the old
  SourceForge site, KVR and their forums. Done on the repository side: a README
  line for users of the old Windows Convolver, issue forms, the
  [social preview source](.github/social-preview.html) and the
  [steps after publishing](docs/develop/releasing.md#after-publishing-make-it-findable)
  (PR #64). jrp set the repository description, topics, website and social
  preview, and the SourceForge project summary now points here (24 October
  2026); the old SourceForge website stays as it is. Remaining, at each
  preview: the steps after publishing.
- [ ] **DOC-10 — One short route to load a filter.** From jrp's
  [file-loading review](docs/archive/reviews/2026-11-07-file-loading-review.md)
  (written 7 November 2026, posted 8 November): the guides lead readers into making and
  installing presets, and host preset folders appear on several pages.
  The ordinary instructions should be: choose a filter, check its rate and
  channels, play, save the project; host preset libraries go in a separate
  optional section, with one page owning the inputs each plug-in reads and
  one table of preset folders, linked rather than repeated. Done when
  [plug-in presets](docs/use/presets.md), the plug-ins guide and the
  journeys lead with choosing a filter, one page lists the inputs each
  plug-in reads, and one table gives the preset folders. Progress
  (8 November 2026, PR #140): [plug-in presets](docs/use/presets.md) no longer says
  the plug-ins have no window or that LV2 has no file chooser, the
  plug-ins guide's summary names Choose… beside presets, and the
  Audacity cleanup in [host testing](docs/develop/host-testing.md#prepare-the-mac)
  removes only the files it copied. Follows PLUG-26 for the wording of
  choosing a preset.
- [ ] **HOST-1 — Named host matrix.** Record versions and results for Windows/macOS
  hosts: scan, load, preset/state restore, silence/tails, seek, variable callbacks,
  bypass, filter changes, multiple instances and unequal channel counts. SDK
  validation and pluginval alone do not complete this item. Steinberg's free
  VST3PluginTestHost (Windows/macOS) is a convenient manual check without a DAW;
  the SDK's sample hosts were evaluated and not added to CI
  ([host qualification](docs/develop/host-testing.md#steinbergs-sdk-hosts)).
  First step, before the first preview: jrp's smoke tests in REAPER and
  Audacity on his Mac, from the latest candidate
  ([smoke tests](docs/develop/host-testing.md#smoke-test-in-each-first-host)).
  Progress (6 November 2026): a [48 kHz stereo listening clip](examples/host-audio/README.md)
  and its generator are available for the manual checks. The clip does not
  replace the first-use numerical comparisons or establish a host result.
  The REAPER smoke test also takes over the host steps that REL-5 left out:
  install, render, save and reopen, and presets that do not fit, in
  [check a stereo filter in a host](docs/use/check-a-stereo-filter.md),
  and the 2→6 crossover journey
  ([check a crossover in a host](docs/use/check-a-crossover-in-a-host.md)),
  from the same candidate package: the record names the package, platform,
  host version, routing, the rendered comparison and the save-and-reopen
  result (step 12 of the REAPER smoke test). REL-3 reuses that evidence
  where it applies; any gaps it leaves are recorded separately.
  In each host, also check that the VST3 Status text follows a change
  without side effects: the controller asks the host to reread every
  parameter (`restartComponent(kParamValuesChanged)`, which the SDK meant
  for program changes) whenever the status changes. Not yet run.
  The external review of `91fcf1f` (0.8.13, 5 November 2026) asks that
  HOST-1, AU-4, AU-3/AU-5, PLUG-5, REL-2 and REL-3 come before further
  conveniences, all against one exact candidate, and stay separate from
  automated library tests passing. Each record keeps: the package's SHA-256
  and the candidate run, the host's and the system's versions, the expected
  output each step was compared with (`expected.wav` and the
  `convolver --compare` result), and recovery from a refused filter (a
  preset for another rate or channel count: the status and message shown,
  silence or the previous filter as the contract says, then a fitting
  filter loads, and saving and reopening keep it).
  Progress (6 November 2026, PR #129): jrp ran the REAPER smoke test (REAPER
  7.85, macOS 27.2 arm64, candidate 0.10.9 from `83aa64d`;
  [record](docs/archive/reviews/2026-11-06-host1-reaper.md)). The smoke steps, the stereo journey (render, save and
  reopen, presets that do not fit; every `--compare` a Match) and the
  window's settings passed. Two results are open
  ([unresolved host findings](docs/develop/host-testing.md#unresolved-host-findings)):
  a new VST3 Latency was not used until the project was reopened, and
  REAPER gave the plug-in 6 inputs and 6 outputs, so the 2→6 crossover was
  refused and its render, reopen and refusal steps were not run. Audacity,
  and REAPER's display of the Status parameter, remain.
  Progress (7 November 2026, PR #131): with PLUG-23 the VST3 crossover
  passed part D in REAPER 7.85: render, Mix 0%, bypass, a refused six-input
  preset, the wrong rate, and (after a restore fix in the same PR) saving,
  quitting and reopening, and FX Offline then Online
  ([record](docs/archive/reviews/2026-11-07-reaper-crossover-retest.md)).
  The Latency finding was fixed by VST3-16 (PR #134,
  [acceptance](docs/archive/reviews/2026-11-08-vst3-16-reaper-acceptance.md)).
  Progress (7 November 2026, PR #137): jrp ran the Audacity steps
  (Audacity 4.0.2 arm64, macOS 27.2) with the VST3 plug-in from PR #131's
  candidate (0.10.13, run 42404299284: PR head `c3dbf0d`, built as merge
  `1b0d873`, so with the window from before PLUG-22; the record gives the
  plug-in executable's SHA-256, not the package's;
  [record](docs/archive/reviews/2026-11-07-host1-audacity.md)). Audacity
  validated the plug-in; the export with no filter matched the original
  file, and the seven 48 kHz stereo exports with a filter or bypass matched
  the CLI's output with `convolver --compare` (worst error −123.67 dB
  relative to the output peak): identity, echo, repeat, echo applied from
  the Effect menu, host bypass, echo kept after a refused 44.1 kHz preset and a
  stop and start, and echo after saving, quitting, restarting Audacity and
  reopening. Three findings, now in the guides
  ([Audacity steps](docs/develop/host-testing.md#audacity-vst3)):
  *Preset options › Import* does not read `.vstpreset` (as documented; the
  run loaded presets through prepared Audacity user presets, a test
  fixture; copies in the user's `~/Library/Audio/Presets` folder were not
  how Audacity found them);
  real-time playback ran at Audacity's Default sample rate (44100 Hz)
  while the export ran at 48000 Hz, so a 48 kHz `.cfg` was refused until
  the playback rate was changed (PLUG-25); and Convolver's window shows
  only with *Preset options › Use vendor UI* on. The Status wording it
  found unclear ("checked when the host starts it") is reworded by
  DOC-9, with jrp's proposed "waiting for audio settings from the host". Still to run in Audacity, from a candidate with the current window:
  listening (clicks, preset changes, whether a preset chosen during
  playback is heard at once); a successful **Choose…** of a `.cfg` with
  playback at the filter's rate; the system preset folder route in
  [preparation](docs/develop/host-testing.md#prepare-the-mac) and saving
  an Audacity preset after **Choose…**; a mono track; a Latency change;
  a filter tail longer than the clip's silent ending (every export here
  ended in silence, so tails past the end of a clip are not qualified);
  VoiceOver and the window's layout (screen capture failed). REAPER's
  display of the Status parameter also remains.
  Before the first preview, every REAPER and Audacity step, including
  those that passed on earlier candidates, is run again on the preview
  candidate, so the earlier results are shown to hold for the current
  window and code: in REAPER that repeats the window and settings steps
  (part C) on the window PLUG-22 redesigned.
  Progress (8 November 2026): jrp ran REAPER part C with the 0.12.5
  candidate from `main` (run 42437291088, `4117a6d`, tagged `v0.12.5`;
  [record](docs/archive/reviews/2026-11-08-host1-reaper-window.md)): on the
  redesigned window **Choose…**, Gain, Mix, undo, reset, the details and
  **Copy Diagnostics** worked, and save, quit and reopen kept the filter,
  Gain −6 dB, Mix 70% and Latency 1024; the VST3-16 latency renders lined
  up again. Listening and REAPER's display of the Status parameter were not
  checked. PLUG-31 changes the candidate; jrp decided these results stand
  for the final one, where part C needs only a packaged preset, save and
  reopen, listening and the Status display, with the rest of the REAPER
  and Audacity steps.
- [ ] **HOST-2 — Additional formats and UI.** CLAP only on request: every
  CLAP host found also loads VST3. An editor for all formats at once,
  now requested (progress below). Write adapters against the
  formats' own headers, not a framework.
  Progress (24 October 2026): jrp asked for an editor window, "not least for
  debugging", and that it configure the plug-in as the old Convolver did,
  as inspiration rather than a copy; the [editor plan](docs/project/plans/2026-10-24-plugin-editor.md) proposes native
  views (decided) over shared report code, with gain, latency and mix as host
  parameters, in phases PLUG-9, PLUG-10, PLUG-11, VST3-3, VST3-4, VST3-11 to
  VST3-13, AU-6, AU-7, LV2-6 and LV2-7, with decisions for jrp.
  Progress (25 October 2026): jrp's choices recorded; the first preview waits
  for the settings and the windows (VST3 on macOS, Windows, and Linux in
  VSTGUI as a stop-gap; the Audio Unit, which joins the first preview, with
  the shared macOS window); settings saved in each format's own way, with the
  divergence documented. Since narrowed: the first preview is macOS-only,
  and the Windows and Linux windows come in later previews
  ([decided](docs/project/decisions.md#plug-in-behaviour)).
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
[plug-in formats plan](docs/project/plans/2026-10-21-plugin-formats.md). Each
adapter is written against its format's own headers over `HostProcessor` and
follows the [shared adapter contract](docs/develop/plugin-contract.md). VST3
work continues under HOST-1, PLUG-5 and the VST3 items
[below](#vst3-sdk-features-not-yet-used).

Shared work, done once for all three formats:

- [ ] **PLUG-5 — One release pipeline.** REL-1 for all three formats,
  extending the existing pipeline (`packaging/release-policy.json`,
  `scripts/release/`, `.github/workflows/release.yml`) rather than writing a
  new one. Previews are unsigned on macOS (ad-hoc signature; the install guide
  explains removing the download quarantine) and Windows (SmartScreen warns),
  labelled as unsigned previews, and published by jrp from one candidate run
  ([releasing](docs/develop/releasing.md)). Developer ID signing with
  notarization (shared by VST3 and AU), and Authenticode, are the later
  production gate; the draft signing scripts are in history at `f4519022c`
  (`git show f4519022c:scripts/release/sign-macos.sh` and `sign-windows.ps1`).
  Progress (22 October 2026, PR #38): packages ship `convolver-preset` and
  `convolver-equalizer-apo` with their guides, and `release.stage`,
  `package_smoke.py` and `verify_artifact.py` require and run both.
  Progress (1 November 2026, PR #101): packages ship `bin/convolver` and the
  first-use examples with a preset made from each (stereo and 2→6 included);
  `release.stage`, `package_smoke.py`, `verify_artifact.py` and the debugging
  symbols cover the CLI, and the packaged guide's examples run it.
  Progress (5 November 2026, PR #115): the VST trademark notice in the
  wording jrp approved (® on the first mention of VST, "Convolver supports
  VST® 3." and "VST is a registered trademark of Steinberg Media Technologies
  GmbH."), with Steinberg's unmodified VST Compatible logo, in the README,
  every packaged guide that mentions VST, the VST3 bundle's `NOTICE.txt` and
  the macOS installer, from one source checked in pre-commit
  ([trademarks](packaging/trademarks/README.md)); and the Audio Unit in both
  macOS candidates (AU-5): `Components/Convolver.component`, a third
  installer package for `/Library/Audio/Plug-Ins/Components`, its debugging
  symbols, notices and SDK source, checked by `release.stage`,
  `verify_artifact.py`, `symbols.py` and, after installing the `.pkg` on the
  CI runner, `auval -strict` on the installed component; all of these first
  passed on both Macs in candidates [run 42108659224](https://github.com/jrp2014/convolver/actions/runs/42108659224) (`cdb99d6`)
  ([what a package holds](docs/develop/releasing.md#what-a-package-holds)).
  Still needed for the first preview, all outside the pipeline: HOST-1 and
  AU-4 from one exact candidate, REL-3's walkthrough of its files, and jrp's
  publication. The logo in the window (an About box) is follow-up work,
  PLUG-20, not needed for the preview. Later: LV2 in
  a later preview (LV2-5), and Developer ID signing, notarization and
  Authenticode at the production gate (REL-1).
- [ ] **PLUG-9 — Editor report and loading from a file.** Phase 1 of the
  [editor plan](docs/project/plans/2026-10-24-plugin-editor.md#phases-and-todo-items), needing no window: a
  report value in `Convolver::host` built on the control side (status, full
  failure, notice and fault messages with request numbers, host and filter
  details, latency and tail, FFT backend and kernels, log path and state),
  `report_fields()` for each format's transport and `report_text()` for the
  window and Copy diagnostics, worded like the log; and the preset tool's
  file-to-state step as a shared function, so a load from the window gives
  state byte-identical to `convolver-preset`'s. Table-driven tests per
  failure kind in `native.host`; the contract change in its own pull request.
  What the first preview needs is done; the rest is for a later preview.
  Progress (25 October 2026, PR #84): the report, `report_fields()`,
  `report_text()` and Copy diagnostics' `diagnostics_text()` in
  `portable/host_report.hpp`, checked per state in `native.host_report`, for
  the Audio Unit's first window; the log's start line now gives the same
  mono-track advice. The file-to-state step is shared as
  `portable/filter_file.hpp` (`filter_file_state()`, `read_filter_file()`),
  used by `convolver-preset`, the LV2 plug-in and the Audio Unit's Choose
  Filter; `au.editor` checks that a file loaded in the window saves the
  state the tool writes. Remaining: the settings' computed gains and meters
  as PLUG-10 adds them.
- [ ] **PLUG-10 — Gain, mix and latency settings.** Phase 1 of the
  [editor plan](docs/project/plans/2026-10-24-plugin-editor.md#settings): in `HostProcessor`, an output gain
  with three modes (a set value, the noise estimate and the no-clip bound of
  `estimate_attenuation()`, computed during preparation), a
  latency-aligned wet/dry mix, and a chosen block size (64 to 4096 frames,
  default 256), with 64-sample ramps; output peak, clip count and callback
  load as audio-thread counters; host state version 3 written only for
  non-default settings; each format saves the settings its own way, and
  where they can diverge (presets from `convolver-preset` carry the filter
  only; states moved between formats keep the filter, not the settings) is
  documented in `docs/use/plugins.md` and listed here as a limitation
  ([editor plan](docs/project/plans/2026-10-24-plugin-editor.md#settings)). Defaults leave the sound unchanged.
  Gain, Mix and Latency are in the first preview; the other gain modes and
  the meters are for a later preview. Oracle tests with the worst
  error relative to the peak; the contract change in its own pull request.
  Progress (25 October 2026, PR #80): Gain and Mix in `HostProcessor`, the
  contract's [settings](docs/develop/plugin-contract.md#settings) and the
  divergence in [how the plug-ins behave](docs/use/plugins.md#settings), with
  the Audio Unit's parameters (AU-7). Deferred to keep the first version
  simple (jrp, 25 October 2026): the Estimate and No clipping gain modes
  (IDs 1002 approved), the Latency choice (1004 approved; latency stays 256
  frames) and the meters; the first try at all three is in PR #80's history
  (commit 7f2ce96). Host state version 3, which only VST3 writes, lands with
  VST3-4.
  Progress (31 October 2026, PR #95): the Latency choice in `HostSettings`
  and `HostProcessor`, with oracle tests at each of the seven block sizes,
  and in the Audio Unit (AU-7). A new Latency plays at once, after a gap
  in the sound ([settings](docs/develop/plugin-contract.md#settings)). Remaining: the
  Estimate and No clipping gain modes and the meters.
- [ ] **PLUG-11 — FFT planning in FFTW-built plug-ins (optional).** From the
  [editor plan](docs/project/plans/2026-10-24-plugin-editor.md#settings), as jrp asked:
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
- [ ] **PLUG-26 — Choose a `.vstpreset` in the window.** From jrp's
  [file-loading review](docs/archive/reviews/2026-11-07-file-loading-review.md)
  (posted 8 November 2026, with the Audacity session): the
  window's **Choose…** and drop read IR files, `.cfg` files and
  configuration lists (`read_filter_file()`), but not the self-contained
  `.vstpreset` files the package ships, which only the host's preset menu
  and `convolver-preset` (`presets::read_vst3_preset()`) read. So a user
  has to learn each host's preset importer and folders, and Audacity 4's
  importer does not read `.vstpreset` at all. Let Choose… and drop in the
  macOS window (VST3 and the Audio Unit) also read Convolver's
  `.vstpreset`, through one input reader shared with `convolver-preset`,
  reusing its decoder and identity and size checks; `.cfg` files, nested
  lists and their relative paths work as today. No `.aupreset` or LV2
  reader only for symmetry. Choosing a preset loads its Gain, Mix and
  Latency too, as the host's own preset menu does; a preset made by
  `convolver-preset`, which holds only a filter, leaves the current
  settings as they are ([decided](docs/project/decisions.md#plug-in-behaviour)).
  Done
  when the same file loads from any user folder in VST3 and the Audio Unit
  with no installation, conversion or administrator step, saving and
  reopening reproduces the CLI's output, and another plug-in's preset or a
  damaged file is refused with a message saying what to do. Waits for the
  freeze unless jrp brings it forward.
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
- [ ] **PLUG-31 — Generated VST3 presets carry the filter's name.** From
  jrp's [VST3-16 acceptance](docs/archive/reviews/2026-11-08-vst3-16-reaper-acceptance.md#filter-name-observation)
  (8 November 2026): after a packaged `.vstpreset` loads, the window's
  Filter row says "From the saved project or preset". VST3 already saves
  and restores a file's name for a file chosen in the window (the
  controller state holds the name and the filter's identity, PLUG-22), but
  `convolver-preset` writes no controller state for VST3
  (`portable/preset_main.cpp`), so generated presets have only the
  component chunk. Write the same name and identity there, the file name
  with its extension and without its folder. jrp suggests "Restored filter"
  for a state with no name, and origin wording that uses only what the
  host says; both are user-facing and his to confirm. No change to the
  processor's audio state. Done when a generated preset loaded in a host
  shows its file name, a name whose identity does not match is not shown,
  and the plug-ins guide says so. Waits for the freeze unless jrp brings
  it forward.
  Progress (8 November 2026, PR #148): `convolver-preset` writes the name
  and identity as the `Cont` chunk; the SDK's `PresetFile::loadPreset`
  shows the name in the adapter test (`vst3.preset`), and a converted
  preset keeps it only while the identity matches. jrp chose "Restored
  filter" for a state with no name (8 November 2026), now the heading.
  Remaining: a host check (REAPER with a packaged preset, showing
  `impulse.wav`). jrp wants this in before the preview (8 November 2026).
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
- [ ] **LV2-3 — Layouts and validation.** Plug-in URIs for the PLUG-4 layouts;
  `lv2lint` and pluginval in CI.
  Progress (22 October 2026, PR #43): the eight discrete URIs and `lv2lint` in
  CI came with LV2-1; pluginval and named layouts remain.
  Progress (6 November 2026): pluginval validates each of the eight plug-ins
  at strictness 10 in the LV2 workflow
  ([pluginval](docs/develop/testing.md#pluginval)); named layouts remain.
- [ ] **LV2-4 — User guide and host matrix.** LV2 in `docs/use/plugins.md` and
  `docs/use/install.md` (written in DOC-4), plus a PipeWire configuration; Ardour, REAPER, Carla, Qtractor and PipeWire results.
- [ ] **LV2-5 — Release bundles.** Linux tarball, then distro packages; macOS and
  Windows bundles under PLUG-5.
  Before the first release, jrp decides how `VERSION` maps to
  `lv2:minorVersion` and `lv2:microVersion` (today MINOR and PATCH, set by
  `plugins/lv2/generate_ttl.py`). LV2 treats an odd minor or micro as a
  development version and minor 0 as experimental, has no major, and lilv
  loads the highest (minor, micro) it finds, so 1.0.0 mapped this way would
  lose to an old 0.4.x left in `~/.lv2`. The mapping must keep increasing,
  with minor even and non-zero and micro even for releases. Mapped this way,
  0.12.x builds are minor 12, which LV2 accepts, but every odd patch number
  is a micro version LV2 reads as a development version (0.11.x was minor
  11, which LV2 reads as a development version too; LV2 is not in the first
  preview).
- [ ] **LV2-6 — Editor report and UI (optional).** Only if Linux users show a
  need: generic UIs already offer the `filter` file chooser and the status
  port. The report as `patch:Set` atoms on the notify port, prepared in
  `work()` and written allocation-free by `run()` (new property URIs for jrp
  to name), then an LV2 UI reusing the macOS and Windows views
  ([editor plan](docs/project/plans/2026-10-24-plugin-editor.md#per-format)).
- [ ] **LV2-7 — Settings as control ports.** PLUG-10's gain, gain mode, mix
  and latency as control input ports after the existing ones, the latency
  port following the chosen block size; port symbols for jrp to approve;
  older sessions restored with defaults in Ardour, Carla and PipeWire
  `filter-chain`.

AU (AUv2, macOS: Logic, GarageBand, SoundSource):

- [ ] **AU-3 — Validation.** `auval` and pluginval in macOS CI. `auval -strict`
  runs since PR #44; still to add: pluginval on the component, and a
  ThreadSanitizer build of `au.adapter` (bypass switched while rendering).
  Needed for the first preview, the ThreadSanitizer build included
  ([decisions](docs/project/decisions.md#plug-in-behaviour)).
  Progress (6 November 2026): pluginval validates the installed component
  at strictness 10, editor tests included, in the Audio Unit workflow
  ([pluginval](docs/develop/testing.md#pluginval)); the ThreadSanitizer build
  remains.
- [ ] **AU-4 — User guide and host matrix.** The Audio Unit in
  `docs/use/plugins.md` and `docs/use/install.md` (written in DOC-4); Logic, GarageBand,
  SoundSource, Live, REAPER and FL Studio results, including loading an
  `.aupreset` from a host's preset menu. Progress (23 October 2026, PR #56):
  GarageBand is jrp's first Audio Unit host, with a
  [smoke test](docs/develop/host-testing.md#garageband-audio-unit) at
  44.1 kHz. Progress (25 October 2026, PRs #80 and #82): jrp's first checks
  in GarageBand 10.4 with the unit built from source. On a stereo track
  `repeat-44100-stereo` plays its audible repeat; Gain −12 dB lowers the
  level without a click; Mix changes the dry and wet balance; Gain and Mix
  come back after save, quit and reopen; a stereo preset on a mono track
  plays silence, as the contract says, and GarageBand's generic window shows
  Status with no value (AU-6). These are checks, not host qualification: the
  full smoke test (every step, recorded with the GarageBand and macOS
  versions), its export and preset-replacement steps, and the other hosts
  remain. GarageBand's smoke test, with the checks that passed on earlier
  builds, runs on the preview candidate.
  Needed for the first preview.
  Progress (8 November 2026): jrp ran GarageBand 10.4.14 with the Audio
  Unit from the 0.12.5 candidate (run 42437291088, `4117a6d`;
  [record](docs/archive/reviews/2026-11-08-au4-garageband.md)): the presets,
  **Choose…** of a packaged `filter.cfg`, a refused 48 kHz preset that kept
  the filter, Gain, Mix and Latency kept after save and reopen, and two
  echo exports matching the CLI (worst error −112.99 dB; GarageBand's
  *Export projects at full volume* must be off for a comparison). Not
  checked: listening to the presets and to Gain and Mix, a click when
  bypassing, whether the track stays in time after a Latency change,
  VoiceOver and the Recent list. jrp decided these results stand for the
  final candidate after PLUG-31, where a packaged preset, save and reopen
  and the unchecked steps remain.
- [ ] **AU-5 — Release.** In the first preview. Unsigned preview component under PLUG-5 (ad-hoc
  signed, documented quarantine removal). Developer ID signing and
  notarization follow at the production gate.
  Progress (5 November 2026, PR #115): both macOS candidates hold
  `Components/Convolver.component`, ad-hoc signed after the last step that
  changes it, with `Convolver-au.dSYM`, the AudioUnitSDK notice and source,
  and its own installer package (receipt `io.github.jrp2014.convolver.pkg.au`);
  the install guide covers the Components folder and quarantine removal. CI
  installs the `.pkg` and runs `auval -strict` on the installed component;
  first passed on both Macs in candidates [run 42108659224](https://github.com/jrp2014/convolver/actions/runs/42108659224) (`cdb99d6`), window listed.
  Remaining for the preview: AU-4's GarageBand smoke test from that
  candidate; then the production gate.
- [ ] **AU-6 — Editor window.** Custom properties (IDs from 64000) for the
  PLUG-9 report and a load request, with property-change events and a
  `ClassInfo` change after a load; then a Cocoa view factory
  (`kAudioUnitProperty_CocoaUI`) over the VST3-11 view, with class names that
  carry the version; `auval` lists the view; GarageBand smoke steps, including
  a `.cfg` chosen inside its sandbox ([editor plan](docs/project/plans/2026-10-24-plugin-editor.md#per-format)).
  Needed for the first preview.
  From jrp's first GarageBand run (25 October 2026): Apple's generic window
  shows the Status row with no value, so a stereo preset on a mono track
  played silence with no visible reason. The window must show why the
  plug-in is silent.
  Progress (25 October 2026, PR #84): the smallest useful window, at jrp's
  request in his review of 0.5.5: report and Copy diagnostics properties
  (64000, 64001) with change notifications, and an AppKit view named by
  `kAudioUnitProperty_CocoaUI`, with versioned class names: the status, Gain
  and Mix sliders (GarageBand shows the window instead of its generic
  controls), the full reason and what to change, the details, Copy
  diagnostics and Show log, sized to its text. `au.editor` and `auval`
  check it. jrp's GarageBand run on 25 October (build `a05818c`, before the
  compact layout and the sliders): the window opens from the Plug-ins slot,
  shows Playing on a saved project and Load failed with the stereo-track
  advice on a mono track; Copy diagnostics and Show log work.
  Then, at jrp's request before more testing (the old Convolver chose a
  `.cfg` or sound file directly): **Choose Filter…** and dropping a file on
  the window, through a load property (64002) that reads the file with
  PLUG-9's shared step and installs it as a state restore does, with a
  `ClassInfo` change notification after it, accepted or refused. Sound
  files are read with macOS's AudioToolbox, not libsndfile (jrp, 30 October
  2026), so the component links only macOS (`au.bundle` checks it);
  `native.audio_toolbox` compares the two readers.
  The window still reads the report itself rather than copying reports
  pushed to it (the editor plan's
  [rule 2](docs/project/plans/2026-10-24-plugin-editor.md#threads)), but a
  read no longer waits for a load: while a load holds the control lock it
  returns the last report (the window's next timed read shows the
  result), and `au.editor` checks that with a restore held open on another
  thread. Limitation: a chosen file is
  read and its filter prepared on the host's main thread, so the window
  (and the host) waits for a long filter; the cached report does not help
  there; a worker thread, as LV2 has, is for later.
  The view is now shared with VST3-11 and has the editor plan's
  conveniences, Recent, Reload and reloading when the file changes
  ([PR #103](https://github.com/jrp2014/convolver/pull/103); checked by
  `vst3.editor`, not yet tried in GarageBand). Remaining: these acceptance
  gates (jrp, 30 October 2026),
  each to pass and be recorded, with the GarageBand and macOS versions,
  before AU-6 is done:
  jrp's GarageBand run of `54eac9f` (30 October 2026; GarageBand 10.4.14,
  macOS 27.2, Apple Silicon, Accelerate and AudioToolbox) passed the
  checked items; it also found a refused file's summary reading "0 in,
  0 out" and command-line advice in the window, both fixed in `8177017`.
  GarageBand gave Convolver stereo even on a mono track, so the mono-track
  refusal could not be reproduced there.
  jrp's follow-up run of `d1dc0a2` (31 October 2026; same versions, macOS
  27.2 26B123, 51/51 local tests, auval) passed the refusal summaries and
  advice, repeated refusals, the held Gain slider (checked before and
  after), Mix automation followed in the open window (also while the mouse
  was held elsewhere) and the Filter detail after an explicit save. It
  found that choosing a file alone did not mark the project changed (no
  save prompt; the file was lost on reopening) and that the file dialog
  opened behind GarageBand's windows. jrp's run of `bf7adb6` (31 October
  2026) passed the save prompt after only choosing a file (the host's
  preset name became the file's; *Don't Save* then restored the saved
  filter) and the echo after a refused file (exports before and after
  identical in all 705,600 samples). The file dialog was still behind
  Convolver's window and took no typing. On `eda43e4` it opens above
  Convolver's window, but still took typing only after a click, as
  GarageBand kept the keyboard. jrp's run of `abb2edf` logged the focus
  state (Convolver's window, an `NSServiceViewControllerWindow` in the
  plug-in's process, was key; the separate dialog could not become key)
  and showed that the dialog as a sheet on Convolver's window took typing
  at once and loaded a file. The sheet is the only dialog since; the
  investigation's logging, Option-click switch, activation and window-level
  changes are gone.
  Not checked: VoiceOver; the sheet in a host that runs the plug-in in its
  own process (REAPER, for example), where it attaches to the host's window.
  - [x] Choose Filter in GarageBand: a sound file, and a `.cfg` inside its
    sandbox whose WAV files it read (allowed in that session); a dropped
    `.cfg`.
  - [x] If the sandbox refuses a `.cfg`'s WAV files, the window says
    permission was denied: a `.cfg` in Music naming a WAV in Downloads was
    refused with Core Audio error -54, the explanation said permission was
    denied in a sandboxed host, and the previous filter stayed (`b0f2552`).
  - [x] Save, quit and reopen a project after choosing a file, with the
    original files moved away: the filter and the settings came back.
  - [x] After a save and reopen, the Filter detail says the filter came
    from the saved project (`d1dc0a2`).
  - [x] A refused replacement keeps the previous filter.
  - [x] GarageBand marks the project changed after only choosing a file
    (`bf7adb6`: the file becomes the present preset).
  - [x] The file dialog, a sheet as the only dialog (`b0f2552`), opens
    in front of the host's windows and Convolver's: a plain click takes
    typing and loads a file, Cancel keeps the filter, closing Convolver's
    window from GarageBand's plug-in button while the sheet is open loads
    nothing, and two Convolver instances each keep their own file (exports
    of the two tracks: echoes on the `repeat` one only). Not checked: two
    windows open at once, as GarageBand closed one when the other opened.
  - [x] The echo is kept after a refused file (`bf7adb6`: exported audio
    identical before and after).
  - [x] Mix automation with the window open: the slider followed, also
    while the mouse was held elsewhere; a held Gain slider kept its value
    (`d1dc0a2`; the sliders send no begin and end gestures yet, so
    recording automation by dragging them is untested).
  - [x] Repeated refusals of the same file change nothing else (`d1dc0a2`).
  - [x] The refused-file summary and advice (a mono WAV on a stereo track:
    "the filter has 1 in, 1 out, the plug-in 2 in, 2 out"; a 48 kHz WAV:
    set the host's project or audio device), on `d1dc0a2`.
  - [ ] VoiceOver reads the status, the sliders and the buttons. Tried on
    `b0f2552` but not heard reliably; an accessibility inspection found the
    text, the Gain and Mix values and the three buttons, which is not a
    VoiceOver check.
  - [x] A long explanation (a list of twelve configurations, of which it
    names ten and "… and 2 more") scrolls to its end with the sliders and
    buttons usable; the compact layout comes back after a short one
    (`b0f2552`).
  - [x] The main-thread pause for a long chosen filter, measured on
    `b0f2552` during playback: the report showed after about 1.4 s for a
    30-second stereo filter (1,323,000 taps) and about 1.7 s for a
    120-second one (5,292,000 taps), observation included. Load
    performance work is deferred
    ([decisions](docs/project/decisions.md#plug-in-behaviour)).
  - [ ] The host stays responsive and the audio uninterrupted while a long
    filter loads: not verified. (The file dialog also closes with a
    perceptible delay when the window closes; not measured.)

  Redesign (1 November 2026, at jrp's request, from a mockup he approved):
  status symbol with the explanation under it, a form (Filter with the
  file's name and Choose…, Gain and Mix with typed values and Option-click
  reset, Latency with its time), an Audio section of plain-word facts from
  the shared report (with the new Loudest peak), and the file, software and
  log lines folded away; stack views instead of fixed frames. What it says
  about the filter and audio comes from `host_report`, so VST3-11 to
  VST3-13 can show the same (on
  Linux the status symbol becomes a glyph and the palette is fixed).
  `au.editor` checks it on macOS CI. jrp's GarageBand run (1 November,
  `bbf94c5`, Release): typing a value (also with a decimal comma), the
  Latency row, the fold-out and a refused file passed; Option-click during
  an unfinished edit was undone on leaving the field (fixed in `ddfc64c`,
  with an `au.editor` case; jrp's re-check in GarageBand passed); the window stayed dark with macOS set to Light, as
  GarageBand's own window does. Still to check: PR #98's typed boxes (a box
  sets only text that was changed) in GarageBand, a light rendering (in a
  host with a light window), a manual Mix drag, listening to the Latency
  gap and the kept echo, and VoiceOver reading the Audio facts and typed
  fields.
- [ ] **AU-7 — Settings as Audio Unit parameters.** PLUG-10's gain, gain
  mode, mix and latency as global parameters (IDs beside Status 1001 in
  `plugins/identities.json`), latency indexed and not automatable, with
  latency and tail change notifications ([editor plan](docs/project/plans/2026-10-24-plugin-editor.md#per-format)).
  Gain, Mix and Latency are in the first preview; Gain mode is for a later
  preview.
  Progress (25 October 2026, PR #80): Gain (0) and Mix (1003), saved in
  `ClassInfo` ([settings](docs/use/plugins.md#settings)). Progress
  (31 October 2026, PR #95): Latency (1004), with the latency change
  notification and a menu in the window; a choice made while the unit runs
  plays at once (a new processor handed to Render). Not yet checked in
  GarageBand (AU-4's step 10). Remaining, with PLUG-10's deferred parts:
  Gain mode (1002).

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
  yesterday's files. When jrp publishes a preview, keep a small collection
  made by that exact release, in its own folder with its own runner (for
  example `tests/upgrade/VERSION/`, apart from the `.cfg` corpus in
  `tests/compatibility/`, since the guarantees differ): its example presets in
  every format it ships, a saved REAPER project containing Convolver,
  the input and the expected audio rendered by that release, with the package
  SHA-256 and how each file was made. CI cannot open the host project, so the
  test takes the plug-in state chunk out of it and restores that through each
  adapter; the project itself is kept for reopening by hand in the host. Later
  builds must load every retained file and reproduce the expected audio after
  the reported latency: bit-identical when the FFT backend, block size and
  kernel set (AVX2+FMA or baseline, PERF-4) are unchanged, otherwise within the PLUG-2 oracle tolerance (1e-5 absolute). Never regenerate a retained file; a deliberate
  incompatibility is jrp's decision, recorded in the
  [decisions log](docs/project/decisions.md). The literal
  version-1 and version-2 state bytes in `native.state` stay as the first
  fixtures. Add the capture step to the
  [publication steps](docs/develop/releasing.md#publishing-an-unsigned-preview)
  when this is implemented. The four guarantees it protects are listed in
  [status](docs/project/status.md#compatibility-guarantees).
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
- [ ] **IO-1 — Large files.** Specify and test boundary sizes and pipe behavior
  for output beyond 4 GiB. Input decoder support is not a promise of strict
  validation for every container.
  Progress (23 October 2026, PR #60): RF64, W64, CAF, FLAC and raw output have
  no 4 GiB limit; WAV and AIFF stop with an error at it. Tests at the boundary
  sizes remain.
- [ ] **IO-2 — Optional offline IR resampling.** Only add as an explicit operation
  with tests for gain, delay, length and reported metadata; retain mismatch errors
  by default. Users cite missing rates, and resampled responses that changed
  level or clipped elsewhere; rank 1 of the [edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#1-a-filter-for-every-sample-rate),
  with EDGE-1. jrp decides whether Convolver resamples at all. Compare [libsamplerate](https://libsndfile.github.io/libsamplerate/)
  and other suitable implementations before choosing a dependency.
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
- [ ] **VST3-11 — Editor window on macOS.** An AppKit view behind `IPlugView`
  (`kPlatformTypeNSView`), shared with AU-6: status and full messages,
  active configuration, choose and reload a filter (with drag and drop,
  recent files and reload when the file changes), the settings (gain and its
  mode, mix, latency) with the output peak, clip count and load, copy
  diagnostics, show the log, bypass. Needed for the first preview. Opened in CI by `editorhost` and pluginval's GUI tests, with an
  accessibility test of names, roles and text; smoke steps in REAPER and
  Audacity, with VoiceOver ([editor plan](docs/project/plans/2026-10-24-plugin-editor.md)).
  Progress (1 November 2026, PR #103): the Audio Unit's window, moved to
  `plugins/editor/mac/`, shown through the SDK's `EditorView` for
  `kPlatformTypeNSView`, with settings edited as `beginEdit`, `performEdit`
  and `endEdit` (a drag is one edit), a Bypass checkbox editing the bypass
  parameter, files loaded through VST3-3's message (with the Recent menu,
  Reload and reloading when the file changes, shared with the Audio Unit)
  and the window resizing through `IPlugFrame::resizeView`; checked off
  screen by `vst3.editor` on macOS, and opened by pluginval's editor tests
  on macOS CI ([format notes](docs/develop/plugin-contract.md#format-notes)).
  `editorhost` is not used: it is an interactive program that runs until its
  window is closed, so pluginval's editor tests stand in for it. Remaining:
  the steps in REAPER and Audacity ([host testing](docs/develop/host-testing.md#reaper-vst3))
  and VoiceOver for the first preview; the meters (with PLUG-10) for a later
  preview.
- [ ] **VST3-12 — Editor window on Windows.** The same window as a Win32 view
  (`kPlatformTypeHWND`), with `IPlugViewContentScaleSupport` and keyboard
  navigation; checked with NVDA or Narrator.
  For a later preview: the first preview is macOS-only ([decided](docs/project/decisions.md#plug-in-behaviour)).
- [ ] **VST3-13 — Editor window on Linux.** The window in VSTGUI, for the
  Linux VST3 build only, as a stop-gap, driven by the
  host's `IRunLoop`; VSTGUI (BSD-3-Clause) and the libraries it links added
  to the package checks and licence notices; opened by `editorhost` under
  `xvfb-run`. Not reachable by screen readers, which the guide says. A native
  X11 view can replace it later if Linux users need one
  ([editor plan](docs/project/plans/2026-10-24-plugin-editor.md#framework-choice)).
  For a later preview: the first preview is macOS-only ([decided](docs/project/decisions.md#plug-in-behaviour)).
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
  candidates, not supported backends or automatic fallbacks:

  | Candidate / primary reference | Question to answer |
  | --- | --- |
  | [PFFFT](https://github.com/marton78/pffft) | Does its SIMD implementation improve the target audio workloads after layout conversion? Check supported lengths, alignment and workspace requirements. |
  | [PocketFFT](https://github.com/mreineck/pocketfft) | Does its C++ implementation simplify deployment or improve awkward transform lengths? Confirm preparation and execution allocation behavior. |
  | [KissFFT](https://github.com/mborgerding/kissfft) | Is a small alternative useful on constrained platforms compared with the existing Ooura backend? Measure the whole engine. |
  | [Intel oneMKL DFT](https://www.intel.com/content/www/us/en/docs/onemkl/developer-reference-c/2025-0/fourier-transform-functions.html) | Is there a demonstrated Intel workload benefit worth another platform dependency and redistribution review? |

  Before adopting one: pin a revision, check licence and supported CPUs, document
  transform sizes/scaling/layout, ensure no processing allocation or locks, and
  pass the same direct DFT and convolution oracles. Compare setup, memory,
  p50/p99/max callback times and throughput against all relevant existing backends.
  Keep Convolver's C++20 and platform baseline unless deliberately changed.
- [ ] **PERF-1 — Reproducible current benchmarks.** Rerun workloads on named target
  machines after substantial engine changes; record commit, compiler, dependency
  revisions, plans, block size and numerical error. Keep dated results labelled.
  Plug-in workloads (6- and 8-channel filters, callback-time distributions)
  belong to PERF-3. On the 19 October cloud VM, a
  65536-tap stereo filter at B = 256 took about 0.6 µs per frame (about 3% of
  one core at 48 kHz) and B = 1024 was about 3.5 times cheaper; a 6×6 matrix of
  16384-tap filters (36 paths) took about 2.9 µs per frame (about 14%).
  PERF-4 measured compiler flags on x86-64 Linux only; rerun the
  room-correction cases of `convolver_benchmark` (cases 10 to 18) on Apple
  Silicon (AppleClang, Accelerate) and Windows (MSVC, Ooura) before choosing
  flags for those builds.
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
- [ ] **PERF-3 — Resource budgets and supported workloads.** Measure, on
  named machines, the distribution of callback durations (median, 99th
  percentile, 99.9th percentile and maximum, not only the average), memory in
  use and filter-preparation time (including FFTW planning where used) for
  representative long filters and 6- and 8-channel routing at 48 and 96 kHz,
  at the host block sizes users meet. Include the costly moments: a live
  replacement, while two engines run together, and a reset, whose work grows
  with stored history. Record the results as dated measurements in the
  [FFT guide](docs/develop/fft-backends.md) and turn them into stated supported
  workloads (for example "8 channels of 65536 taps at 96 kHz uses under N% of
  one core and M MB") in the user guides. Only these results decide whether
  partitioning (PERF-2), memory layout or another FFT (FFT-2) deserves work,
  and whether slow preparation needs VST3-5. Include long reverbs ([edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#3-long-reverbs-at-low-cpu-and-latency)):
  10, 30 and 60 s filters, stereo and true stereo (four paths), at 48 and
  96 kHz, with callback maxima while playing, replacing and resetting.
  Distinguish core and command-line benchmarks from supported plug-in
  workloads, and record the encoded saved-state size beside CPU and memory:
  a 60 s, 96 kHz true-stereo filter needs about 88 MiB, over the 64 MiB
  limit, so the command line renders it but the plug-ins refuse it. A
  workload over the limit needs a separate storage decision before plug-in
  qualification (EDGE-12); the same budget applies to multi-rate states (EDGE-1) and
  A/B comparison (EDGE-4). PERF-1 keeps the throughput
  benchmarks reproducible; ROBUST-1 adds stress under repeated changes.
  Progress: `convolver_callback_benchmark` (PR #70) times callbacks at a host
  callback size chosen apart from B, for warmed processing, startup, reset,
  live replacement and configuration loading; its first results are in
  [startup, reset and loading](docs/develop/fft-backends.md#startup-reset-and-filter-loading).
  jrp's review of `6920a45` (31 October 2026) asks for callback tail latency,
  peak memory and preparation time during loading, replacement and reset,
  across realistic routing and callback sizes, before further substantial
  DSP changes; those results decide whether more complex partitioning is
  justified. The external review of `91fcf1f` (0.8.13, 5 November 2026)
  agrees (keep shared forward transforms, prepared buffers and control-side
  retirement; nothing yet calls for another engine or nonuniform
  partitioning) and sets the acceptance: on named machines, realistic 6- and
  8-channel filters, small callbacks, 48 and 96 kHz and repeated
  replacements, measure preparation time, peak memory while decoded
  configurations and several prepared engines coexist, and callback
  percentiles and maxima during replacement and reset; publish the
  supported workloads with those percentiles and maxima, and decide from
  them whether PERF-2, PERF-7 or PERF-8 is worth doing. PERF-14 lets the
  load-timing tests start the replacement and the reset from a full history
  (`--warm`); still to do here: 6- and 8-channel routing, 96 kHz, peak
  memory and enough observations for tail percentiles.
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
October 2026), not an optimisation item. Every adapter except LV2 reads and
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
preview gate. What remains for the gate: the release qualification run on
the intended machine ([REL-6](docs/develop/releasing.md#responsiveness-qualification),
a first-preview gate) and, if the 30 s case
exceeds 250 ms on a supported platform, jrp's choice between background
preparation in VST3 and the Audio Unit (VST3-5) and a higher limit with the
pause documented.
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

- [ ] **EDGE-1 — A filter for every sample rate in one plug-in state.**
  Done when a list's entries that fit the host's channels are kept in the
  saved state, the entry for the host's rate plays and a rate change
  switches with the usual crossfade; a rate no entry has is refused as now;
  states over the saved-state limit are refused with a clear message.
  Needs jrp's decision on the state format and the preset tool's options.
  [Plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#1-a-filter-for-every-sample-rate). Effort M.
- [ ] **EDGE-2 — Show what a filter does.** Done when `--inspect --json`
  and the library report each path's magnitude, phase, group delay and
  energy decay, checked against an independent computation, and the macOS
  window plots them (layout approved by jrp as an addition to the
  [editor plan](docs/project/plans/2026-10-24-plugin-editor.md)). Effort M.
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
- [ ] **EDGE-5 — Publish the workloads and the evidence.** Done when the
  user guides state PERF-3's supported plug-in workloads (CPU, memory and
  saved-state size) beside the open convolvers measured on the same
  machines, and a `docs/use/` page shows a null test with `--compare`
  against a shipped reference render. jrp approves the claims. Effort S
  for the page, M for the comparison.
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
- [ ] **EDGE-10 — Sound-aware and platform-aware optimisation.** Done when
  each [candidate](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#sound-aware-and-platform-aware-optimisation) has
  been benchmarked on PERF-3's long-reverb workloads against the gain the
  plan states for it, and either adopted as its own item or stopped and set
  aside with its numbers. Exact and rounding-only
  changes keep the oracle and divergence bounds; a bounded approximation
  needs jrp's decision on its bound. OpenMP only where it causes no
  cross-platform incompatibility (offline paths, optional, serial build
  byte-identical); `libomp` ships on macOS only for a meaningful gain there,
  measured beside `dispatch_apply`. Explicit NEON or vDSP kernels only with
  a new workload, hardware target or hypothesis, measured against the
  [24 October 2026 vDSP experiment](docs/archive/reviews/2026-10-24-apple-vdsp.md).
  Goes with PERF-2. Effort M for the survey.
- [ ] **EDGE-11 — Savings in today's engine.** Done when each of these has
  been measured on PERF-3's workloads and kept only if it reaches the gain
  the plan states for it, or stopped with its numbers; sharing identical
  spectra and removing unnecessary work come first. Exact, with
  bit-identical output: idling once silent input has outlasted the tail;
  skipping all-zero filter blocks; holding identical responses once;
  reading each input history once for all its paths. Rounding only, within
  `native.kernel_divergence`'s bound: the older filter blocks'
  multiply-adds done in the callbacks between engine blocks when host
  callbacks are smaller than the Latency block; one inverse transform per
  output where outputs are fewer than paths. The oracle tests pass with the
  worst error reported, and nothing new allocates or locks on the audio
  thread.
  [Plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#2-savings-in-todays-engine), rank 2. Effort S to M each.
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

- [ ] **CI-34 — Reduce duplicate CI scheduling, preserving coverage.** A
  separate proposed PR, following the
  [CI plan](docs/project/plans/2026-11-08-ci-scheduling.md). Inventory unique
  coverage before narrowing candidate and fuzz triggers; retain oracle,
  regression, validator, platform and release qualification checks. Done
  when scope tests and affected CI pass, every configuration has a retained
  trigger, and matched runs demonstrate waiting-time savings. No workflow
  changes are part of REL-7.

- [ ] **CI-32 — Run the most extreme numerical cases less often, once they
  cost.** From jrp's simplification review of 5 November 2026: keep
  representative oracle and allocation checks as everyday gates, and run
  the largest and most extreme cases (`native.kernel_divergence`'s 128k to
  512k filters and its noise near 1e-35, where much of the arithmetic was
  subnormal before PERF-15) in scheduled or DSP-focused runs, keeping their evidence. Not
  done yet, because today they cost little. On 5 November 2026 (Linux
  x86-64, Clang 18, a cloud build container), the test took 17.6 s under
  Debug with sanitizers, which already skips the long filters; the
  near-1e-35 case was about 5 s of that. In Release it took 10.5 s for all
  nine workloads, about 9 s of them the long filters. Under
  `ctest --parallel 4` the whole run took 55 s, with `native.fft_planning`
  alone taking 35 s, so moving them would shorten no job. Take
  it up when one of them sets a job's or a contributor's run time. A DSP-focused
  run would then need a `dsp` answer from `scripts/ci_scope.py`.

## Documented limitations

This inventory gathers constraints stated across the guides. Some are deliberate
API or safety policies, not defects. Change them only for an explicit need and
with compatibility tests; linked IDs point to open work above.

| Limitation | Current handling / follow-up | Authoritative detail |
| --- | --- | --- |
| Plug-in settings (gain, mix, latency) in VST3 and the Audio Unit, not yet LV2; a preset from `convolver-preset` carries the filter only, and a state moved between formats keeps the filter, not the settings | The same project can play at another gain, mix or latency in another format; LV2-7 adds the settings to LV2. | [Settings](docs/use/plugins.md#settings) |
| Changing the Audio Unit's Latency during playback leaves a gap (the new latency plus the filter's own delay) and cuts a sounding tail | The new processor starts with no history; a crossfade would need both sizes running at once. Not automatable for this reason (PLUG-10). | [Latency and tail](docs/use/plugins.md#latency-and-tail) |
| A host rate that is not a whole number of hertz (such as 48000 ÷ 1.001) plays silence in VST3 and is refused by the Audio Unit and LV2 | No filter is made for such a rate and nothing is resampled (VST3-9, decided by jrp). The Audio Unit and LV2 could accept it the same way when a host needs it. | [When a filter does not fit](docs/use/plugins.md#when-a-filter-does-not-fit) |
| No IR resampling or automatic channel adaptation for direct sound-file filters | Rate/count mismatches fail; use an explicit configuration. IO-2 covers resampling. | [Configuration](docs/use/configuration.md) |
| One VST3 input bus and one output bus, each 1–8 channels; a window on macOS only | Presets and generic host controls elsewhere; layouts are checked with a notice only; the Windows and Linux windows are VST3-12 and VST3-13, host switching VST3-2. | [VST3](docs/use/plugins.md#loading-a-filter) |
| No promised migration of old VST2 project state | New VST3 identity; define a migration requirement before implementing one (VST3-6). | [VST3](docs/use/plugins.md#identities) |
| No built-in device application or clock management | CLI pipes need a producer/consumer; VST3 relies on its host. Standalone device support requires a separate design. | [Concepts](docs/use/concepts.md#files-and-continuous-streams) |
| One FFT backend per binary; Ooura/Accelerate have no FFTW-style measured plans or wisdom | Intentional deployment choice. Assess alternatives under FFT-2; do not imply runtime fallback. | [FFT guide](docs/develop/fft-backends.md#selection-and-contract) |
| FFTW wisdom is CPU/build/size-sensitive and process-global | Retune when relevant inputs change; external planner users must coordinate. A successful import does not prove optimality. | [Planning](docs/use/command-line.md#fftw-plan-tuning) |
| Core processing uses interleaved float buffers that must not overlap | Use `HostProcessor` for planar/in-place float/double host buffers; internal DSP remains float. | [Engine API](docs/develop/library.md#engine-api) |
| Latency and filter replacement are not instantaneous | Core latency is B; the plug-ins' default is 256 frames. Replacement starts with fresh history and warms/crossfades; reset work scales with the block buffers and delay lines, not the filter length (PERF-5). | [Replacement](docs/develop/plugin-contract.md#replacement-and-threads) |
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

Finished items, one line each so their IDs stay defined. What each delivered is
described on the linked page and in the [CHANGELOG](CHANGELOG.md). The longer
entries written before 7 November 2026, with their evidence, are
[archived](docs/archive/todo/2026-11-06-done-entries.md).

- [x] **AU-1 — Adapter and test.** 23 October 2026, PR #44, with the bypass
  follow-up in PR #49; [plug-in guide](docs/use/plugins.md).
- [x] **AU-2 — Loading filters.** 23 October 2026, PR #44;
  [presets](docs/use/presets.md).
- [x] **AU-8 — Split the adapter's source by responsibility.** 31 October 2026,
  PR #92; [implementation map](docs/develop/architecture.md).
- [x] **AU-12 — Wake the Latency work with a dispatch source.** 5 November
  2026, PR #109; [Audio Unit settings](docs/develop/plugin-contract.md).
- [x] **AU-14 — A completion witness for the Latency work.** 5 November 2026.
- [x] **AU-15 — Render through AVAudioEngine.** 6 November 2026; [AVAudioEngine
  render](docs/develop/testing.md#avaudioengine-render).
- [x] **AU-13 — Safe partial construction of the Latency work.** 5 November
  2026, PR #111.
- [x] **AU-16 — Show a bypass switched while the control lock is held.**
  8 November 2026, PR #143; [Status and log](docs/develop/plugin-contract.md#status-and-log).
- [x] **AU-11 — Separate file watching from the window.** 5 November 2026;
  [shared macOS window](docs/develop/architecture.md#the-shared-macos-window).
- [x] **AU-10 — Separate the window's setting editing from its layout.** 1
  November 2026, PR #100; [implementation map](docs/develop/architecture.md).
- [x] **AU-9 — Simplify the macOS plug-in code.** 31 October 2026, PR #93.
- [x] **CI-1 — Skip builds and tests for documentation-only pull requests.** 24
  October 2026, PR #63; [documentation-only pull
  requests](docs/develop/testing.md#documentation-only-pull-requests).
- [x] **CI-2 — Shorten CI without losing coverage.** 24 October 2026, PR #65;
  [which configurations run which
  tests](docs/develop/testing.md#which-configurations-run-which-tests).
- [x] **CI-3 — Latest stable Clang tools.** 24 October 2026, PR #69;
  [formatting and analysis](CONTRIBUTING.md#formatting-and-analysis).
- [x] **CI-4 — Report the oracle error relative to the signal.** 24 October
  2026, PR #74; [native
  tests](docs/develop/testing.md#native-production-tests).
- [x] **CI-5 — No actions on a deprecated Node.js.** 24 October 2026, PR #76;
  [lint checks](CONTRIBUTING.md#lint-checks).
- [x] **CI-6 — Keep supporting libraries on their newest releases.** 24 October
  2026, PR #77; [supporting
  libraries](docs/develop/build.md#supporting-libraries).
- [x] **CI-7 — Resolve shared SDKs once per workflow.** 5 November 2026;
  [supporting libraries](docs/develop/build.md#supporting-libraries).
- [x] **CI-8 — Keep evidence from advisory failures.** 5 November 2026;
  [evidence from CI
  runs](docs/develop/testing.md#evidence-from-failed-ci-runs).
- [x] **CI-9 — Shorter CI jobs, no test dropped.** 25 October 2026, PR #85;
  [build targets and test
  order](docs/develop/testing.md#build-targets-and-test-order).
- [x] **CI-10 — One Linux source build for candidates.** 27 October 2026, PR
  #86; [candidate checks](docs/develop/testing.md#plug-in-candidate-checks).
- [x] **CI-11 — FFT backend configurations as parallel jobs.** 27 October 2026,
  closed without the split, PR #86; [CI
  triggers](docs/develop/testing.md#ci-triggers).
- [x] **CI-12 — Fuzz campaigns side by side.** 27 October 2026, PR #86; [fuzz
  smoke tests](docs/develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).
- [x] **CI-13 — Run only the affected format's workflows.** 27 October 2026, PR
  #86; [pull requests for one plug-in
  format](docs/develop/testing.md#pull-requests-for-one-plug-in-format).
- [x] **CI-14 — Use every runner CPU.** 27 October 2026, PR #86; [build targets
  and test order](docs/develop/testing.md#build-targets-and-test-order).
- [x] **CI-15 — Test the fuzz scheduler's failure paths.** 28 October 2026, PR
  #87; [fuzz smoke
  tests](docs/develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).
- [x] **CI-16 — A `--jobs` limit for release builds.** 28 October 2026, PR #87;
  [preview recipe](docs/develop/build.md#preview-recipe).
- [x] **CI-17 — Remove unused Python imports.** 29 October 2026, PR #88;
  [Python formatting and lint](CONTRIBUTING.md#python-formatting-and-lint).
- [x] **CI-18 — Format and lint the Python with ruff.** 29 October 2026, PR
  #88; [Python formatting and
  lint](CONTRIBUTING.md#python-formatting-and-lint).
- [x] **CI-22 — Make intermittent CI failures fixable.** 1 November 2026;
  [intermittent failures](docs/develop/testing.md#intermittent-failures).
- [x] **CI-30 — Complete the tests' replaced allocation functions.** 5 November
  2026, PR #111.
- [x] **CI-31 — Keep `native.host_settings` within its time limit under
  sanitizers.** 5 November 2026, PR #111.
- [x] **CI-33 — Compare audio, not file bytes, in the no-clip test.** 6
  November 2026, PR #124; [intermittent
  failures](docs/develop/testing.md#intermittent-failures).
- [x] **CI-29 — Use LLVM's parallel clang-tidy runner.** 5 November 2026, PR
  #108; [formatting and analysis](CONTRIBUTING.md#formatting-and-analysis).
- [x] **CI-24 — Find why `native.request_worker` timed out on Windows.** 5
  November 2026, closed without a cause, PR #109;
  [evidence](docs/archive/reviews/2026-11-05-request-worker.md).
- [x] **CI-25 — Find why the watchdog check hung on the macOS arm64
  candidate.** 5 November 2026, closed without a cause, PR #109;
  [evidence](docs/archive/reviews/2026-11-05-request-worker.md).
- [x] **CI-28 — Keep `vst3.adapter`'s Status and log case's log as evidence.**
  5 November 2026; [evidence from failed CI
  runs](docs/develop/testing.md#evidence-from-failed-ci-runs).
- [x] **CI-27 — Keep the Audio Unit adapter test's log after a crash.** 2
  November 2026; [evidence from failed CI
  runs](docs/develop/testing.md#evidence-from-failed-ci-runs).
- [x] **CI-26 — Run only VST3, Audio Unit and the candidates for window
  changes.** 2 November 2026; [one plug-in
  format](docs/develop/testing.md#pull-requests-for-one-plug-in-format).
- [x] **CI-23 — Run `vst3.fft_symbols` on Windows.** 1 November 2026; [VST3
  tests](docs/develop/testing.md#plug-in-format-tests).
- [x] **CI-19 — Build the Apple silicon candidate on `macos-15`.** 31 October
  2026; [candidate checks](docs/develop/testing.md#plug-in-candidate-checks).
- [x] **CI-20 — Keep unpacked ZIP files inside their destination.** 1 November
  2026, PR #102; [candidates](docs/develop/releasing.md#candidates).
- [x] **CI-21 — Pin third-party actions to a commit SHA.** 1 November 2026, PR
  #102; [lint checks](CONTRIBUTING.md#lint-checks).
- [x] **DIAG-1 — Review how diagnostics reach the user.** 23 to 31 October
  2026, PRs #57, #68, #72, #73, #96; [status and
  log](docs/develop/plugin-contract.md#status-and-log).
- [x] **DIAG-2 — Actionable diagnostics.** 5 November 2026, PR #110;
  [troubleshooting](docs/use/troubleshooting.md).
- [x] **DIAG-3 — Report faults around a filter change together.** 5 November
  2026; [contract](docs/develop/plugin-contract.md#status-and-log).
- [x] **DIAG-4 — Keep messages in their parts inside the library.** 5 November
  2026; [failure reasons](docs/develop/plugin-contract.md#failure-reasons).
- [x] **DOC-0 — Refresh the entry points for 0.4.0.** 20 October 2026.
- [x] **DOC-1 — Runnable first-use examples.** 22 October 2026, PR #28;
  [first-use examples](examples/first-use/README.md).
- [x] **DOC-3 — Resolve adapter-plan review findings.** 21 October 2026.
- [x] **DOC-4 — Organise the documentation by reader.** 23 October 2026, PRs
  #47, #51, #52, #53; [documentation guide](docs/develop/documentation.md).
- [x] **DOC-5 — Refresh the status page's evidence.** 1 November 2026, PR #99;
  [status](docs/project/status.md).
- [x] **DOC-6 — One home for progress.** 1 November 2026, PR #105; [recording
  progress](docs/develop/workflow.md#recording-progress-in-todo).
- [x] **DOC-7 — Archive finished entries and remove cruft.** 6 November 2026,
  PR #123, on jrp's request; [recording
  progress](docs/develop/workflow.md#recording-progress-in-todo).
- [x] **DOC-8 — Tune the agent guidance and workflow.** 6 November 2026,
  PR #129, from jrp's review; [workflow](docs/develop/workflow.md#kinds-of-task).
- [x] **DOC-9 — Consistent words for filters, presets and projects.** 7
  November 2026, PR #136, from jrp's review, with clearer Waiting status wording;
  [words Convolver uses](docs/use/concepts.md#words-convolver-uses).
- [x] **DOC-11 — Reconcile the release status page.** 8 November 2026,
  PR #142, from jrp's review of `bf6a207`; [what still blocks the first
  preview](docs/project/status.md#what-still-blocks-the-first-preview).
- [x] **FFMPEG-1 — Test the pipe workflow.** 5 November 2026;
  [tests](docs/develop/testing.md#native-production-tests).
- [x] **FFT-1 — Check that we have the latest Ooura version.** 21 October 2026;
  [upstream
  comparison](portable/third_party/ooura/README.md#upstream-comparison-21-october-2026).
- [x] **FFT-3 — Accelerate by default on macOS.** 30 October 2026, PR #89;
  [which libraries each build
  uses](docs/develop/build.md#which-libraries-each-build-uses).
- [x] **FFT-4 — Remove the finished vDSP experiment.** 5 November 2026, PR
  #108.
- [x] **IO-3 — More output containers.** 23 October 2026, PR #60; [output
  formats](docs/use/command-line.md#output-formats).
- [x] **IO-4 — Raw filters and raw sample types.** 24 October 2026, PR #60;
  [RAW streams](docs/use/command-line.md#raw-streams).
- [x] **IO-7 — Dither defaults by output resolution.** 25 October 2026, PR #79;
  [integer
  output](docs/use/command-line.md#integer-formats-dither-and-noise-shaping).
- [x] **IO-9 — Shortest float text from the standard library.** 5 November
  2026, PR #109; [building for macOS before
  14](docs/develop/build.md#building-for-macos-before-14).
- [x] **IO-6 — FLAC in the preview packages.** 24 October 2026, PR #60; [FLAC
  and Ogg](docs/use/command-line.md#flac-and-ogg).
- [x] **HOST-5 — A headless Ardour session in CI.** 6 November 2026; [Ardour,
  headless](docs/develop/testing.md#ardour-headless).
- [x] **LV2-1 — Adapter and headless test.** 22 October 2026, PR #43; [LV2
  tests](docs/develop/testing.md#lv2-tests).
- [x] **LV2-2 — Loading filters.** 23 October 2026, PR #48; [LV2
  tests](docs/develop/testing.md#lv2-tests).
- [x] **LV2-8 — Status after a request the host drops.** 6 November 2026,
  closed without a change, PR #122; [worker
  lifecycle](docs/project/plans/2026-10-21-plugin-formats.md#lv2-adapter-design).
- [x] **PERF-4 — Compiler flags and CPU-specific kernels.** 24 October 2026, PR
  #61; [compiler flags and AVX2
  kernels](docs/develop/fft-backends.md#compiler-flags-and-avx2-kernels).
- [x] **PERF-5 — Track valid input history.** 24 October 2026, PR #70;
  [startup, reset and
  loading](docs/develop/fft-backends.md#startup-reset-and-filter-loading).
- [x] **PERF-6 — Decode each filter file once per load.** 24 October 2026, PR
  #70; [startup, reset and
  loading](docs/develop/fft-backends.md#startup-reset-and-filter-loading).
- [x] **PERF-9 — Load timing test.** 5 November 2026, PR #111; [load
  timing](docs/develop/testing.md#load-timing-perf-9).
- [x] **PERF-10 — The load-timing gate cannot pass unqualified.** 5 November
  2026; [load timing](docs/develop/testing.md#load-timing-perf-9).
- [x] **PERF-11 — Quiet noise as the load-timing input.** 5 November 2026.
- [x] **DOC-2 — Maintain documentation automatically.** 6 November 2026, PR
  #118; [automated checks](docs/develop/documentation.md#automated-checks).
- [x] **GAIN-1 — Define no-clip headroom precisely.** 6 November 2026, PR #118;
  [gain](docs/use/command-line.md#gain-and-attenuation).
- [x] **GAIN-3 — `no-clip` reserves the integer headroom.** 6 November 2026;
  [gain](docs/use/command-line.md#gain-and-attenuation).
- [x] **PERF-12 — Load-timing verdicts from records, tested without timing.** 5
  November 2026; [load timing](docs/develop/testing.md#load-timing-perf-9).
- [x] **PERF-14 — Load timing from a full history.** 5 November 2026.
- [x] **VST3-16 — A new Latency in REAPER.** 8 November 2026, PR #134;
  [settings](docs/use/plugins.md#settings),
  [acceptance](docs/archive/reviews/2026-11-08-vst3-16-reaper-acceptance.md).
- [x] **PLUG-28 — Name the packaged presets after their filter.** 8
  November 2026, PR #141 and, for the first-use presets, PR #144; [first sound in a host](docs/use/first-sound-in-a-host.md#load-the-examples).
- [x] **PLUG-23 — A filter reads the first inputs of a wider input bus.** 6
  November 2026; [channels](docs/use/plugins.md#channels).
- [x] **PLUG-22 — Name the filter in the window, and say each fact once.** 7
  November 2026, PR #128; [Convolver's
  window](docs/use/plugins.md#convolvers-window).
- [x] **PLUG-21 — Teardown tested at chosen stages.** 5 November 2026.
- [x] **PERF-13 — Load-timing records as JSON.** 5 November 2026; [load
  timing](docs/develop/testing.md#load-timing-perf-9).
- [x] **PERF-15 — Subnormal numbers flushed while processing.** 5 November
  2026, PR #117; [subnormal
  numbers](docs/develop/architecture.md#subnormal-numbers).
- [x] **GAIN-2 — No noise simulation for `--gain-db no-clip`.** 5 November
  2026, PR #111.
- [x] **PLUG-1 — FFT backend for plug-ins.** 21 October 2026; [plug-in
  builds](docs/develop/fft-backends.md#plug-in-builds).
- [x] **PLUG-2 — Shared adapter test harness.** 22 October 2026, PR #28;
  [plug-in comparisons](docs/develop/testing.md#plug-in-comparisons-plug-2).
- [x] **PLUG-3 — One preset tool.** 22 October 2026, PR #32; [preset
  files](docs/develop/plugin-contract.md#preset-files).
- [x] **PLUG-4 — Layouts and identities.** 22 October 2026, PR #27; [plug-in
  identities](docs/develop/plugin-identities.md).
- [x] **PLUG-6 — Pin format dependencies.** 22 October 2026, PR #31;
  [supporting libraries](docs/develop/build.md#supporting-libraries).
- [x] **PLUG-7 — Lifecycle sequence tests.** 23 October 2026, PR #58;
  [lifecycle sequences](docs/develop/testing.md#lifecycle-sequences-plug-7).
- [x] **PLUG-8 — Keep a structured failure reason.** 23 October 2026, PR #46;
  [failure reasons](docs/develop/plugin-contract.md#failure-reasons).
- [x] **PLUG-12 — Accept only filters that can be saved.** 31 October 2026;
  [host state](docs/develop/plugin-contract.md#host-state).
- [x] **PLUG-13 — Keep room for the settings in saved state.** 1 November 2026,
  PR #105; [host state](docs/develop/plugin-contract.md#host-state).
- [x] **PLUG-15 — One macOS deployment target.** 5 November 2026, PR #108;
  [build options](docs/develop/build.md#build-options).
- [x] **PLUG-17 — Ramps measured in time rather than frames.** 5 November 2026,
  PR #109; [contract](docs/develop/plugin-contract.md#settings).
- [x] **PLUG-19 — One frame's allowance at a ramp's end.** 5 November 2026;
  [settings](docs/develop/plugin-contract.md#settings).
- [x] **PLUG-18 — Exact bypass and mute ramp endpoints.** 5 November 2026, PR
  #111; [settings](docs/develop/plugin-contract.md#settings).
- [x] **PLUG-16 — Name the transition lengths.** 5 November 2026, PR #108;
  [contract](docs/develop/plugin-contract.md).
- [x] **REL-2 — Define the first release by user journeys.** 6 November 2026,
  PR #118; [check a stereo filter](docs/use/check-a-stereo-filter.md), [a
  crossover](docs/use/check-a-crossover-in-a-host.md), [file
  rendering](docs/use/check-files-with-the-command-line.md).
- [x] **REL-6 — Responsiveness qualification before a release.** 5 November
  2026; [procedure](docs/develop/releasing.md#responsiveness-qualification).
- [x] **REL-5 — Early limited walkthrough on macOS.** 1 November 2026, PR #101;
  [check a stereo filter in a host](docs/use/check-a-stereo-filter.md).
- [x] **ROBUST-3 — Silence when memory runs out after a recorded failure.** 31
  October 2026; [replacement and
  threads](docs/develop/plugin-contract.md#replacement-and-threads).
- [x] **ROBUST-7 — A failed first load fails however early memory runs out.** 5
  November 2026, PR #111; [status and
  log](docs/develop/plugin-contract.md#status-and-log).
- [x] **ROBUST-4 — Low-memory quantizer and non-finite gain estimates.** 24
  October 2026, PR #72; [gain](docs/use/command-line.md#gain-and-attenuation).
- [x] **IO-5 — Speaker labels beyond WAV.** 6 November 2026, PR #118; [speaker
  labels](docs/use/command-line.md#speaker-labels).
- [x] **ROBUST-2 — Bound report output.** 6 November 2026, PR #118; [resource
  limits](docs/develop/library.md#resource-limits).
- [x] **ROBUST-6 — State the shared limits once.** 5 November 2026, PR #108;
  [resource limits](docs/develop/library.md#resource-limits).
- [x] **VST3-15 — Test the messages through a host's proxy.** 5 November 2026;
  [messages](docs/develop/plugin-contract.md#status-and-log).
- [x] **VST3-14 — Separate the controller from the processor's file.** 5
  November 2026; [implementation map](docs/develop/architecture.md).
- [x] **VST3-1 — Speaker arrangements, not only channel counts.** 23 October
  2026, PR #42; [channel
  layouts](docs/develop/plugin-identities.md#channel-layouts).
- [x] **VST3-3 — Editor messaging between controller and processor.** 1
  November 2026, PR #103;
  [messages](docs/develop/plugin-contract.md#status-and-log).
- [x] **VST3-4 — Gain, mix and latency parameters.** 1 November 2026, PR #103;
  [settings](docs/develop/plugin-contract.md#settings).
- [x] **VST3-7 — Follow the shared adapter contract.** 22 October 2026, PR #35;
  [adapter contract](docs/develop/plugin-contract.md).
- [x] **VST3-8 — Follow the SDK documentation where the audit found gaps.** 23
  October 2026, PR #59; [adapter contract](docs/develop/plugin-contract.md).
- [x] **VST3-9 — Non-integer host rates.** 5 November 2026; [when a filter does
  not fit](docs/use/plugins.md#when-a-filter-does-not-fit).
- [x] **VST3-10 — Mark silent output.** 24 October 2026, PR #70; [format
  notes](docs/develop/plugin-contract.md#format-notes).
