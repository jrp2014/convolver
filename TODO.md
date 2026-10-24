<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver TODO

Open work on the active portable implementation, with a stable ID for each
item. This is the shared backlog, not a promise that every idea will be
implemented. Finished items are listed briefly under [done](#done); what they
delivered is described in the guides and the [CHANGELOG](CHANGELOG.md).

**In short:** the next milestone is an independent user running an exact
preview package ([order of work](#execution-priorities)); [status](docs/project/status.md)
shows what users can run now, and [decisions](docs/project/decisions.md) what
jrp has decided. The open themes are user journeys, release qualification and
host testing (REL, HOST), the VST3, LV2 and AU plug-ins (PLUG, VST3, LV2, AU),
diagnostics (DIAG), documentation (DOC), files kept from releases (COMPAT), a
parked FFmpeg filter (FFMPEG), other configuration readers (CFG), large files
and resampling (IO), no-clip headroom (GAIN), stress testing (ROBUST), and FFT
or performance work only where measurements justify it (FFT, PERF). The
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
regressions take priority whenever discovered. Record ownership before starting
so independent tasks do not duplicate changes.

Direction (owner's decision, 21 October 2026): VST3, LV2 and AU are equal first
priorities, developed in parallel after shared groundwork. The FFmpeg filter and
Windows-native adapters are parked; CLAP and an editor wait for a request. See
the [plug-in formats plan](docs/project/plans/2026-10-21-plugin-formats.md).

**Next milestone** (jrp's [guidance](docs/review-2026-10/release-readiness-guidance-2026-10-22.md),
22 October 2026): an independent user successfully installs and uses an exact
preview package, with the lifecycle regression fixed and the advertised
workflows demonstrated. "Ready" means the user journeys of REL-2 work, not
that a set of items is ticked. The blockers and their evidence are listed in
[status](docs/project/status.md#next-milestone-an-independent-user-runs-a-preview).
Finishing a dependable product now matters more than another substantial
refactor: move code into shared layers only when two formats need the same
behaviour (as with `HostFilter`), do not generalise host lifecycle or release
machinery beyond the requirements in hand, and keep libsndfile at the I/O
boundary and C++20 in the core.

| Order | Work | Exit criterion / dependency |
| --- | --- | --- |
| 1. In parallel | **VST3** (HOST-1), **LV2-3 and LV2-4**, **AU-2 to AU-4** | Each stream passes the PLUG-2 comparisons and follows the [shared adapter contract](docs/develop/plugin-contract.md); LV2-2 and AU-2 load the presets `convolver-preset` writes. |
| 2. Next milestone | jrp's conditions: **REL-2** journeys demonstrated and **REL-3** an independent user with an exact **PLUG-5** preview package (the third, the reset fix, is done). Proposed with them: a **HOST-1** smoke test (PLUG-7, the lifecycle sequences, is done) | The first preview holds VST3, the `convolver` CLI and the preset tool; LV2 and AU join later previews once their own journeys are demonstrated (owner's decision, 23 October 2026). |
| 3. Release | **PLUG-5 / REL-1**, **LV2-5**, **AU-5**, **COMPAT-1** at each publication, then **REL-4** | First releases are unsigned previews on macOS and Windows (owner's decision, 21 October 2026), published by jrp from one candidate run; see PLUG-5. Signing is not a prerequisite for them. Developer ID signing, notarization and Authenticode remain the later production gate. Each preview repeats the REL-2 journeys; from the second, the COMPAT-1 upgrade tests too. |
| 4. After the plug-in streams | **DIAG-1:** diagnostics review | Starts once the plug-in streams are implemented, so every surface that can report an error exists. The reasons are already kept (PLUG-8). The plug-ins' Status parameter and Convolver log came first, before the host smoke tests (owner's decision, 23 October 2026); the rest of the review remains. |
| Before any optimisation | **PERF-3:** resource budgets | Callback-time distributions, memory and preparation time for long and 6/8-channel filters, including replacement and reset, define the supported workloads. PERF-2, FFT-2 and VST3-5 start only if PERF-3 shows a need. |
| Supporting, cheap | **FFMPEG-1**, **PERF-1**, **HOST-4** | FFMPEG-1 reuses DOC-1 fixtures. HOST-4 (Equalizer APO export, system-wide on Windows; owner chose it on 22 October 2026 over the Media Foundation transform) runs in Equalizer APO's own filter code on Windows CI; a manual check on a real device remains. PERF-1 measures 6- and 8-channel room-correction filters on named machines and keeps the throughput benchmarks reproducible; PERF-3, not PERF-1, decides whether PERF-2 is needed. |
| Parked | **FFMPEG-2 to FFMPEG-5**, **HOST-3** | Revisit only by an owner decision. FFmpeg routes that avoid a patched FFmpeg are preferred; Windows-native needs more thought first. |
| On request or measured need | **HOST-2** (CLAP, editor), **VST3-2 to VST3-6**, **FFT-2**, **PERF-2** | Define the user need or measured bottleneck and acceptance tests first. |

DOC-2 automation and the CFG/IO/GAIN/ROBUST items remain open supporting work;
prioritize them when a reproducible defect or a selected milestone requires them.
This table owns the current ordering. The earlier
[21 October review order](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md#recommended-order),
which put FFmpeg next, is kept as a dated record and is superseded here.

## User experience and distribution

- [ ] **DOC-2 — Maintain documentation automatically.** Extend checks to build
  options and executable CLI examples in CI. Local Markdown links/anchors and
  checker regression tests now run through pre-commit; the separate
  `documentation` CTest protects the historical website. See the
  [check scope](docs/develop/documentation.md#automated-checks).
- [ ] **REL-1 — Qualify downloadable plug-ins.** The VST3 preview pipeline from
  `codex/plugin-distribution` was merged on 21 October 2026: pinned dependencies
  and corresponding source, staging, unsigned installers, artifact verification,
  promotion gates and the candidate workflow
  ([record](docs/develop/releasing.md), [guide](docs/use/README.md)).
  PLUG-5 extends it to LV2 and AU. Still open: track minimum OS/CPU versions,
  dependency closure, licences/corresponding source, signing/notarization, clean
  machine installation and immutable release assets. Start with the
  [distribution plan](docs/project/plans/2026-10-18-plugin-distribution.md).
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
  ships (owner's decision, 23 October 2026); the package has no CLI yet (PLUG-5). See
  [status](docs/project/status.md#user-journeys-for-the-first-preview).
- [ ] **REL-3 — Independent walkthrough of an exact preview.** Someone who
  did not write the code or the guides follows the REL-2 journeys from the
  exact preview files (identified by their SHA-256 sums), on their own
  machine, using only the documentation. The tester uses the chosen
  candidate run's files before they are uploaded, so the pre-release can
  describe what was demonstrated (owner's decision, 23 October 2026); the
  tester is still to be chosen. Record who (a name or "independent
  tester"), the package hash, OS, host and version, and for each step whether
  it worked, where they hesitated and what they could not do without help.
  Fix the guides or the product for each finding and repeat the affected
  steps. This is the exit criterion of the
  [next milestone](#execution-priorities). jrp chooses the tester.
- [ ] **REL-4 — Make the preview findable.** Room-correction users and the
  `.cfg` format's existing users should find Convolver from search, the old
  SourceForge site, KVR and their forums. On 23 October 2026 the repository had
  no topics, releases, website link or custom social preview, and a description
  in the 2006 wording (GitHub API); nothing on the SourceForge project page or
  the old site pointed here, although Roon's convolution help page links to the
  old site's format pages (checked that day). Repository side
  (jrp chose this scope on 24 October 2026): a README line for users of the
  old Windows Convolver, issue forms, the
  [social preview source](.github/social-preview.html) and the
  [steps after publishing](docs/develop/releasing.md#after-publishing-make-it-findable).
  Still for jrp, now: the repository description, topics and Website field,
  uploading the social preview, and a line pointing here at the top of the old
  SourceForge pages and project summary. At each preview: the steps after
  publishing.
- [ ] **HOST-1 — Named host matrix.** Record versions and results for Windows/macOS
  hosts: scan, load, preset/state restore, silence/tails, seek, variable callbacks,
  bypass, filter changes, multiple instances and unequal channel counts. SDK
  validation and pluginval alone do not complete this item. Steinberg's free
  VST3PluginTestHost (Windows/macOS) is a convenient manual check without a DAW.
  Progress (22 October 2026): REAPER and Carla are the suggested first hosts
  (jrp to try); the SDK sample hosts were evaluated and not added to CI
  ([host qualification](docs/develop/host-testing.md#steinbergs-sdk-hosts)).
  Progress (23 October 2026, PR #56): jrp's first hosts, on his Mac, are
  REAPER and Audacity for VST3 (owner's decision, 23 October 2026, replacing REAPER and
  Carla); each has a step-by-step
  [smoke test](docs/develop/host-testing.md#smoke-test-in-each-first-host).
  Not yet run.
- [ ] **HOST-2 — Additional formats and UI.** LV2 and AU are now first
  priorities alongside VST3 (see [plug-ins](#plug-ins-vst3-lv2-and-au) below).
  CLAP only on request: every CLAP host found also loads VST3. A filter-loading
  editor only for an identified need, then for all formats at once. Write
  adapters against the formats' own headers, not a framework.
- [ ] **HOST-3 — Windows Media Foundation transform (parked).** Needs more
  thought before any work starts (owner, 21 October 2026). Wrap the engine as a
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
  documentation (owner, 21 October 2026). Add tests that each failure produces
  its message on each surface.
  Progress (23 October 2026, PR #57): the host-facing part was brought forward
  before the HOST-1 smoke tests (owner's decision, 23 October 2026). VST3 and
  AU have a read-only Status parameter and write every load, start and failure
  to the Convolver log ([status and log](docs/develop/plugin-contract.md#status-and-log)),
  tested per failure through each adapter. Progress (24 October 2026, PR #68, from
  jrp's review of 0.4.37): processing faults (rejected input, output
  overflow, processing failure, and invalid calls from Audio Unit hosts) are counted on the audio
  thread and shown as Status Fault, explained in the log on the control side;
  the log names the process, numbers requests and starts with what ran (FFT
  backend, CPU kernels); an unwritable log is said in the Status text;
  failed CI test jobs keep their evidence, and preview candidates keep
  matching debugging symbols.
  Remaining: the failure-by-surface table, the CLI and preset tool, and what
  named hosts actually show (HOST-1).

## Plug-ins: VST3, LV2 and AU

VST3, LV2 and AU are developed in parallel (owner's decision, 21 October
2026). Design, host evidence and framework choice:
[plug-in formats plan](docs/project/plans/2026-10-21-plugin-formats.md). Each
adapter is written against its format's own headers over `HostProcessor` and
follows the [shared adapter contract](docs/develop/plugin-contract.md). VST3
work continues under VST3-2 to VST3-6, HOST-1 and REL-1.

Shared work, done once for all three formats:

- [ ] **PLUG-5 — One release pipeline.** REL-1 for all three formats. Extend the
  merged VST3 pipeline (`packaging/release-policy.json`, `scripts/release/`,
  `.github/workflows/release.yml`) rather than writing a new one. Signing
  (owner's decision, 21 October 2026): start unsigned on macOS and Windows.
  Mac bundles still carry an ad-hoc signature (Apple Silicon will not load
  unsigned code) and the install guide explains removing the download
  quarantine; Windows installers show a SmartScreen warning. Label these
  releases as unsigned previews. The preview gate and the production gate are
  separate, and publication is a manual step owned by jrp
  ([releasing](docs/develop/releasing.md)). Developer ID signing with notarization
  (shared by VST3 and AU), and Authenticode, come later as the production gate;
  the draft signing scripts are kept in history at `f4519022c` (the last commit of
  the merged `codex/plugin-distribution` branch): `git show
  f4519022c:scripts/release/sign-macos.sh` and `sign-windows.ps1`.
  Progress 22 October 2026 (PR #38): preview packages ship `convolver-preset`
  and `convolver-equalizer-apo` on every platform, and not the older
  `convolver-vst3-preset` (owner's decisions, 22 October 2026), with the
  presets and Equalizer APO guides;
  `release.stage`, `package_smoke.py` and `verify_artifact.py` require and run
  both tools. Still to add: the `convolver` CLI, which the first preview ships
  alongside VST3 and the preset tool, with LV2 and AU in later previews
  (owner's decision, 23 October 2026).
  Also before the first preview: Steinberg's usage guidelines ask anyone who
  uses the word "VST" for ® on first use, the sentence "VST is a registered
  trademark of Steinberg Media Technologies GmbH." in documentation and
  packages, and the VST Compatible logo where they require it
  ([VST3 API audit](docs/review-2026-10/vst3-api-audit-2026-10-23.md)); jrp
  to approve the wording.

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
  with minor even and non-zero and micro even for releases.

AU (AUv2, macOS: Logic, GarageBand, SoundSource):

- [ ] **AU-2 — Loading filters.** Host state in `ClassInfo`; `.aupreset` files
  from PLUG-3 load through the host's preset menu. Progress (22 October 2026,
  PR #44): ClassInfo holds the encoded state under `convolver-state`; the
  tests load `.aupreset` dictionaries from the PLUG-3 writer. Loading one from a
  named host's preset menu is checked under AU-4.
- [ ] **AU-3 — Validation.** `auval` and pluginval in macOS CI. `auval -strict`
  runs since PR #44; still to add: pluginval on the component, and a
  ThreadSanitizer build of `au.adapter` (bypass switched while rendering).
- [ ] **AU-4 — User guide and host matrix.** The Audio Unit in
  `docs/use/plugins.md` and `docs/use/install.md` (written in DOC-4); Logic, GarageBand,
  SoundSource, Live, REAPER and FL Studio results. Progress (23 October 2026,
  PR #56): GarageBand is jrp's first Audio Unit host (owner's decision, 23 October 2026),
  with a [smoke test](docs/develop/host-testing.md#garageband-audio-unit)
  limited to 44.1 kHz stereo. Not yet run.
- [ ] **AU-5 — Release.** Unsigned preview component under PLUG-5 (ad-hoc
  signed, documented quarantine removal). Developer ID signing and
  notarization follow at the production gate.

## FFmpeg integration

Parked (owner, 21 October 2026): maintaining a patched FFmpeg source tree adds
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
  layer (shared with VST3-4), filter gain, and a faster long-IR choice (larger
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
  incompatibility is an owner's decision recorded beside it. The literal
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
- [ ] **ROBUST-3 — Silence when memory runs out after a recorded failure.**
  When an active VST3 or AU instance's first load fails, the adapter records
  the failure and then prepares silence to replace the unfiltered input. If
  memory runs out while preparing that silence, the error is swallowed and
  the input keeps playing unfiltered, though the state says "load failed"
  (the contract says a failed first load never plays the unfiltered input).
  The PLUG-7 sequences cannot reach it: they fail either every allocation
  from one onwards or a single one. A fix could mute from the audio thread,
  as LV2 does with `HostProcessor::mute`.

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
- [ ] **VST3-3 — Editor and controller-processor messaging.** A filter-loading
  editor (HOST-2) needs `IPlugView` (VSTGUI 4.15 or native), content scaling
  (`IPlugViewContentScaleSupport`), Linux X11/Wayland (preview in 3.8), and
  `IConnectionPoint`/`IMessage` to pass a chosen file or prepared filter to the
  processor off the audio thread. `IDataExchangeHandler` could feed meters.
  Plug-in snapshots for host browsers (`smtg_target_add_plugin_snapshots`)
  need an editor first.
- [ ] **VST3-4 — Mix, gain and low-latency parameters.** Consider dry/wet and
  output-gain parameters, tagged with `IParameterFunctionName` (`kDryWetMix`) so
  hosts can find them. A `kLowLatencyMode` switch only makes sense if PERF-2
  provides a lower-latency mode; changing latency then needs
  `restartComponent(kLatencyChanged)` and tests in hosts that compensate it.
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
- [ ] **VST3-10 — Mark silent output.** Set the output silence flags once the
  output has been silent for the tail and latency, as the Audio Unit does, so
  hosts can skip downstream work and suspend the instance.

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
  plug-ins need less CPU, first consider letting the user choose a larger block
  (`HostProcessor` already accepts one; it changes reported latency and needs
  saved state and each format's latency-change call). Nonuniform partitions are
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

## Documented limitations

This inventory gathers constraints stated across the guides. Some are deliberate
API or safety policies, not defects. Change them only for an explicit need and
with compatibility tests; linked IDs point to open work above.

| Limitation | Current handling / follow-up | Authoritative detail |
| --- | --- | --- |
| No IR resampling or automatic channel adaptation for direct sound-file filters | Rate/count mismatches fail; use an explicit configuration. IO-2 covers resampling. | [Configuration](docs/use/configuration.md) |
| One VST3 input bus and one output bus, each 1–8 channels; no custom editor | Presets and generic host controls; evaluate layouts/editor under HOST-1/HOST-2 and VST3-1 to VST3-3. | [VST3](docs/use/plugins.md#loading-a-filter) |
| No promised migration of old VST2 project state | New VST3 identity; define a migration requirement before implementing one (VST3-6). | [VST3](docs/use/plugins.md#identities) |
| No built-in device application or clock management | CLI pipes need a producer/consumer; VST3 relies on its host. Standalone device support requires a separate design. | [Concepts](docs/use/concepts.md#files-and-continuous-streams) |
| One FFT backend per binary; Ooura/Accelerate have no FFTW-style measured plans or wisdom | Intentional deployment choice. Assess alternatives under FFT-2; do not imply runtime fallback. | [FFT guide](docs/develop/fft-backends.md#selection-and-contract) |
| FFTW wisdom is CPU/build/size-sensitive and process-global | Retune when relevant inputs change; external planner users must coordinate. A successful import does not prove optimality. | [Planning](docs/use/command-line.md#fftw-plan-tuning) |
| Core processing uses interleaved float buffers that must not overlap | Use `HostProcessor` for planar/in-place float/double host buffers; internal DSP remains float. | [Engine API](docs/develop/library.md#engine-api) |
| Latency and filter replacement are not instantaneous | Core latency is B; VST3 is 256 frames. Replacement starts with fresh history and warms/crossfades; reset work scales with storage. | [Replacement](docs/develop/plugin-contract.md#replacement-and-threads) |
| Callback safety has a defined thread contract | One audio thread per instance; serialized control calls; teardown requires stopped audio. Stress/deadline evidence remains ROBUST-1. | [Implementation invariants](docs/develop/architecture.md#invariants-to-preserve) |
| Resource limits cap channels, paths, delays, text, IRs and state | Intentional bounds; assess any increase with memory/overflow tests. Report-size follow-up is ROBUST-2. | [Core limits](docs/develop/library.md#resource-limits), [tests](docs/develop/testing.md) |
| The configuration format is not identical across consumers | No external consumer has verified this corpus yet; CFG-1. Older bundled configs may need delay lines and portable paths. | [Compatibility matrix](docs/use/other-programs.md#what-convolver-accepts) |
| Path portability checks are partial | No drive/share remapping; a clean profile is not certification for Windows or another reader. Extend checks only with fixtures. | [Profiles](docs/use/other-programs.md#check-that-a-configuration-will-travel) |
| Output is WAV, RF64, W64, AIFF, CAF, FLAC or headerless raw; speaker labels and reduced valid bits are WAV-only | Input codecs and FLAC output depend on the libsndfile build; the preview packages have no FLAC or Ogg, by decision ([workarounds](docs/use/command-line.md#flac-and-ogg)). Boundary-size tests remain IO-1; labels in other containers are IO-5. RAW has no embedded rate/channel metadata. | [Output formats](docs/use/command-line.md#output-formats) |
| Output writes are not transactional | I/O/numeric failure may leave a partial file; consider temporary-file/atomic publication for regular-file renders if required. Never promise rollback for pipes. | [Output safety](docs/use/command-line.md#output-safety) |
| Gain estimation is not a limiter; float samples can exceed full scale | Integer samples clip; strict quantizer headroom is GAIN-1. | [Gain](docs/use/command-line.md#gain-and-attenuation) |
| Dither/shaping/valid bits apply to integer file output only | Float/RAW output rejects those options; VST3 exchanges floats. Fixed shaping coefficients do not imply equal audibility at every sample rate. | [Integer output](docs/use/command-line.md#integer-formats-dither-and-noise-shaping) |
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
- [x] **CI-1 — Skip builds and tests for documentation-only pull requests.**
  24 October 2026, PR #63; [documentation-only pull requests](docs/develop/testing.md#documentation-only-pull-requests).
- [x] **CI-2 — Shorten CI without losing coverage.** 24 October 2026, PR #65:
  LV2 sanitizer stages as parallel jobs building only their targets,
  one CTest entry per package consumer, a parallel offline rebuild, ccache on
  Linux and macOS, and each kind of test run only where it can change (owner's
  decision, 24 October 2026); [which configurations run which tests](docs/develop/testing.md#which-configurations-run-which-tests).
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
- [x] **IO-3 — More output containers.** 23 October 2026, PR #60;
  [output formats](docs/use/command-line.md#output-formats).
- [x] **IO-4 — Raw filters and raw sample types.** 24 October 2026, PR #60;
  `f32le` by default and reported, `--raw-filter-format` for others;
  [filter files](docs/use/configuration.md#filter-files),
  [RAW streams](docs/use/command-line.md#raw-streams).
- [x] **IO-6 — FLAC in the preview packages.** 24 October 2026, PR #60:
  decided not to ship the codec libraries; limitation and workarounds in
  [FLAC and Ogg](docs/use/command-line.md#flac-and-ogg).
- [x] **LV2-1 — Adapter and headless test.** 22 October 2026, PR #43;
  [LV2 tests](docs/develop/testing.md#lv2-tests).
- [x] **LV2-2 — Loading filters.** 23 October 2026, PR #48;
  [loading a filter](docs/use/plugins.md#loading-a-filter), [LV2 tests](docs/develop/testing.md#lv2-tests).
- [x] **PERF-4 — Compiler flags and CPU-specific kernels.** 24 October 2026,
  PR #61: run-time AVX2+FMA kernels, with FMA by owner's decision (24 October
  2026) and a divergence test; [compiler flags and AVX2 kernels](docs/develop/fft-backends.md#compiler-flags-and-avx2-kernels).
- [x] **PLUG-1 — FFT backend for plug-ins.** 21 October 2026;
  [plug-in builds](docs/develop/fft-backends.md#plug-in-builds).
- [x] **PLUG-2 — Shared adapter test harness.** 22 October 2026, PR #28;
  [plug-in comparisons](docs/develop/testing.md#plug-in-comparisons-plug-2).
- [x] **PLUG-3 — One preset tool.** 22 October 2026, PR #32;
  [plug-in presets](docs/use/presets.md), [preset files](docs/develop/plugin-contract.md#preset-files).
- [x] **PLUG-4 — Layouts and identities.** 22 October 2026, PR #27;
  [plug-in identities](docs/develop/plugin-identities.md).
- [x] **PLUG-6 — Pin format dependencies.** 22 October 2026, PR #31;
  [pinned format dependencies](docs/develop/build.md#pinned-format-dependencies).
- [x] **PLUG-7 — Lifecycle sequence tests.** 23 October 2026, PR #58;
  [lifecycle sequences](docs/develop/testing.md#lifecycle-sequences-plug-7).
- [x] **PLUG-8 — Keep a structured failure reason.** 23 October 2026, PR #46;
  [failure reasons](docs/develop/plugin-contract.md#failure-reasons).
- [x] **VST3-1 — Speaker arrangements, not only channel counts.** 23 October
  2026, PR #42; [channel layouts](docs/develop/plugin-identities.md#channel-layouts).
- [x] **VST3-7 — Follow the shared adapter contract.** 22 October 2026, PR #35,
  with the reset follow-up in PR #40; [adapter contract](docs/develop/plugin-contract.md).
- [x] **VST3-8 — Follow the SDK documentation where the audit found gaps.**
  23 October 2026, PR #59, from the
  [VST3 API audit](docs/review-2026-10/vst3-api-audit-2026-10-23.md): tail
  includes the latency, input silence flags not trusted, Bypass text converts
  back, controller state calls succeed; [adapter contract](docs/develop/plugin-contract.md).
