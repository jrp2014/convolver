<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver TODO

Open improvement ideas for the active portable implementation. This is the shared
backlog, not a promise that every idea will be implemented. The baseline inventory
was checked on 20 October 2026 against `2fe503e`; Ooura and the adapter plans were
reviewed on 21 October against `75f3924`, and the plug-in plans again against
`4e1bb1b` after the owner's review. Keep completed behavior in the guides and
[CHANGELOG](CHANGELOG.md), and keep this list for outstanding work.

In short: the next milestone is an independent user running an exact preview
package ([below](#execution-priorities)); [release readiness](docs/release-readiness.md)
shows what users can run now and the evidence for it. The main open themes are
first-use examples and documentation automation (DOC), user journeys, plug-in
release qualification and host testing (REL, HOST), VST3, LV2 and AU plug-ins
developed in parallel, with lifecycle sequences and failure reasons shared by
every adapter (PLUG, LV2, AU), unused VST3 SDK features (VST3), files kept from
releases (COMPAT), a parked FFmpeg filter (FFMPEG), checks against other
configuration readers (CFG), large files and resampling (IO), precise no-clip headroom (GAIN), how
diagnostics reach the user once the plug-ins exist (DIAG), longer
stress testing (ROBUST), and FFT or performance work only where measurements
justify it (FFT, PERF). The
[limitations table](#documented-limitations) lists current constraints.

Each item has a stable ID. To take one on, open a draft pull request titled with
its ID; that is the claim, and its description holds the handoff note. Record
progress and the outcome here as the [workflow guide](docs/workflow.md#recording-progress-in-todo)
describes. Confirm the current source first: separate work may already have
implemented part of it. Compatibility takes priority over
speed. Do not change the historical archive to implement a portable feature.

## Execution priorities

This is the order for new work, not approval to start every item. Correctness
regressions take priority whenever discovered. Record ownership before starting
so independent tasks do not duplicate changes.

Direction (owner's decision, 21 October 2026): VST3, LV2 and AU are equal first
priorities, developed in parallel after shared groundwork. The FFmpeg filter and
Windows-native adapters are parked; CLAP and an editor wait for a request. See
the [plug-in formats plan](docs/plans/2026-10-21-plugin-formats.md).

**Next milestone** (jrp's [guidance](docs/review-2026-10/release-readiness-guidance-2026-10-22.md),
22 October 2026): an independent user successfully installs and uses an exact
preview package, with the lifecycle regression fixed and the advertised
workflows demonstrated. "Ready" means the user journeys of REL-2 work, not
that a set of items is ticked. The blockers and their evidence are listed in
[release readiness](docs/release-readiness.md#next-milestone-an-independent-user-runs-a-preview).
Finishing a dependable product now matters more than another substantial
refactor: move code into shared layers only when two formats need the same
behaviour (as with `HostFilter`), do not generalise host lifecycle or release
machinery beyond the requirements in hand, and keep libsndfile at the I/O
boundary and C++20 in the core.

| Order | Work | Exit criterion / dependency |
| --- | --- | --- |
| 1. Done 21 October | **PLUG-1:** plug-in FFT backend | Plug-in builds default to Ooura (Linux, Windows) or Accelerate (macOS); `vst3.fft_symbols` checks the binary. |
| 1. Done 22 October | **DOC-1 + PLUG-2:** shared fixtures and the adapter comparison harness | One set of redistributable fixtures (mono, stereo, 2→6) drives the documented CLI commands and the VST3 comparison against the CLI render in CI (PR #28). |
| 1. Done 22 October | **PLUG-4:** layout list and permanent identities | `plugins/identities.json` records the layouts (discrete or named, per side) and IDs (VST3 class IDs, AU `aufx`/`Conv`/`CONV`, LV2 URIs) that every adapter reads; version-1 presets keep loading (PR #27). VST3-1 checks VST3 speaker arrangements against it (done 23 October, PR #42: a notice only). |
| 1. Done 22 October | **PLUG-6:** pin format dependencies | AudioUnitSDK 1.4.0 and `lv2` 1.18.10 pinned by commit and checksum in the release policy; `lilv` and `lv2lint` pinned in the LV2 workflow (extended by LV2-1), the SDK compiled and `auval` run in the Audio Unit workflow (extended by AU-1) ([pins](docs/plugins/build.md#pinned-format-dependencies)). |
| 2. Done 21 October | **REL-1:** reconcile `codex/plugin-distribution` | Merged into `main`: pinned sources, VST3 staging, unsigned installers, artifact checks and the candidate workflow. The signing scripts stayed on the branch. PLUG-5 extends this pipeline; do not start a second one. |
| 3. Now, shared | **PLUG-7:** lifecycle sequences; **PLUG-8:** failure reasons | PLUG-7 checks `HostFilter`, `HostProcessor` and VST3 against a small reference model, then each new adapter. PLUG-8 lands before LV2-2 and AU-2 settle their error paths, so no adapter discards a reason. |
| 3. In parallel | **VST3** (VST3-1, VST3-7, HOST-1), **LV2-1 to LV2-4**, **AU-1 to AU-4** | Each stream passes the PLUG-2 comparisons and follows the [shared adapter contract](docs/plans/2026-10-21-plugin-formats.md#shared-adapter-contract); **PLUG-3** (preset tool, done 22 October, PR #32) writes the presets LV2-2 and AU-2 must load. The AU component is GPL-3.0-or-later (decided 21 October 2026). |
| 4. Next milestone | jrp's three conditions: the reset fix (VST3-7 follow-up, done 23 October, PR #40), **REL-2** journeys demonstrated, **REL-3** an independent user with an exact **PLUG-5** preview package. Proposed with them: **PLUG-7**, a **HOST-1** smoke test | The first preview holds VST3, the `convolver` CLI and the preset tool; LV2 and AU join later previews once their own journeys are demonstrated (owner's decision, 23 October 2026). |
| 5. Release | **PLUG-5 / REL-1**, **LV2-5**, **AU-5**, **COMPAT-1** at each publication | First releases are unsigned previews on macOS and Windows (owner's decision, 21 October 2026), published by jrp from one candidate run; see PLUG-5. Signing is not a prerequisite for them. Developer ID signing, notarization and Authenticode remain the later production gate. Each preview repeats the REL-2 journeys; from the second, the COMPAT-1 upgrade tests too. |
| 6. After the plug-in streams | **DIAG-1:** diagnostics review | Starts once the order-3 streams (VST3-1, VST3-7, LV2-1 to LV2-4, AU-1 to AU-4) and PLUG-3 are implemented, so every surface that can report an error exists. With PLUG-8 done it mostly decides how to show reasons already kept. |
| Before any optimisation | **PERF-3:** resource budgets | Callback-time distributions, memory and preparation time for long and 6/8-channel filters, including replacement and reset, define the supported workloads. PERF-2, FFT-2 and VST3-5 start only if PERF-3 shows a need. |
| Supporting, cheap | **FFMPEG-1**, **PERF-1**, **HOST-4** | FFMPEG-1 reuses DOC-1 fixtures. HOST-4 (Equalizer APO export, system-wide on Windows; owner chose it on 22 October 2026 over the Media Foundation transform) runs in Equalizer APO's own filter code on Windows CI; a manual check on a real device remains. PERF-1 measures 6- and 8-channel room-correction filters on named machines and keeps the throughput benchmarks reproducible; PERF-3, not PERF-1, decides whether PERF-2 is needed. |
| Parked | **FFMPEG-2 to FFMPEG-5**, **HOST-3** | Revisit only by an owner decision. FFmpeg routes that avoid a patched FFmpeg are preferred; Windows-native needs more thought first. |
| On request or measured need | **HOST-2** (CLAP, editor), **VST3-2 to VST3-6**, **FFT-2**, **PERF-2** | Define the user need or measured bottleneck and acceptance tests first. FFT-1 is complete. |

DOC-2 automation and the CFG/IO/GAIN/ROBUST items remain open supporting work;
prioritize them when a reproducible defect or a selected milestone requires them.
This table owns the current ordering. The earlier
[21 October review order](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md#recommended-order),
which put FFmpeg next, is kept as a dated record and is superseded here.

## User experience and distribution

- [x] **DOC-1 — Runnable first-use examples.** Done 22 October 2026 (PR #28).
  [`examples/first-use/`](examples/first-use/README.md) holds a mono IR, a
  stereo `.cfg` and a 2→6 crossover, each with an input and the oracle's
  expected output, all synthetic (GPL-2.0-or-later, provenance in that README).
  `examples.first_use` runs the commands marked in the README and the examples
  README exactly as written and compares with the expected output;
  `examples.fixtures` checks the files against their generator. See
  [plug-in comparisons](docs/testing.md#plug-in-comparisons-plug-2).
- [ ] **DOC-2 — Maintain documentation automatically.** Extend checks to build
  options and executable CLI examples in CI. Local Markdown links/anchors and
  checker regression tests now run through pre-commit; the separate
  `documentation` CTest protects the historical website. See the
  [check scope](docs/documentation.md#automated-checks).
- [x] **DOC-3 — Resolve adapter-plan review findings.** Corrected the plans and
  maintained guide: separate input/output speaker layouts, specify MFT partial
  buffering and queried latency, require host FFT-option support, and explain
  the pipe's stream/channel scope and Equalizer APO coverage. Adapter code and
  planned tests remain future work. See the
  [21 October review and recommended order](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md#documentation-findings).
- [ ] **REL-1 — Qualify downloadable plug-ins.** The VST3 preview pipeline from
  `codex/plugin-distribution` was merged on 21 October 2026: pinned dependencies
  and corresponding source, staging, unsigned installers, artifact verification,
  promotion gates and the candidate workflow
  ([record](docs/releases/developer-preview.md), [guide](docs/plugins/index.md)).
  PLUG-5 extends it to LV2 and AU. Still open: track minimum OS/CPU versions,
  dependency closure, licences/corresponding source, signing/notarization, clean
  machine installation and immutable release assets. Start with the
  [distribution plan](docs/plans/2026-10-18-plugin-distribution.md).
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
  Write them once in the plug-in guides that ship in the package
  (`docs/plugins/`), with per-platform differences marked, and make the
  package contain what they need: presets made from the stereo and 2→6
  filters, the preset tool and the `convolver` CLI, which the first preview
  ships (owner's decision, 23 October 2026); the package has no CLI yet (PLUG-5). See
  [release readiness](docs/release-readiness.md#user-journeys-for-the-first-preview).
- [ ] **REL-3 — Independent walkthrough of an exact preview.** Someone who
  did not write the code or the guides follows the REL-2 journeys from the
  exact preview files (identified by their SHA-256 sums), on their own
  machine, using only the documentation. Proposal for jrp: the tester uses the
  chosen candidate run's files before they are uploaded, so the pre-release
  can describe what was demonstrated; the alternative is to publish first,
  labelled unverified, and update the release notes (never the assets)
  afterwards. Record who (a name or "independent
  tester"), the package hash, OS, host and version, and for each step whether
  it worked, where they hesitated and what they could not do without help.
  Fix the guides or the product for each finding and repeat the affected
  steps. This is the exit criterion of the
  [next milestone](#execution-priorities). jrp chooses the tester.
- [ ] **HOST-1 — Named host matrix.** Record versions and results for Windows/macOS
  hosts: scan, load, preset/state restore, silence/tails, seek, variable callbacks,
  bypass, filter changes, multiple instances and unequal channel counts. SDK
  validation and pluginval alone do not complete this item. Steinberg's free
  VST3PluginTestHost (Windows/macOS) is a convenient manual check without a DAW.
  Progress (22 October 2026): REAPER and Carla are the suggested first hosts
  (jrp to try); the SDK sample hosts were evaluated and not added to CI
  ([host qualification](docs/plugins/hosts.md#steinbergs-sdk-hosts)).
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
  [MFT plan](docs/plans/2026-10-21-media-foundation-transform.md).
- [ ] **HOST-4 — System-wide filtering through Equalizer APO.** Progress
  22 October 2026: `convolver-equalizer-apo` exports an IR, `.cfg` or list as
  an Equalizer APO configuration ([guide](docs/equalizer-apo.md)), chosen by
  the device's sample rate and channel count, silent when nothing fits.
  `export.equalizer_apo` checks it against a model of Equalizer APO 1.4.2 and
  the double-precision oracle. On Windows CI, `export.equalizer_apo.runtime`
  runs the same cases in Equalizer APO 1.4.2's `Benchmark.exe` (its own filter
  code, without the audio service). Still open: the manual check on a real
  Windows device in the [guide](docs/equalizer-apo.md#how-it-is-tested)
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
  have no editor: today the VST3 adapter keeps the newest explanation in
  `HostFilter` but shows it nowhere. It reports a preset with the wrong rate
  or channel counts, or with corrupt state, to the host only as
  `kResultFalse`; a mismatch found at activation activates and plays silence
  with nothing reported; and `setActive`'s `catch (...)` drops the text of any
  other activation failure. PLUG-8 keeps a structured reason for every failure first,
  so this review decides presentation rather than recovering lost
  information. The
  [shared adapter contract](docs/plans/2026-10-21-plugin-formats.md#loading-and-failure)
  asks for the message in the host's log and a "load failed" status, but only
  LV2 has a defined route (`log:log` and the status port). VST3 and AU have no
  host log interface and no agreed place yet; an editor is VST3-3/HOST-2. Make
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

## Plug-ins: VST3, LV2 and AU

First priority (owner's decision, 21 October 2026): develop VST3, LV2 and AU in
parallel. Design, host evidence and framework choice:
[plug-in formats plan](docs/plans/2026-10-21-plugin-formats.md). Each
adapter is written against its format's own headers over `HostProcessor`. VST3
work continues under VST3-1 to VST3-6, HOST-1 and REL-1 (VST3-7 is done).

Shared groundwork, done once so the three streams stay independent:

- [x] **PLUG-1 — FFT backend for plug-ins.** Done 21 October 2026 (owner's
  decision). `CONVOLVER_FFT_BACKEND=AUTO` (the default) selects Ooura on Linux
  and Windows and Accelerate on macOS when `CONVOLVER_BUILD_VST3` is ON, and FFTW
  otherwise; FFTW stays available as an explicit override, with a configure
  warning. `vst3.fft_symbols` checks that the plug-in exports no FFTW symbols and,
  unless FFTW was chosen, imports none. The release policy uses the same
  backends and ships FFTW's notice only when FFTW is linked. See
  [plug-in builds](docs/fft-backends.md#plug-in-builds).
- [x] **PLUG-2 — Shared adapter test harness.** Done 22 October 2026 (PR #28).
  `tests/plugins/harness.hpp` checks every adapter against the double-precision
  oracle (1e-5) and the CLI render of the DOC-1 fixtures (identical after the
  256-frame latency), with exact latency and tails and the lifecycle cases of
  the [harness spec](docs/plans/2026-10-21-plugin-formats.md#shared-test-harness-plug-2).
  `vst3.harness` drives the VST3 adapter: 53 cases pass (worst error 1.8e-7)
  and 13 known deviations are left to VST3-7 (since removed: all 66 pass). LV2-1 and AU-1 each add a driver;
  format-specific status and error reporting stay in their own tests. See
  [plug-in comparisons](docs/testing.md#plug-in-comparisons-plug-2).
- [x] **PLUG-3 — One preset tool.** Done 22 October 2026 (PR #32).
  `convolver-preset` writes `.vstpreset`, `.aupreset` and LV2 preset bundles
  from one encoded host state, several per run, and converts an existing
  `.vstpreset`; it needs no plug-in SDK. `convolver-vst3-preset` is the same
  source under its old name (same arguments, messages and bytes).
  `vst3.preset_format` checks the `.vstpreset` writer is byte-identical to the
  SDK's `PresetFile::savePreset` and that each reads the other's files;
  `plugins.presets` reads all three formats with independent decoders. Owner's
  decision, 22 October 2026: AU presets keep Apple's standard keys with an
  empty `data` and the state under `convolver-state`; LV2 presets hold it as an
  `atom:Chunk` under the base URI plus `state`, with no file path. AU-2 and
  LV2-2 read these. Shipping the tool in release archives belongs to PLUG-5.
  See [plug-in presets](docs/presets.md).
- [x] **PLUG-4 — Layouts and identities.** Done 22 October 2026 (PR #27).
  Identities are owner's decisions: AU codes 21 October 2026; LV2 URI scheme
  (`https://github.com/jrp2014/convolver/lv2#discrete-N-M`), the eight fixed
  layouts and `io.github.jrp2014.convolver.*` macOS identifiers (replacing
  `org.convolver.*`) 22 October 2026.
  [`plugins/identities.json`](plugins/identities.json) records the permanent IDs
  (VST3 class IDs, AU `aufx`/`Conv`/`CONV` chosen 21 October 2026, LV2 URIs,
  macOS bundle and installer IDs, Windows installer keys, host-state magic), the
  channel bounds, the fixed LV2 layouts and the speaker meaning of each WAV mask
  bit in VST3, AU and LV2. CMake generates `convolver_identities.hpp` from it;
  the VST3 adapter, `Info.plist` and the macOS installer read it. Tests:
  `identities` (format rules, frozen values, installer files agree, no stray
  copies; also in pre-commit), `vst3.identities` (bundle class IDs and bundle
  identifier, SDK speaker bits), and `vst3.adapter`'s `version1_presets`
  (version-1 presets with any output mask load on every six-channel arrangement
  and route by bus index). Modified builds are asked to change the product
  identities listed in the [identities guide](docs/plugin-identities.md), which
  owns the layout rules.
  Recording an input layout still needs a new state version, 3 (an owner's
  decision; version 2 is VST3-7's failed-load marker).
- [ ] **PLUG-5 — One release pipeline.** REL-1 for all three formats. Extend the
  merged VST3 pipeline (`packaging/release-policy.json`, `scripts/release/`,
  `.github/workflows/release.yml`) rather than writing a new one. Signing
  (owner's decision, 21 October 2026): start unsigned on macOS and Windows.
  Mac bundles still carry an ad-hoc signature (Apple Silicon will not load
  unsigned code) and the install guide explains removing the download
  quarantine; Windows installers show a SmartScreen warning. Label these
  releases as unsigned previews. The preview gate and the production gate are
  separate ([criteria](docs/plans/2026-10-21-plugin-formats.md#unsigned-previews-and-production)):
  signing is not a prerequisite for a preview. The candidate workflow cannot
  publish, so publication is a manual step owned by jrp: run the candidate
  workflow by hand on the chosen tag (the version bot's tag push does not start
  it), upload exactly its `complete-preview-candidate` artifact's files to a
  GitHub pre-release with their SHA-256 sums and download instructions, and
  never replace an asset afterwards. Developer ID signing with notarization
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
- [x] **PLUG-6 — Pin format dependencies.** Done 22 October 2026 (PR #31).
  AudioUnitSDK 1.4.0 (`bd98b31`) and the `lv2` 1.18.10 headers (`0bcde33`) are
  pinned by commit and archive SHA-256 in `packaging/release-policy.json`, each
  marked with the formats that use it; the candidate workflow's `sources` job
  downloads and checks them. The test-only `lilv` (Ubuntu 24.04 package
  `0.24.22-1build1`) and `lv2lint` 0.16.2 (archive SHA-256) are pinned in the
  [LV2 workflow](.github/workflows/lv2.yml), which builds LV2's example plug-ins
  against the pinned headers, runs one through `lilv` and checks lv2lint's
  report on it; the
  [Audio Unit workflow](.github/workflows/au.yml) compiles the pinned SDK (1.4.0,
  C++23, jrp's choice on 22 October 2026) and runs `auval`. The weekly
  [Dependency updates workflow](.github/workflows/updates.yml) reports newer
  upstream releases of every pin (jrp's request, 22 October 2026). See [pinned format dependencies](docs/plugins/build.md#pinned-format-dependencies).
- [ ] **PLUG-7 — Lifecycle sequence tests.** The reset defect found in the
  review of 0.4.19 (a filter loaded after a reset during a transition waited
  for the interrupted crossfade) passed every single-operation test: reset,
  replacement and initial loading were each tested, their combination was
  not. Add deterministic sequences of load, process, reset, bypass on and off,
  failed replacement, failed initial load, save and restore, and stop and
  restart (deactivate, reactivate, change of rate or layout). After every
  operation, compare with a small reference model of the contract: which
  filter is saved, which filter is audible (after the latency and any
  crossfade), whether silence is required, and that audio-thread calls
  neither allocate nor lock. Enumerate every sequence up to a short length
  (four or five operations) and add a few seeded longer ones; a failing
  sequence prints its operations so it can become a fixed regression. The
  model is a plain function over a few fields, not a state-machine framework.
  Run it against `HostFilter` and `HostProcessor` in `native.host`, then
  through each adapter in the PLUG-2 harness. LV2-2 extends it with
  outstanding worker loads (a request arriving during preparation, a stale
  completion, teardown with work outstanding).
- [ ] **PLUG-8 — Keep a structured failure reason.** Today `HostFilter` keeps
  the newest load or preparation error as one string. In VST3, `setState`
  returns `kResultFalse`; a mismatch found at activation activates and plays
  silence with nothing reported; and `setActive`'s `catch (...)` drops the
  text of any other activation failure. Record each failure as a value: what
  failed (missing or unreadable filter file, unreadable or malformed state,
  a newer state version, sample rate, channel counts or layout, preparation,
  activation), the request it belongs
  to (where the filter came from, its rate and counts, and a request number),
  whether the instance plays silence or keeps a working filter, and the
  explanatory message. Keep it on the control side, never on the audio
  thread. Every adapter records it on every failure path and clears it on
  success, and its tests read it, even before anything displays it. DIAG-1
  then decides only where it appears. Notices that are not failures (such
  as the speaker-layout notice VST3-1 proposes) use the same record, or the
  item says why not. The
  [shared adapter contract](docs/plans/2026-10-21-plugin-formats.md#failure-reasons)
  states the rule.

Shared adapter contract, for every format (detail in the
[plan](docs/plans/2026-10-21-plugin-formats.md#shared-adapter-contract)):
a failed replacement keeps the running filter, with an explicit error; initial
restoration installs the filter directly, without a crossfade; an empty
instance and a failed initial load have one stated, tested behaviour (owner's
decision, 21 October 2026: identity for an empty instance with equal counts;
silence, with an error shown, for unequal counts and after a failed initial
load); bypass with unequal counts fades every output to silence (owner's
decision, 22 October 2026). VST3 follows it since VST3-7; the shared parts are
`HostFilter` and direct installation in `HostProcessor`, which LV2 and AU reuse.

- [x] **VST3-7 — Follow the shared adapter contract.** Done 22 October 2026
  (PR #35). The 13 known deviations in `vst3.harness` are gone and all 66
  cases pass (worst error 1.8e-7): an empty instance with unequal counts plays
  silence; once the plug-in has been activated at the current rate and buses,
  `setState` rejects a mismatched filter while inactive too and keeps the last
  accepted one (`setupProcessing` alone is not binding: JUCE hosts call it with
  placeholder values before restoring state); a
  failed first load plays silence; state restored before setup that does not
  suit the host activates as silence and returns when the host is corrected;
  a filter set after activation but before audio is installed directly. The
  decisions live in the host layer (`HostFilter`, `unfiltered_configuration`;
  tests in `native.host`). Owner's decisions, 22 October 2026: the "known
  after activation" rule; no message for a mismatch found at activation until
  VST3-3, as VST3 has no status display without an editor; and saving after a
  failed first load records it (host state version 2, flag 4, written only
  then), so reopening stays silent. See
  [loading filters in the VST3 guide](docs/vst3.md#loading-filters).
  Follow-up from jrp's review of 0.4.19: a reset during a crossfade now ends
  it, so a filter loaded before audio resumes is installed directly (tests:
  `native.host` reset during warm-up and crossfade, `vst3.adapter`
  stop/restart mid-transition); the preset guide states VST3's reporting limits.

LV2 (Linux first; also PipeWire system audio):

- [x] **LV2-1 — Adapter and headless test.** `plugins/lv2/`: audio, latency and
  bypass ports; a `lilv` test using PLUG-2. Done 22 October 2026 (PR #43): one
  bundle with a plug-in URI per PLUG-4 layout, generated from the registry;
  `enabled`, latency and status ports; filters through state (the
  `convolver-preset` property), with the control lock and failure rules of the
  [LV2 design](docs/plans/2026-10-21-plugin-formats.md#lv2-adapter-design).
  `lv2.harness` passes every PLUG-2 case (restore-before-setup and tail checks
  skipped: LV2 has neither); `lv2.adapter` covers ports, status, log messages,
  presets, lilv state and seeded lifecycle sequences; `lv2lint` runs on every
  plug-in in CI ([LV2 guide](docs/lv2.md)).
- [ ] **LV2-2 — Loading filters.** `atom:Path` filter parameter (host file
  chooser), worker-thread preparation, state with the embedded filter and mapped
  path, and PipeWire-style restore from a bare path string. Follow the
  [worker and state lifecycle](docs/plans/2026-10-21-plugin-formats.md#lv2-adapter-design):
  one adapter lock for all `HostProcessor` control calls that `run()` never
  takes; requests passed as copied worker payloads with a generation number,
  skipped if already stale after loading and checked again after the
  replacement is prepared, just before publication (needs a
  prepare-then-publish step in `HostProcessor`), so a stale completion never
  wins; status follows the newest request and the reported path follows the
  installed filter, with regression tests for a request that arrives during
  preparation or just before publication; no
  `state:threadSafeRestore` at first; initial restoration installs directly;
  rapid loads coalesce to the newest; defined handling of scheduling
  failure, saving during a load, synchronous (free-wheel) work and teardown
  with work outstanding, each with a test.
  Progress (22 October 2026, PR #43): restoring the embedded state (presets and
  sessions) works, prepared in `restore()`; the path parameter, worker, mapped
  path and PipeWire's bare path remain. Extend `lv2.adapter`'s lifecycle
  sequences with outstanding worker loads.
- [ ] **LV2-3 — Layouts and validation.** Plug-in URIs for the PLUG-4 layouts;
  `lv2lint` and pluginval in CI.
  Progress (22 October 2026, PR #43): the eight discrete URIs and `lv2lint` in
  CI came with LV2-1; pluginval and named layouts remain.
- [ ] **LV2-4 — User guide and host matrix.** `docs/lv2.md` with a PipeWire
  configuration; Ardour, REAPER, Carla, Qtractor and PipeWire results.
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

- [ ] **AU-1 — Adapter and test.** `plugins/au/` on Apple's AudioUnitSDK:
  PLUG-4 channel configurations, latency and tail reported in **seconds**
  (`frames / sample rate`), and bypass stored atomically by the property
  setter and applied with `HostProcessor::set_bypass` in `Render`, rather than
  `AUEffectBase`'s pass-through (which skips the delayed dry path
  and ramp and handles N→M poorly). Tests using PLUG-2, plus bypass, tails,
  silence flags and 2→6. Licence (owner's decision, 21 October 2026): the AU
  component is distributed under GPL-3.0-or-later because AudioUnitSDK is
  Apache-2.0, which is compatible with GPLv3 but not GPLv2; ship its notices
  (see the [plan](docs/plans/2026-10-21-plugin-formats.md#au-licence)).
  Progress (22 October 2026, PR #44, with AU-2): `plugins/au/` builds
  `Convolver.component` (`-DCONVOLVER_BUILD_AU=ON`); `au.harness` (PLUG-2),
  `au.adapter` and `au.bundle` run in the [Audio Unit workflow](.github/workflows/au.yml)
  with `auval -strict` ([AU guide](docs/au.md)). Nothing else remains in
  AU-1 once PR #44 is reviewed and merged; the ThreadSanitizer run belongs to AU-3.
- [ ] **AU-2 — Loading filters.** Host state in `ClassInfo`; `.aupreset` files
  from PLUG-3 load through the host's preset menu. Progress (22 October 2026,
  PR #44): ClassInfo holds the encoded state under `convolver-state`; the
  tests load `.aupreset` dictionaries from the PLUG-3 writer. Loading one from a
  named host's preset menu is checked under AU-4.
- [ ] **AU-3 — Validation.** `auval` and pluginval in macOS CI. `auval -strict`
  runs since PR #44; still to add: pluginval on the component, and a
  ThreadSanitizer build of `au.adapter` (bypass switched while rendering).
- [ ] **AU-4 — User guide and host matrix.** `docs/au.md`; Logic, GarageBand,
  SoundSource, Live, REAPER and FL Studio results.
- [ ] **AU-5 — Release.** Unsigned preview component under PLUG-5 (ad-hoc
  signed, documented quarantine removal). Developer ID signing and
  notarization follow at the production gate.

## FFmpeg integration

Parked (owner, 21 October 2026): maintaining a patched FFmpeg source tree adds
uncomfortable coupling. FFMPEG-1 is still worth doing. If this is revisited,
prefer the pipe, a filter accepted upstream, or upstream state support in
FFmpeg's generic `lv2` filter (it cannot pass a file path today), which would let
the LV2 plug-in serve FFmpeg.

Design and evidence: [FFmpeg filter plan](docs/plans/2026-10-21-ffmpeg-filter.md)
(21 October 2026). FFmpeg cannot load filters at run time, so a built-in filter
means a patched FFmpeg build with `--enable-gpl`.

- [ ] **FFMPEG-1 — Test the pipe workflow.** The
  [FFmpeg pipe example](docs/portable-core.md#with-ffmpeg) is documented and was
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
  [configuration compatibility](docs/configuration-compatibility.md).
- [ ] **COMPAT-1 — Files from released builds.** Round-trip tests with
  today's writer and reader can agree with each other while both break
  yesterday's files. When jrp publishes a preview, keep a small collection
  made by that exact release, in its own folder with its own runner (for
  example `tests/upgrade/VERSION/`, apart from the `.cfg` corpus in
  `tests/compatibility/`, since the guarantees differ): its example presets in
  every format it ships, a saved REAPER or Carla project containing Convolver,
  the input and the expected audio rendered by that release, with the package
  SHA-256 and how each file was made. CI cannot open the host project, so the
  test takes the plug-in state chunk out of it and restores that through each
  adapter; the project itself is kept for reopening by hand in the host. Later
  builds must load every retained file and reproduce the expected audio after
  the reported latency: bit-identical when the FFT backend and block size are
  unchanged, otherwise within the PLUG-2 oracle tolerance (1e-5 absolute). Never regenerate a retained file; a deliberate
  incompatibility is an owner's decision recorded beside it. The literal
  version-1 and version-2 state bytes in `native.state` stay as the first
  fixtures. Add the capture step to the
  [publication steps](docs/releases/developer-preview.md#publishing-an-unsigned-preview-planned-not-yet-done)
  when this is implemented. The four guarantees it protects are listed in
  [release readiness](docs/release-readiness.md#compatibility-guarantees).
- [ ] **IO-1 — Large files.** Specify and test RF64 output, boundary sizes and pipe
  behavior. Current output choices are ordinary WAV and raw float; input decoder
  support is not a promise of strict validation for every container.
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
and 3.8.1 change notes) on 21 October 2026. The adapter already uses one main
input/output bus, 32/64-bit processing, silence flags, sample-accurate bypass
(`kIsBypass`), latency/tail reporting, component state, generated
`moduleinfo.json` and the default (empty) process-context requirements. Items
below are candidates; adopt each only with adapter, validator and pluginval tests.

- [x] **VST3-1 — Speaker arrangements, not only channel counts.** Done 23
  October 2026 (PR #42). The adapter accepts any arrangement with 1–8 channels
  and routes by index; it now compares a filter's output channel mask (the
  `.cfg`'s first line, kept in version-1 presets) with the host's arrangement,
  channel by channel, and records an explanatory notice, never a rejection
  (`speaker_notice`; tests `native.host`, `vst3.adapter` `speaker_arrangements`
  and `version1_presets`). VST3 needs no channel mapping, since arrangements
  order channels by bit. Owner's decision, 23 October 2026: warn only, with no
  enforced layouts (no host state version 3) and no buses above 8 channels;
  revisit either on request. The notice reaches users only with DIAG-1 or
  VST3-3. [Layout rules](docs/plugin-identities.md#channel-layouts).
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

Not relevant to a convolver unless the product changes: MIDI and note
interfaces (`IMidiMapping2`, `IMidiLearn2`, note expression, MIDI 2.0
articulations), `ITransportControl`, `IPrefetchableSupport`,
`IAudioPresentationLatency`, `IInfoListener` (track name/colour),
`IAutomationState`, `IXmlRepresentationController`,
`IEditControllerHostEditing`, `IParameterFinder`, side-chain or auxiliary buses,
`IComponent::setIoMode`, and `IStreamAttributes` (presets embed their filters,
so the preset file path is not needed).

## FFT libraries and performance

- [x] **FFT-1 — Check that we have the latest Ooura version.** Checked the
  [official packages](https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html) on
  21 October 2026: no C algorithm changes are missing. All 36 non-threaded
  function definitions match after the documented adaptations; the advertised
  December 2006 correction was to the Fortran version. Keep the current code.
  [Provenance and hashes](portable/third_party/ooura/README.md#upstream-comparison-21-october-2026)
  and [review/validation](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md)
  record the evidence. Recheck upstream if considering a later update.
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
  [FFT guide](docs/fft-backends.md) and turn them into stated supported
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
| No IR resampling or automatic channel adaptation for direct sound-file filters | Rate/count mismatches fail; use an explicit configuration. IO-2 covers resampling. | [Configuration](docs/configuration.md) |
| One VST3 input bus and one output bus, each 1–8 channels; no custom editor | Presets and generic host controls; evaluate layouts/editor under HOST-1/HOST-2 and VST3-1 to VST3-3. | [VST3](docs/vst3.md#loading-filters) |
| No promised migration of old VST2 project state | New VST3 identity; define a migration requirement before implementing one (VST3-6). | [VST3](docs/vst3.md#loading-filters) |
| No built-in device application or clock management | CLI pipes need a producer/consumer; VST3 relies on its host. Standalone device support requires a separate design. | [Concepts](docs/concepts.md#files-and-continuous-streams) |
| One FFT backend per binary; Ooura/Accelerate have no FFTW-style measured plans or wisdom | Intentional deployment choice. Assess alternatives under FFT-2; do not imply runtime fallback. | [FFT guide](docs/fft-backends.md#selection-and-contract) |
| FFTW wisdom is CPU/build/size-sensitive and process-global | Retune when relevant inputs change; external planner users must coordinate. A successful import does not prove optimality. | [Planning](docs/portable-core.md#fftw-plan-tuning) |
| Core processing uses interleaved float buffers that must not overlap | Use `HostProcessor` for planar/in-place float/double host buffers; internal DSP remains float. | [Engine API](docs/portable-core.md#engine-api) |
| Latency and filter replacement are not instantaneous | Core latency is B; VST3 is 256 frames. Replacement starts with fresh history and warms/crossfades; reset work scales with storage. | [Lifecycle](docs/vst3.md#processing-and-lifecycle) |
| Callback safety has a defined thread contract | One audio thread per instance; serialized control calls; teardown requires stopped audio. Stress/deadline evidence remains ROBUST-1. | [Implementation invariants](docs/project-structure.md#invariants-to-preserve) |
| Resource limits cap channels, paths, delays, text, IRs and state | Intentional bounds; assess any increase with memory/overflow tests. Report-size follow-up is ROBUST-2. | [Core limits](docs/portable-core.md#resource-limits), [tests](docs/testing.md) |
| The configuration format is not identical across consumers | No external consumer has verified this corpus yet; CFG-1. Older bundled configs may need delay lines and portable paths. | [Compatibility matrix](docs/configuration-compatibility.md#compatibility-matrix) |
| Path portability checks are partial | No drive/share remapping; a clean profile is not certification for Windows or another reader. Extend checks only with fixtures. | [Profiles](docs/configuration-compatibility.md#profiles) |
| Output is ordinary WAV or little-endian raw float; codecs depend on libsndfile build | WAV size limits and broader container validation remain IO-1. RAW has no embedded rate/channel metadata. | [RAW I/O](docs/portable-core.md#raw-streams) |
| Output writes are not transactional | I/O/numeric failure may leave a partial file; consider temporary-file/atomic publication for regular-file renders if required. Never promise rollback for pipes. | [Output safety](docs/portable-core.md#output-safety) |
| Gain estimation is not a limiter; float samples can exceed full scale | Integer samples clip; strict quantizer headroom is GAIN-1. | [Gain](docs/portable-core.md#gain-and-attenuation) |
| Dither/shaping/valid bits apply to integer file output only | Float/RAW output rejects those options; VST3 exchanges floats. Fixed shaping coefficients do not imply equal audibility at every sample rate. | [Integer output](docs/portable-core.md#integer-formats-dither-and-noise-shaping) |
| C++ packages are static development libraries, not self-contained installers or a stable binary ABI | Rebuild with compatible toolchains; shared dependencies must be deployed. REL-1 covers end-user bundles. | [Package guide](docs/cmake-package.md) |
| Source tags and configured CI do not prove a release is qualified | Tie results to exact commits; named hosts, clean machines and signatures remain release evidence. | [Versioning](docs/versioning.md), [testing](docs/testing.md) |
| Finite oracle, fuzz and heap-guard tests do not prove all-input correctness or hard realtime | Keep explicit numerical tolerances and test boundaries; ROBUST-1. | [Testing](docs/testing.md#validation-limits-and-release-work) |
| Equalizer APO 1.4.2 (x86/x64) convolution drops up to three bins just below half the sample rate when a block is not a multiple of four frames | Upstream behaviour, inaudible (about 22 kHz at 44.1 kHz); the export does not work around it. The runtime test uses 480-frame blocks. | [Equalizer APO guide](docs/equalizer-apo.md#how-it-is-tested) |
| Historical Windows applications are not built or host-tested by portable CI | Old PCM32, shaping, CMD tail and host-negotiation gaps remain historical; fix only proven defects, without implying modern host support. | [Historical coverage](docs/testing.md#historical-coverage) |

## Documentation maintenance

- [x] **DOC-0 — Refresh the entry points for 0.4.0.** Separate current guides from
  historical records, add a configuration reference and concepts page, correct
  stale feature claims, and add a source/invariant/test map for contributors and
  agents. See [documentation review](docs/archive/reviews/2026-10-20-documentation-review.md).

External links identify starting points, not endorsements or a completed licence
or performance assessment. Primary project pages were checked during this review;
verify the exact release again when implementing an item.
