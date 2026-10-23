<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Changelog

Changes to the portable C++ core, CLI and plug-ins. The portable package is
versioned separately from the historical Windows releases (1.x to 4.4), whose
release notes are archived in [docs/historical](docs/historical).

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Until 1.0 the C++ API may change between minor versions.

## [Unreleased]

### Added

- An LV2 plug-in for Linux (LV2-1), built with `-DCONVOLVER_BUILD_LV2=ON`: one
  bundle, `convolver.lv2`, with a plug-in for each fixed layout (1→1, 2→2, 1→2,
  2→4, 2→6, 2→8, 6→6 and 8→8). Filters load from presets written by
  `convolver-preset` and from saved sessions; bypass, 256-frame latency
  reporting and a status port; rejected loads are explained in the host's log.
  See [how the plug-ins behave](docs/use/plugins.md).
- LV2 filter files (LV2-2): a **Filter** parameter that hosts such as Ardour
  and Jalv show as a file chooser, loaded on the host's worker thread with a
  Loading status while it is prepared; rapid choices load only the newest,
  and an overtaken load is never heard. Sessions save the file's path
  (mapped with `state:mapPath`) beside the embedded filter, and restoring
  falls back on the file when the embedded copy cannot be used; a path alone
  restores too, as PipeWire's `filter-chain` `config` passes it. The plug-in
  now links libsndfile. For adapters, `HostProcessor::prepare()` and
  `publish()` split `replace()` in two, and `SupersededRequest` abandons an
  overtaken request.
- An Audio Unit (AUv2 effect) adapter for macOS, `Convolver.component`
  (AU-1, AU-2): build with `-DCONVOLVER_BUILD_AU=ON` and the pinned
  AudioUnitSDK, then load `.aupreset` files from `convolver-preset` through the
  host's preset menu. 1 to 8 channels per side (counts may differ), latency and
  tail reported in seconds, bypass with the same 64-sample ramp and delayed dry
  signal as VST3 (left to the host to save, as is usual for Audio Units), and
  the shared rules for presets that do not fit. The component is distributed
  under GPL-3.0-or-later because it includes
  AudioUnitSDK (Apache-2.0). See the [AU guide](docs/use/plugins.md). Not yet tested in
  named hosts.
- `HostFilter`, `HostLayout` and `unfiltered_configuration` in
  `convolver/host.hpp`: the control side of the shared plug-in adapter
  contract (which filter a load replaces, what is saved and what plays), for
  every format adapter to reuse. `HostState::failed` marks a saved failed
  first load (encoded as host state version 2). `HostProcessor::replace` now installs a filter
  directly, without warm-up or crossfade, when no audio has been processed
  since construction or `reset()`.
- `HostFailure` and `HostFilter::failure()` (PLUG-8): every failed load, and
  every reason a plug-in plays silence, is kept as a value (what failed, the
  request it belongs to, whether a working filter still plays, and the
  explanation), not just a string. The VST3, LV2 and AU adapters record it
  on every failure path, including activation failures VST3 used to drop.
  Where it is shown is DIAG-1's decision. `decode_host_state` throws
  `NewerHostState` for state from a newer Convolver, and the message says to
  update Convolver rather than calling the state damaged.
- `speaker_notice` and `HostFilter::notice()` (VST3-1) compare the speakers a
  filter's outputs are labelled for (the `.cfg` channel mask) with the host's
  output speakers, now carried in `HostLayout`, and explain which outputs
  land on which speakers. Advice only: nothing is rejected, and channels stay
  routed by bus index. The VST3 adapter passes its speaker arrangements to it;
  it has no way to show the notice yet.
- `convolver-equalizer-apo` (HOST-4) exports an IR, `.cfg` or configuration
  list as an [Equalizer APO](docs/use/equalizer-apo.md) configuration, so Windows
  can apply the filter to everything a device plays. The device's sample rate
  and channel count choose the list entry (on a device with more channels,
  the largest entry that fits); a device that no entry fits, and device
  channels a filter does not use, are silenced rather than played unfiltered. Checked against a model of Equalizer APO and, on Windows CI, in
  Equalizer APO 1.4.2's own filter code through its `Benchmark.exe`.

- `convolver-preset` (PLUG-3) makes self-contained presets for VST3
  (`.vstpreset`), Audio Units (`.aupreset`) and LV2 (`NAME.preset.lv2`
  bundles) from one IR, `.cfg` or configuration list, several formats in one
  run, and converts an existing `.vstpreset` to the other formats. All carry the
  same encoded filter. It needs no plug-in SDK, so AU and LV2 builds can ship
  it. `convolver-vst3-preset` is now the same tool under its old name, with the
  same arguments and byte-identical output. See [plug-in presets](docs/use/presets.md).

