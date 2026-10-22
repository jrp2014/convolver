<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Implementation map

[Documentation index](README.md) · [Repository rules](../AGENTS.md) · [Tests](testing.md)

This is a source map for contributors and coding agents. It describes the active
implementation at `2fe503e`, reviewed 20 October 2026. Verify the current code
before changing it; dated plans are design context, not evidence of completion.
For the audio vocabulary, start with [concepts](concepts.md).

## Components and source of truth

| Responsibility | Entry points and implementation | Dependencies / boundary |
| --- | --- | --- |
| In-memory filter graph and processing | [`engine.hpp`](../include/convolver/engine.hpp): `Configuration`, `FilterPath`, `Route`, `Engine`; [`engine.cpp`](../portable/engine.cpp) | `Convolver::core`; no file or SDK access |
| Validation, latency, tail and storage estimates | `validate_configuration`, `analyze_configuration`, `configuration_tail_frames` in the same files | Shared by engine construction and inspection; no FFT planning in analysis |
| Offline attenuation estimates | `estimate_attenuation` in [`attenuation.cpp`](../portable/attenuation.cpp) | Core; prepares and runs an engine, so never call it from an audio callback |
| FFT contract and implementations | [`fft.hpp`](../portable/fft.hpp): `RealFft`, `fft_size`, `fft_storage`; [`fft_fftw.cpp`](../portable/fft_fftw.cpp), [`fft_accelerate.cpp`](../portable/fft_accelerate.cpp), [`fft_ooura.cpp`](../portable/fft_ooura.cpp) | One backend per build; plan/storage details stay private |
| Host buffering, bypass and filter replacement | [`host.hpp`](../include/convolver/host.hpp): `HostProcessor`; [`host.cpp`](../portable/host.cpp) | `Convolver::host` depends on core; planar/in-place host buffers |
| Shared adapter contract, control side | `HostFilter`, `HostLayout`, `unfiltered_configuration` in [`host.hpp`](../include/convolver/host.hpp) | Which filter a load replaces, what is saved and what plays; every plug-in adapter uses it under its control lock |
| Embedded state | `HostState`, `encode_host_state`, `decode_host_state`; [`state.cpp`](../portable/state.cpp) | Bounded wire format; control-side work |
| Sound files, RAW streams and integer conversion | [`io.hpp`](../include/convolver/io.hpp): `AudioReader`, `AudioWriter`, `Quantizer`; [`io.cpp`](../portable/io.cpp) | `Convolver::io` adds libsndfile; `Pcm16Quantizer` wraps the 16-bit case |
| Configuration grammar | [`config_parse.hpp`](../portable/config_parse.hpp), [`config_parse.cpp`](../portable/config_parse.cpp) | Private parser; retains tokens and source positions; no decoding |
| Resolution, list selection and decoding | `load_configuration`, `ConfigurationSelection`; [`config.cpp`](../portable/config.cpp): `Resolver`, `decode_impulses`, `decode_audio_filter` | File access and limits shared by rendering and inspection |
| Inspection API | [`inspection.hpp`](../include/convolver/inspection.hpp): `InspectionReport`; [`inspection.cpp`](../portable/inspection.cpp) | Reports the same grammar and selection behavior; no output audio or FFT plans |
| CLI rendering and reports | [`main.cpp`](../portable/main.cpp), [`inspection_cli.cpp`](../portable/inspection_cli.cpp) | Options, safe output creation, draining, report serialization and exit status |
| VST3 adapter | [`processor.cpp`](../plugins/vst3/processor.cpp), [`processor.hpp`](../plugins/vst3/processor.hpp) | SDK boundary; stream processing goes through `HostProcessor` |
| Preset files for every format | [`presets.hpp`](../portable/presets.hpp), [`presets.cpp`](../portable/presets.cpp): `.vstpreset`, `.aupreset` and LV2 bundles around one encoded `HostState`; the tool [`preset_main.cpp`](../portable/preset_main.cpp) (`convolver-preset`, and `convolver-vst3-preset` built with `CONVOLVER_VST3_PRESET_ALIAS`) | Private `convolver_presets` library; no plug-in SDK. The AU key and LV2 state URI are permanent ([presets guide](presets.md#what-the-files-contain)) |
| Permanent plug-in identities and channel layouts | [`plugins/identities.json`](../plugins/identities.json), read by [`ConvolverIdentities.cmake`](../cmake/ConvolverIdentities.cmake) into `convolver::identity` | One registry for every format; adapters and release scripts never repeat its values ([guide](plugin-identities.md)) |
| Build, exports and feature switches | [`CMakeLists.txt`](../CMakeLists.txt), [`CMakePresets.json`](../CMakePresets.json), [`cmake/`](../cmake/) | Static core/host/io components; see [package guide](cmake-package.md) |
| Version generation | [`VERSION`](../VERSION), [`version_source.py`](../scripts/version_source.py), [`ConvolverBuildVersion.cmake`](../cmake/ConvolverBuildVersion.cmake) | One numeric version source; Git identity added to build strings |
| Shared fixtures and plug-in comparisons | [`examples/first-use/`](../examples/first-use/README.md), [`fixtures.py`](../tests/plugins/fixtures.py); [`harness.hpp`](../tests/plugins/harness.hpp), [`oracle.hpp`](../tests/native/oracle.hpp) | Test-only; each adapter's test implements `Adapter` over its own host API ([PLUG-2](testing.md#plug-in-comparisons-plug-2)) |
| Plug-in distribution | [`release-policy.json`](../packaging/release-policy.json), [`scripts/release/`](../scripts/release/), [`ConvolverPluginPackaging.cmake`](../cmake/ConvolverPluginPackaging.cmake), [release workflow](../.github/workflows/release.yml); format tool pins in the [LV2](../.github/workflows/lv2.yml) and [Audio Unit](../.github/workflows/au.yml) workflows; weekly [update check](../.github/workflows/updates.yml) (`check_updates.py`) | Optional (`CONVOLVER_PACKAGE_PLUGINS`); pinned dependencies (SDKs marked with the formats that use them), staging, installers and gates; tests in `tests/release/` |

## Processing data flow

1. `load_configuration` resolves a sound file, configuration or list into an
   in-memory `Configuration`. No path lookup is needed during processing.
2. `Engine` validates that graph, prepares FFTs, partitions the IRs and allocates
   all buffers. Filter spectra receive their `1/N` normalization here.
3. `Engine::process` accepts interleaved float frames, applies input delays, and
   stages B frames. `Engine::Impl::compute` does the block convolution.
4. `same_mix` groups identical **ordered** input routes, including gain bits.
   Each `Mix` owns one forward-transform history shared by its paths. Each path
   retains its own filter spectra, inverse transform contribution and overlap.
5. Results are added in path/route order, pass through output delays, and leave
   through the output staging buffer. Caller chunk sizes do not change the stream.
6. `reset` clears processing history. It does not rebuild plans or read files.

The FFT interface uses interleaved real/imaginary bins. The engine stores its
partition spectra in separate real and imaginary arrays for multiply-accumulate
loops. Do not confuse these two layouts when changing a backend. All backends
expose an unscaled forward DFT and an inverse returning N times the real signal.
See [FFT contracts](fft-backends.md#selection-and-contract).

## Invariants to preserve

| Area | Contract | Evidence location |
| --- | --- | --- |
| Audio callback | No allocation, locking, file I/O or FFT planning in `Engine::process` or its callees | `native.engine`; inspect callees as well as the heap guard |
| Timing | B-frame core latency; tail excludes latency; arbitrary caller chunks and reset preserve defined behavior | `native.engine`, `native.host`, `native.cli` |
| Numeric changes | Independent direct DFT and double-precision convolution references; backend agreement alone is insufficient | `fft_tests.cpp`, `engine_tests.cpp`, backend comparison script |
| Routes | Preserve order, repeated-route addition, fractional gains and signed zero | Parser, engine, I/O and compatibility corpus tests |
| Selection | First matching list entry; relative paths beside their owner; no implicit resampling or guessed drive mapping | `config.cpp`, `native.inspection`, `compatibility.corpus` |
| Errors | Expected input errors become located diagnostics; resource exhaustion propagates; all-entry validation can continue | `native.inspection_cli`, `native.limits` |
| Integer output | Seeded draw order and per-channel shaping state are independent of caller chunking | `native.io`, `native.cli` |
| State | Bounded transactional decoding; no partial application of invalid state | `native.state`, `vst3.adapter` |
| Identities and old presets | Released plug-in identities never change; version-1 state loads on any arrangement with matching counts | `identities`, `vst3.identities`, `vst3.adapter` (`version1_presets`) |
| Output files | Protect inputs, IRs and configurations, including aliases; do not claim transactional writes | `native.utilities`, `native.cli` |

The host thread contract is in [`host.hpp`](../include/convolver/host.hpp): one
audio thread calls `process`, `reset` and `set_bypass`; control calls are serialized.
Only `replace` and `collect` may overlap audio processing. Construction,
reconfiguration and destruction require stopped audio. Replaced engines are
retired on the control side so FFT destruction cannot enter the callback.
The [VST3 guide](vst3.md#processing-and-lifecycle) records adapter transition timing.

Keep release binaries compatible with the declared CPU baseline. Do not enable
blanket fast-math or machine-specific compilation flags without an explicit
compatibility decision and numerical tests. Benchmark a separate optimized build
when evaluating such changes.

## Change and test map

Use these as focused checks while developing, then run the full applicable CTest
configuration before reporting success. [Testing](testing.md) gives commands.

| If changing… | Inspect together | Focused verification |
| --- | --- | --- |
| Convolution, routes or delays | `engine.cpp`, analysis/tail functions, host assumptions | `native.engine`, `native.host`, `native.cli`; report worst oracle error |
| FFT backend | `fft.hpp`, all three adapters, CMake discovery/exports | `native.fft`, `native.engine`, `native.fft_planning`, external consumer, cross-backend comparison |
| Configuration syntax or selection | Parser, resolver, inspection, CLI and preset selectors | `native.config_parse`, `native.inspection`, `native.inspection_cli`, corpus, config fuzz replays |
| Decoder or quantizer | I/O API, CLI validation, direct-audio decoding, IR fuzz caller | `native.io`, `native.cli`, `native.limits`, `fuzz.replay.ir_file` |
| Host lifecycle or state | Host/state implementation and VST3 adapter | `native.host`, `native.state`, host-state fuzz replay and all VST3 tests, including `vst3.harness` |
| Plug-in identities or layouts | [Plug-in identities](plugin-identities.md); never change a released value | `identities` (also pre-commit), `vst3.identities`, `vst3.adapter` |
| Preset files or the preset tool | [Presets guide](presets.md); version-1 state must keep loading | `native.presets`, `plugins.presets`, and with the SDK `vst3.preset_format` and `vst3.preset` |
| A plug-in adapter | Its format folder, the [shared adapter contract](plans/2026-10-21-plugin-formats.md#shared-adapter-contract) | Its `Adapter` driver for the [PLUG-2 harness](testing.md#plug-in-comparisons-plug-2), its validator, pluginval where it applies |
| First-use examples or their generator | `examples/first-use/`, `tests/plugins/fixtures.py`, marked commands in the READMEs | `examples.fixtures`, `examples.first_use`, then every adapter's harness test |
| Package/dependencies | Top-level CMake, `cmake/`, presets and workflow matrix | Full build plus `core-only` and `packaging.consumer` |
| Versioning | `SOURCE_PATHS`, build-version CMake and version workflow | `python3 tests/versioning_tests.py`; do not run the mutating helper as a dry run |
| Documentation | [Page ownership and maintenance](documentation.md) | Markdown/link checks; run changed examples; preserve historical hashes |

A private helper signature can have callers in `tests/fuzz/`; search all callers
and build all enabled targets, not only the nearby test executable. A callback
heap guard does not prove that every external library is allocation-free or that
callbacks meet deadlines. Report the actual checks and their limits.

## Where future work belongs

Keep this page about implemented structure. Put open work in [TODO](../TODO.md)
and substantial design decisions in a linked, dated plan in [`plans/`](plans/).
Prefer a focused change at the existing boundary over reorganizing files merely
to shorten them.

A new plug-in format follows the VST3 layout: the adapter in
`plugins/<format>/`, its tests in `tests/<format>/`, and its guide at
`docs/<format>.md`, listed in the [index](README.md). The
[repository checks](workflow.md#automated-checks) require all three once the
adapter folder exists, and `scripts/check_format.py` covers the new sources
without a file list to update.
The old Windows applications and extracted regression harnesses are explained in
[history](history.md); they are not an alternate implementation of the current API.
