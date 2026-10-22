<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver TODO

Open improvement ideas for the active portable implementation. This is the shared
backlog, not a promise that every idea will be implemented. The baseline inventory
was checked on 20 October 2026 against `2fe503e`; Ooura and the adapter plans were
reviewed on 21 October against `75f3924`, and the plug-in plans again against
`4e1bb1b` after the owner's review. Keep completed behavior in the guides and
[CHANGELOG](CHANGELOG.md), and keep this list for outstanding work.

In short: the main open themes are first-use examples and documentation
automation (DOC), plug-in release qualification and host testing (REL, HOST),
unused VST3 SDK features (VST3), VST3, LV2 and AU plug-ins developed in parallel (PLUG, LV2, AU), a parked FFmpeg filter (FFMPEG), checks against other configuration readers
(CFG), large files and resampling (IO), precise no-clip headroom (GAIN), longer
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

| Order | Work | Exit criterion / dependency |
| --- | --- | --- |
| 1. Done 21 October | **PLUG-1:** plug-in FFT backend | Plug-in builds default to Ooura (Linux, Windows) or Accelerate (macOS); `vst3.fft_symbols` checks the binary. |
| 1. Now | **DOC-1 + PLUG-2:** shared fixtures and the adapter comparison harness | One set of redistributable fixtures (mono, stereo, 2→6) drives the documented CLI commands and the VST3 comparison against the CLI render in CI. |
| 1. Done 22 October | **PLUG-4:** layout list and permanent identities | `plugins/identities.json` records the layouts (discrete or named, per side) and IDs (VST3 class IDs, AU `aufx`/`Conv`/`CONV`, LV2 URIs) that every adapter reads; version-1 presets keep loading. Checking named VST3 speaker arrangements against it is VST3-1, in the VST3 stream. |
| 1. Now | **PLUG-6:** pin format dependencies | AudioUnitSDK, `lv2`, `lilv` and `lv2lint` pinned before LV2-1 and AU-1 depend on them. |
| 2. Done 21 October | **REL-1:** reconcile `codex/plugin-distribution` | Merged into `main`: pinned sources, VST3 staging, unsigned installers, artifact checks and the candidate workflow. The signing scripts stayed on the branch. PLUG-5 extends this pipeline; do not start a second one. |
| 3. In parallel | **VST3** (VST3-1, HOST-1), **LV2-1 to LV2-4**, **AU-1 to AU-4** | Each stream passes the PLUG-2 comparisons and follows the [shared adapter contract](docs/plans/2026-10-21-plugin-formats.md#shared-adapter-contract); **PLUG-3** (preset tool) is needed before LV2-2 and AU-2 are complete. The AU component is GPL-3.0-or-later (decided 21 October 2026). |
| 4. Release | **PLUG-5 / REL-1**, **LV2-5**, **AU-5** | First releases are unsigned previews on macOS and Windows (owner's decision, 21 October 2026), published by jrp from one candidate run; see PLUG-5. Signing is not a prerequisite for them. Developer ID signing, notarization and Authenticode remain the later production gate. |
| Supporting, cheap | **FFMPEG-1**, **PERF-1** | FFMPEG-1 reuses DOC-1 fixtures. PERF-1 measures 6- and 8-channel room-correction filters on named machines and decides whether PERF-2 is needed for plug-ins. |
| Parked | **FFMPEG-2 to FFMPEG-5**, **HOST-3** | Revisit only by an owner decision. FFmpeg routes that avoid a patched FFmpeg are preferred; Windows-native needs more thought first. |
| On request or measured need | **HOST-2** (CLAP, editor), **VST3-2 to VST3-6**, **FFT-2**, **PERF-2** | Define the user need or measured bottleneck and acceptance tests first. FFT-1 is complete. |

DOC-2 automation and the CFG/IO/GAIN/ROBUST items remain open supporting work;
prioritize them when a reproducible defect or a selected milestone requires them.
This table owns the current ordering. The earlier
[21 October review order](docs/review-2026-10/ooura-and-roadmap-2026-10-21.md#recommended-order),
which put FFmpeg next, is kept as a dated record and is superseded here.

## User experience and distribution

- [ ] **DOC-1 — Runnable first-use examples.** Provide small redistributable IRs,
  inputs and expected output for mono, stereo and a crossover. Run the exact
  documented commands in CI; record fixture provenance/licences.
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
- [ ] **HOST-1 — Named host matrix.** Record versions and results for Windows/macOS
  hosts: scan, load, preset/state restore, silence/tails, seek, variable callbacks,
  bypass, filter changes, multiple instances and unequal channel counts. SDK
  validation and pluginval alone do not complete this item. Steinberg's free
  VST3PluginTestHost (Windows/macOS) is a convenient manual check without a DAW.
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
  correction, an export to Equalizer APO's `Convolution:`/`Copy:` configuration
  is likely cheaper than an MFT or our own APO. See the
  [MFT plan](docs/plans/2026-10-21-media-foundation-transform.md).

## Plug-ins: VST3, LV2 and AU

First priority (owner's decision, 21 October 2026): develop VST3, LV2 and AU in
parallel. Design, host evidence and framework choice:
[plug-in formats plan](docs/plans/2026-10-21-plugin-formats.md). Each
adapter is written against its format's own headers over `HostProcessor`. VST3
work continues under VST3-1 to VST3-6, HOST-1 and REL-1.

Shared groundwork, done once so the three streams stay independent:

- [x] **PLUG-1 — FFT backend for plug-ins.** Done 21 October 2026 (owner's
  decision). `CONVOLVER_FFT_BACKEND=AUTO` (the default) selects Ooura on Linux
  and Windows and Accelerate on macOS when `CONVOLVER_BUILD_VST3` is ON, and FFTW
  otherwise; FFTW stays available as an explicit override, with a configure
  warning. `vst3.fft_symbols` checks that the plug-in exports no FFTW symbols and,
  unless FFTW was chosen, imports none. The release policy uses the same
  backends and ships FFTW's notice only when FFTW is linked. See
  [plug-in builds](docs/fft-backends.md#plug-in-builds).
- [ ] **PLUG-2 — Shared adapter test harness.** Two references for every
  adapter: the independent double-precision convolution oracle (the authority)
  and the CLI render (adapter equivalence; both share the engine, so the CLI
  alone could hide a shared defect). Fixed tolerance (1e-5 against the oracle,
  identical to the CLI once latency is aligned), exact latency, exact tail for a
  fresh processor and the documented conservative (never-shrinking) bound after
  replacements,
  and distinct IRs per route so channel order is checked. Cases: initial
  restoration, live replacement, failed initial load and failed replacement,
  empty instance, bypass, reset, silence flags, variable blocks, in-place
  buffers, mono, stereo and 2→6. Reuse `scripts/release/generate_examples.py`
  and the oracle helpers in `tests/vst3/pluginval_tests.py` rather than adding
  a second fixture system; DOC-1 documents the same fixtures. See the
  [harness spec](docs/plans/2026-10-21-plugin-formats.md#shared-test-harness-plug-2).
- [ ] **PLUG-3 — One preset tool.** `convolver-preset`, writing `.vstpreset`,
  `.aupreset` and LV2 presets from the same host state. It must build without
  the VST3 SDK so AU and LV2 builds can ship it; `.vstpreset` output uses the SDK
  when present or a small writer of the documented format tested against the
  SDK's reader. Keep `convolver-vst3-preset` working with the same arguments and
  output for existing scripts.
- [x] **PLUG-4 — Layouts and identities.** Done 22 October 2026 (PR #27).
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
  and route by bus index). Forks are told to change every value in the
  [identities guide](docs/plugin-identities.md), which owns the layout rules.
  Recording an input layout still needs state version 2 (an owner's decision).
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
- [ ] **PLUG-6 — Pin format dependencies.** AudioUnitSDK by commit and
  checksum (like the VST3 SDK), the `lv2` headers, and the test-only `lilv` and
  `lv2lint`: shipped code in `packaging/release-policy.json`, test tools in CI.

Shared adapter contract, for every format (detail in the
[plan](docs/plans/2026-10-21-plugin-formats.md#shared-adapter-contract)):
a failed replacement keeps the running filter, with an explicit error; initial
restoration installs the filter directly, without a crossfade; an empty
instance and a failed initial load have one stated, tested behaviour (owner's
decision, 21 October 2026: identity for an empty instance with equal counts;
silence, with an error shown, for unequal counts and after a failed initial
load). VST3 must change to match: today an empty instance with unequal counts
passes input *c* to output *c*, and a rejected preset loaded while inactive
replaces the saved state.

LV2 (Linux first; also PipeWire system audio):

- [ ] **LV2-1 — Adapter and headless test.** `plugins/lv2/`: audio, latency and
  bypass ports; a `lilv` test using PLUG-2.
- [ ] **LV2-2 — Loading filters.** `atom:Path` filter parameter (host file
  chooser), worker-thread preparation, state with the embedded filter and mapped
  path, and PipeWire-style restore from a bare path string. Follow the
  [worker and state lifecycle](docs/plans/2026-10-21-plugin-formats.md#lv2-adapter-design):
  one adapter lock for all `HostProcessor` control calls that `run()` never
  takes; requests passed as copied worker payloads with a generation number,
  rechecked after loading so a stale completion never wins; no
  `state:threadSafeRestore` at first; initial restoration installs directly;
  rapid loads coalesce to the newest; defined handling of scheduling
  failure, saving during a load, synchronous (free-wheel) work and teardown
  with work outstanding, each with a test.
- [ ] **LV2-3 — Layouts and validation.** Plug-in URIs for the PLUG-4 layouts;
  `lv2lint` and pluginval in CI.
- [ ] **LV2-4 — User guide and host matrix.** `docs/lv2.md` with a PipeWire
  configuration; Ardour, REAPER, Carla, Qtractor and PipeWire results.
- [ ] **LV2-5 — Release bundles.** Linux tarball, then distro packages; macOS and
  Windows bundles under PLUG-5.

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
- [ ] **AU-2 — Loading filters.** Host state in `ClassInfo`; `.aupreset` files
  from PLUG-3 load through the host's preset menu.
- [ ] **AU-3 — Validation.** `auval` and pluginval in macOS CI.
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

- [ ] **VST3-1 — Speaker arrangements, not only channel counts.** The adapter
  accepts any arrangement with 1–8 channels and routes by index, so a 7.1 filter
  could run on a different 8-channel layout without warning. Record the intended
  arrangement (or the WAV channel mask from `--speaker-map`) in the preset, check
  or map it in `setBusArrangements`, and decide whether to offer layouts above 8
  channels (`SpeakerArr`, for example 7.1.4). Use the PLUG-4 speaker table (VST3
  speaker bits equal WAV mask bits) and keep `version1_presets` passing:
  [layout rules](docs/plugin-identities.md#channel-layouts).
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
  filter synchronously on the host's thread. Measure the worst case for long,
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
  For the plug-ins, include 6- and 8-channel room-correction filters at 48 and
  96 kHz, which have not been measured yet. On the 19 October cloud VM, a
  65536-tap stereo filter at B = 256 took about 0.6 µs per frame (about 3% of
  one core at 48 kHz) and B = 1024 was about 3.5 times cheaper; a 6×6 matrix of
  16384-tap filters (36 paths) took about 2.9 µs per frame (about 14%).
- [ ] **PERF-2 — Inverse-transform sharing or nonuniform partitions.** Prototype
  only for a demonstrated long-filter/latency bottleneck. If PERF-1 shows the
  plug-ins need less CPU, first consider letting the user choose a larger block
  (`HostProcessor` already accepts one; it changes reported latency and needs
  saved state and each format's latency-change call). Nonuniform partitions are
  the route to low latency and low CPU together. Preserve routing, tail,
  delay and reset behavior; account for changed floating-point accumulation order.
  Shared forward transforms are already implemented.

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
| Historical Windows applications are not built or host-tested by portable CI | Old PCM32, shaping, CMD tail and host-negotiation gaps remain historical; fix only proven defects, without implying modern host support. | [Historical coverage](docs/testing.md#historical-coverage) |

## Documentation maintenance

- [x] **DOC-0 — Refresh the entry points for 0.4.0.** Separate current guides from
  historical records, add a configuration reference and concepts page, correct
  stale feature claims, and add a source/invariant/test map for contributors and
  agents. See [documentation review](docs/archive/reviews/2026-10-20-documentation-review.md).

External links identify starting points, not endorsements or a completed licence
or performance assessment. Primary project pages were checked during this review;
verify the exact release again when implementing an item.
