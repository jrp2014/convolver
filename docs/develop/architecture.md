<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Implementation map

[Documentation index](../README.md) · [Repository rules](../../AGENTS.md) · [Tests](testing.md)

This is a source map for contributors and coding agents. It describes the active
implementation at `9fa13f7` (0.4.44), reviewed 24 October 2026. Verify the current code
before changing it; dated plans are design context, not evidence of completion.
For the audio vocabulary, start with [concepts](../use/concepts.md).

## Components and source of truth

| Responsibility | Entry points and implementation | Dependencies / boundary |
| --- | --- | --- |
| In-memory filter graph and processing | [`engine.hpp`](../../include/convolver/engine.hpp): `Configuration`, `FilterPath`, `Route`, `Engine`; [`engine.cpp`](../../portable/engine.cpp) | `Convolver::core`; no file or SDK access |
| Validation, latency, tail and storage estimates | `validate_configuration`, `analyze_configuration`, `configuration_tail_frames` in the same files | Shared by engine construction and inspection; no FFT planning in analysis |
| Offline attenuation estimates | `estimate_attenuation` and `worst_case_peak` in [`attenuation.cpp`](../../portable/attenuation.cpp) | Core; `estimate_attenuation` prepares and runs an engine, and both allocate, so never call them from an audio callback |
| Partition multiply-accumulate kernels | [`kernels.hpp`](../../portable/kernels.hpp): `Kernels`, `kernels()`; bodies in [`kernels.inc`](../../portable/kernels.inc), compiled for the baseline in [`kernels.cpp`](../../portable/kernels.cpp) and for AVX2 with FMA in [`kernels_avx2.cpp`](../../portable/kernels_avx2.cpp); an Apple vDSP variant in [`kernels_vdsp.cpp`](../../portable/kernels_vdsp.cpp) is built only for the `CONVOLVER_EXPERIMENT_VDSP_MAC` benchmark (off by default) | Private to core; chosen once from the CPU's features when an engine is built; the sets differ by float rounding only (`native.kernel_divergence`) |
| FFT contract and implementations | [`fft.hpp`](../../portable/fft.hpp): `RealFft`, `fft_size`, `fft_storage`; [`fft_fftw.cpp`](../../portable/fft_fftw.cpp), [`fft_accelerate.cpp`](../../portable/fft_accelerate.cpp), [`fft_ooura.cpp`](../../portable/fft_ooura.cpp) | One backend per build; plan/storage details stay private |
| Host buffering, bypass and filter replacement | [`host.hpp`](../../include/convolver/host.hpp): `HostProcessor`; [`host.cpp`](../../portable/host.cpp) | `Convolver::host` depends on core; planar/in-place host buffers |
| Shared adapter contract, control side | `HostFilter`, `HostLayout`, `unfiltered_configuration`, `speaker_notice`, `HostFailure` in [`host.hpp`](../../include/convolver/host.hpp) | Which filter a load replaces, what is saved and what plays, and why a request failed (PLUG-8); every plug-in adapter uses it under its control lock |
| Status and the Convolver log (DIAG-1) | `HostStatus`, `host_status`, processing faults (`HostFaults`, `HostProcessor::faults`, `HostFilter::observe`, `fault_message`) in [`host.hpp`](../../include/convolver/host.hpp); `host_log`, `host_log_written`, `execution_line`, `loaded_line`, `rejected_line`, `started_line`, `playing_words` (what plays, shared with the editor report), `unfiltered_sound` (what Gain and Mix let through with no filter heard), `host_log_recent` in [`host_log.hpp`](../../portable/host_log.hpp), [`host_log.cpp`](../../portable/host_log.cpp); the editor report (PLUG-9: `host_report`, `report_fields`, `report_text`, `diagnostics_text`) in [`host_report.hpp`](../../portable/host_report.hpp), [`host_report.cpp`](../../portable/host_report.cpp) | Part of `Convolver::host`; file I/O on the control thread only; the VST3 and AU adapters show the line as a read-only Status parameter ([contract](plugin-contract.md#status-and-log)) |
| Embedded state | `HostState`, `encode_host_state`, `decode_host_state`; [`state.cpp`](../../portable/state.cpp) | Bounded wire format; control-side work |
| Sound files, RAW streams and integer conversion | [`io.hpp`](../../include/convolver/io.hpp): `AudioReader`, `AudioWriter`, `Quantizer`, `sound_file_header`; [`io.cpp`](../../portable/io.cpp) | `Convolver::io` adds libsndfile, whose detection (plus a few container markers it does not report) decides what is a sound file; `Pcm16Quantizer` wraps the 16-bit case |
| How the loader reads filter files | [`sound_files.hpp`](../../portable/sound_files.hpp): `SoundFile`, `SoundFiles`, `libsndfile_files` (in [`io.cpp`](../../portable/io.cpp)), `audio_toolbox_files` ([`audio_toolbox.cpp`](../../portable/audio_toolbox.cpp), macOS only); the checks both readers share in [`sound_files.cpp`](../../portable/sound_files.cpp) | The loader links no sound-file library: the tools, inspection and LV2 hand it libsndfile; the Audio Unit hands it macOS's AudioToolbox, so the component links nothing beyond macOS. `native.audio_toolbox` compares the two readers |
| Configuration grammar | [`config_parse.hpp`](../../portable/config_parse.hpp), [`config_parse.cpp`](../../portable/config_parse.cpp) | Private parser; retains tokens and source positions; no decoding |
| Resolution, list selection and decoding | `load_configuration`, `ConfigurationSelection`; [`config.cpp`](../../portable/config.cpp): `detail::load_configuration` (with a reader), `Resolver`, `decode_impulses`, `decode_audio_filter`, `raw_filter` | Private `convolver_configuration` library with the grammar, under `Convolver::io` (the public `load_configuration` there reads with libsndfile); file access and limits shared by rendering and inspection; syntax-only inspection opens no filter (`raw_filter_name` looks at the name only) |
| Inspection API | [`inspection.hpp`](../../include/convolver/inspection.hpp): `InspectionReport`; [`inspection.cpp`](../../portable/inspection.cpp) | Reports the same grammar and selection behavior; no output audio or FFT plans |
| CLI rendering and reports | [`main.cpp`](../../portable/main.cpp), [`inspection_cli.cpp`](../../portable/inspection_cli.cpp), [`compare_cli.cpp`](../../portable/compare_cli.cpp) (`--compare`), [`dither_advice.cpp`](../../portable/dither_advice.cpp) (dither defaults and the measured advice table) | Options, safe output creation, draining, report serialization and exit status |
| VST3 adapter | [`processor.hpp`](../../plugins/vst3/processor.hpp), [`processor.cpp`](../../plugins/vst3/processor.cpp): `Processor`, and `Controller` with the Gain, Mix, Latency, Bypass and Status parameters and the window's messages (VST3-3, VST3-4); [`factory.cpp`](../../plugins/vst3/factory.cpp) registers both classes; the window (VST3-11): [`editor.hpp`](../../plugins/vst3/editor.hpp), on macOS [`editor.mm`](../../plugins/vst3/editor.mm) (the SDK's `EditorView` showing the [shared macOS window](#the-shared-macos-window), its blocks reaching the controller) and elsewhere [`editor_none.cpp`](../../plugins/vst3/editor_none.cpp) (no window yet) | SDK boundary; stream processing goes through `HostProcessor`; the window talks only to the controller |
| Audio Unit adapter | [`effect.hpp`](../../plugins/au/effect.hpp) (`convolver::au::Effect`, an `AUBase`), defined in [`effect.cpp`](../../plugins/au/effect.cpp) (initialization, cleanup, stream formats, `Render`, latency and tail), [`effect_state.cpp`](../../plugins/au/effect_state.cpp) (saved state, a chosen file, installing or refusing a filter) and [`effect_properties.cpp`](../../plugins/au/effect_properties.cpp) (parameters, properties, the window's report, notifications and logging), with [`core_foundation.hpp`](../../plugins/au/core_foundation.hpp) (owned Core Foundation values); [`factory.cpp`](../../plugins/au/factory.cpp) (the bundle's only export); the window (AU-6): [`view_properties.hpp`](../../plugins/au/view_properties.hpp) (its custom properties), [`view_factory.mm`](../../plugins/au/view_factory.mm) (the `AUCocoaUIBase` factory, which shows the [shared macOS window](#the-shared-macos-window)); the Latency worker is a [`RequestWorker`](../../portable/request_worker.hpp) (PLUG-10: a thread that runs a task after each request, and a stop it never loses) | macOS only; AudioUnitSDK and adapter are C++23; bypass and reset are atomics applied in `Render`; the component is GPL-3.0-or-later ([plug-in guide](../use/plugins.md)) |
| LV2 adapter | [`plugin.cpp`](../../plugins/lv2/plugin.cpp), port numbers in [`plugin.hpp`](../../plugins/lv2/plugin.hpp); Turtle written by [`generate_ttl.py`](../../plugins/lv2/generate_ttl.py) from the identity registry; lilv test host in [`tests/lv2/host.hpp`](../../tests/lv2/host.hpp) | `lv2` headers and `Convolver::io` (libsndfile) for filter files; exports only `lv2_descriptor`; one plug-in URI per fixed layout; `HostFilter` and `HostProcessor` (two-step `prepare`/`publish` for worker loads) behind the LV2 threading rules ([guide](../use/plugins.md)) |
| Preset files for every format | [`presets.hpp`](../../portable/presets.hpp), [`presets.cpp`](../../portable/presets.cpp): `.vstpreset`, `.aupreset` and LV2 bundles around one encoded `HostState`; the tool [`preset_main.cpp`](../../portable/preset_main.cpp) (`convolver-preset`, and `convolver-vst3-preset` built with `CONVOLVER_VST3_PRESET_ALIAS`) | Private `convolver_presets` library; no plug-in SDK. The AU key and LV2 state URI are permanent ([presets guide](plugin-contract.md#preset-files)) |
| Filter file to plug-in state (PLUG-9) | [`filter_file.hpp`](../../portable/filter_file.hpp), [`filter_file.cpp`](../../portable/filter_file.cpp): `filter_file_state` (what `convolver-preset` embeds) and `read_filter_file` (the same for a plug-in's rate and channels, classifying a refusal) | Private `convolver_filter_file` library on `Convolver::host` and `convolver_configuration`, linked by the preset tool and the LV2 plug-in (reading with libsndfile) and the Audio Unit (reading with AudioToolbox); reads files, so never on the audio thread |
| Equalizer APO export | [`equalizer_apo.hpp`](../../portable/equalizer_apo.hpp), [`equalizer_apo.cpp`](../../portable/equalizer_apo.cpp): `make_export` turns configurations into Equalizer APO commands and mono IR files; the tool [`equalizer_apo_main.cpp`](../../portable/equalizer_apo_main.cpp) (`convolver-equalizer-apo`) | Text only, no audio processing; a device no filter fits is silenced ([guide](../use/equalizer-apo.md)) |
| Permanent plug-in identities and channel layouts | [`plugins/identities.json`](../../plugins/identities.json), read by [`ConvolverIdentities.cmake`](../../cmake/ConvolverIdentities.cmake) into `convolver::identity` | One registry for every format; adapters and release scripts never repeat its values ([guide](plugin-identities.md)) |
| Build, exports and feature switches | [`CMakeLists.txt`](../../CMakeLists.txt), [`CMakePresets.json`](../../CMakePresets.json), [`cmake/`](../../cmake/) | Static core/host/io components; see [package guide](library.md) |
| Version generation | [`VERSION`](../../VERSION), [`version_source.py`](../../scripts/version_source.py), [`ConvolverBuildVersion.cmake`](../../cmake/ConvolverBuildVersion.cmake) | One numeric version source; Git identity added to build strings |
| Shared fixtures and plug-in comparisons | [`examples/first-use/`](../../examples/first-use/README.md), [`fixtures.py`](../../tests/plugins/fixtures.py); [`harness.hpp`](../../tests/plugins/harness.hpp), [`oracle.hpp`](../../tests/native/oracle.hpp) | Test-only; each adapter's test implements `Adapter` over its own host API ([PLUG-2](testing.md#plug-in-comparisons-plug-2)) |
| Plug-in distribution | [`release-policy.json`](../../packaging/release-policy.json), [`scripts/release/`](../../scripts/release/), [`ConvolverPluginPackaging.cmake`](../../cmake/ConvolverPluginPackaging.cmake), [release workflow](../../.github/workflows/release.yml); `resolve.py` and `upstream.py` find each dependency's newest release and record it, `sources.py` fetches and checks the archives (format tools are resolved in the [VST3](../../.github/workflows/vst3.yml), [LV2](../../.github/workflows/lv2.yml) and [Audio Unit](../../.github/workflows/au.yml) workflows too) | Optional (`CONVOLVER_PACKAGE_PLUGINS`); where each dependency comes from (SDKs marked with the formats that use them), staging, installers and gates; tests in `tests/release/` |

### The shared macOS window

The Audio Unit (AU-6) and VST3 on macOS (VST3-11) show the same AppKit view,
from [`plugins/editor/mac/`](../../plugins/editor/mac/): the view and its
layout in [`diagnostic_view.mm`](../../plugins/editor/mac/diagnostic_view.mm),
its Gain, Mix and Latency rows and what the user does with them (dragging,
reset, typing, choosing; AU-10) in
[`setting_rows.mm`](../../plugins/editor/mac/setting_rows.mm), and the fonts,
labels and stack views both lay out with in
[`view_layout.h`](../../plugins/editor/mac/view_layout.h). It knows no plug-in
format: each format hands it blocks that read the report, the diagnostics
and the settings, change a setting (with `begin` and `end` around a drag
where the format marks gestures, as VST3's `beginEdit` and `endEdit` do),
optionally a bypass (VST3's Bypass checkbox), and load a file. Reload and
reloading when the file changes use the report's `filter path` and
`filter files` (from `HostRequest::files`, which `read_filter_file()`
fills); the Recent menu reads and writes the per-user list in
[`recent_files.hpp`](../../portable/recent_files.hpp), part of
`Convolver::host`. Each format compiles these files into its own bundle with
class names of its own, version included (`ConvolverDiagnosticView_<version>`
in the Audio Unit, `ConvolverVST3View_<version>` in VST3), because Objective-C
classes share one namespace per process. `scripts/check_repository.py` treats
`plugins/editor/` as shared code, not a format, and a change there runs every
workflow.

## Processing data flow

1. `load_configuration` resolves a sound file, configuration or list into an
   in-memory `Configuration`. No path lookup is needed during processing.
   `decode_impulses` opens and decodes each filter file once, however many
   paths name it (PERF-6), and keeps no cache between loads.
2. `Engine` validates that graph, prepares FFTs, partitions the IRs and allocates
   all buffers. Filter spectra receive their `1/N` normalization here.
3. `Engine::process` accepts interleaved float frames, applies input delays, and
   stages B frames. `Engine::Impl::compute` does the block convolution.
4. `same_mix` groups identical **ordered** input routes, including gain bits.
   Each `Mix` owns one forward-transform history shared by its paths. Each path
   retains its own filter spectra, inverse transform contribution and overlap.
5. Results are added in path/route order, pass through output delays, and leave
   through the output staging buffer. Caller chunk sizes do not change the stream.
6. `reset` invalidates processing history: each `Mix` counts the spectra
   written since construction or reset, and paths multiply only those
   partitions (PERF-5), so neither reset nor the blocks after it pay for
   history known to be silence. It does not rebuild plans or read files.

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
| State | Bounded transactional decoding; no partial application of invalid state. `HostFilter::load` builds everything it keeps before its install callback publishes, so what plays is what is saved even when memory runs out | `native.state`, `native.host` (`allocation_failures`), `vst3.adapter`, `lv2.adapter` |
| Failure reasons | Every failed request, and every reason an instance plays silence, is kept as a `HostFailure` on the control side; never touched by the audio thread; a request overtaken by a newer one (`SupersededRequest`) records nothing | `native.host` (`failure_reasons`), `vst3.adapter` (`failure_records`), `lv2.adapter`, `au.adapter` |
| Identities and old presets | Released plug-in identities never change; version-1 state loads on any arrangement with matching counts | `identities`, `vst3.identities`, `vst3.adapter` (`version1_presets`) |
| Output files | Protect inputs, IRs and configurations, including aliases; do not claim transactional writes | `native.utilities`, `native.cli` |

The host thread contract is in [`host.hpp`](../../include/convolver/host.hpp): one
audio thread calls `process`, `reset`, `set_bypass`, `mute` and `publications`; control calls are
serialized. Only `replace`, `prepare`, `publish` and `collect` may overlap audio
processing (`prepare` and `publish` are `replace` in two steps, for an adapter
that checks for a newer request in between). Construction,
reconfiguration and destruction require stopped audio. Replaced engines are
retired on the control side so FFT destruction cannot enter the callback.
The [plug-in adapter contract](plugin-contract.md#replacement-and-threads) records adapter transition timing.

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
| Host lifecycle or state | Host/state implementation, VST3, AU and LV2 adapters | `native.host`, `native.host_lifecycle`, `native.state`, host-state fuzz replay, all VST3 tests including `vst3.harness` and `vst3.lifecycle`, `ctest -L lv2`, and on macOS `au.harness`, `au.adapter` and `au.lifecycle` |
| Plug-in identities or layouts | [Plug-in identities](plugin-identities.md); never change a released value | `identities` (also pre-commit), `vst3.identities`, `vst3.adapter` |
| Preset files or the preset tool | [Presets guide](../use/presets.md); version-1 state must keep loading | `native.presets`, `plugins.presets`, and with the SDK `vst3.preset_format` and `vst3.preset` |
| The Equalizer APO export | [Equalizer APO guide](../use/equalizer-apo.md); the model in the test must follow Equalizer APO's sources | `export.equalizer_apo`, and `export.equalizer_apo.runtime` in Equalizer APO itself on Windows CI; report their worst errors |
| A plug-in adapter | Its format folder, the [shared adapter contract](plugin-contract.md) | Its `Adapter` driver for the [PLUG-2 harness](testing.md#plug-in-comparisons-plug-2), its validator, pluginval where it applies |
| The macOS window | `plugins/editor/mac/` and both formats that show it | On macOS `au.editor` and `vst3.editor` |
| First-use examples or their generator | `examples/first-use/`, `tests/plugins/fixtures.py`, marked commands in the READMEs | `examples.fixtures`, `examples.first_use`, then every adapter's harness test |
| Adding a native test | `convolver_native_test` in [`CMakeLists.txt`](../../CMakeLists.txt) | The new test is built by `cmake --build build --target check` from a fresh build directory, and is listed by `ctest -N -L native` |
| Package/dependencies | Top-level CMake, `cmake/`, presets and workflow matrix | Full build plus `core-only` and `packaging.consumer.*` |
| Versioning | `SOURCE_PATHS`, build-version CMake and version workflow | `python3 tests/versioning_tests.py`; do not run the mutating helper as a dry run |
| Documentation | [Page ownership and maintenance](documentation.md) | Markdown/link checks; run changed examples; preserve historical hashes |

A private helper signature can have callers in `tests/fuzz/`; search all callers
and build all enabled targets, not only the nearby test executable. A callback
heap guard does not prove that every external library is allocation-free or that
callbacks meet deadlines. Report the actual checks and their limits.

## Dependency choices

[Which libraries each build uses](build.md#which-libraries-each-build-uses)
lists the FFT and sound-file library of every build and how to change them.
The reasons: on macOS every build prefers what macOS includes (Accelerate), so
nothing extra is installed or shipped, and the other platforms' libraries stay
build options. Plug-in builds never default to FFTW, because other code in the
host process may call the same FFTW, whose planner Convolver cannot lock
([plug-in builds](fft-backends.md#plug-in-builds)); on Linux and Windows they
use the built-in Ooura. The [FFT guide](fft-backends.md) has the numerical
contract and dated measurements; none is asserted to be fastest for every
workload.

[libsndfile](https://libsndfile.github.io/libsndfile/) remains the file
decoding and encoding boundary. The core and host libraries can omit it, and
the Audio Unit reads filter files with macOS's AudioToolbox instead, so that
the component needs nothing beyond macOS. It is
neither an audio-device layer nor a resampler. Replacing it would need a
concrete format, packaging or correctness benefit and matching real-file tests;
macOS's AudioToolbox, considered for the tools, did not offer one
([sound files](build.md#which-libraries-each-build-uses)).

Plug-in adapters use each format's own headers (VST3 SDK, AudioUnitSDK, `lv2`)
rather than a framework; builds take each one's newest release
([build from source](build.md#supporting-libraries)). Keep standard C++20
facilities where suitable (the Audio Unit target alone is C++23, for
AudioUnitSDK, which has needed it since 1.4.0) and platform-specific operations at the I/O, FFT or host
boundary. A new library or language standard should earn its deployment cost.
Alternative FFT implementations are investigations in
[TODO](../../TODO.md#fft-libraries-and-performance), not implicit fallbacks.

## Where future work belongs

Keep this page about implemented structure. Put open work in [TODO](../../TODO.md),
substantial designs in a dated plan in [`project/plans/`](../project/plans/),
and jrp's decisions in the [decisions log](../project/decisions.md). Prefer a
focused change at the existing boundary over reorganizing files merely to
shorten them. Move code into a shared layer only when two formats need the
same behaviour, as `HostFilter` was.

A new plug-in format follows the VST3 layout: the adapter in
`plugins/<format>/`, its tests in `tests/<format>/`, and a column for it in
[how the plug-ins behave](../use/plugins.md). The
[repository checks](workflow.md#automated-checks) require all three once the
adapter folder exists, and `scripts/check_format.py` covers the new sources
without a file list to update.
The old Windows applications and extracted regression harnesses are explained in
[history](../project/history.md); they are not an alternate implementation of the current API.
