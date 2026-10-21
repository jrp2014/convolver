<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver TODO

Open improvement ideas for the active portable implementation. This is the shared
backlog, not a promise that every idea will be implemented. Checked on 20 October
2026 against `2fe503e`. Keep completed behavior in the guides and
[CHANGELOG](CHANGELOG.md), and keep this list for outstanding work.

In short: the main open themes are first-use examples and documentation
automation (DOC), plug-in release qualification and host testing (REL, HOST),
unused VST3 SDK features (VST3), VST3, LV2 and AU plug-ins developed in parallel (PLUG, LV2, AU), a parked FFmpeg filter (FFMPEG), checks against other configuration readers
(CFG), large files and resampling (IO), precise no-clip headroom (GAIN), longer
stress testing (ROBUST), and FFT or performance work only where measurements
justify it (FFT, PERF). The
[limitations table](#documented-limitations) lists current constraints.

Each item has a stable ID. When taking one on, record the owner/issue or branch,
evidence needed, and outcome here. Confirm the current source first: separate
work may already have implemented part of it. Compatibility takes priority over
speed. Do not change the historical archive to implement a portable feature.

## User experience and distribution

- [ ] **DOC-1 — Runnable first-use examples.** Provide small redistributable IRs,
  inputs and expected output for mono, stereo and a crossover. Run the exact
  documented commands in CI; record fixture provenance/licences.
- [ ] **DOC-2 — Maintain documentation automatically.** Extend checks to build
  options and executable CLI examples in CI. Local Markdown links/anchors and
  checker regression tests now run through pre-commit; the separate
  `documentation` CTest protects the historical website. See the
  [check scope](docs/documentation.md#automated-checks).
- [ ] **REL-1 — Qualify downloadable plug-ins.** Reconcile the separate plug-in
  distribution work with main before duplicating it. Track minimum OS/CPU versions,
  dependency closure, licences/corresponding source, signing/notarization, clean
  machine installation and immutable release assets. Start with the
  [distribution plan](docs/superpowers/plans/2026-10-18-plugin-distribution.md).
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
  [MFT plan](docs/superpowers/plans/2026-10-21-media-foundation-transform.md).

## Plug-ins: VST3, LV2 and AU

First priority (owner's decision, 21 October 2026): develop VST3, LV2 and AU in
parallel. Design, host evidence and framework choice:
[plug-in formats plan](docs/superpowers/plans/2026-10-21-lv2-plugin.md). Each
adapter is written against its format's own headers over `HostProcessor`. VST3
work continues under VST3-1 to VST3-6, HOST-1 and REL-1.

Shared groundwork, done once so the three streams stay independent:

- [ ] **PLUG-1 — FFT backend for plug-ins.** A shared system FFTW planner is not
  serialized with other plug-ins in the same host process. Use Ooura or
  Accelerate, static FFTW with hidden symbols, or
  `fftwf_make_planner_thread_safe()` for VST3, LV2 and AU builds.
- [ ] **PLUG-2 — Shared adapter test harness.** Common fixtures (mono, stereo,
  2→6 crossover; with DOC-1) and a comparison against the CLI render, used by
  every adapter's tests.
- [ ] **PLUG-3 — One preset tool.** Extend `convolver-vst3-preset` into a tool
  writing `.vstpreset`, `.aupreset` and LV2 presets from the same host state.
- [ ] **PLUG-4 — Layouts and identities.** One list of supported channel layouts
  and a registry of permanent IDs: VST3 class IDs, AU codes (registered with
  Apple), LV2 URIs.
- [ ] **PLUG-5 — One release pipeline.** REL-1 for all three formats, with macOS
  signing and notarization shared by VST3 and AU.

LV2 (Linux first; also PipeWire system audio):

- [ ] **LV2-1 — Adapter and headless test.** `plugins/lv2/`: audio, latency and
  bypass ports; a `lilv` test using PLUG-2.
- [ ] **LV2-2 — Loading filters.** `atom:Path` filter parameter (host file
  chooser), worker-thread preparation, state with the embedded filter and mapped
  path, and PipeWire-style restore from a bare path string.
- [ ] **LV2-3 — Layouts and validation.** Plug-in URIs for the PLUG-4 layouts;
  `lv2lint` and pluginval in CI.
- [ ] **LV2-4 — User guide and host matrix.** `docs/lv2.md` with a PipeWire
  configuration; Ardour, REAPER, Carla, Qtractor and PipeWire results.
- [ ] **LV2-5 — Release bundles.** Linux tarball, then distro packages; macOS and
  Windows bundles under PLUG-5.

AU (AUv2, macOS: Logic, GarageBand, SoundSource):

- [ ] **AU-1 — Adapter and test.** `plugins/au/` on Apple's AudioUnitSDK:
  PLUG-4 channel configurations, latency, tail and bypass; tests using PLUG-2.
- [ ] **AU-2 — Loading filters.** Host state in `ClassInfo`; `.aupreset` files
  from PLUG-3 load through the host's preset menu.
- [ ] **AU-3 — Validation.** `auval` and pluginval in macOS CI.
- [ ] **AU-4 — User guide and host matrix.** `docs/au.md`; Logic, GarageBand,
  SoundSource, Live and REAPER results.
- [ ] **AU-5 — Release.** Signed, notarized component under PLUG-5.

## FFmpeg integration

Parked (owner, 21 October 2026): maintaining a patched FFmpeg source tree adds
uncomfortable coupling. FFMPEG-1 is still worth doing. If this is revisited,
prefer the pipe, a filter accepted upstream, or upstream state support in
FFmpeg's generic `lv2` filter (it cannot pass a file path today), which would let
the LV2 plug-in serve FFmpeg.

Design and evidence: [FFmpeg filter plan](docs/superpowers/plans/2026-10-21-ffmpeg-filter.md)
(21 October 2026). FFmpeg cannot load filters at run time, so a built-in filter
means a patched FFmpeg build with `--enable-gpl`.

- [ ] **FFMPEG-1 — Test the pipe workflow.** The
  [FFmpeg pipe example](docs/portable-core.md#with-ffmpeg) is documented and was
  run by hand; add a CI test that runs it when `ffmpeg` is installed.
- [ ] **FFMPEG-2 — C API.** A C99 header over `HostProcessor` and the
  configuration loader, a `Convolver::c` target and `convolver.pc`, with C-only
  tests and the same output as `Engine`.
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
  channels (`SpeakerArr`, for example 7.1.4).
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

- [ ] **FFT-1 — Check that we have the latest Ooura version.** Compare the vendored
  [Ooura copy](portable/third_party/ooura/README.md) with the
  [upstream packages](https://www.kurims.kyoto-u.ac.jp/~ooura/fft.html). Record the
  upstream archive/hash, algorithm family and local modifications. Do not infer
  freshness from file dates. Review licensing/notices, compare changes, and run
  FFT, convolution-oracle and backend-equivalence tests before updating it.
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
- [ ] **PERF-2 — Inverse-transform sharing or nonuniform partitions.** Prototype
  only for a demonstrated long-filter/latency bottleneck. Preserve routing, tail,
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
  agents. See [documentation review](docs/documentation-review-2026-10-20.md).

External links identify starting points, not endorsements or a completed licence
or performance assessment. Primary project pages were checked during this review;
verify the exact release again when implementing an item.