- Pinned plug-in format dependencies (PLUG-6): AudioUnitSDK 1.4.0 and the
  `lv2` 1.18.10 headers by commit and checksum in the release policy, and the
  test tools `lilv` 0.24.22 and `lv2lint` 0.16.2 in new LV2 and Audio Unit CI
  workflows. A weekly Dependency updates workflow reports newer upstream
  releases of every pin and keeps one tracking issue current. See
  [pinned format dependencies](docs/develop/build.md#pinned-format-dependencies).

- First-use examples in `examples/first-use/`: a mono IR, a stereo
  configuration and a stereo-to-six-channel crossover, each with an input and
  the expected output. CI runs the documented commands as written and checks
  the results.
- A shared test harness for plug-in adapters (PLUG-2): every format is checked
  against a direct double-precision convolution and the CLI's renders of the
  same examples, with the lifecycle cases of the shared adapter contract. The
  VST3 adapter runs it as `vst3.harness`.
- One registry of permanent plug-in identities and channel layouts,
  `plugins/identities.json` (PLUG-4): VST3 class IDs, AU codes `aufx`/`Conv`/`CONV`,
  LV2 URIs and fixed layouts, macOS bundle and installer identifiers, Windows
  installer keys and the speaker meaning of each WAV channel-mask bit in VST3, AU
  and LV2. The build generates `convolver_identities.hpp` from it, and new tests
  (`identities`, `vst3.identities`) stop identities from changing or being copied
  elsewhere. See [plug-in identities](docs/develop/plugin-identities.md); modified
  builds should change the product identities it lists.
- VST3 CI loads the plug-in in an independent host as well as Steinberg's SDK:
  [pluginval](https://github.com/Tracktion/pluginval) at strictness 10 on
  Linux, macOS (with real-time-safety checks) and Windows, plus acceptance
  renders that load real filters through the host and match a direct
  convolution. Optional locally via `-DCONVOLVER_PLUGINVAL`.
- The SDK validator also runs in extensive mode (`vst3.validator.extensive`).
- Unsigned VST3 preview distributions: pinned dependencies and corresponding
  source, relocatable archives with synthetic presets, unsigned installer
  templates, and a read-only candidate workflow on Windows, Linux and both macOS
  architectures (`-DCONVOLVER_PACKAGE_PLUGINS=ON`, preset `release-vst3`).
  Production qualification (signing, clean machines, named hosts) remains open.
- macOS VST3 bundles are re-sealed with an ad-hoc signature after the SDK adds
  `moduleinfo.json`, and `vst3.signature` verifies the seal.

### Changed

- Host testing names jrp's first hosts on macOS: REAPER and Audacity for the
  VST3 plug-in and GarageBand for the Audio Unit, each with a step-by-step
  [smoke test](docs/develop/host-testing.md#smoke-test-in-each-first-host)
  (HOST-1, AU-4); they replace REAPER and Carla. The
  [presets guide](docs/use/presets.md#load-it-in-a-host) now says where
  Audacity 4 finds `.vstpreset` files, since its preset menu cannot import
  them.
- Documentation for contributors and coding agents moved into `docs/develop/`
  (DOC-4, first step): one [build page](docs/develop/build.md) for every build,
  the [plug-in adapter contract](docs/develop/plugin-contract.md) taken out of
  the plug-in formats plan, and writing rules for each reader and kind of page.
  The `todo-item` and `review-change` skills now only point to the workflow
  guide, which holds the full procedures. The
  [index](docs/README.md#moved-pages) lists the old and new paths.
- User documentation moved into `docs/use/` (DOC-4, second step), written for
  people applying filters: a [start page](docs/use/README.md), two tutorials
  ([first filter](docs/use/first-filter.md), whose commands the
  `examples.first_use` test now runs, and
  [first sound in a host](docs/use/first-sound-in-a-host.md)), one
  [install page](docs/use/install.md) and one
  [plug-in page](docs/use/plugins.md) for VST3, AU and LV2,
  [troubleshooting](docs/use/troubleshooting.md) and [limits](docs/use/limits.md).
  The CLI guide is now the [command-line reference](docs/use/command-line.md);
  its engine API and limits moved to the [library guide](docs/develop/library.md),
  and test evidence from every user page to [testing](docs/develop/testing.md).
  Preview packages ship all of `docs/use/` instead of `docs/plugins/` and two
  copied guides. The tools' messages point at the new paths.
- Status, direction and decisions moved into `docs/project/` (DOC-4, third
  step). One [status page](docs/project/status.md) replaces `REVIEW.md`,
  release readiness and the modernization page; the dependency choices are in
  the [implementation map](docs/develop/architecture.md#dependency-choices). A
  new [decisions log](docs/project/decisions.md) lists jrp's decisions with the
  page that explains each. Releasing is a [developer guide](docs/develop/releasing.md),
  and the 18 October release record and the old review are archived. History,
  the convolverPipe page and open plans are in `docs/project/`.
- Shorter front pages (DOC-4, last step): the [README](README.md) is a start
  page with one example, `AGENTS.md` links to the guides instead of repeating
  them, and [TODO](TODO.md) holds open work, with finished items reduced to one
  line each under "Done". `check_repository.py` now requires every maintained
  page to sit in a reader folder, and fails when a page whose paths it checks
  is missing.
- Preview packages (PLUG-5) ship `convolver-preset` and
  `convolver-equalizer-apo` in `bin/`, with the presets and Equalizer APO
  guides in `docs/`, instead of `convolver-vst3-preset` (still built from
  source, for existing scripts). The example presets are made with
  `convolver-preset` (byte-identical). Release checks run both tools from a
  relocated package.
- The VST3 plug-in follows the shared adapter contract (VST3-7). With no preset
  loaded and different input and output counts (for example a 2→6 crossover)
  it plays silence instead of passing input *c* to output *c*. Once the plug-in has
  been activated, a preset at the wrong sample rate or channel counts is
  rejected while it is inactive too, keeping the last valid one; if none was loaded, it plays
  silence. A project restored at the wrong rate or layout now activates and
  plays silence, keeping its filter, instead of failing to activate. A preset
  loaded after activation but before any audio plays from the first sample
  without a crossfade. Saving after a failed first load records the failure,
  so reopening stays silent: host state version 2, used only for that state
  (other states are still written as version 1). The shared decisions are in
  the host layer (`HostFilter`), for LV2 and AU to reuse.
- Bypass with unequal input and output counts (for example a stereo-to-six
  crossover) now ramps every output to silence instead of passing input *c* to
  output *c*, so bypassing a crossover never sends full-range audio to a driver.
  Filters with equal counts bypass as before.
- The macOS VST3 bundle identifier is now `io.github.jrp2014.convolver.vst3` and the installer package
  identifiers are `io.github.jrp2014.convolver.pkg.vst3` and `.pkg.tools` (they were
  `org.convolver.*`, a domain the project does not control). No release used
  the old identifiers.
- The preserved 2005–2006 Windows source moved from `convolver/` to
  `historical/`, so the top-level folder named after the project is no longer
  the old code. Open plans moved from `docs/superpowers/` to `docs/plans/`.
  Nothing in the portable build, its targets or installed files changed.
- Plug-in builds no longer use FFTW by default. `CONVOLVER_FFT_BACKEND` now
  defaults to `AUTO`: FFTW for ordinary builds, and the built-in Ooura FFT
  (Accelerate on macOS) when `CONVOLVER_BUILD_VST3` is ON, because another
  plug-in or the host may use the same FFTW and its planner is not thread-safe.
  `-DCONVOLVER_FFT_BACKEND=FFTW` still selects FFTW (CMake warns in a plug-in
  build). A new `vst3.fft_symbols` test checks the plug-in binary, and the
  release candidates for Linux and Windows use Ooura. Output differs from FFTW
  builds only by floating-point rounding (worst error against the double oracle
  2.6e-7 with Ooura on Linux x86-64).
- `convolver --help` describes `--gain-db no-clip` as the filter's worst-case
  peak bound rather than a gain that can never clip, matching the CLI reference:
  it assumes input within full scale and leaves no dither or shaping headroom.
- When no filter matches the input (for example a mono IR given for stereo
  audio, a configuration for another sample rate, or a list with no matching
  entry), the CLI, inspection and the VST3 preset tool now say what each
  candidate offers, what differs and what to change. For a mono IR used with
  multichannel input the message includes a ready-to-save configuration that
  applies the IR to every channel. Missing IRs, out-of-range IR or route
  channels, IR sample-rate mismatches and `-` without RAW options also get
  fuller explanations.

### Fixed

- A filter loaded while the host had stopped processing in the middle of a
  crossfade now plays from the first resumed sample. Before, `HostProcessor`
  kept the interrupted transition after `reset()`, so the old filter went on
  playing until that crossfade finished (jrp's review of 0.4.19 measured 1.004
  where 3 was expected). Any reset now ends the transition and the newer filter
  plays at once; that includes the internal recovery reset after non-finite
  output, which used to bring the old filter back and repeat the crossfade.

## [0.4.0] - 2026-10-19

### Added

Features that only the historical engine had:

- Dither on every integer output format (PCM8, PCM16, PCM24, PCM32), with the
  historical engine's five error-feedback noise shapers: `--noise-shaping
  simple|second-order|third-order|ninth-order|sony-sbm`. Dither stays off by
  default. `convolver::Quantizer` generalizes `Pcm16Quantizer`, which remains.
- `wav-pcm8` output and `--valid-bits N`, which writes WAVE_FORMAT_EXTENSIBLE
  with fewer valid bits than the container (for example 20 bits in 24).
- `--gain-db auto` (the historical white-noise optimum-attenuation estimate,
  seeded so it is repeatable) and `--gain-db no-clip` (an exact bound that
  cannot clip), from `convolver::estimate_attenuation`.
- `--output-channels N` and `ConfigurationSelection` choose a list entry by
  output channel count as well as rate and inputs; inspection reports it.
- `--fft-plan exhaustive`, `--fft-time-limit SECONDS`, `FftPlanning::exhaustive`,
  `FftOptions::time_limit_seconds` and `tune_fft`.
- `-DCONVOLVER_FFT_BACKEND=OOURA`: Ooura's FFT built in (permissive licence),
  for a core, CLI and plug-in with no FFT library dependency. FFTW stays the
  default.
- VST3 buses of 1 to 8 channels each, with unequal input and output counts.
  `HostProcessor` accepts unequal counts. `convolver-vst3-preset` gains
  `--sample-rate`, `--input-channels` and `--output-channels`.

### Changed

- PCM24 output is now quantized by Convolver (round to nearest, ties up) instead
  of libsndfile's float conversion; samples can differ by at most 1 LSB.
- CMake consumers now request `find_package(Convolver 0.4)`; the package keeps
  its same-minor compatibility policy.

## [0.3.1] - 2026-10-18

### Fixed

- Inspection reports direct sound-file decoding and filesystem resolution failures
  as located diagnostics, and continues with later entries in `--all-configurations`.
- Invalid UTF-8 warnings now affect `--warnings-as-errors`, JSON validity, stderr
  and the exit status, including invalid bytes in diagnostic messages.
- Triangular PCM16 dither explicitly sequences random draws for reproducible output
  across compilers. Its noise distribution is unchanged; a compiler that previously
  evaluated the right operand first will produce a different seeded noise sequence.

## [0.3.0] - 2026-10-18

### Added

- Configuration compatibility corpus (`tests/compatibility/`): generated
  fixtures rendered through the CLI and checked against an independent
  direct-convolution reference at several block sizes and both latency modes.
- `convolver --inspect` and `convolver --validate`, with `--json` (report
  schema 1), `--syntax-only`, `--all-configurations`, `--sample-rate`,
  `--input-channels` and `--warnings-as-errors`.
- `convolver::inspect_configuration` (`convolver/inspection.hpp`, in
  `Convolver::io`) and `convolver::analyze_configuration` (core).
- `--profile portable-paths` (`InspectionProfile::portable_paths`): warnings
  for absolute references, references leaving the configuration's folder,
  letter-case differences from the names on disk, and names Windows cannot
  store. `docs/configuration-compatibility.md` records the evidence for each
  accepted feature and what is known about other readers of the format.
- A diagnostic for configurations in the format before 3.0 (no delay lines)
  that says which lines to add.
- Boundary tests for every configuration, list, IR and host-state limit
  (`native.limits`), and libFuzzer harnesses for configuration text, lists,
  host state and IR files (`tests/fuzz/`, `-DCONVOLVER_BUILD_FUZZERS=ON` with
  Clang). Committed seeds and regression inputs replay on every compiler as
  `fuzz.replay.*`; CI fuzzes each target for 60 seconds.
- `--speaker-map config` labels WAV output channels with the configuration's
  hex channel mask (WAVE_FORMAT_EXTENSIBLE), and `OutputMetadata` is an optional
  fifth `AudioWriter` argument, after `QuantizationOptions`. Masks must name
  one known speaker per output channel; samples keep their order. Without the
  flag output is unchanged.
- `AudioReader::declared_frames()`: the frame count a file header declares.
- Two new resolution limits: 16 MiB of configuration text read per load or
  inspection, and 256 inspection diagnostics followed by one
  `diagnostics-truncated` warning.

### Changed

- Configuration errors report the file, physical line and column, and the
  chain of enclosing lists. Rejected Windows drive, UNC and rooted paths name
  an existing relative replacement when one is found beside the configuration.
- `Convolver::io` now links `Convolver::core`. The package version is 0.3.0;
  consumers ask for `find_package(Convolver 0.3 ...)`.
- The configuration parser is separated from list resolution and IR decoding
  (`portable/config_parse.*`, private). Selection and rendered audio are
  unchanged, and so are accepted configurations, except that lists reading more
  than 16 MiB of configuration text in one load are now rejected (see Added).

### Fixed

- An IR whose header declares more than 64 Mi samples is rejected before any
  sample is read, instead of after decoding up to the limit.
- An audio file with an out-of-range sample rate or channel count in its header
  is reported as an unreadable file. Previously the exception escaped
  inspection and list traversal (found by fuzzing).

## [0.2.2] - 2026-10-18

### Added

- PCM16 dither. `Pcm16Quantizer` and `QuantizationOptions` define a deterministic,
  seeded policy with `none`, `rectangular`, `triangular` (TPDF) and
  `triangular_highpass` (the historical engine's "Triangular") modes.
  CLI options `--dither` and `--dither-seed`; dither is off unless requested.
- The benchmark reports p50, p99 and maximum single-block times and peak memory,
  and has two 6×6 matrix cases.

### Changed

- Paths with identical ordered input routes share one forward FFT and spectrum
  history. Output is bit-identical; dense routing matrices run 1.2 to 1.4 times
  faster and use less memory.
- PCM16 output rounds to nearest. libsndfile 1.2.2's float-to-PCM16 conversion
  with clipping enabled rounds down, which added a -0.5 LSB bias.

## [0.2.1] - 2026-10-18

### Added

- Automatic patch versions and annotated source tags on `main`, with one
  `VERSION` file shared by the CMake package, CLI and VST3 bundle.
- CLI and VST3 factory build identification including the Git revision and a
  marker for uncommitted changes. See [source versioning](docs/develop/versioning.md).

## [0.2.0] - 2026-10-18

### Added

- Portable host lifecycle (`Convolver::host`) and a VST3 adapter built against
  VST3 SDK 3.8.1, with adapter, preset and SDK validator tests in CI.
- Documented streaming contract for hosts.
- FFT backend notes and benchmarks (`docs/fft-backends.md`, `docs/benchmarks/`):
  FFTW build flags, planner modes, and packaging gaps on Apple Silicon and vcpkg.

### Changed

- Filter and history spectra are stored split per partition (real parts, then
  imaginary parts) so the multiply-accumulate vectorises. Output is unchanged;
  long filters run 1.15 to 1.3 times faster on x86-64.
- Windows CI builds FFTW with SSE2, AVX and AVX2.
- Windows VST3 builds link FFTW statically so hosts can load the plug-in.
- VST3 factory and macOS bundle versions now come from the CMake project version.
- The package version check accepts only matching minor versions, so consumers
  must now ask for `find_package(Convolver 0.2 ...)`.

## [0.1.0] - 2026-10-18

### Added

- Portable C++20 convolution core with FFTW and Apple Accelerate backends,
  FFTW wisdom support, and a libsndfile command-line tool.
- `.cfg` reader compatible with the historical Convolver format.
- CMake package exports (`Convolver::core`, `Convolver::io`), presets, and
  native oracle, sanitizer and historical regression tests on Linux, macOS and
  Windows.

### Fixed

- Windows FFTW DLL data imports, RAW pipe end-of-file handling, and portable
  documentation and config paths.

[Unreleased]: https://github.com/jrp2014/convolver/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/jrp2014/convolver/compare/v0.3.1...v0.4.0
[0.3.1]: https://github.com/jrp2014/convolver/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/jrp2014/convolver/compare/v0.2.2...v0.3.0
[0.2.2]: https://github.com/jrp2014/convolver/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/jrp2014/convolver/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/jrp2014/convolver/compare/82cf94b...v0.2.0
[0.1.0]: https://github.com/jrp2014/convolver/commit/82cf94b
