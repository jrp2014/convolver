<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver TODO

Open work on the active portable implementation, with a stable ID for each
item. This is the shared backlog, not a promise that every idea will be
implemented. Finished items are listed briefly under [done](#done); what they
delivered is described in the guides and the [CHANGELOG](CHANGELOG.md).

**In short:** the next milestone is end-to-end testing: an independent user
running an exact preview package from the 0.6 series. The
[order of work](#execution-priorities) gets there in four steps: finish the
open fixes, run jrp's host smoke tests (HOST-1), put the CLI
and the journey presets in the package while writing the user journeys
(PLUG-5, REL-2), then the independent walkthrough (REL-3).
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
cut from the 0.6 series and holds the VST3 plug-in, the Audio Unit (macOS),
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
| 1. Finish the open fixes | **DIAG-1** fault state after reloads and Load failed before Fault ([PR #73](https://github.com/jrp2014/convolver/pull/73)); from jrp's review of `6920a45` (31 October 2026), **PLUG-12** (a filter that loads but cannot be saved) and **ROBUST-3** (silence after a failed first load that cannot run out of memory) | DIAG-1 done: merged on 25 October 2026 (`7ed7e38`), and the "Plug-in distribution candidates" run on that commit passed ([run](https://github.com/jrp2014/convolver/actions/runs/40145929436)). PLUG-12 and ROBUST-3 are pre-preview fixes, each with a test of the sequence that failed. |
| 2. Host smoke tests | **HOST-1** (REAPER and Audacity, VST3), on jrp's Mac, from the latest candidate; **AU-4** (GarageBand, Audio Unit built from source until AU-5 packages it) at the same sitting | Each [smoke test](docs/develop/host-testing.md#smoke-test-in-each-first-host) recorded with the package SHA-256, host and macOS versions, and every finding fixed or listed. HOST-1 and AU-4 both gate the first preview, which holds the Audio Unit too. Needs no CLI in the package, so it can start as soon as step 1 is merged. Its notes (where each host shows the Status parameter and presets) feed REL-2. |
| 3. Alongside step 2 | **PLUG-5** first-preview contents: the `convolver` CLI and presets made from the stereo and 2→6 first-use filters, in the package, and the VST trademark notice. **REL-2:** the three journeys written in `docs/use/` | The package holds everything the journeys use, and the release tests run it; each journey step states its expected result and how to check the output. |
| Early walkthrough (macOS) | **REL-5**: an explicitly limited walkthrough of an exact candidate on macOS, the stereo journey only, as soon as step 3 has its stereo preset and journey | Recorded like REL-3, with its limits stated. It finds installation, documentation, loading and reopening problems while the other windows are still being built; it does not change the first preview's contents (jrp's 25 October decision) and does not replace REL-3. |
| 4. Settings, windows and the Audio Unit | The **HOST-2** editor, which jrp wants in the first preview with the Audio Unit: **PLUG-9** and **PLUG-10**, then **VST3-3**, **VST3-4**, **AU-7** and the first half of **AU-6**, then the windows **VST3-11** (macOS, shared with the Audio Unit in **AU-6**), **VST3-12** (Windows) and **VST3-13** (Linux, VSTGUI stop-gap); and the Audio Unit's own path into the package, **AU-3** (validation), **AU-4** (guide and hosts) and **AU-5** (the component in the preview) | The [editor plan's phases](docs/project/plans/2026-10-24-plugin-editor.md#phases-and-todo-items), each merged with its tests; the REL-2 journeys and the host smoke tests include the window. **Acceptance gate:** loading a long filter from a window or a preset, on the supported workloads (PERF-3), causes no noticeable pause in the host's interface or audio, measured by automated tests rather than by hand; if it does, background preparation comes first ([loading responsiveness](#loading-responsiveness)). PLUG-9 and PLUG-10 can start alongside steps 2 and 3. |
| 5. The milestone | A candidate from a 0.6.x tag; **REL-3**, the independent walkthrough of its exact files; fixes for what the tester finds | Recorded walkthrough. jrp then publishes the unsigned preview ([releasing](docs/develop/releasing.md#publishing-an-unsigned-preview)), with **COMPAT-1**'s capture and **REL-4**'s steps after publishing. |
| 6. Later previews | **LV2-3** to **LV2-5**, with the editor's **LV2-7**, **REL-1** for all formats; **COMPAT-1** at each publication | Each format's REL-2 journeys demonstrated before it joins a preview. Each preview repeats the REL-2 journeys; from the second, the COMPAT-1 upgrade tests too. Developer ID signing, notarization and Authenticode remain the later production gate. |
| Diagnostics | The rest of **DIAG-1**: jrp's choice among the proposals in the [diagnostics by surface](docs/project/plans/2026-10-24-diagnostics-by-surface.md) plan, then the CLI and preset tool | Same explanatory text on every surface, with a test per failure. |
| Before further substantial DSP changes, and any optimisation | **PERF-3:** resource budgets | Callback tail latency, peak memory and preparation time while loading, replacing and resetting, across realistic routing and callback sizes, define the supported workloads (jrp's review of `6920a45`: keep the core architecture and finish this first). PERF-2, FFT-2 and VST3-5, and any more complex partitioning, start only if PERF-3 shows a need; the loading-responsiveness gate uses its workloads. |
| Supporting, cheap | **VST3-9**, **FFMPEG-1**, **PERF-1**, **HOST-4** | VST3-9 affects only hosts running at a pull-down rate such as 47952 Hz, which the journeys do not use. It does not block the first preview; it moves up if a tester meets it. HOST-4's remaining check needs a Windows computer. PERF-1 reruns the room-correction benchmarks on Apple Silicon and Windows. |
| Parked | **FFMPEG-2 to FFMPEG-5**, **HOST-3** | Revisit only by an owner decision. |
| Optional editor work | **PLUG-11** (FFT planning in FFTW-built plug-ins), **LV2-6** (an LV2 window, only if Linux users ask) | Not needed for any preview. |
| On request or measured need | **HOST-2** (CLAP), **VST3-2**, **VST3-5**, **VST3-6**, **FFT-2**, **PERF-2**, **CFG-2** | Define the user need or measured bottleneck and acceptance tests first. CFG-2 waits for CFG-1. |

DOC-2 automation and the CFG-1, IO-1, IO-2, IO-5, GAIN-1 and ROBUST-1/2 items
remain open supporting work; take them up when a reproducible defect or the milestone
needs them. This table owns the current ordering; the dated
[21 October review order](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md#recommended-order)
is superseded.

## User experience and distribution

- [ ] **DOC-2 — Maintain documentation automatically.** Extend checks to build
  options and executable CLI examples in CI. Local Markdown links/anchors and
  checker regression tests now run through pre-commit; the separate
  `documentation` CTest protects the historical website. See the
  [check scope](docs/develop/documentation.md#automated-checks).
- [ ] **REL-1 — Qualify downloadable plug-ins.** The candidate workflow
  builds unsigned packages with pinned dependencies, corresponding source,
  staging, installers, artifact verification and promotion gates
  ([releasing](docs/develop/releasing.md)); PLUG-5 extends it to LV2 and AU.
  Still open, for the production gate: verified minimum OS and CPU versions,
  dependency closure, the final licence and corresponding-source review,
  signing and notarization, installation on clean machines, and immutable
  release assets ([production gates](docs/develop/releasing.md#production-gates)).
  The 18 October [distribution plan](docs/archive/plans/2026-10-18-plugin-distribution.md)
  is archived as superseded.
- [ ] **REL-2 — Define the first release by user journeys.** Write three
  journeys as numbered steps for a reader who has never seen the repository:
  stereo room correction and a 2→6 crossover in a plug-in host, and offline
  file rendering with the CLI, using the DOC-1 filters. Each journey covers
  download and checksum, installation, loading the filter, checking the
  expected output, saving and reopening, and recovering from a wrong preset
  (wrong rate or channel count), and states the expected result of every step.
  Saving and reopening and recovering from a wrong preset apply to the
  plug-in journeys; the CLI journey instead covers a filter that does not fit
  the input (wrong rate or channel count) and the message that explains it.
  Each journey must say how a user checks the output, for example the host's
  offline render of `input.wav` compared with `expected.wav` by a stated
  command, with a listening check only where no comparison is possible.
  Write them once in the user guides that ship in the package
  (`docs/use/`, starting from its two first-use tutorials), with per-platform
  differences marked, and make the
  package contain what they need: presets made from the stereo and 2→6
  filters, the preset tool and the `convolver` CLI, which the first preview
  ships; the package has no CLI yet (PLUG-5). See
  [status](docs/project/status.md#user-journeys-for-the-first-preview).
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
  [next milestone](#execution-priorities). jrp chooses the tester.
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
- [ ] **REL-5 — Early limited walkthrough on macOS.** Proposed in jrp's
  review of `6920a45` (31 October 2026): before the other platforms' windows
  are finished, someone who did not write the code or the guides follows the
  stereo room-correction journey (REL-2) on macOS from an exact candidate
  package (identified by its SHA-256 sums), in one named host, using only the
  documentation, and the record says plainly what it does not cover (the
  other journeys, platforms and formats). It looks for installation,
  documentation, loading and reopening problems early. Where the journey
  checks output, it does so by command (the host's render compared with
  `expected.wav`), not by ear or screen, as jrp prefers testing through the
  CLI where that makes sense. Record it as REL-3 says, and fix what it finds. It needs the stereo preset in the package
  (PLUG-5) and the stereo journey written (REL-2). It does not change what
  the first preview holds (jrp's
  [25 October decision](docs/project/decisions.md), confirmed on 31 October)
  and does not replace REL-3.
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
  Not yet run.
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
- [ ] **DIAG-1 — Review how diagnostics reach the user.** Do this once the
  current plug-in plans are implemented (see the
  [execution priorities](#execution-priorities)). For every failure a user can
  meet, check where the explanation is produced and where it actually appears,
  in the CLI, the preset tool and each plug-in format. The CLI prints
  explanatory messages on standard error (for example, a configuration that
  matches nothing lists each candidate and what to change), and
  `--inspect`/`--validate --json` report diagnostics as JSON. The plug-ins
  have no editor: the VST3 adapter keeps a structured reason for every
  failure (PLUG-8, done) but, before the progress below, showed it nowhere. It reports a preset with the
  wrong rate or channel counts, or with corrupt state, to the host only as
  `kResultFalse`, and a mismatch found at activation activates and plays
  silence with nothing reported. Because PLUG-8 keeps the reasons, this review
  decides presentation rather than recovering lost information. The
  [shared adapter contract](docs/develop/plugin-contract.md#loading-and-failure)
  asks for the message in the host's log and a "load failed" status, but only
  LV2 had a defined route (`log:log` and the status port); VST3 and AU have no
  host log interface, so they now use a Status parameter and the Convolver log
  (progress below); an editor is VST3-3/HOST-2. Make
  a table of failures (missing or unreadable filter, rate mismatch, channel
  count or layout mismatch, corrupt or newer state, failed preparation, failed
  activation) against surfaces (CLI standard error, exit status and JSON
  reports; preset tool; VST3 return codes and what named hosts show; LV2 log
  and status port; AU `OSStatus` and the macOS log). Then propose where each
  message should appear without an editor (for example a read-only status
  parameter, the host's log or the system log) for jrp to decide, using the
  same explanatory text everywhere. jrp prefers diagnostics that explain
  generously: what failed, why, and what to change, without needing the
  documentation. Add tests that each failure produces
  its message on each surface.
  Progress (23 October 2026, PR #57): the host-facing part was brought forward
  before the HOST-1 smoke tests. VST3 and
  AU have a read-only Status parameter and write every load, start and failure
  to the Convolver log ([status and log](docs/develop/plugin-contract.md#status-and-log)),
  tested per failure through each adapter. Progress (24 October 2026, PR #68, from
  jrp's review of 0.4.37): processing faults (rejected input, output
  overflow, processing failure, and invalid calls from Audio Unit hosts) are counted on the audio
  thread and shown as Status Fault, explained in the log on the control side;
  the log names the process, numbers requests and starts with what ran (FFT
  backend, CPU kernels); an unwritable log is said in the Status text;
  failed CI test jobs keep their evidence, and preview candidates keep
  matching debugging symbols. Progress (24 October 2026, PR #72, from jrp's
  review of 0.4.43): the candidate checks each kept symbol file's debugging
  information and its identity against the packaged binary (GNU build ID,
  Mach-O UUID, PDB GUID and age), tested by `release.symbols`.
  Progress (24 October 2026, PR #73, from jrp's review of 0.4.43): one fault
  snapshot when a load is accepted, so faults from before it are logged but no
  longer shown; Load failed stays ahead of Fault on every adapter's audio
  thread; the user guides point to the Status parameter and the log. The
  failure-by-surface table, CLI and preset tool included, is in the
  [diagnostics by surface](docs/project/plans/2026-10-24-diagnostics-by-surface.md)
  plan, with eight proposals for jrp.
  Remaining: jrp's choice among those proposals, their implementation and
  tests, and what named hosts actually show (HOST-1).

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
  Still needed for the first preview: the `convolver` CLI in the package (run
  by the same release tests); presets made from the stereo and 2→6 first-use
  filters, which the REL-2 journeys load; and the VST trademark notice that
  Steinberg's usage guidelines ask for (® on first use of "VST", the sentence
  "VST is a registered trademark of Steinberg Media Technologies GmbH." in the
  documentation and packages, and the VST Compatible logo where they require
  it; [VST3 API audit](docs/review-2026-10/vst3-api-audit-2026-10-23.md)), with
  wording jrp approves; and the Audio Unit component (AU-5), which is in the
  first preview. LV2 joins a later preview.
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
  as PLUG-10 adds them, and VST3's load message (VST3-3).
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
  and in the Audio Unit (AU-7). A new Latency takes effect at the host's next
  start of the plug-in ([settings](docs/develop/plugin-contract.md#settings));
  HOST-1 records what makes each host restart it. Remaining: the Estimate and
  No clipping gain modes and the meters.
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

LV2 (Linux first; also PipeWire system audio):

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
  a 0.6.x build is minor 6, but every odd patch number is a micro version
  LV2 reads as a development version.
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
  Remaining: recent files and reload (the editor plan's conveniences), the
  view shared with VST3-11, and these acceptance gates (jrp, 30 October 2026),
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
  log lines folded away; stack views instead of fixed frames. Every word
  comes from `host_report`, so VST3-11 to VST3-13 can show the same (on
  Linux the status symbol becomes a glyph and the palette is fixed).
  `au.editor` checks it on macOS CI. To check in GarageBand with the gates
  above: the new layout at both appearances, typing a value, Option-click,
  the disclosure, and VoiceOver reading the Audio facts and typed fields.
- [ ] **AU-7 — Settings as Audio Unit parameters.** PLUG-10's gain, gain
  mode, mix and latency as global parameters (IDs beside Status 1001 in
  `plugins/identities.json`), latency indexed and not automatable, with
  latency and tail change notifications ([editor plan](docs/project/plans/2026-10-24-plugin-editor.md#per-format)).
  Needed for the first preview.
  Progress (25 October 2026, PR #80): Gain (0) and Mix (1003), saved in
  `ClassInfo` ([settings](docs/use/plugins.md#settings)). Progress
  (31 October 2026, PR #95): Latency (1004), with the latency change
  notification and a menu in the window; a choice made while the unit runs
  waits for the next `Initialize`. Remaining, with PLUG-10's deferred parts:
  Gain mode (1002).

## FFmpeg integration

Parked: maintaining a patched FFmpeg source tree adds
uncomfortable coupling. FFMPEG-1 is still worth doing. If this is revisited,
prefer the pipe, a filter accepted upstream, or upstream state support in
FFmpeg's generic `lv2` filter (it cannot pass a file path today), which would let
the LV2 plug-in serve FFmpeg.

Design and evidence: [FFmpeg filter plan](docs/project/plans/2026-10-21-ffmpeg-filter.md)
(21 October 2026). FFmpeg cannot load filters at run time, so a built-in filter
means a patched FFmpeg build with `--enable-gpl`.

- [ ] **FFMPEG-1 — Test the pipe workflow.** The
  [FFmpeg pipe example](docs/use/command-line.md#with-ffmpeg) is documented and was
  originally run by hand for one stereo audio/video pair. Add a CI test when
  `ffmpeg` is installed, covering explicit track selection, 2-in/6-out routing,
  frame counts/tails and timestamp offsets. The revised commands are not new
  end-to-end validation evidence.
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
- [ ] **IO-1 — Large files.** Specify and test boundary sizes and pipe behavior
  for output beyond 4 GiB. Input decoder support is not a promise of strict
  validation for every container.
  Progress (23 October 2026, PR #60): RF64, W64, CAF, FLAC and raw output have
  no 4 GiB limit; WAV and AIFF stop with an error at it. Tests at the boundary
  sizes remain.
- [ ] **IO-5 — Speaker labels beyond WAV.** Carry `--speaker-map` labels into
  the channel layouts of the other containers (WAVE_FORMAT_EXTENSIBLE in RF64
  and W64, FLAC's `WAVEFORMATEXTENSIBLE_CHANNEL_MASK` tag, CAF and AIFF channel
  layouts). Check what libsndfile can write and what readers honour first; a
  rejected layout must fail as WAV does, not fall back to a default.
- [ ] **IO-2 — Optional offline IR resampling.** Only add as an explicit operation
  with tests for gain, delay, length and reported metadata; retain mismatch errors
  by default. Compare [libsamplerate](https://libsndfile.github.io/libsamplerate/)
  and other suitable implementations before choosing a dependency.
- [ ] **GAIN-1 — Define no-clip headroom precisely.** Test the exact full-scale
  endpoints with floating-point rounding, integer quantization, dither and noise
  shaping. The current filter bound assumes input magnitude at most 1 and does
  not reserve separate quantizer headroom; distinguish that from a limiter.
- [ ] **ROBUST-1 — Longer fuzz and transition stress.** Keep discovered inputs as
  regressions. Measure worst callback times and exercise repeated filter changes;
  allocation guards and average throughput are not a deadline guarantee.
- [ ] **ROBUST-2 — Bound report output.** Review traversal/report growth after the
  configuration-visit limit and diagnostic truncation, including huge invalid
  lists. Bound reporting work as well as file reads.

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
- [ ] **VST3-3 — Editor messaging between controller and processor.** Phase 1
  of the [editor plan](docs/project/plans/2026-10-24-plugin-editor.md#phases-and-todo-items), with no
  window yet: the processor sends the PLUG-9 report with the Status line, the
  controller keeps a copy for a view; a `ConvolverLoad` message carries a
  chosen path to the processor, which loads it as `setState` does
  (`HostFilterSource::file`) and the controller calls `setDirty`; Bypass edits
  go through the controller. Headless tests in `vst3.adapter` and
  `vst3.lifecycle`. `IDataExchangeHandler` could feed meters later. Plug-in
  snapshots for host browsers (`smtg_target_add_plugin_snapshots`) need the
  window first.
  Needed for the first preview.
- [ ] **VST3-4 — Gain, mix and latency parameters.** PLUG-10's settings as
  VST3 parameters ([editor plan](docs/project/plans/2026-10-24-plugin-editor.md#settings)): Gain (ID 0,
  VST2's attenuation ID, with a new range for jrp to approve), Gain mode, Mix
  tagged `kDryWetMix` through `IParameterFunctionName`, and Latency (not
  automatable), saved in the component state, with
  `restartComponent(kLatencyChanged)` when latency changes and tests in hosts
  that compensate it. The IDs (Gain on 0, the others from 1002) go in
  `plugins/identities.json`, `tests/identities_tests.py` and
  `docs/develop/plugin-identities.md`, which today keeps 0 to 2 free. A
  `kLowLatencyMode` switch only makes sense if PERF-2 provides a lower-latency
  mode. Needed for the first preview.
- [ ] **VST3-5 — Progress for slow filter preparation.** `setState` prepares the
  filter synchronously on the host's thread. Measure the worst case (PERF-3) for long,
  multichannel filters (including FFTW planning) and, if hosts stall noticeably,
  report it with `IProgress` (`AsyncStateRestoration`) or move preparation to a
  worker.
- [ ] **VST3-6 — Replacing the old VST2 plug-in.** If a migration requirement is
  defined (see limitations), declare the VST2 ID `'Conv'` in `moduleinfo.json`'s
  `Compatibility` section or `IPluginCompatibility`, and map old parameters with
  `IRemapParamID`. Old VST2 chunk state would still need a converter.
- [ ] **VST3-9 — Non-integer host rates.** `setupProcessing` refuses a rate that
  is not a whole number (for example a 0.1% pull-down rate), so the host reports
  a failed plug-in. Accept any finite rate in range and play silence with the
  reason (DIAG-1's status), as for any rate mismatch. jrp to decide whether a
  small tolerance counts as a match.
- [ ] **VST3-11 — Editor window on macOS.** An AppKit view behind `IPlugView`
  (`kPlatformTypeNSView`), shared with AU-6: status and full messages,
  active configuration, choose and reload a filter (with drag and drop,
  recent files and reload when the file changes), the settings (gain and its
  mode, mix, latency) with the output peak, clip count and load, copy
  diagnostics, show the log, bypass. Needed for the first preview. Opened in CI by `editorhost` and pluginval's GUI tests, with an
  accessibility test of names, roles and text; smoke steps in REAPER and
  Audacity, with VoiceOver ([editor plan](docs/project/plans/2026-10-24-plugin-editor.md)).
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
  justified.

### Loading responsiveness

An acceptance gate for the first preview (jrp's review of `6920a45`, 31
October 2026), not an optimisation item. Every adapter except LV2 reads and
prepares a filter on the thread that asks for it: VST3's `setState`, the
Audio Unit's `RestoreState` and, in PR #84, the Audio Unit window's
Choose Filter property call. Cached reports keep other readers from waiting,
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

## Developer tooling

- [ ] **CI-7 — Resolve shared SDKs once per workflow.** Each matrix job of the
  VST3, LV2, Audio Unit and candidate workflows resolves the newest releases
  itself (CI-6), so jobs of one run can in principle build against different
  releases, and every job repeats the lookups and downloads. Resolve once in a
  first job, pass its lock file to the matrix jobs (`sources.py --lock`), and
  keep the per-job summaries. Suggested in jrp's review of 0.5.2; no coverage
  may be lost. See
  [supporting libraries](docs/develop/build.md#supporting-libraries).
- [ ] **CI-8 — Keep evidence from advisory failures.** Test jobs keep their
  evidence (JUnit results, logs) only when a job fails, but advisory checks
  (the pluginval pre-release, the newest upstream lilv) report problems as
  warnings and leave the job green, so their evidence is not kept. Upload it
  when an advisory step reports a problem as well. Suggested in jrp's review
  of 0.5.2. See
  [pre-releases and newer upstream versions](docs/develop/build.md#pre-releases-and-newer-upstream-versions).

## Documented limitations

This inventory gathers constraints stated across the guides. Some are deliberate
API or safety policies, not defects. Change them only for an explicit need and
with compatibility tests; linked IDs point to open work above.

| Limitation | Current handling / follow-up | Authoritative detail |
| --- | --- | --- |
| Plug-in settings (gain, mix, latency) only in the Audio Unit so far; a preset from `convolver-preset` carries the filter only, and a state moved between formats keeps the filter, not the settings | The same project can play at another gain, mix or latency in another format; VST3-4 and LV2-7 add the settings there. | [Settings](docs/use/plugins.md#settings) |
| A new Audio Unit Latency takes effect only when the host next starts (initializes) the plug-in | The plug-in keeps and reports the latency it started with, and the window says which plays and which waits; switching while running would need a lock-free swap of the whole processor (PLUG-10). | [Latency and tail](docs/use/plugins.md#latency-and-tail) |
| No IR resampling or automatic channel adaptation for direct sound-file filters | Rate/count mismatches fail; use an explicit configuration. IO-2 covers resampling. | [Configuration](docs/use/configuration.md) |
| One VST3 input bus and one output bus, each 1–8 channels; no custom editor | Presets and generic host controls; layouts are checked with a notice only; an editor is HOST-2 and VST3-3, host switching VST3-2. | [VST3](docs/use/plugins.md#loading-a-filter) |
| No promised migration of old VST2 project state | New VST3 identity; define a migration requirement before implementing one (VST3-6). | [VST3](docs/use/plugins.md#identities) |
| No built-in device application or clock management | CLI pipes need a producer/consumer; VST3 relies on its host. Standalone device support requires a separate design. | [Concepts](docs/use/concepts.md#files-and-continuous-streams) |
| One FFT backend per binary; Ooura/Accelerate have no FFTW-style measured plans or wisdom | Intentional deployment choice. Assess alternatives under FFT-2; do not imply runtime fallback. | [FFT guide](docs/develop/fft-backends.md#selection-and-contract) |
| FFTW wisdom is CPU/build/size-sensitive and process-global | Retune when relevant inputs change; external planner users must coordinate. A successful import does not prove optimality. | [Planning](docs/use/command-line.md#fftw-plan-tuning) |
| Core processing uses interleaved float buffers that must not overlap | Use `HostProcessor` for planar/in-place float/double host buffers; internal DSP remains float. | [Engine API](docs/develop/library.md#engine-api) |
| Latency and filter replacement are not instantaneous | Core latency is B; VST3 is 256 frames. Replacement starts with fresh history and warms/crossfades; reset work scales with the block buffers and delay lines, not the filter length (PERF-5). | [Replacement](docs/develop/plugin-contract.md#replacement-and-threads) |
| Callback safety has a defined thread contract | One audio thread per instance; serialized control calls; teardown requires stopped audio. Stress/deadline evidence remains ROBUST-1. | [Implementation invariants](docs/develop/architecture.md#invariants-to-preserve) |
| Resource limits cap channels, paths, delays, text, IRs and state | Intentional bounds; assess any increase with memory/overflow tests. Report-size follow-up is ROBUST-2. | [Core limits](docs/develop/library.md#resource-limits), [tests](docs/develop/testing.md) |
| The configuration format is not identical across consumers | No external consumer has verified this corpus yet; CFG-1. Older bundled configs may need delay lines and portable paths. | [Compatibility matrix](docs/use/other-programs.md#what-convolver-accepts) |
| Path portability checks are partial | No drive/share remapping; a clean profile is not certification for Windows or another reader. Extend checks only with fixtures. | [Profiles](docs/use/other-programs.md#check-that-a-configuration-will-travel) |
| Output is WAV, RF64, W64, AIFF, CAF, FLAC or headerless raw; speaker labels and reduced valid bits are WAV-only | Input codecs and FLAC output depend on the libsndfile build; the preview packages have no FLAC or Ogg, by decision ([workarounds](docs/use/command-line.md#flac-and-ogg)). Boundary-size tests remain IO-1; labels in other containers are IO-5. RAW has no embedded rate/channel metadata. | [Output formats](docs/use/command-line.md#output-formats) |
| Output writes are not transactional | I/O/numeric failure may leave a partial file; consider temporary-file/atomic publication for regular-file renders if required. Never promise rollback for pipes. | [Output safety](docs/use/command-line.md#output-safety) |
| Gain estimation is not a limiter; float samples can exceed full scale | Integer samples clip; strict quantizer headroom is GAIN-1. | [Gain](docs/use/command-line.md#gain-and-attenuation) |
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
- [x] **CI-19 — Build the Apple silicon candidate on `macos-15`.** 31 October
  2026: GitHub retires the `macos-14` image on 2 November 2026, and its
  brownouts on 30 October cancelled the candidate job before it ran (PRs #84
  and #89, [runner-images#13518](https://github.com/actions/runner-images/issues/13518));
  the package still targets macOS 14; [candidate checks](docs/develop/testing.md#plug-in-candidate-checks).
- [x] **DOC-0 — Refresh the entry points for 0.4.0.** 20 October 2026;
  [documentation review](docs/archive/reviews/2026-10-20-documentation-review.md).
- [x] **DOC-1 — Runnable first-use examples.** 22 October 2026, PR #28;
  [first-use examples](examples/first-use/README.md).
- [x] **DOC-3 — Resolve adapter-plan review findings.** 21 October 2026;
  [review](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md#documentation-findings).
- [x] **DOC-4 — Organise the documentation by reader.** 23 October 2026,
  PRs #47, #51, #52 and #53; [documentation guide](docs/develop/documentation.md),
  [plan](docs/archive/plans/2026-10-23-documentation-by-reader.md).
- [x] **FFT-1 — Check that we have the latest Ooura version.** 21 October 2026;
  [provenance](portable/third_party/ooura/README.md#upstream-comparison-21-october-2026).
- [x] **FFT-3 — Accelerate by default on macOS.** 30 October 2026, PR #89: every
  macOS build, the command-line tools included, uses Accelerate unless built
  with `-DCONVOLVER_FFT_BACKEND=FFTW`;
  [which libraries each build uses](docs/develop/build.md#which-libraries-each-build-uses).
- [x] **IO-3 — More output containers.** 23 October 2026, PR #60;
  [output formats](docs/use/command-line.md#output-formats).
- [x] **IO-4 — Raw filters and raw sample types.** 24 October 2026, PR #60;
  `f32le` by default and reported, `--raw-filter-format` for others;
  [filter files](docs/use/configuration.md#filter-files),
  [RAW streams](docs/use/command-line.md#raw-streams).
- [x] **IO-7 — Dither defaults by output resolution.** 25 October 2026, PR #79;
  [integer output](docs/use/command-line.md#integer-formats-dither-and-noise-shaping).
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
- [x] **ROBUST-3 — Silence when memory runs out after a recorded failure.**
  31 October 2026, from jrp's review of `6920a45`: a failed first load
  silences the processor with `HostProcessor::silence()`, which cannot
  allocate or fail, in VST3, the Audio Unit and LV2; lifecycle sequence
  `state-malformed-starved`;
  [replacement and threads](docs/develop/plugin-contract.md#replacement-and-threads).
- [x] **ROBUST-4 — Low-memory quantizer and non-finite gain estimates.**
  24 October 2026, PR #72, from jrp's review of 0.4.43: `Quantizer` allocates
  in its constructor, so out of memory reaches the caller, and
  `estimate_attenuation` refuses a non-finite noise response; tests in
  `native.io` and `native.engine`; [gain](docs/use/command-line.md#gain-and-attenuation).
- [x] **VST3-1 — Speaker arrangements, not only channel counts.** 23 October
  2026, PR #42; [channel layouts](docs/develop/plugin-identities.md#channel-layouts).
- [x] **VST3-7 — Follow the shared adapter contract.** 22 October 2026, PR #35,
  with the reset follow-up in PR #40; [adapter contract](docs/develop/plugin-contract.md).
- [x] **VST3-8 — Follow the SDK documentation where the audit found gaps.**
  23 October 2026, PR #59, from the
  [VST3 API audit](docs/review-2026-10/vst3-api-audit-2026-10-23.md): tail
  includes the latency, input silence flags not trusted, Bypass text converts
  back, controller state calls succeed; [adapter contract](docs/develop/plugin-contract.md).
- [x] **VST3-10 — Mark silent output.** 24 October 2026, PR #70: an output
  channel is flagged silent exactly when a call left it all zero, so a tail
  that is still sounding is never flagged; [format notes](docs/develop/plugin-contract.md#format-notes).
