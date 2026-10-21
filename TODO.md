<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver TODO

Open improvement ideas for the active portable implementation. This is the shared
backlog, not a promise that every idea will be implemented. Checked on 20 October
2026 against `2fe503e`. Keep completed behavior in the guides and
[CHANGELOG](CHANGELOG.md), and keep this list for outstanding work.

In short: the main open themes are first-use examples and documentation
automation (DOC), plug-in release qualification and host testing (REL, HOST),
checks against other configuration readers (CFG), large files and resampling
(IO), precise no-clip headroom (GAIN), longer stress testing (ROBUST), and FFT or
performance work only where measurements justify it (FFT, PERF). The
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
  validation alone does not complete this item.
- [ ] **HOST-2 — Additional formats and UI.** Assess Audio Unit for Apple users,
  [CLAP](https://github.com/free-audio/clap), and a filter-loading editor against
  actual user needs. Keep the core and host lifecycle reusable; require host tests
  and distribution work for each adapter.

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
| One VST3 input bus and one output bus, each 1–8 channels; no custom editor | Presets and generic host controls; evaluate wider layouts/editor under HOST-1/HOST-2. | [VST3](docs/vst3.md#loading-filters) |
| No promised migration of old VST2 project state | New VST3 identity; define a migration requirement before implementing one. | [VST3](docs/vst3.md#loading-filters) |
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
