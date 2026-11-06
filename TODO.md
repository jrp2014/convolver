<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver TODO

Open work on the active portable implementation, with a stable ID for each
item. This is the shared backlog, not a promise that every idea will be
implemented. Finished items are listed briefly under [done](#done); what they
delivered is described in the guides and the [CHANGELOG](CHANGELOG.md).

**In short:** the next milestone is end-to-end testing: an independent user
running an exact preview package from the 0.10 series. The
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
(ROBUST), a parked FFmpeg filter (FFMPEG), and FFT or performance work only
where measurements justify it (FFT, PERF). The
[limitations table](#documented-limitations) lists current constraints.

To take an item on, open a draft pull request titled with its ID; that is the
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
cut from the 0.10 series and holds the VST3 plug-in, the Audio Unit (macOS),
the `convolver` CLI and the preset tool, with the editor's settings and window
in both plug-in formats; LV2 joins a later preview once
its own journeys are demonstrated. What still blocks it, with the evidence, is in
[status](docs/project/status.md#what-still-blocks-the-first-preview).
Finishing a dependable product matters more than another substantial
refactor: move code into shared layers only when two formats need the same
behaviour (as with `HostFilter`), do not generalise host lifecycle or release
machinery beyond the requirements in hand, and keep libsndfile at the I/O
boundary and C++20 in the core.

| Order | Work | Exit criterion / dependency |
| --- | --- | --- |
| 1. Finish the open fixes (done) | **DIAG-1** fault state after reloads and Load failed before Fault ([PR #73](https://github.com/jrp2014/convolver/pull/73)); from jrp's review of `6920a45` (31 October 2026), **PLUG-12** (a filter that loads but cannot be saved) and **ROBUST-3** (silence after a failed first load that cannot run out of memory); from his review of `276c7dc` (1 November 2026), **PLUG-13** (room for the settings in saved state, [PR #105](https://github.com/jrp2014/convolver/pull/105)) | DIAG-1 done: merged on 25 October 2026 (`7ed7e38`), and the "Plug-in distribution candidates" run on that commit passed ([run](https://github.com/jrp2014/convolver/actions/runs/40145929436)). PLUG-12 and ROBUST-3 done: merged on 31 October 2026 ([PR #91](https://github.com/jrp2014/convolver/pull/91), `7542576`), each with a test of the sequence that failed. |
| 2. Host smoke tests | **HOST-1** (REAPER and Audacity, VST3), on jrp's Mac, from the latest candidate; **AU-4** (GarageBand, the Audio Unit from the same candidate, which holds it since PR #115) at the same sitting | Each [smoke test](docs/develop/host-testing.md#smoke-test-in-each-first-host) recorded with the package SHA-256, host and macOS versions, and every finding fixed or listed. HOST-1 and AU-4 both gate the first preview, which holds the Audio Unit too. Needs no CLI in the package, so it can start now that step 1 is merged. Its notes (where each host shows the Status parameter and presets) feed REL-2. |
| 3. Alongside step 2 | **PLUG-5** first-preview contents: in the candidates since PR #115 (the Audio Unit on macOS, the VST trademark notice; the `convolver` CLI and the first-use presets since PR #101); what remains is qualifying one exact candidate (HOST-1, AU-4, REL-3) and jrp's publication. **REL-2** (done): the stereo, 2→6 crossover and CLI journeys are written in `docs/use/` | The package holds everything the journeys use, and the release tests run it; each journey step states its expected result and how to check the output. |
| Early walkthrough (macOS) (done) | **REL-5**: an explicitly limited walkthrough of an exact candidate on macOS, the stereo journey only | Done on 1 November 2026 as a package and command-line check only, by jrp's choice ([done](#done)); its host steps moved to HOST-1's REAPER smoke test and REL-3. It did not change the first preview's contents and does not replace REL-3. |
| 4. Settings, windows and the Audio Unit | The **HOST-2** editor, which jrp wants in the first preview with the Audio Unit: **PLUG-9** and **PLUG-10**, then **VST3-3**, **VST3-4**, **AU-7** and the first half of **AU-6**, then the windows **VST3-11** (macOS, shared with the Audio Unit in **AU-6**), **VST3-12** (Windows) and **VST3-13** (Linux, VSTGUI stop-gap); and the Audio Unit's own path into the package, **AU-3** (validation), **AU-4** (guide and hosts) and **AU-5** (the component in the preview, in the macOS candidates since PR #115) | The [editor plan's phases](docs/project/plans/2026-10-24-plugin-editor.md#phases-and-todo-items), each merged with its tests; the REL-2 journeys and the host smoke tests include the window. **Acceptance gate:** loading a long filter from a window or a preset, on the supported workloads (PERF-3), causes no noticeable pause in the host's interface or audio, measured by automated tests rather than by hand; if it does, background preparation comes first ([loading responsiveness](#loading-responsiveness)). PLUG-9 and PLUG-10 can start alongside steps 2 and 3. |
| 5. The milestone | A candidate from a 0.10.x tag; **REL-3**, the independent walkthrough of its exact files; fixes for what the tester finds | Recorded walkthrough. jrp then publishes the unsigned preview ([releasing](docs/develop/releasing.md#publishing-an-unsigned-preview)), with **COMPAT-1**'s capture and **REL-4**'s steps after publishing. |
| 6. Later previews | **LV2-3** to **LV2-5**, with the editor's **LV2-7**, **REL-1** for all formats; **COMPAT-1** at each publication | Each format's REL-2 journeys demonstrated before it joins a preview. Each preview repeats the REL-2 journeys; from the second, the COMPAT-1 upgrade tests too. Developer ID signing, notarization and Authenticode remain the later production gate. |
| Before further substantial DSP changes, and any optimisation | **PERF-3:** resource budgets | Callback tail latency, peak memory and preparation time while loading, replacing and resetting, across realistic routing and callback sizes, define the supported workloads (jrp's review of `6920a45`: keep the core architecture and finish this first). PERF-2, FFT-2 and VST3-5, and any more complex partitioning, start only if PERF-3 shows a need; the loading-responsiveness gate uses its workloads. |
| Supporting, cheap | **PERF-1**, **HOST-4** | HOST-4's remaining check needs a Windows computer. PERF-1 reruns the room-correction benchmarks on Apple Silicon and Windows. |
| Parked | **FFMPEG-2 to FFMPEG-5**, **HOST-3** | Revisit only by an owner decision. |
| Optional editor work | **PLUG-11** (FFT planning in FFTW-built plug-ins), **LV2-6** (an LV2 window, only if Linux users ask) | Not needed for any preview. |
| On request or measured need | **HOST-2** (CLAP), **VST3-2**, **VST3-5**, **VST3-6**, **FFT-2**, **PERF-2**, **CFG-2** | Define the user need or measured bottleneck and acceptance tests first. CFG-2 waits for CFG-1. |

The CFG-1, GAIN-3, IO-1, IO-2 and ROBUST-1 items
remain open supporting work (GAIN-3 decided by jrp on 6 November 2026); take them up when a reproducible defect or the milestone
needs them. This table owns the current ordering; the dated
[21 October review order](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md#recommended-order)
is superseded.

## User experience and distribution

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
  The REAPER smoke test also takes over the host steps that REL-5 left out:
  install, render, save and reopen, and presets that do not fit, in
  [check a stereo filter in a host](docs/use/check-a-stereo-filter.md),
  and, as jrp decided on 6 November 2026, the 2→6 crossover journey
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
  divergence documented.
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
  Needed for the first preview.
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
  Needed for the first preview. Oracle tests with the worst
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
- [ ] **LV2-3 — Layouts and validation.** Plug-in URIs for the PLUG-4 layouts;
  `lv2lint` and pluginval in CI.
  Progress (22 October 2026, PR #43): the eight discrete URIs and `lv2lint` in
  CI came with LV2-1; pluginval and named layouts remain.
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
  0.10.x builds are minor 10, which LV2 accepts, but every odd patch number
  is a micro version LV2 reads as a development version (0.9.x was minor 9,
  a development version; LV2 is not in the first preview).
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

- [ ] **LV2-8 — Status after a request the host drops.** Found by PLUG-21:
  a host whose worker drops queued requests at deactivation (the tests'
  deferred worker can) leaves the status port at Loading until another
  load is answered; the original filter keeps playing and stays saved.
  Whether any real host drops requests has not been checked, so this may be
  marginal: check the hosts first (Ardour, Carla, Jalv, PipeWire), and fix
  only if one does.

AU (AUv2, macOS: Logic, GarageBand, SoundSource):

- [ ] **AU-3 — Validation.** `auval` and pluginval in macOS CI. `auval -strict`
  runs since PR #44; still to add: pluginval on the component, and a
  ThreadSanitizer build of `au.adapter` (bypass switched while rendering).
  Needed for the first preview.
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
  remain.
  Needed for the first preview.
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
  Needed for the first preview.
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
- [ ] **GAIN-3 — `no-clip` reserves the integer headroom.** jrp's answer on
  PR #118 (6 November 2026): `--gain-db no-clip` lowers its gain by the
  headroom the chosen integer format, dither and noise shaping need (the
  bound H in [gain](docs/use/command-line.md#gain-and-attenuation), GAIN-1),
  allowing for float rounding too, so integer output never clips on input
  within full scale; and it prints the applied gain with enough digits to
  reproduce it with `--gain-db`. Numeric gains, `auto` and float output stay
  as they are. Acceptance: `native.cli` renders the worst-case input (as
  `native.engine`'s `no_clip_endpoint` builds it) for each integer type,
  dither and shaper and finds no clipped sample, float output unchanged,
  and passing the printed figure back reproduces the file exactly. It
  changes the output level, so it has a pull request of its own and a
  CHANGELOG line saying so; until then the guide's workaround (lower the
  reported gain by the table's value and 0.01 dB) stands.
- [ ] **IO-1 — Large files.** Specify and test boundary sizes and pipe behavior
  for output beyond 4 GiB. Input decoder support is not a promise of strict
  validation for every container.
  Progress (23 October 2026, PR #60): RF64, W64, CAF, FLAC and raw output have
  no 4 GiB limit; WAV and AIFF stop with an error at it. Tests at the boundary
  sizes remain.
- [ ] **IO-2 — Optional offline IR resampling.** Only add as an explicit operation
  with tests for gain, delay, length and reported metadata; retain mismatch errors
  by default. Compare [libsamplerate](https://libsndfile.github.io/libsamplerate/)
  and other suitable implementations before choosing a dependency.
- [ ] **ROBUST-1 — Longer fuzz and transition stress.** Keep discovered inputs as
  regressions. Measure worst callback times and exercise repeated filter changes;
  allocation guards and average throughput are not a deadline guarantee.

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
  the steps in REAPER and Audacity ([host testing](docs/develop/host-testing.md#reaper-vst3)),
  VoiceOver, and the meters (with PLUG-10).
- [ ] **VST3-12 — Editor window on Windows.** The same window as a Win32 view
  (`kPlatformTypeHWND`), with `IPlugViewContentScaleSupport` and keyboard
  navigation; checked with NVDA or Narrator.
  Needed for the first preview.
- [ ] **VST3-13 — Editor window on Linux.** The window in VSTGUI, for the
  Linux VST3 build only, as a stop-gap, driven by the
  host's `IRunLoop`; VSTGUI (BSD-3-Clause) and the libraries it links added
  to the package checks and licence notices; opened by `editorhost` under
  `xvfb-run`. Not reachable by screen readers, which the guide says. A native
  X11 view can replace it later if Linux users need one
  ([editor plan](docs/project/plans/2026-10-24-plugin-editor.md#framework-choice)). Needed for the first preview.
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
  Shared forward transforms are already implemented.
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
  and whether slow preparation needs VST3-5. PERF-1 keeps the throughput
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
- [ ] **PERF-15 — Denormal numbers while processing.** Found by PERF-14 (5
  November 2026): the load-timing tests' 30 s filter decays below the
  smallest normal float, and on the Linux cloud VM (clang Release, Ooura,
  stereo 48 kHz, 512-frame callbacks, 10.7 ms apart) each callback at full
  history took 25 to 43 ms; with flush-to-zero and denormals-are-zero set on
  the audio thread, 6 to 7 ms. Neither the engine nor the plug-ins set
  them; most hosts do on their audio threads, but not all, and a reverb tail
  fading to silence reaches the same numbers. jrp decided
  ([decision](docs/project/decisions.md#configuration-and-sound)) that
  Convolver flushes them itself, in its own pull request: one small
  RAII helper sets flush-to-zero (and denormals-are-zero on x86-64; FPCR on
  Apple Silicon) for each processing call and restores the caller's mode,
  covering `Engine::process` (the CLI) and the whole `HostProcessor` call
  (conversion, gain and mixing too). Tests: restoration on normal return,
  early return, exceptions, nested scopes and a caller that already
  flushes; the oracle tests with the worst error reported; subnormal input
  and a decaying tail; the `--warm` runs with the incoming mode on and off.
  Document that subnormal results are flushed (no promise of IEEE gradual
  underflow). With flushing, the 30 s filter's 6 to 7 ms still exceeds half
  the 10.7 ms period on that VM: the fix does not qualify that workload
  there, and the limit stays.
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
Measure, on the PERF-3 supported workloads, how long that thread is held
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
the PERF-3 workloads, and records how long the caller is blocked and how
late each callback is. jrp sets the threshold for both; the gate passes when
every adapter meets it. If one does not, preparation moves to a worker, with
the adapter (as LV2 does) owning request order, overtaken requests,
publication and shutdown.

PERF-9 (done, PR #111) built that test on interim workloads, with jrp's
limits (5 November 2026): in optimised builds, no late callback and none
over half its period while a load runs on another thread, and the host's
thread held at most 100 ms with filters up to 10 s and 250 ms with 30 s
([load timing](docs/develop/testing.md#load-timing-perf-9)). What remains:
the PERF-3 workloads in place of the interim ones, and, if the 30 s case
ever exceeds 250 ms on a supported platform, jrp's choice between
background preparation in VST3 and the Audio Unit (VST3-5) and a higher
limit with the pause documented.
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
and the open PERF-15 to PERF-18. The 100 ms and 250 ms limits and the
callback limits stay. With `--warm` (PERF-14, same cloud VM and build), the
10 s filter at full history took about 3 ms a callback and the 30 s filter
25 to 43 ms, every callback late, so its replacement and reset failed on
callbacks while the hold (167 to 192 ms) met its limit; with flush-to-zero
on the audio thread, 6 to 7 ms and no late callback (PERF-15). The
archived request worker's Windows timeout
([archived](docs/archive/reviews/2026-11-05-request-worker.md)) was never
explained; AU-12 removed that worker, which is not a diagnosis.

### Keeping `host.cpp` manageable

Guidance, not an item (external review of `91fcf1f`, 0.8.13, 5 November
2026): `portable/host.cpp` combines filter acceptance, failure policy,
diagnostic wording, status text and the real-time `HostProcessor`. When one
of these next changes, move the control-side filter policy apart from
`HostProcessor`'s audio code, as VST3-14 and AU-11 moved code out of the
adapters, with public APIs and ownership unchanged. Diagnostics already stay
in their structured parts (problem, cause, fix) inside and are formatted
only where they leave the library (DIAG-4).
No new general framework. Such moves keep saved state, diagnostic text,
allocation guards and numerical results unchanged, and never share a pull
request with a DSP change.

## Developer tooling

- [ ] **CI-32 — Run the most extreme numerical cases less often, once they
  cost.** From jrp's simplification review of 5 November 2026: keep
  representative oracle and allocation checks as everyday gates, and run
  the largest and most extreme cases (`native.kernel_divergence`'s 128k to
  512k filters and its noise near 1e-35, where much of the arithmetic is
  subnormal) in scheduled or DSP-focused runs, keeping their evidence. Not
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
| Gain estimation is not a limiter; float samples can exceed full scale | Integer samples clip; `no-clip` reserves no quantizer headroom, for which the guide gives a safe bound and a workaround (GAIN-1) until GAIN-3. | [Gain](docs/use/command-line.md#gain-and-attenuation) |
| Dither/shaping/valid bits apply to integer file output only | Float/RAW output rejects those options; the plug-ins exchange floats, so the host does any final conversion. | [Integer output](docs/use/command-line.md#integer-formats-dither-and-noise-shaping) |
| C++ packages are static development libraries, not self-contained installers or a stable binary ABI | Rebuild with compatible toolchains; shared dependencies must be deployed. REL-1 covers end-user bundles. | [Package guide](docs/develop/library.md) |
| Source tags and configured CI do not prove a release is qualified | Tie results to exact commits; named hosts, clean machines and signatures remain release evidence. | [Versioning](docs/develop/versioning.md), [testing](docs/develop/testing.md) |
| Finite oracle, fuzz and heap-guard tests do not prove all-input correctness or hard realtime | Keep explicit numerical tolerances and test boundaries; ROBUST-1. | [Testing](docs/develop/testing.md#validation-limits-and-release-work) |
| Equalizer APO 1.4.2 (x86/x64) convolution drops up to three bins just below half the sample rate when a block is not a multiple of four frames | Upstream behaviour, inaudible (about 22 kHz at 44.1 kHz); the export does not work around it. The runtime test uses 480-frame blocks. | [Equalizer APO guide](docs/develop/testing.md#equalizer-apo-export-tests) |
| Historical Windows applications are not built or host-tested by portable CI | Old PCM32, shaping, CMD tail and host-negotiation gaps remain historical; fix only proven defects, without implying modern host support. | [Historical coverage](docs/develop/testing.md#historical-coverage) |

## Done

Finished items, listed so their IDs stay defined. What each delivered is
described on the linked page and in the [CHANGELOG](CHANGELOG.md).

- [x] **AU-1 — Adapter and test.** 23 October 2026, PR #44, with the bypass
  follow-up in PR #49; [plug-in guide](docs/use/plugins.md),
  [Audio Unit licence](docs/use/plugins.md#audio-unit-licence).
- [x] **AU-2 — Loading filters.** 23 October 2026, PR #44: host state in
  `ClassInfo` under `convolver-state`, and `.aupreset` files from the preset
  tool; loading one from a named host's preset menu is checked under AU-4;
  [presets](docs/use/presets.md).
- [x] **AU-8 — Split the adapter's source by responsibility.** 31 October
  2026, PR #92, from jrp's review of 30 October: `plugins/au/effect.cpp`
  (1,166 lines) moved unchanged into `effect.cpp`, `effect_state.cpp` and
  `effect_properties.cpp`, then the repeated silence and refusal steps were
  made one function each and Core Foundation values owned by `cf_owned`; no
  behaviour changes (CHANGELOG has the one out-of-memory exception);
  [implementation map](docs/develop/architecture.md).
- [x] **AU-12 — Wake the Latency work with a dispatch source.** 5 November
  2026, PR #109, from the project review of that day, on jrp's go-ahead:
  the Audio Unit applies a new Latency on a serial dispatch queue woken by a
  `DISPATCH_SOURCE_TYPE_DATA_OR` source, in place of
  `portable/request_worker.hpp`, a thread of our own. Apple does not
  document `dispatch_source_merge_data` as real-time safe; it is an atomic
  OR, then a wake-up that may make a system call to ask for a thread, and
  the open-source libdispatch neither allocates nor takes a user-space lock
  there (`au.adapter` counts only C++ allocations, so it cannot see one
  inside libdispatch). The
  worker's tests, the watchdog workflow and `scripts/ci_watchdog_probe.py`
  went with it ([archived](docs/archive/reviews/2026-11-05-request-worker.md));
  [Audio Unit settings](docs/develop/plugin-contract.md).
- [x] **AU-14 — A completion witness for the Latency work.** 5 November
  2026, from jrp's review of `db54160`: `au.adapter` slept 100 ms to show
  that an unchanged Latency choice sent no second notification, and 20 ms
  "for its log line" after an allocation failure, although the work logs
  after releasing the lock the test had waited for. Each unit now numbers
  its requests, and each finished run (logged, the host told) reports the
  number it had seen when it began, with the unit; the test waits until a
  run of the unit it changed that saw its change has finished, with a 10 s
  deadline that only catches a deadlock. The first version counted for the
  whole process, so one unit's finished work satisfied another's check
  (jrp's review of `5f47ecb`, reproduced with two units); a two-unit case
  now checks this. The component's only addition is a lock-free increment
  per request and a null test hook. It runs on macOS CI only.
- [x] **AU-13 — Safe partial construction of the Latency work.** 5 November
  2026, PR #111, from the external review of `91fcf1f` (0.8.13), a source
  review: AU-12 created the dispatch source before the semaphore, so if the
  semaphore could not be created an inactive source was released, which
  `dispatch_activate`'s documentation calls undefined. The semaphore and
  queue are now created first and the source last, and an activated source
  is released only after it is cancelled. `au.adapter` makes each creation
  fail in turn (`latency_queue_failure_for_tests`) and closes units while
  Latency work is queued or running; it runs on macOS CI only.
- [x] **AU-11 — Separate file watching from the window.** 5 November 2026,
  from jrp's review of `276c7dc`: following the playing filter's files and
  deciding when a change is due to reload moved from
  `plugins/editor/mac/diagnostic_view.mm` (now 811 lines) to
  `plugins/editor/mac/file_watch.mm`, a C++ class, so it needs no versioned
  Objective-C name; behaviour unchanged, checked by `vst3.editor` and
  `au.editor` on macOS; [shared macOS window](docs/develop/architecture.md#the-shared-macos-window).
- [x] **AU-10 — Separate the window's setting editing from its layout.**
  1 November 2026, PR #100, from jrp's review of PR #98: the Gain, Mix and
  Latency rows and everything the user does with them (dragging, reset,
  typing, choosing) moved from `plugins/au/diagnostic_view.mm` (959 lines,
  now about 630) to `plugins/au/setting_rows.mm`, with the shared layout
  helpers in `plugins/au/view_layout.h`. Behaviour unchanged; `au.editor`
  unchanged. See the [implementation map](docs/develop/architecture.md).
- [x] **AU-9 — Simplify the macOS plug-in code.** 31 October 2026, PR #93,
  jrp's request: behaviour-preserving simplifications of the AudioToolbox
  reader, the window and the adapter's property calls, one commit each,
  undoing no reviewed fix, plus the out-of-memory fix AU-8's review found;
  what was left alone, and why, is listed in the pull request.
- [x] **CI-1 — Skip builds and tests for documentation-only pull requests.**
  24 October 2026, PR #63; [documentation-only pull requests](docs/develop/testing.md#documentation-only-pull-requests).
- [x] **CI-2 — Shorten CI without losing coverage.** 24 October 2026, PR #65:
  LV2 sanitizer stages as parallel jobs building only their targets,
  one CTest entry per package consumer, a parallel offline rebuild, ccache on
  Linux and macOS, and each kind of test run only where it can change; [which configurations run which tests](docs/develop/testing.md#which-configurations-run-which-tests).
- [x] **CI-3 — Latest stable Clang tools.** 24 October 2026, PR #69:
  formatting and analysis use the latest stable packaged Clang in CI and cloud
  sessions; local installation on jrp's Mac is deferred;
  [formatting and analysis](CONTRIBUTING.md#formatting-and-analysis).
- [x] **CI-4 — Report the oracle error relative to the signal.** 24 October
  2026, PR #74: `native.engine` prints the worst error relative to the output
  peak for each group of cases and kernel set, with B, and where the largest
  absolute error came from, so a new, louder test (PERF-5's
  `history_validity`, peak 7.6) no longer looks like a tenfold loss of
  accuracy; [native tests](docs/develop/testing.md#native-production-tests).
- [x] **CI-5 — No actions on a deprecated Node.js.** 24 October 2026, PR #76:
  an inline MSVC developer-environment step replaces `ilammy/msvc-dev-cmd`, and
  `ccache-action` uses its newest exact release; [GitHub Actions versions](CONTRIBUTING.md#lint-checks).
- [x] **CI-6 — Keep supporting libraries on their newest releases.** 24
  October 2026, PR #77, by jrp's decision: builds resolve the newest release of
  each SDK, library and tool instead of a pin, and record version, commit and
  checksum in the job summary, source and artifact manifests and, for VST3 and
  AU, the Convolver log; `sources.py --lock` repeats a recorded build. Formal
  tests use full releases; newer pre-releases (pluginval 2.0 beta) and
  upstream lilv run as advisory checks;
  [supporting libraries](docs/develop/build.md#supporting-libraries).
- [x] **CI-7 — Resolve shared SDKs once per workflow.** 5 November 2026,
  from jrp's review of 0.5.2: the VST3 and Audio Unit workflows resolve
  their SDK in a first `sdk` job, whose lock file every build job of the run
  fetches with `sources.py --lock`; each job's summary still lists the
  release. LV2's formal jobs need no shared SDK (one job fetches the lv2
  headers; the rest use Ubuntu's packages), and the candidates already share
  their `sources` job's corresponding source. Per-OS tools (pluginval,
  lv2lint) are still resolved per job
  ([supporting libraries](docs/develop/build.md#supporting-libraries)).
- [x] **CI-8 — Keep evidence from advisory failures.** 5 November 2026,
  from jrp's review of 0.5.2: when the newest pluginval pre-release reports
  problems, or the LV2 tests fail against the newest upstream lilv, the job
  keeps CTest's output as an `advisory-evidence-…` artifact, although it
  stays green ([evidence from CI runs](docs/develop/testing.md#evidence-from-failed-ci-runs)).
- [x] **CI-9 — Shorter CI jobs, no test dropped.** 25 October 2026, PR #85,
  from jrp's CI analysis: the Linux VST3 jobs and the Audio Unit sanitizer job
  build only what their tests run, long tests carry a CTest `COST` so they
  start first, and the portable clang-tidy files are analysed side by side;
  [build targets and test order](docs/develop/testing.md#build-targets-and-test-order).
- [x] **CI-10 — One Linux source build for candidates.** 27 October 2026,
  PR #86, from jrp's CI analysis: the Linux candidate is built from the
  verified archive with networking disabled and CMake's default generator, so
  the second, offline build is gone; `source_build.py` stays for local checks
  (CI no longer runs it);
  [candidate checks](docs/develop/testing.md#plug-in-candidate-checks).
- [x] **CI-11 — FFT backend configurations as parallel jobs.** Closed
  without the split, 27 October 2026, PR #86, by jrp's choice: once builds used
  every CPU (CI-14) the macOS job's Accelerate and Ooura builds took about
  65–82 s instead of 120 s, and one commit already starts 6 macOS jobs against
  the account's limit of 5, so more macOS jobs would make the candidates queue
  longer; [CI triggers](docs/develop/testing.md#ci-triggers).
- [x] **CI-12 — Fuzz campaigns side by side.** 27 October 2026, PR #86, from
  jrp's CI analysis: two campaigns at a time, one per core of the Linux
  runner's two, each keeping its 60 seconds and about its usual executions;
  the step went from 244 s to 122 s;
  [fuzz smoke tests](docs/develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).
- [x] **CI-13 — Run only the affected format's workflows.** 27 October 2026,
  PR #86, from jrp's CI analysis: a pull request that changes only one plug-in
  format's own files (or one workflow file) runs Lint and that format's
  workflows; a test keeps every other file from depending on a format's files;
  [pull requests for one plug-in format](docs/develop/testing.md#pull-requests-for-one-plug-in-format).
- [x] **CI-14 — Use every runner CPU.** 27 October 2026, PR #86: builds and
  tests used 2 of the 4 CPUs on the Linux and Windows runners; they now use
  all of them; the macOS Accelerate and Ooura builds went from about 120 s
  to 65–82 s (whole workflows vary too much between runs to show more);
  [build targets and test order](docs/develop/testing.md#build-targets-and-test-order).
- [x] **CI-15 — Test the fuzz scheduler's failure paths.** 28 October 2026,
  PR #87, from the external review of PR #86: the scheduler is
  `scripts/fuzz_campaigns.sh`, and `tests/fuzz/scheduler_tests.py` checks a
  failing campaign, a missing status file and the concurrency limit with
  stand-in fuzzers;
  [fuzz smoke tests](docs/develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).
- [x] **CI-16 — A `--jobs` limit for release builds.** 28 October 2026,
  PR #87, from the external review of PR #86: `candidate.py`,
  `build_dependencies.py` and `source_build.py` take `--jobs N`; the default
  stays every CPU; [preview recipe](docs/develop/build.md#preview-recipe).
- [x] **CI-17 — Remove unused Python imports.** 29 October 2026, PR #88,
  from a Skylos scan: 11 unused imports in the release scripts and four test
  scripts; [Python formatting and lint](CONTRIBUTING.md#python-formatting-and-lint).
- [x] **CI-18 — Format and lint the Python with ruff.** 29 October 2026,
  PR #88: the latest stable ruff in pre-commit and lint CI, with every stable
  rule but a few listed in `ruff.toml`;
  [Python formatting and lint](CONTRIBUTING.md#python-formatting-and-lint).
- [x] **CI-22 — Make intermittent CI failures fixable.** 1 November 2026,
  after jrp lost time re-running jobs (his five points): Windows jobs turn
  off vcpkg's metrics, whose copy of `vcpkg.exe` the evidence shows blocked
  parallel app-local DLL steps ([vcpkg-tool#2163](https://github.com/microsoft/vcpkg-tool/issues/2163));
  `native.request_worker`'s watchdog reports a timeout with where each
  thread was and their stacks; waits are by the clock; the package consumer
  builds count two CPUs in CTest; failed tests stay in each attempt's job
  summary and evidence; `au.editor` reads the report after a load until it
  shows; `au.adapter` no longer changes `CONVOLVER_LOG` while a worker
  reads it; the build and test jobs have time limits. No automatic re-runs;
  CI-24 and CI-25 continue;
  [intermittent failures](docs/develop/testing.md#intermittent-failures).
- [x] **CI-30 — Complete the tests' replaced allocation functions.**
  5 November 2026, PR #111, from the external review of `91fcf1f` (0.8.13):
  `native.engine` replaced `operator new` without the sized `operator
  delete`, so with GCC's library AddressSanitizer stopped it with
  alloc-dealloc-mismatch before the oracle tests ran. The array and sized
  forms are now one shared file, `tests/native/replaced_allocation.hpp`,
  used by every native test that watches allocation.
- [x] **CI-31 — Keep `native.host_settings` within its time limit under
  sanitizers.** 5 November 2026, PR #111: the external review of `91fcf1f`
  (0.8.13) saw it time out in a GCC AddressSanitizer build. The shared
  double-precision oracle (`tests/native/oracle.hpp`) spent 90 of its 91
  seconds there; it now works out each route's invariant parts once, with
  the same arithmetic in the same order (worst errors unchanged), and the
  test takes about 4 seconds.
- [x] **CI-29 — Use LLVM's parallel clang-tidy runner.** 5 November 2026,
  PR #108, from the project review of that day: CI runs `run-clang-tidy.py`,
  which the clang-tidy package installs, in place of our
  `scripts/clang_tidy.py` and its tests;
  [lint checks](CONTRIBUTING.md#formatting-and-analysis).
- [x] **CI-24 — Find why `native.request_worker` timed out on Windows.**
  Closed 5 November 2026 without a cause, PR #109: AU-12 removed the worker
  and its test; the evidence is
  [archived](docs/archive/reviews/2026-11-05-request-worker.md).
- [x] **CI-25 — Find why the watchdog check hung on the macOS arm64
  candidate.** Closed 5 November 2026 without a cause, PR #109: AU-12
  removed the check, its workflow and its supervisor; the evidence is
  [archived](docs/archive/reviews/2026-11-05-request-worker.md).
- [x] **CI-28 — Keep `vst3.adapter`'s Status and log case's log as
  evidence.** 5 November 2026, from CI-27's review: the case pointed
  `CONVOLVER_LOG` at a temporary file with `setenv` while the processor
  existed, and deleted it. `vst3.adapter` and `vst3.preset` (which runs the
  same tests) now each have a log of their own in the build folder, set once
  in `main` (wide on Windows), and the case reads it from
  an offset with the helper the Audio Unit tests use, moved to
  `tests/plugins/test_log.hpp`;
  [evidence from failed CI runs](docs/develop/testing.md#evidence-from-failed-ci-runs).
- [x] **CI-27 — Keep the Audio Unit adapter test's log after a crash.** 2
  November 2026, from jrp's review of `f2dc691`: the test kept its Convolver
  log in a private temporary file and copied it to the build folder only on
  a normal exit, so a sanitizer abort, crash or forced end lost it, and the
  rotated `.old` file was deleted (`au.editor` deleted its private log).
  Both now log to their own file in the build folder from the start, never
  copied or deleted; the evidence collector takes every test log and its
  `.old` file; `au.log_evidence` checks both after an abort and a kill;
  [evidence from failed CI runs](docs/develop/testing.md#evidence-from-failed-ci-runs).
- [x] **CI-26 — Run only VST3, Audio Unit and the candidates for window
  changes.** 2 November 2026, from jrp's review of `276c7dc`: a pull
  request that changes only the shared macOS window (`plugins/editor/mac/`)
  no longer runs the native, LV2 and fuzz workflows; any shared code in the
  same pull request still runs everything;
  [one plug-in format](docs/develop/testing.md#pull-requests-for-one-plug-in-format).
- [x] **CI-23 — Run `vst3.fft_symbols` on Windows.** 1 November 2026: CTest
  runs outside a developer prompt, so `dumpbin` was not on PATH and the test
  always skipped there; it now finds `dumpbin` through `vswhere`;
  [VST3 tests](docs/develop/testing.md#plug-in-format-tests).
- [x] **CI-19 — Build the Apple silicon candidate on `macos-15`.** 31 October
  2026: GitHub retires the `macos-14` image on 2 November 2026, and its
  brownouts on 30 October cancelled the candidate job before it ran (PRs #84
  and #89, [runner-images#13518](https://github.com/actions/runner-images/issues/13518));
  the package still targets macOS 14; [candidate checks](docs/develop/testing.md#plug-in-candidate-checks).
- [x] **CI-20 — Keep unpacked ZIP files inside their destination.** 1
  November 2026, PR #102, from a Skylos scan: `candidate.extract_zip` reads member
  names as POSIX paths (so `/name` is refused on Windows too) and checks each
  resolved path stays under the destination, as `sources.extract` does for
  source archives; tested in `release.source`;
  [candidates](docs/develop/releasing.md#candidates).
- [x] **CI-21 — Pin third-party actions to a commit SHA.** 1 November 2026,
  PR #102, jrp's choice after a Skylos scan: `hendrikmuhs/ccache-action` is pinned to
  v1.2.24's commit with the release in a comment, and Dependabot still moves
  it to each new release; GitHub's own actions keep major tags;
  [GitHub Actions versions](CONTRIBUTING.md#lint-checks).
- [x] **DIAG-1 — Review how diagnostics reach the user.** Status parameter
  and Convolver log (23 October 2026, PR #57), processing faults and run
  identity (PR #68), symbol checks (PR #72), fault state after reloads (PR
  #73); the [diagnostics by surface](docs/archive/plans/2026-10-24-diagnostics-by-surface.md)
  table, and jrp's choices among its proposals implemented (31 October 2026,
  PR #96): CLI sample faults and incomplete outputs, next steps for damaged
  files, `convolver-preset` wording and option order, logged plug-in refusals
  and failed saves, with a test per message. What named hosts show is
  HOST-1 and AU-4; [output safety](docs/use/command-line.md#output-safety),
  [status and log](docs/develop/plugin-contract.md#status-and-log).
- [x] **DIAG-2 — Actionable diagnostics.** 5 November 2026, PR #110, from
  jrp's request of that day: every message a user can meet states the
  problem, its cause and the fix, with a note for an environment, host or
  design limit. The command-line tools print them as labelled lines, the
  Convolver log as one line, and the macOS window shows the problem in bold,
  Cause and Fix rows, a button that carries out the fix (Choose Filter… or
  Copy Diagnostics) and a Note row, with Bypass and the diagnostics buttons
  moved off the bottom edge (jrp's choices); a test per message. The window's
  look in a host is part of HOST-1;
  [troubleshooting](docs/use/troubleshooting.md),
  [plan](docs/archive/plans/2026-11-05-actionable-diagnostics.md).
- [x] **DIAG-3 — Report faults around a filter change together.** 5
  November 2026, from jrp's simplification review: faults counted while a
  replacement was prepared are logged once, as met around a filter change,
  rather than split from the new filter's own; `HostFaultReport` holds
  `faults`, `changed` and `held`, and `HostFilter` keeps no separate tally
  of them. Kept, because each protects something users see: one snapshot as
  the baseline of both the status and the audio thread's signal (new faults
  show, old ones do not), Fault signalled once per baseline, and the hold
  that stops Fault replacing Load failed;
  [contract](docs/develop/plugin-contract.md#status-and-log).
- [x] **DIAG-4 — Keep messages in their parts inside the library.** 5
  November 2026, from jrp's simplification review: `HostFailure::message`,
  `HostFilter::notice()`, `speaker_notice()`, `host_state_size_error()` and
  the readers' `FilterFileFailure` hold an `Advice` (problem, cause, fix,
  note; new public header `convolver/advice.hpp`), and `reject()` takes one.
  The log, the window and `HostFilter::error()` format it where it leaves
  the library, so the report no longer parses text it had just written.
  Only exception messages are still parsed, as they must be. The wording is
  unchanged;
  [failure reasons](docs/develop/plugin-contract.md#failure-reasons).
- [x] **DOC-0 — Refresh the entry points for 0.4.0.** 20 October 2026;
  [documentation review](docs/archive/reviews/2026-10-20-documentation-review.md).
- [x] **DOC-1 — Runnable first-use examples.** 22 October 2026, PR #28;
  [first-use examples](examples/first-use/README.md).
- [x] **DOC-3 — Resolve adapter-plan review findings.** 21 October 2026;
  [review](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md#documentation-findings).
- [x] **DOC-4 — Organise the documentation by reader.** 23 October 2026,
  PRs #47, #51, #52 and #53; [documentation guide](docs/develop/documentation.md),
  [plan](docs/archive/plans/2026-10-23-documentation-by-reader.md).
- [x] **DOC-5 — Refresh the status page's evidence.** 1 November 2026,
  PR #99; [status](docs/project/status.md), checked against `main` at 0.8.2.
- [x] **DOC-6 — One home for progress.** 1 November 2026, PR #105, from
  jrp's review of `276c7dc`: the status page had drifted (the CLI and
  journey presets "missing", journeys "not started", the VST3 half
  "unclaimed"). Checked against 0.8.6; progress detail now lives only in
  TODO's `Progress` lines, and the status page names what remains and links
  the items ([recording progress](docs/develop/workflow.md#recording-progress-in-todo)).
- [x] **FFMPEG-1 — Test the pipe workflow.** 5 November 2026:
  `native.ffmpeg_pipe` runs the guide's FFmpeg pipe on a film FFmpeg makes,
  checking track selection, 2-in/6-out routing, the frame count with the
  tail, and the start time of late audio, wherever FFmpeg is installed (the
  native g++ Release job on Linux CI);
  [tests](docs/develop/testing.md#native-production-tests).
- [x] **FFT-1 — Check that we have the latest Ooura version.** 21 October 2026;
  [provenance](portable/third_party/ooura/README.md#upstream-comparison-21-october-2026).
- [x] **FFT-3 — Accelerate by default on macOS.** 30 October 2026, PR #89: every
  macOS build, the command-line tools included, uses Accelerate unless built
  with `-DCONVOLVER_FFT_BACKEND=FFTW`;
  [which libraries each build uses](docs/develop/build.md#which-libraries-each-build-uses).
- [x] **FFT-4 — Remove the finished vDSP experiment.** 5 November 2026, PR #108,
  from the project review of that day: `portable/kernels_vdsp.cpp` and the
  `CONVOLVER_EXPERIMENT_VDSP_MAC` option are gone with the experiment's two
  scripts; its timing program stays as `convolver_kernel_benchmark`; the
  [24 October results](docs/archive/reviews/2026-10-24-apple-vdsp.md) stay
  archived; the code can be rebuilt from `v0.8.10` (`f5ecb12`) or earlier.
- [x] **IO-3 — More output containers.** 23 October 2026, PR #60;
  [output formats](docs/use/command-line.md#output-formats).
- [x] **IO-4 — Raw filters and raw sample types.** 24 October 2026, PR #60;
  `f32le` by default and reported, `--raw-filter-format` for others;
  [filter files](docs/use/configuration.md#filter-files),
  [RAW streams](docs/use/command-line.md#raw-streams).
- [x] **IO-7 — Dither defaults by output resolution.** 25 October 2026, PR #79;
  [integer output](docs/use/command-line.md#integer-formats-dither-and-noise-shaping).
- [x] **IO-9 — Shortest float text from the standard library.** 5 November
  2026, PR #109, from the project review of that day, on jrp's "Do io-9":
  the Equalizer APO exporter formats gains with `std::to_chars` instead of
  trying 1 to 160 decimal places with `snprintf` and `strtof`. Compared for
  every positive finite float with libstdc++ and libc++, the text is the
  same except for ±2^-96 (about −578 dB), which `to_chars` writes with one
  digit fewer; both texts read back as the same float. The inspection report keeps its
  9-step `%g` search: `std::to_chars`' general format switches to exponents
  at another point than `%g`, so using it would change that report. The
  exporter now needs macOS 13.3 or later to build;
  [building for macOS before 14](docs/develop/build.md#building-for-macos-before-14)
  says what older targets need.
- [x] **IO-6 — FLAC in the preview packages.** 24 October 2026, PR #60:
  decided not to ship the codec libraries; limitation and workarounds in
  [FLAC and Ogg](docs/use/command-line.md#flac-and-ogg).
- [x] **LV2-1 — Adapter and headless test.** 22 October 2026, PR #43;
  [LV2 tests](docs/develop/testing.md#lv2-tests).
- [x] **LV2-2 — Loading filters.** 23 October 2026, PR #48;
  [loading a filter](docs/use/plugins.md#loading-a-filter), [LV2 tests](docs/develop/testing.md#lv2-tests).
- [x] **PERF-4 — Compiler flags and CPU-specific kernels.** 24 October 2026,
  PR #61: run-time AVX2+FMA kernels, with FMA by jrp's decision and a divergence test; [compiler flags and AVX2 kernels](docs/develop/fft-backends.md#compiler-flags-and-avx2-kernels).
- [x] **PERF-5 — Track valid input history.** 24 October 2026, PR #70: paths
  multiply only the history written since start or reset, and reset
  invalidates instead of clearing; output bit-identical;
  [startup, reset and loading](docs/develop/fft-backends.md#startup-reset-and-filter-loading).
- [x] **PERF-6 — Decode each filter file once per load.** 24 October 2026,
  PR #70: same errors and limits;
  [startup, reset and loading](docs/develop/fft-backends.md#startup-reset-and-filter-loading).
- [x] **PERF-9 — Load timing test.** 5 November 2026, PR #111, from the
  external review of `91fcf1f` (0.8.13), on jrp's go-ahead: `vst3.load_timing`,
  `au.load_timing` and `lv2.load_timing` time each format's load entry points
  while another thread renders at the host's pace, and report how long the
  caller was held and how late callbacks were, failing above jrp's limits
  in optimised builds ([loading responsiveness](#loading-responsiveness));
  [load timing](docs/develop/testing.md#load-timing-perf-9).
- [x] **PERF-10 — The load-timing gate cannot pass unqualified.** 5 November
  2026, from jrp's review of `59ac8e0` (0.10.0): each run of an entry is
  passed, failed or not qualified; the host thread's hold is judged whatever
  the audio thread saw; a run is not qualified only when callbacks broke the
  limits with a late baseline (half a second before the phases and another
  after them), or the build does not enforce them; the test exits 77
  (CTest: skipped) rather than 0 when an entry did not qualify; an entry no
  limit applies to is reported only, not passed; the summary keeps every
  run that did not pass and counts passes after a run that did not; an
  entry passes only if one run met both limits (jrp's review of `cc2ebde`:
  limits met in different runs are not qualified, or failed when every run
  broke one), and the summary names the run that met each limit for an
  entry that did not pass; a test where nothing was judged (every entry
  reported only) is skipped, not passed. `native.load_timing_harness` checks the verdict
  rules on made-up runs, and drives the harness with a mock adapter: slow
  loads fail, with late callbacks too (the review's reproduction); a load
  slow on the first run only meets the hold limit later, with that run kept;
  late callbacks with a late baseline do not qualify; a load no limit
  applies to is reported only; a refused load fails;
  [load timing](docs/develop/testing.md#load-timing-perf-9).
- [x] **PERF-11 — Quiet noise as the load-timing input.** 5 November 2026,
  from jrp's review of `59ac8e0` (0.10.0): the input generator subtracted
  in unsigned arithmetic, so the negative half became about 1.8e15 (measured
  range 0.0002 to 1.84e15). It now subtracts in signed arithmetic, and
  `native.load_timing_harness` checks the range (-0.1 to 0.1) and that both
  signs occur; it failed with the old generator. The loading timings were
  measured again with it ([loading responsiveness](#loading-responsiveness)).
- [x] **DOC-2 — Maintain documentation automatically.** 6 November 2026, PR #118:
  `check_repository.py` (pre-commit and lint CI) checks every CMake `-D`
  option in the guides, workflows and scripts against the build, with its
  allowed values, and that the build guide describes every option; and
  every option shown with a Convolver tool against the options its source
  parses, and that each tool's guide names them all. The packaged guide's
  marked examples already ran in the candidates workflow (PR #101). Links
  and anchors were already checked;
  [automated checks](docs/develop/documentation.md#automated-checks).
- [x] **GAIN-1 — Define no-clip headroom precisely.** 6 November 2026, PR #118: with
  the no-clip gain, the input reaching the bound stays within full scale up
  to float rounding (largest sample 0.99999994, `native.engine`); integer
  output never clips within ±(1 − H / 2^(B−1)), H = (1 + S)(½ + D) + ½
  steps for dither D and shaper coefficient sum S (a safe bound with half a
  step of margin), checked for 8, 16 and
  24 bits, every dither and shaper (`native.io`, which detects clipping by
  comparing with a quantizer one bit wider), with the exact undithered
  endpoints. No change to the sound: `no-clip` still reserves no quantizer
  headroom; [gain](docs/use/command-line.md#gain-and-attenuation).
- [x] **PERF-12 — Load-timing verdicts from records, tested without
  timing.** 5 November 2026, from jrp's review of `db54160`: the renderer
  records each callback (due, wake, entry into and return from the plug-in,
  and the thread's CPU time where the platform gives it), and a pure
  `judge()` gives a run's verdict from those records. A callback that missed
  its deadline only because the thread woke late is not the plug-in's own
  time: not judged, so the run is not qualified rather than failed. A slow
  call with little CPU time was at first excused as descheduled too; after
  jrp's review of `5f47ecb` (a call blocked on a lock in the plug-in looks
  the same) it is judged as the plug-in's, and its note says the records
  cannot tell blocked from descheduled. `native.load_timing_harness` checks the
  rules on made-up records (deadline missed with a fast callback, slow
  callback, loader over budget, unsuitable baseline, functional errors,
  mixed results across runs) and keeps only a few short real-thread runs:
  about 9 s instead of 40 to 60;
  [load timing](docs/develop/testing.md#load-timing-perf-9).
- [x] **PERF-14 — Load timing from a full history.** 5 November 2026,
  from jrp's review of `db54160`: a newly loaded long filter is cheap
  because the engine processes only the parts that have heard input, so the
  timing tests measured startup, not a long filter's sustained cost. With
  `--warm`, the renderer plays the filter's length of audio unpaced before
  the replacement and before the reset, and shows half a second of each
  filter at full history ("warm first", "warm second", reported only: no
  limit for steady playing is decided). Opt-in because filling takes
  minutes with the 30 s filter; CI keeps the cold runs, and REL-6 runs it.
  `native.load_timing_harness` checks the phases with a mock. It found
  PERF-15 at once; [loading responsiveness](#loading-responsiveness).
- [x] **PLUG-21 — Teardown tested at chosen stages.** 5 November 2026
  (first committed as PLUG-20, which main then gave to the VST logo),
  from jrp's review of `db54160` ("test shutdown at explicit stages rather
  than hoping repeated runs encounter the troublesome interleaving"):
  `lv2.adapter` holds `work()` at each of its pause points (read, prepared,
  checked) while another thread deactivates the instance, and releases it
  only once `deactivate()` has signalled from inside that it is waiting for
  `work()` (jrp's review of `5f47ecb`: otherwise the worker could finish
  before deactivation began): reactivated, the new filter plays and is
  reported and saved; closed with a newer request queued, nothing is used
  after it is freed. `au.adapter`
  holds a run of the Latency work through a new test-only hook
  (`latency_started_for_tests`) while the unit closes, alone and with a
  change queued, releasing it only once closing has cancelled the work and
  is about to wait for the run (a second hook, `latency_closing_for_tests`);
  the queued run must then never start. A 60 s watchdog stops either test,
  naming the stage. With the lock in LV2's `deactivate()` removed, the
  staged case fails (before the handshake, under ASan, with a heap use
  after free) while the earlier repeated loop passes. Loading and replacement ordering
  was already tested this way (the preparation-window cases). Found: LV2-8.
- [x] **PERF-13 — Load-timing records as JSON.** 5 November 2026, from
  jrp's review of `db54160`: each timing test writes every run's records
  (each callback's due, wake, entry, return, CPU time and success; each
  phase's hold and summary; verdicts and reasons) with the Convolver
  version, build, compiler, FFT backend, operating system and processor
  count, to `FORMAT-load-timing.json` in its work folder. The VST3 and Audio
  Unit Release jobs keep them as an artifact, and a failed job keeps them
  with its test evidence. Timing the loading stages inside the plug-in
  remains open; [load timing](docs/develop/testing.md#load-timing-perf-9).
- [x] **GAIN-2 — No noise simulation for `--gain-db no-clip`.** 5 November
  2026, PR #111, from the external review of `91fcf1f` (0.8.13): no-clip
  ran the whole noise estimate to use only the bound it also returns. It
  now validates the configuration and takes `worst_case_peak()` directly
  (0.43 ms rather than 69.5 ms for a 65,536-tap stereo filter in the
  review's Release measurement); `auto` still filters noise. `native.cli`
  checks the gain and the rendered output for silent, cancelling, delayed
  and multichannel configurations.
- [x] **PLUG-1 — FFT backend for plug-ins.** 21 October 2026;
  [plug-in builds](docs/develop/fft-backends.md#plug-in-builds).
- [x] **PLUG-2 — Shared adapter test harness.** 22 October 2026, PR #28;
  [plug-in comparisons](docs/develop/testing.md#plug-in-comparisons-plug-2).
- [x] **PLUG-3 — One preset tool.** 22 October 2026, PR #32;
  [plug-in presets](docs/use/presets.md), [preset files](docs/develop/plugin-contract.md#preset-files).
- [x] **PLUG-4 — Layouts and identities.** 22 October 2026, PR #27;
  [plug-in identities](docs/develop/plugin-identities.md).
- [x] **PLUG-6 — Pin format dependencies.** 22 October 2026, PR #31;
  [supporting libraries](docs/develop/build.md#supporting-libraries); replaced by CI-6.
- [x] **PLUG-7 — Lifecycle sequence tests.** 23 October 2026, PR #58;
  [lifecycle sequences](docs/develop/testing.md#lifecycle-sequences-plug-7).
- [x] **PLUG-8 — Keep a structured failure reason.** 23 October 2026, PR #46;
  [failure reasons](docs/develop/plugin-contract.md#failure-reasons).
- [x] **PLUG-12 — Accept only filters that can be saved.** 31 October 2026,
  from jrp's review of `6920a45`: a `.cfg` may hold 64 Mi samples but saved
  state only 64 MiB, so a 16,777,216-sample filter loaded and then could not
  be saved. `HostFilter::load` refuses a filter whose state would exceed the
  limit before installing it (`too_large`), and `convolver-preset` refuses
  to write one; [host state](docs/develop/plugin-contract.md#host-state).
- [x] **PLUG-13 — Keep room for the settings in saved state.** 1 November
  2026, PR #105, from jrp's review of `276c7dc`: a VST3 filter that filled
  the limit at default settings could not be saved once Gain changed. The
  size check keeps 256 bytes free for the settings whether or not they are
  in use;
  [host state](docs/develop/plugin-contract.md#host-state).
- [x] **PLUG-15 — One macOS deployment target.** 5 November 2026, PR #108,
  from the project review of that day: the top-level `CMakeLists.txt` sets
  `CMAKE_OSX_DEPLOYMENT_TARGET` to 14.0 before `project()`, as CMake asks,
  in place of the VST3 and Audio Unit folders forcing 11.0 after it, so
  every Mac build targets what the packages do;
  [build options](docs/develop/build.md#build-options).
- [x] **PLUG-17 — Ramps measured in time rather than frames.** 5 November
  2026, PR #109: jrp chose "whichever is easier to use, for the final
  user". `HostProcessor`'s gain, mix, bypass and mute ramps last 1.33 ms and
  a replacement's crossfade 5.33 ms at every sample rate (64 and 256 frames
  at 48 kHz, to the nearest frame elsewhere), so they are as smooth at
  96 kHz and above as at 48 kHz, which plays exactly as before;
  `native.host_settings` checks the lengths at 44.1, 48 and 96 kHz;
  [contract](docs/develop/plugin-contract.md#settings).
- [x] **PLUG-19 — One frame's allowance at a ramp's end.** 5 November
  2026, from jrp's simplification review: a setting transition must not
  click, must finish promptly and must end exactly at its target, but may
  end one frame after its last (inaudible: about 5.7 µs at 176.4 kHz).
  `native.host_settings` tests that allowance; PLUG-18's frame counting is
  kept because it is no larger than the float step it replaced. Reported
  latency and channel alignment have no allowance;
  [settings](docs/develop/plugin-contract.md#settings).
- [x] **PLUG-18 — Exact bypass and mute ramp endpoints.** 5 November 2026,
  PR #111, from the external review of `91fcf1f` (0.8.13): PLUG-17's bypass
  and mute ramps summed a float step, so at 176.4 kHz they left about
  2.4e-6 of unit input for one frame after their 235 frames. They now count
  frames and reach exactly 0 or 1 on the last frame, also when reversed;
  `native.host_settings` checks 8 kHz to 768 kHz;
  [settings](docs/develop/plugin-contract.md#settings).
- [x] **PLUG-16 — Name the transition lengths.** 5 November 2026, PR #108,
  from the project review of that day: `HostProcessor`'s 64-frame setting,
  bypass and mute ramps and its 256-frame crossfade are named constants in
  `portable/host.cpp`, with the same values, and the plug-in contract says a
  replacement plays for one Latency block before the crossfade, as it has
  since PLUG-10; [contract](docs/develop/plugin-contract.md).
- [x] **REL-2 — Define the first release by user journeys.** 6 November
  2026, PR #118: the three journeys are written in `docs/use/` as numbered steps with
  the expected result of each, from download and checksum:
  [check a stereo filter in a host](docs/use/check-a-stereo-filter.md)
  (PR #101), [check a crossover in a host](docs/use/check-a-crossover-in-a-host.md)
  (2→6 in REAPER, VST3 and the Audio Unit) and
  [check file rendering with the command line](docs/use/check-files-with-the-command-line.md)
  (the three examples, each checked with `--compare`, and filters refused
  for another rate or channel count, whose messages the guide quotes). The
  package holds what they use, now also required by `verify_artifact.py` for
  the crossover; the release tests run the guides' commands, and since this
  change also check each quoted refusal (`<!-- refused-example -->`). No
  journey has been followed in a host yet: the host steps, and REAPER's
  routing of an effect with 2 inputs and 6 outputs in particular, are for
  HOST-1 and REL-3 to demonstrate.
- [x] **REL-6 — Responsiveness qualification before a release.** 5
  November 2026, from jrp's review of `db54160`: CI's timing tests run on
  shared runners and report "not qualified" when they cannot judge, which
  is not qualification. The
  [procedure](docs/develop/releasing.md#responsiveness-qualification): on a
  named machine (jrp's Mac first, through the command line), build the
  candidate's tag with the SDKs its source manifest records, optimised and
  without sanitizers; run the timing tests cold and `--warm`; the release
  qualifies only if every entry passed (or no limit applied), with the
  existing limits; every run's records are kept and the release notes state
  the result either way. Not yet carried out: the first preview's run is
  part of its publication.
- [x] **REL-5 — Early limited walkthrough on macOS.** 1 November 2026,
  PR #101. An assisted check for jrp, not by an independent tester, of the
  stereo journey's package and command-line steps on the pull request's
  macOS arm64 candidate
  ([run 41347884655](https://github.com/jrp2014/convolver/actions/runs/41347884655),
  version 0.8.3 built from merge commit `773605c`, macOS 27.2, package
  SHA-256
  `936cc921e39c046d5bd5aa8073ff15532e3bf5513c46127b08b8b433861821a1`):
  the hashes matched, the package held every file the guide names, and the
  command-line render matched `expected.wav` (largest difference -131 dB).
  No defect found. jrp closed it without the host steps (install, render,
  save and reopen, presets that do not fit), which moved to HOST-1 and
  REL-3; nor did it cover the browser's quarantine, the installer, the
  Audio Unit or other platforms;
  [check a stereo filter in a host](docs/use/check-a-stereo-filter.md).
- [x] **ROBUST-3 — Silence when memory runs out after a recorded failure.**
  31 October 2026, from jrp's review of `6920a45`: a failed first load
  silences the processor with `HostProcessor::silence()`, which cannot
  allocate or fail, in VST3, the Audio Unit and LV2; lifecycle sequence
  `state-malformed-starved`;
  [replacement and threads](docs/develop/plugin-contract.md#replacement-and-threads).
- [x] **ROBUST-7 — A failed first load fails however early memory runs
  out.** 5 November 2026, PR #111, from the external review of `91fcf1f`
  (0.8.13), which reproduced it: `HostFilter::record` copied the request
  before marking the instance failed, so with allocations failing a refused
  first load left `failed()` false and the adapters let the input through.
  The failure is now marked first without allocating
  (`HostFilter::fail_request()`), with a record of its kind and request
  number; `reject()` never throws; VST3, the Audio Unit and LV2 also mark it
  when memory runs out building the message; an accepted filter is kept.
  Lifecycle sequences now require it after every starved attempt
  (`native.host` `unrecorded_failures` too);
  [status and log](docs/develop/plugin-contract.md#status-and-log).
- [x] **ROBUST-4 — Low-memory quantizer and non-finite gain estimates.**
  24 October 2026, PR #72, from jrp's review of 0.4.43: `Quantizer` allocates
  in its constructor, so out of memory reaches the caller, and
  `estimate_attenuation` refuses a non-finite noise response; tests in
  `native.io` and `native.engine`; [gain](docs/use/command-line.md#gain-and-attenuation).
- [x] **IO-5 — Speaker labels beyond WAV.** 6 November 2026, PR #118: checked with
  libsndfile 1.2.2 and FFmpeg's reader. RF64 now carries `--speaker-map`
  labels in its WAVE_FORMAT_EXTENSIBLE header, any mask, read back exactly
  by both. libsndfile writes no labels in W64 or FLAC; in CAF and AIFF it
  writes only Apple layouts, refusing side-speaker 5.1 and 7.1 and turning
  WAVE's back speakers into surround ones that FFmpeg reads as side, so
  those containers, like W64 and FLAC, refuse a map before the output is
  created. Tests in `native.io` and `native.cli`;
  [speaker labels](docs/use/command-line.md#speaker-labels).
- [x] **ROBUST-2 — Bound report output.** 6 November 2026, PR #118: traversal and
  diagnostics were already bounded (4096 visits, 256 diagnostics), but
  `--inspect --all-configurations` gives each entry a render's decoding
  budget, so a 20 KB list naming a 64 MB filter 4095 times kept it busy for
  about 18 minutes, and a 16 MiB list of 4096-path configurations made a
  533 MB JSON report using 2.9 GB. One check now stops, with one error,
  before the next entry once it has decoded 1 Gi samples, or before an entry
  that would take it past 16,384 reported paths (4.2 s and 4 MB on the same
  lists) or 131,072 reported routes (jrp's review of `7b4cb7a`: 128 entries
  of one path with 8192 routes made a 37 MB report; now 4.6 MB, about 1 s
  and 130 MiB under ASan); a no-match message
  names at most ten sample rates, as it already did entries. Tests in
  `native.limits`;
  [resource limits](docs/develop/library.md#resource-limits).
- [x] **ROBUST-6 — State the shared limits once.** 5 November 2026, PR #108,
  from the project review of that day: the sample-rate, channel, block and
  impulse limits are `constexpr` values in `convolver/engine.hpp`, used by
  the engine, readers, saved state, tools and adapters in place of literals
  in fifteen files, with the same messages;
  [resource limits](docs/develop/library.md#resource-limits).
- [x] **VST3-15 — Test the messages through a host's proxy.** 5 November
  2026, from jrp's review of `276c7dc`: `vst3.adapter` connects the
  components through a forwarding proxy and a deferring one
  (`IConnectionPoint`). The deferring one showed two assumptions of direct
  delivery: the host was asked to restart for a new Latency before the
  processor had it, and a refused file marked the project changed. The
  processor's `ConvolverStatus` now answers each load and Latency by its
  request number, and the controller acts on those answers (a restart only
  for the last Latency chosen) rather than on
  `sendMessage()`'s result; a deferred load returns before its answer and
  the report shows it;
  [messages](docs/develop/plugin-contract.md#status-and-log).
- [x] **VST3-14 — Separate the controller from the processor's file.**
  5 November 2026, from jrp's review of `276c7dc`: `Controller` moved from
  `plugins/vst3/processor.cpp` (then 1,041 lines, about 780 after) to
  `plugins/vst3/controller.cpp`, unchanged; the Status values' names, which both use, moved to
  `processor.hpp`; [implementation map](docs/develop/architecture.md).
- [x] **VST3-1 — Speaker arrangements, not only channel counts.** 23 October
  2026, PR #42; [channel layouts](docs/develop/plugin-identities.md#channel-layouts).
- [x] **VST3-3 — Editor messaging between controller and processor.**
  1 November 2026, PR #103: the processor sends the PLUG-9 report with each
  Status update and when the controller asks (`ConvolverReport`); a
  `ConvolverLoad` message carries a chosen path, which the processor reads
  as `convolver-preset` does (AudioToolbox on macOS) and installs as
  `setState` does, and the controller then calls `setDirty`; headless tests
  in `vst3.adapter` (the lifecycle sequences have no messages to add).
  Bypass stays the host's parameter, edited through the controller as
  before; a Bypass control in the window moved to VST3-11. Not done:
  `IDataExchangeHandler` for meters (with PLUG-10's meters) and plug-in
  snapshots for host browsers (`smtg_target_add_plugin_snapshots`), which
  need the window;
  [messages](docs/develop/plugin-contract.md#status-and-log).
- [x] **VST3-4 — Gain, mix and latency parameters.** 1 November 2026, PR
  #103: Gain (0), Mix (1003, `kDryWetMix`) and Latency (1004, not
  automatable, `restartComponent(kLatencyChanged)`), saved in host state
  version 3 only when they differ from the defaults; tests in
  `vst3.adapter` and `native.state`. Gain mode (1002) waits with PLUG-10's
  deferred parts; whether REAPER and Audacity compensate a new Latency
  moved to their host steps under HOST-1; [settings](docs/develop/plugin-contract.md#settings).
- [x] **VST3-7 — Follow the shared adapter contract.** 22 October 2026, PR #35,
  with the reset follow-up in PR #40; [adapter contract](docs/develop/plugin-contract.md).
- [x] **VST3-8 — Follow the SDK documentation where the audit found gaps.**
  23 October 2026, PR #59, from the
  [VST3 API audit](docs/review-2026-10/vst3-api-audit-2026-10-23.md): tail
  includes the latency, input silence flags not trusted, Bypass text converts
  back, controller state calls succeed; [adapter contract](docs/develop/plugin-contract.md).
- [x] **VST3-9 — Non-integer host rates.** 5 November 2026, on jrp's
  decision of that day: `setupProcessing` accepts any finite rate from 1 to
  768000 Hz, such as 48000 ÷ 1.001 ≈ 47952.04795 Hz (47952 Hz itself was
  always accepted). The rate is kept exactly (`HostLayout::fractional_rate`)
  and matched strictly: no filter suits a fractional rate, so the plug-in
  plays silence (bypass still plays the input), keeps its filter, names both
  rates, and plays the filter again at its rate; only floating-point
  rounding of a whole rate counts as that rate. Checked by `vst3.adapter`
  (`fractional_rates`) and `native.filter_file`;
  [when a filter does not fit](docs/use/plugins.md#when-a-filter-does-not-fit).
- [x] **VST3-10 — Mark silent output.** 24 October 2026, PR #70: an output
  channel is flagged silent exactly when a call left it all zero, so a tail
  that is still sounding is never flagged; [format notes](docs/develop/plugin-contract.md#format-notes).
