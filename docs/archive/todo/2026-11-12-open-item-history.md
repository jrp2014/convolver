<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Open TODO items as written before DOC-14

DOC-14 (12 November 2026, from jrp's review of the backlog) rewrote these
open items so that each says what remains, what would justify doing it and
what counts as finished, merged some, and ticked those finished for the
previews. Their earlier text, with the progress narratives and evidence it
recorded, is kept here exactly as it stood at `e849af5`; only relative links
are adjusted. Use [TODO](../../../TODO.md) for open work and the
[current documentation](../../README.md) for how Convolver behaves now.

## REL-1

- [ ] **REL-1 — Qualify downloadable plug-ins.** The candidate workflow
  builds unsigned packages with pinned dependencies, corresponding source,
  staging, installers, artifact verification and promotion gates
  ([releasing](../../../docs/develop/releasing.md)); PLUG-5 extends it to the Audio
  Unit (done for the preview, PR #115) and LV2 (a later preview).
  Still open, for the production gate: verified minimum OS and CPU versions,
  dependency closure, the final licence and corresponding-source review,
  signing and notarization, installation on clean machines, and immutable
  release assets ([production gates](../../../docs/develop/releasing.md#production-gates)).
  What macOS signing and notarization need, and jrp's steps, are in the
  [signing plan](../../../docs/project/plans/2026-11-09-macos-signing.md).
  The 18 October [distribution plan](../../../docs/archive/plans/2026-10-18-plugin-distribution.md)
  is archived as superseded.

## REL-3

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
  [next milestone](../../../TODO.md#execution-priorities). jrp chooses the tester.
  jrp decided (8 November 2026) to publish the first preview before this
  walkthrough, so it uses the published files and gates the next preview
  ([decisions](../../../docs/project/decisions.md#releases)). Record
  the evidence HOST-1 lists (package hash, host and OS versions, expected
  outputs, recovery from a refused filter) for each journey.
  jrp's review of 8 November 2026 adds the first thing to try: "The
  independent tester should try installing, loading their own filter,
  saving and reopening with only the supplied instructions."

## HOST-1

- [ ] **HOST-1 — Named host matrix.** Record versions and results for Windows/macOS
  hosts: scan, load, preset/state restore, silence/tails, seek, variable callbacks,
  bypass, filter changes, multiple instances and unequal channel counts. SDK
  validation and pluginval alone do not complete this item. Steinberg's free
  VST3PluginTestHost (Windows/macOS) is a convenient manual check without a DAW;
  the SDK's sample hosts were evaluated and not added to CI
  ([host qualification](../../../docs/develop/host-testing.md#steinbergs-sdk-hosts)).
  First step, before the first preview: jrp's smoke tests in REAPER and
  Audacity on his Mac, from the latest candidate
  ([smoke tests](../../../docs/develop/host-testing.md#smoke-test-in-each-first-host)).
  Progress (6 November 2026): a [48 kHz stereo listening clip](../../../examples/host-audio/README.md)
  and its generator are available for the manual checks. The clip does not
  replace the first-use numerical comparisons or establish a host result.
  The REAPER smoke test also takes over the host steps that REL-5 left out:
  install, render, save and reopen, and presets that do not fit, in
  [check a stereo filter in a host](../../../docs/use/check-a-stereo-filter.md),
  and the 2→6 crossover journey
  ([check a crossover in a host](../../../docs/use/check-a-crossover-in-a-host.md)),
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
  [record](../../../docs/archive/reviews/2026-11-06-host1-reaper.md)). The smoke steps, the stereo journey (render, save and
  reopen, presets that do not fit; every `--compare` a Match) and the
  window's settings passed. Two results are open
  ([unresolved host findings](../../../docs/develop/host-testing.md#unresolved-host-findings)):
  a new VST3 Latency was not used until the project was reopened, and
  REAPER gave the plug-in 6 inputs and 6 outputs, so the 2→6 crossover was
  refused and its render, reopen and refusal steps were not run. Audacity,
  and REAPER's display of the Status parameter, remain.
  Progress (7 November 2026, PR #131): with PLUG-23 the VST3 crossover
  passed part D in REAPER 7.85: render, Mix 0%, bypass, a refused six-input
  preset, the wrong rate, and (after a restore fix in the same PR) saving,
  quitting and reopening, and FX Offline then Online
  ([record](../../../docs/archive/reviews/2026-11-07-reaper-crossover-retest.md)).
  The Latency finding was fixed by VST3-16 (PR #134,
  [acceptance](../../../docs/archive/reviews/2026-11-08-vst3-16-reaper-acceptance.md)).
  Progress (7 November 2026, PR #137): jrp ran the Audacity steps
  (Audacity 4.0.2 arm64, macOS 27.2) with the VST3 plug-in from PR #131's
  candidate (0.10.13, run 42404299284: PR head `c3dbf0d`, built as merge
  `1b0d873`, so with the window from before PLUG-22; the record gives the
  plug-in executable's SHA-256, not the package's;
  [record](../../../docs/archive/reviews/2026-11-07-host1-audacity.md)). Audacity
  validated the plug-in; the export with no filter matched the original
  file, and the seven 48 kHz stereo exports with a filter or bypass matched
  the CLI's output with `convolver --compare` (worst error −123.67 dB
  relative to the output peak): identity, echo, repeat, echo applied from
  the Effect menu, host bypass, echo kept after a refused 44.1 kHz preset and a
  stop and start, and echo after saving, quitting, restarting Audacity and
  reopening. Three findings, now in the guides
  ([Audacity steps](../../../docs/develop/host-testing.md#audacity-vst3)):
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
  [preparation](../../../docs/develop/host-testing.md#prepare-the-mac) and saving
  an Audacity preset after **Choose…**; a mono track; a Latency change;
  a filter tail longer than the clip's silent ending (every export here
  ended in silence, so tails past the end of a clip are not qualified);
  VoiceOver and the window's layout (screen capture failed). REAPER's
  display of the Status parameter also remains.
  Before the first preview, every REAPER and Audacity step, including
  those that passed on earlier candidates, is run again on the preview
  candidate, so the earlier results are shown to hold for the current
  window and code: in REAPER that repeats the window and settings steps
  (part C) on the window PLUG-22 redesigned. Since jrp's choice of final
  candidate (8 November 2026), that means the v0.12.8 candidate, except
  REAPER steps 10 and 12, which run on the final candidate
  ([checks left](../../../docs/project/status.md#checks-left-before-publication)).
  Progress (8 November 2026): jrp ran REAPER part C with the 0.12.5
  candidate from `main` (run 42437291088, `4117a6d`, tagged `v0.12.5`;
  [record](../../../docs/archive/reviews/2026-11-08-host1-reaper-window.md)): on the
  redesigned window **Choose…**, Gain, Mix, undo, reset, the details and
  **Copy Diagnostics** worked, and save, quit and reopen kept the filter,
  Gain −6 dB, Mix 70% and Latency 1024; the VST3-16 latency renders lined
  up again. Listening and REAPER's display of the Status parameter were not
  checked. PLUG-31 changes the candidate; jrp decided these results stand
  for the final one. A packaged preset, and save and reopen, passed with
  the candidate from `f11a3dd` (PLUG-31 merged) on 8 November
  ([record](../../../docs/archive/reviews/2026-11-08-plug31-reaper.md)), and jrp
  decided those stand too, so part C needs only listening and the Status
  display, with the rest of the REAPER and Audacity steps
  ([checks left](../../../docs/project/status.md#checks-left-before-publication)).
  Progress (8 November 2026): jrp's checks of the v0.12.8 candidate (run
  42506723627; [record](../../../docs/archive/reviews/2026-11-08-v0128-host-checks.md)).
  REAPER: a render, save, quit and reopen, a refused wrong-rate preset and
  the host at 44.1 kHz then 48 kHz passed, each export compared with the
  CLI; REAPER's generic view lists Status after Bypass and it changed to
  Load failed with nothing else changing. After the refusal the window's
  heading changed from `impulse.wav` to "Restored filter" although the
  old filter kept playing; fixed on PR #156 at jrp's request, with a
  test of both orders of the two states. Audacity: a filter chosen with
  **Choose…** was not in the export (VST3-17); the window is shown and not
  clipped. Listening and VoiceOver were not checked in any host, and the
  rest of the Audacity steps remain.

## HOST-2

- [ ] **HOST-2 — Additional formats and UI.** CLAP only on request: every
  CLAP host found also loads VST3. An editor for all formats at once,
  now requested (progress below). Write adapters against the
  formats' own headers, not a framework.
  Progress (24 October 2026): jrp asked for an editor window, "not least for
  debugging", and that it configure the plug-in as the old Convolver did,
  as inspiration rather than a copy; the [editor plan](../../../docs/archive/plans/2026-10-24-plugin-editor.md) proposes native
  views (decided) over shared report code, with gain, latency and mix as host
  parameters, in phases PLUG-9, PLUG-10, PLUG-11, VST3-3, VST3-4, VST3-11 to
  VST3-13, AU-6, AU-7, LV2-6 and LV2-7, with decisions for jrp.
  Progress (25 October 2026): jrp's choices recorded; the first preview waits
  for the settings and the windows (VST3 on macOS, Windows, and Linux in
  VSTGUI as a stop-gap; the Audio Unit, which joins the first preview, with
  the shared macOS window); settings saved in each format's own way, with the
  divergence documented. Since narrowed: the first preview is macOS-only,
  and the Windows and Linux windows come in later previews
  ([decided](../../../docs/project/decisions.md#plug-in-behaviour)).
  Open risks carried from the archived editor plan's
  [risks](../../../docs/archive/plans/2026-10-24-plugin-editor.md#risks-and-open-questions)
  (8 November 2026), none of them a first-preview gate: Gain's Estimate mode adds
  part of a second to loading a long filter while it is chosen (about 0.6 s
  for a 10-second stereo filter, measured 12 November 2026; when Estimate is
  chosen for a filter already playing, the bound plays until the estimate
  is ready, PR #188); LV2's notify port needs the report's size declared up
  front (LV2-6); and typing in the window must not trigger the host's
  shortcuts, nor the host's keys reach the window.

## PLUG-5

- [ ] **PLUG-5 — One release pipeline.** REL-1 for all three formats,
  extending the existing pipeline (`packaging/release-policy.json`,
  `scripts/release/`, `.github/workflows/release.yml`) rather than writing a
  new one. Previews are unsigned on macOS (ad-hoc signature; the install guide
  explains removing the download quarantine) and Windows (SmartScreen warns),
  labelled as unsigned previews, and published by jrp from one candidate run
  ([releasing](../../../docs/develop/releasing.md)). Developer ID signing with
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
  ([trademarks](../../../packaging/trademarks/README.md)); and the Audio Unit in both
  macOS candidates (AU-5): `Components/Convolver.component`, a third
  installer package for `/Library/Audio/Plug-Ins/Components`, its debugging
  symbols, notices and SDK source, checked by `release.stage`,
  `verify_artifact.py`, `symbols.py` and, after installing the `.pkg` on the
  CI runner, `auval -strict` on the installed component; all of these first
  passed on both Macs in candidates [run 42108659224](https://github.com/jrp2014/convolver/actions/runs/42108659224) (`cdb99d6`)
  ([what a package holds](../../../docs/develop/releasing.md#what-a-package-holds)).
  Still needed for the first preview, all outside the pipeline: HOST-1 and
  AU-4 from one exact candidate, REL-3's walkthrough of its files, and jrp's
  publication. The logo in the window (an About box) is follow-up work,
  PLUG-20, not needed for the preview. Later: LV2 in
  a later preview (LV2-5), and Developer ID signing, notarization and
  Authenticode at the production gate (REL-1).

## PLUG-9

- [ ] **PLUG-9 — Editor report and loading from a file.** Phase 1 of the
  [editor plan](../../../docs/archive/plans/2026-10-24-plugin-editor.md#phases-and-todo-items), needing no window: a
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

## PLUG-10

- [ ] **PLUG-10 — Gain, mix and latency settings.** Phase 1 of the
  [editor plan](../../../docs/archive/plans/2026-10-24-plugin-editor.md#settings): in `HostProcessor`, an output gain
  with three modes (a set value, the noise estimate and the no-clip bound of
  `estimate_attenuation()`, computed during preparation), a
  latency-aligned wet/dry mix, and a chosen block size (64 to 4096 frames,
  default 256; since PERF-2, 0 or 64 to 4096, default 0), with 64-sample ramps; output peak, clip count and callback
  load as audio-thread counters; host state version 3 written only for
  non-default settings; each format saves the settings its own way, and
  where they can diverge (presets from `convolver-preset` carry the filter
  only; states moved between formats keep the filter, not the settings) is
  documented in `docs/use/plugins.md` and listed here as a limitation
  ([editor plan](../../../docs/archive/plans/2026-10-24-plugin-editor.md#settings)). Defaults leave the sound unchanged.
  Gain, Mix and Latency are in the first preview; the other gain modes and
  the meters are for a later preview. Oracle tests with the worst
  error relative to the peak; the contract change in its own pull request.
  Progress (25 October 2026, PR #80): Gain and Mix in `HostProcessor`, the
  contract's [settings](../../../docs/develop/plugin-contract.md#settings) and the
  divergence in [how the plug-ins behave](../../../docs/use/plugins.md#settings), with
  the Audio Unit's parameters (AU-7). Deferred to keep the first version
  simple (jrp, 25 October 2026): the Estimate and No clipping gain modes
  (IDs 1002 approved), the Latency choice (1004 approved; latency stays 256
  frames) and the meters; the first try at all three is in PR #80's history
  (commit 7f2ce96). Host state version 3, which only VST3 writes, lands with
  VST3-4.
  Progress (31 October 2026, PR #95): the Latency choice in `HostSettings`
  and `HostProcessor`, with oracle tests at each of the seven block sizes,
  and in the Audio Unit (AU-7). A new Latency plays at once, after a gap
  in the sound ([settings](../../../docs/develop/plugin-contract.md#settings)). Remaining: the
  Estimate and No clipping gain modes and the meters.
  Progress (12 November 2026, PR #188): the Gain mode (Set, Estimate, No
  clipping) in `HostSettings` and `HostProcessor`, each filter carrying its
  own figures, in VST3 (1002, host state version 4), the Audio Unit (1002)
  and LV2 (`gain_mode`), with a menu and the gain it plays in the macOS and
  Windows windows ([settings](../../../docs/develop/plugin-contract.md#settings)).
  The noise estimate is found only while Estimate is chosen, on the control
  side, so loads in the other modes cost nothing more; until it arrives
  Estimate plays the no-clipping figure. Not yet tried in a host.
  Remaining: the meters (output peak, clip count and callback load).

## LV2-3

- [ ] **LV2-3 — Layouts and validation.** Plug-in URIs for the PLUG-4 layouts;
  `lv2lint` and pluginval in CI.
  Progress (22 October 2026, PR #43): the eight discrete URIs and `lv2lint` in
  CI came with LV2-1; pluginval and named layouts remain.
  Progress (6 November 2026): pluginval validates each of the eight plug-ins
  at strictness 10 in the LV2 workflow
  ([pluginval](../../../docs/develop/testing.md#pluginval)); named layouts remain.

## LV2-4

- [ ] **LV2-4 — User guide and host matrix.** LV2 in `docs/use/plugins.md` and
  `docs/use/install.md` (written in DOC-4), plus a PipeWire configuration; Ardour, REAPER, Carla, Qtractor and PipeWire results.
  The guide should also say plainly what Convolver adds over PipeWire's
  built-in `convolver` node (`.cfg` files, routing matrices, delays, and
  choosing from a list by rate and channels), and can start from the full
  `filter-chain` example in the archived formats plan's
  [PipeWire section](../../../docs/archive/plans/2026-10-21-plugin-formats.md#system-wide-use-with-pipewire).
  Progress (12 November 2026): the PipeWire guide
  ([system-wide filtering with PipeWire](../../../docs/use/pipewire.md)), with what
  Convolver adds over PipeWire's own convolver; PipeWire 1.6.8 results in the
  [LV2 host matrix](../../../docs/develop/host-testing.md#lv2-hosts) (PipeWire before
  1.6 cannot pass the plug-in a filter). Jalv, Carla, REAPER and Qtractor
  results remain, and PipeWire on a desktop with a real sound card through
  its systemd user service (the `systemctl` restart, `journalctl` log and
  `~/.lv2` lookup in the guide were not exercised).

## LV2-5

- [ ] **LV2-5 — Release bundles.** Linux tarball, then distro packages; macOS and
  Windows bundles under PLUG-5.
  `VERSION` maps to `lv2:minorVersion` and `lv2:microVersion` by doubling
  (jrp, 10 November 2026, [decision](../../../docs/project/decisions.md#identities-and-file-formats)):
  minor 2 × (100 × MAJOR + MINOR), micro 2 × PATCH, so 0.13.6 is 26.12
  ([LV2 versions](../../../docs/develop/versioning.md#lv2-versions)).
  Whether to publish LV2 bundles for macOS and Windows at all is
  jrp's to decide; the archived
  [formats plan](../../../docs/archive/plans/2026-10-21-plugin-formats.md#decisions-for-the-owner)
  recommended building them in CI and publishing once Linux has users.
  Progress (10 November 2026, PR #179): the Linux package holds
  `LV2/convolver.lv2`, built against the lv2 headers in the corresponding
  source and checked by the candidate's LV2 tests and the package checks;
  the version mapping awaits jrp.

## AU-4

- [ ] **AU-4 — User guide and host matrix.** The Audio Unit in
  `docs/use/plugins.md` and `docs/use/install.md` (written in DOC-4); Logic, GarageBand,
  SoundSource, Live, REAPER and FL Studio results, including loading an
  `.aupreset` from a host's preset menu. Progress (23 October 2026, PR #56):
  GarageBand is jrp's first Audio Unit host, with a
  [smoke test](../../../docs/develop/host-testing.md#garageband-audio-unit) at
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
  [record](../../../docs/archive/reviews/2026-11-08-au4-garageband.md)): the presets,
  **Choose…** of a packaged `filter.cfg`, a refused 48 kHz preset that kept
  the filter, Gain, Mix and Latency kept after save and reopen, and two
  echo exports matching the CLI (worst error −112.99 dB; GarageBand's
  *Export projects at full volume* must be off for a comparison). Not
  checked: listening to the presets and to Gain and Mix, a click when
  bypassing, whether the track stays in time after a Latency change,
  VoiceOver and the Recent list. jrp decided these results stand after
  PLUG-31; a packaged preset, save and reopen and the unchecked steps
  remain, on the v0.12.8 candidate
  ([checks left](../../../docs/project/status.md#checks-left-before-publication)).
  Progress (8 November 2026, v0.12.8 candidate run 42506723627;
  [record](../../../docs/archive/reviews/2026-11-08-v0128-host-checks.md)): a
  packaged preset from GarageBand's menu, then save, quit and reopen,
  restored it and the export matched the CLI; **Reload** and **Recent**
  loaded the file again with its path kept. Listening, the bypass click,
  the in-time check after a Latency change and VoiceOver remain.

## AU-5

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

## AU-6

- [ ] **AU-6 — Editor window.** Custom properties (IDs from 64000) for the
  PLUG-9 report and a load request, with property-change events and a
  `ClassInfo` change after a load; then a Cocoa view factory
  (`kAudioUnitProperty_CocoaUI`) over the VST3-11 view, with class names that
  carry the version; `auval` lists the view; GarageBand smoke steps, including
  a `.cfg` chosen inside its sandbox ([editor plan](../../../docs/archive/plans/2026-10-24-plugin-editor.md#per-format)).
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
  [rule 2](../../../docs/archive/plans/2026-10-24-plugin-editor.md#threads)), but a
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
  own process (REAPER, for example), where it attaches to the host's window;
  and, from the archived editor plan's
  [risks](../../../docs/archive/plans/2026-10-24-plugin-editor.md#risks-and-open-questions),
  that the window's custom properties still arrive when a host runs the
  Audio Unit in another process (under Rosetta, for example).
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
    ([decisions](../../../docs/project/decisions.md#plug-in-behaviour)).
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

## AU-7

- [ ] **AU-7 — Settings as Audio Unit parameters.** PLUG-10's gain, gain
  mode, mix and latency as global parameters (IDs beside Status 1001 in
  `plugins/identities.json`), latency indexed and not automatable, with
  latency and tail change notifications ([editor plan](../../../docs/archive/plans/2026-10-24-plugin-editor.md#per-format)).
  Gain, Mix and Latency are in the first preview; Gain mode is for a later
  preview.
  Progress (25 October 2026, PR #80): Gain (0) and Mix (1003), saved in
  `ClassInfo` ([settings](../../../docs/use/plugins.md#settings)). Progress
  (31 October 2026, PR #95): Latency (1004), with the latency change
  notification and a menu in the window; a choice made while the unit runs
  plays at once (a new processor handed to Render). Not yet checked in
  GarageBand (AU-4's step 10). Progress (12 November 2026, PR #188): Gain
  mode (1002), with a menu in the window; built by macOS CI only, not yet
  tried in GarageBand. Remaining, with PLUG-10's: the meters.

## COMPAT-1

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
  the reported latency: bit-identical when the FFT backend, block size,
  kernel set (AVX2+FMA or baseline, PERF-4), compiler and its version,
  configuration and compiler flags are unchanged, otherwise within the PLUG-2
  oracle tolerance (1e-5, scaled by the peak of a file louder than full
  scale); a newer compiler may change the output within it
  ([decision](../../../docs/project/decisions.md#releases)). Never regenerate a retained file; a deliberate
  incompatibility is jrp's decision, recorded in the
  [decisions log](../../../docs/project/decisions.md). The literal
  version-1 and version-2 state bytes in `native.state` stay as the first
  fixtures. Add the capture step to the
  [publication steps](../../../docs/develop/releasing.md#publishing-an-unsigned-preview)
  when this is implemented. The four guarantees it protects are listed in
  [status](../../../docs/project/status.md#compatibility-guarantees).
  Progress (9 November 2026, PR #170): 0.13.2 is kept in
  [`tests/upgrade/0.13.2/`](../../../tests/upgrade/README.md): its 21 example presets
  and their filters, byte for byte from the Linux ZIP (the same bytes as
  macOS), with inputs and the audio its own CLI and VST3 plug-in rendered
  (identical to each other). `upgrade.cli` and `vst3.upgrade` check every
  later build; the capture step is in the publication steps. CI's GCC 15
  build, with the same backend and kernels, differed from the release's
  GCC 11 by up to 1.3e-7, so the criterion above now names the compiler,
  configuration and flags too; the runner reports each check's largest
  difference as a share of its bound.
  Progress (12 November 2026, PR #187): the published 0.13.12 is kept in
  `tests/upgrade/0.13.12/`, captured the same way from its Linux ZIP. Every
  preset, filter, input and expected render is byte for byte the same as
  0.13.2's, so 0.13.12 renders those presets exactly as 0.13.2 did; only
  the manifest differs. Remaining: a REAPER project saved with 0.13.12, the
  baseline since 11 November, with its state chunk restored through each
  adapter, and Audio Unit state from a release.

## IO-1

- [ ] **IO-1 — Large files.** Specify and test boundary sizes and pipe behavior
  for output beyond 4 GiB. Input decoder support is not a promise of strict
  validation for every container.
  Progress (23 October 2026, PR #60): RF64, W64, CAF, FLAC and raw output have
  no 4 GiB limit; WAV and AIFF stop with an error at it. Tests at the boundary
  sizes remain.

## IO-2

- [ ] **IO-2 — Optional offline IR resampling.** Only add as an explicit operation
  with tests for gain, delay, length and reported metadata; retain mismatch errors
  by default. Users cite missing rates, and resampled responses that changed
  level or clipped elsewhere; rank 1 of the [edges plan](../../../docs/project/plans/2026-11-06-edges-over-other-convolvers.md#1-a-filter-for-every-sample-rate),
  with EDGE-1. jrp decides whether Convolver resamples at all. Compare [libsamplerate](https://libsndfile.github.io/libsamplerate/)
  and other suitable implementations before choosing a dependency.

## VST3-11

- [ ] **VST3-11 — Editor window on macOS.** An AppKit view behind `IPlugView`
  (`kPlatformTypeNSView`), shared with AU-6: status and full messages,
  active configuration, choose and reload a filter (with drag and drop,
  recent files and reload when the file changes), the settings (gain and its
  mode, mix, latency) with the output peak, clip count and load, copy
  diagnostics, show the log, bypass. Needed for the first preview. Opened in CI by `editorhost` and pluginval's GUI tests, with an
  accessibility test of names, roles and text; smoke steps in REAPER and
  Audacity, with VoiceOver ([editor plan](../../../docs/archive/plans/2026-10-24-plugin-editor.md)).
  Progress (1 November 2026, PR #103): the Audio Unit's window, moved to
  `plugins/editor/mac/`, shown through the SDK's `EditorView` for
  `kPlatformTypeNSView`, with settings edited as `beginEdit`, `performEdit`
  and `endEdit` (a drag is one edit), a Bypass checkbox editing the bypass
  parameter, files loaded through VST3-3's message (with the Recent menu,
  Reload and reloading when the file changes, shared with the Audio Unit)
  and the window resizing through `IPlugFrame::resizeView`; checked off
  screen by `vst3.editor` on macOS, and opened by pluginval's editor tests
  on macOS CI ([format notes](../../../docs/develop/plugin-contract.md#format-notes)).
  `editorhost` is not used: it is an interactive program that runs until its
  window is closed, so pluginval's editor tests stand in for it. Remaining:
  the steps in REAPER and Audacity ([host testing](../../../docs/develop/host-testing.md#reaper-vst3))
  and VoiceOver for the first preview; the meters (with PLUG-10) for a later
  preview.

## VST3-17

- [ ] **VST3-17 — Audacity keeps a filter chosen in the window.** Found
  in jrp's v0.12.8 checks (8 November 2026,
  [record](../../../docs/archive/reviews/2026-11-08-v0128-host-checks.md)): in
  Audacity 4.0.2 a `.cfg` chosen with **Choose…** played in the window's
  copy, but the export and the saved project had no filter (the export
  matched the dry input). Audacity 4 stores a VST3 plug-in's state only at
  the end of a parameter edit (`endEdit` then `flushAndStoreSettings` in
  its `au3/libraries/au3-vst3`); its component handler offers no
  `IComponentHandler2`, so `setDirty`, which Convolver calls after an
  accepted load, does not reach it, and its `restartComponent` does
  nothing. Presets from Audacity's own menu worked (7 November), since
  Audacity loads that state itself. Wrong audio in a required journey, so
  a preview blocker under the
  [acceptance plan](../../../docs/develop/releasing.md#what-blocks-a-preview)
  unless jrp narrows it. The workaround proposed: after an accepted load,
  the controller makes a gesture that changes no value (`beginEdit`,
  `performEdit` with the current value, `endEdit`) so such hosts store the
  state; check its side effects in REAPER (an undo step) and its effect in
  Audacity (export and reopen). jrp chose (8 November 2026, decision card)
  to fix it before the final tag, then set the terms: "hearing one result
  and exporting another is a significant correctness problem"; confirm the
  mechanism once (Choose, a genuine Gain edit, then export and save and
  reopen); pursue the host fix with Audacity; keep any Convolver workaround
  narrow, without manufactured Gain or Mix edits that could affect
  automation or undo; correct the documentation now (DOC-10); and make the
  regression check Choose, export, save and reopen with no parameter
  touched. Audacity's vendor-window path may store only a parameter's
  change at `endEdit`, not the whole component state, so the workaround
  waits for jrp's test. Draft upstream issue for jrp to post: project
  files `audacity-upstream/`.
  **Recent**, **Reload** and reloading when the file changes go through
  the same `Controller::load_file` as **Choose…**, so they share the
  problem; what the export keeps is whatever Audacity stored last, which
  may be an older filter rather than none. jrp, later on 8 November: fix
  and retest it, or explicitly narrow the preview's supported Audacity
  workflow and verify that alternative; defer implementation changes that
  do not prevent the walkthrough. jrp then chose (decision card) to narrow
  Audacity to presets for the first preview
  ([decisions](../../../docs/project/decisions.md#plug-in-behaviour)): the item
  stays open for after the preview, or for Audacity's own fix.
  Progress (9 November 2026): jrp chose to fix it now, despite the
  runtime freeze, as lost state. Audacity 4.0.2's source
  (`src/effects/vst/view/vstviewmodel.cpp`, `au3-vst3/VST3Wrapper.cpp`)
  reads as though, while its vendor window is open, it stores the whole state,
  both `getState` calls, within 100 ms of any `performEdit` when not
  playing; it also stores it when playback stops and when the window
  closes, so a check must export with the window open and without
  playing after the choice, or it can pass without the fix. So after an
  accepted file, a controller whose `setDirty` does not succeed (no
  `IComponentHandler2`, or a host that answers otherwise) reports one edit
  of Latency that keeps its value: not automatable, ignored by
  `process()`, and `endEdit` in Audacity only marks the project unsaved
  and amends its current undo state. No Gain or Mix edit is made, and a
  host whose `setDirty` succeeds sees nothing new (`vst3.adapter` checks
  all three cases). The independent review (PR #177) noted that Audacity
  amends its undo state about 100 ms before it stores the new state, as
  for any edit, so undoing a later action may bring back the previous
  filter; worth one look in the host check. jrp chose on 10 November 2026
  (decision card, "Merge #177 untested, then release") to merge the fix
  without an Audacity check; the guides now say the window route should
  work but is not yet checked in Audacity, and keep the preset menu as the
  checked route. Remaining: jrp's Audacity check (project files
  `vst3-17-audacity/`: Choose, export, save and reopen with no parameter
  touched and the window open, with 0.13.2 as a control that fails), then
  the guides drop the "not yet checked" wording; and a REAPER check that a
  chosen file adds no Latency entry to its undo history.

## VST3-12

- [ ] **VST3-12 — Editor window on Windows.** The same window as a Win32 view
  (`kPlatformTypeHWND`), with `IPlugViewContentScaleSupport` and keyboard
  navigation; checked with NVDA or Narrator.
  For a later preview: the first preview is macOS-only ([decided](../../../docs/project/decisions.md#plug-in-behaviour)).
  Built next among the windows, after PLUG-34; its first part gives the Windows
  and Linux plug-in a sound-file reader (libsndfile, if jrp agrees)
  ([decided](../../../docs/project/decisions.md#direction); [windows plan](../../../docs/project/plans/2026-11-10-windows-linux-plugin-windows.md#steps)).
  Progress (10 November 2026, PR #181): the plug-in reads filter files with
  libsndfile on Windows and Linux (pending jrp's choice of reader), and the
  Win32 window is built (`plugins/vst3/editor_win.cpp`) with `vst3.editor`
  on Windows and pluginval's editor tests there; it ran under Wine before
  Windows CI. Left: a tester in REAPER and one other Windows host, and a
  screen reader (NVDA or Narrator); the release notes' Windows window
  limitation goes with the next preview's notes change.

## VST3-13

- [ ] **VST3-13 — Editor window on Linux.** The window in VSTGUI, for the
  Linux VST3 build only, as a stop-gap, driven by the
  host's `IRunLoop`; VSTGUI (BSD-3-Clause) and the libraries it links added
  to the package checks and licence notices; opened by `editorhost` under
  `xvfb-run`. Not reachable by screen readers, which the guide says. A native
  X11 view can replace it later if Linux users need one
  ([editor plan](../../../docs/archive/plans/2026-10-24-plugin-editor.md#framework-choice)).
  For a later preview: the first preview is macOS-only ([decided](../../../docs/project/decisions.md#plug-in-behaviour)).
  Waits until LV2 is in a Linux package and Linux VST3 users ask for a window
  ([decided](../../../docs/project/decisions.md#direction); [windows plan](../../../docs/project/plans/2026-11-10-windows-linux-plugin-windows.md)).

## PERF-1

- [ ] **PERF-1 — Reproducible current benchmarks.** Rerun workloads on named target
  machines after substantial engine changes; record commit, compiler, dependency
  revisions, plans, block size and numerical error. Keep dated results labelled.
  The [8 November PERF-2 backend comparison](../../../docs/develop/fft-measurements.md#perf-2-backend-and-wisdom-comparison)
  measures Accelerate, native NEON FFTW estimation and saved patient wisdom
  on an M5 Max; a separate paired run records Ooura
  [costs as a percentage of one core](../../../docs/develop/fft-measurements.md#ooura-with-the-nonuniform-engine).
  These records include timing after warm-up and callback counts, but
  do not establish threaded FFTW performance, cold plug-in loading times
  or dropout-free host playback; pursue these only for a demonstrated need.
  Plug-in workloads (6- and 8-channel filters, callback-time distributions)
  belong to PERF-3. On the 19 October cloud VM, a
  65536-tap stereo filter at B = 256 took about 0.6 µs per frame (about 3% of
  one core at 48 kHz) and B = 1024 was about 3.5 times cheaper; a 6×6 matrix of
  16384-tap filters (36 paths) took about 2.9 µs per frame (about 14%).
  PERF-4 measured compiler flags on x86-64 Linux only; rerun the
  room-correction cases of `convolver_benchmark` (cases 10 to 18) on Apple
  Silicon (AppleClang, Accelerate) and Windows (MSVC, Ooura) before choosing
  flags for those builds.

## PERF-3

- [ ] **PERF-3 — Resource budgets and supported workloads.** Measure, on
  named machines, the distribution of callback durations (median, 99th
  percentile, 99.9th percentile and maximum, not only the average), memory in
  use and filter-preparation time (including FFTW planning where used) for
  representative long filters and 6- and 8-channel routing at 48 and 96 kHz,
  at the host block sizes users meet. Include the costly moments: a live
  replacement, while two engines run together, and a reset, whose work grows
  with stored history. Record the results as dated measurements in the
  [FFT guide](../../../docs/develop/fft-backends.md) and turn them into stated supported
  workloads (for example "8 channels of 65536 taps at 96 kHz uses under N% of
  one core and M MB") in the user guides. Only these results decide whether
  partitioning (PERF-2), memory layout or another FFT (FFT-2) deserves work,
  and whether slow preparation needs VST3-5. Include long reverbs ([edges plan](../../../docs/project/plans/2026-11-06-edges-over-other-convolvers.md#3-long-reverbs-at-low-cpu-and-latency)):
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
  [startup, reset and loading](../../../docs/develop/fft-backends.md#startup-reset-and-filter-loading).
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

## EDGE-1

- [ ] **EDGE-1 — A filter for every sample rate in one plug-in state.**
  Done when a list's entries that fit the host's channels are kept in the
  saved state, the entry for the host's rate plays and a rate change
  switches with the usual crossfade; a rate no entry has is refused as now;
  states over the saved-state limit are refused with a clear message.
  Needs jrp's decision on the state format and the preset tool's options.
  [Plan](../../../docs/project/plans/2026-11-06-edges-over-other-convolvers.md#1-a-filter-for-every-sample-rate). Effort M.

## EDGE-5

- [ ] **EDGE-5 — Publish the workloads and the evidence.** Done when the
  user guides state PERF-3's supported plug-in workloads (CPU, memory and
  saved-state size) beside the open convolvers measured on the same
  machines, and a `docs/use/` page shows a null test with `--compare`
  against a shipped reference render. jrp approves the claims. Effort S
  for the page, M for the comparison.

## EDGE-10

- [ ] **EDGE-10 — Sound-aware and platform-aware optimisation.** Done when
  each [candidate](../../../docs/project/plans/2026-11-06-edges-over-other-convolvers.md#sound-aware-and-platform-aware-optimisation) has
  been benchmarked on PERF-3's long-reverb workloads against the gain the
  plan states for it, and either adopted as its own item or stopped and set
  aside with its numbers. Exact and rounding-only
  changes keep the oracle and divergence bounds; a bounded approximation
  needs jrp's decision on its bound. OpenMP only where it causes no
  cross-platform incompatibility (offline paths, optional, serial build
  byte-identical); `libomp` ships on macOS only for a meaningful gain there,
  measured beside `dispatch_apply`. Explicit NEON or vDSP kernels only with
  a new workload, hardware target or hypothesis, measured against the
  [24 October 2026 vDSP experiment](../../../docs/archive/reviews/2026-10-24-apple-vdsp.md).
  Goes with PERF-2. Effort M for the survey.

## EDGE-11

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
  [Plan](../../../docs/project/plans/2026-11-06-edges-over-other-convolvers.md#2-savings-in-todays-engine), rank 2. Effort S to M each.

## CI-34

- [ ] **CI-34 — Reduce duplicate CI scheduling, preserving coverage.** A
  separate proposed PR, following the
  [CI plan](../../../docs/project/plans/2026-11-08-ci-scheduling.md). Inventory unique
  coverage before narrowing candidate and fuzz triggers; retain oracle,
  regression, validator, platform and release qualification checks. Done
  when scope tests and affected CI pass, every configuration has a retained
  trigger, and matched runs demonstrate waiting-time savings. No workflow
  changes are part of REL-7.
  Progress (8 November 2026): jrp approved the first step, now done: a pull
  request builds the candidates only for release files, packaged pages (a
  `docs/use/` guide only with runnable commands or images), the macOS
  window, the build configuration and shipped dependencies, or when started
  by hand; `main`, the weekly run and manual runs build them every
  time ([CI triggers](../../../docs/develop/testing.md#when-pull-requests-build-the-candidates)).
  Remaining: the fuzz triggers, the coverage table and matched runs.

## CI-32

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
