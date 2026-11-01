<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Decisions

[Documentation index](../README.md) · [Status](status.md) · [TODO](../../TODO.md)

For anyone who needs to know what jrp has already decided, so the question is
not asked again. Each row names the decision and links to the page that
explains it and owns the details.

**In short:** jrp decides anything permanent or user-facing: identities, state
and preset formats, `.cfg` syntax, licences, defaults that change the sound,
shipped dependencies and publishing ([workflow](../develop/workflow.md)). When
jrp decides, record it here with its date; no other page carries decision
notes or their dates ([workflow](../develop/workflow.md#while-working)). A
change users will notice, and every finished TODO item, also goes in the
[changelog](../../CHANGELOG.md). Changing a decision is a new decision: add a row
and mark the old one replaced, rather than editing it.

Contents: [direction](#direction) · [releases](#releases) ·
[plug-in behaviour](#plug-in-behaviour) ·
[identities and file formats](#identities-and-file-formats) ·
[configuration and sound](#configuration-and-sound) · [diagnostics](#diagnostics) ·
[licences and dependencies](#licences-and-dependencies) ·
[documentation](#documentation)

## Direction

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | Favour stability and longevity over features | [Plug-in formats plan](plans/2026-10-21-plugin-formats.md#development-frameworks) |
| 21 October 2026 | VST3, LV2 and AU are equal first priorities, developed in parallel. CLAP and an editor wait for a request (the editor requested 24 October 2026). Adapters use each format's own headers, not a framework (DPF is the fallback if a shared editor is ever needed; the fallback replaced 24 October 2026: native views) | [Plug-in formats plan](plans/2026-10-21-plugin-formats.md) |
| 21 October 2026 | The FFmpeg filter is parked: the pipe covers it, and a built-in filter would mean maintaining a patched FFmpeg | [FFmpeg plan](plans/2026-10-21-ffmpeg-filter.md) |
| 21 October 2026 | Windows-native adapters (Media Foundation, APO) are parked | [Media Foundation plan](plans/2026-10-21-media-foundation-transform.md) |
| 22 October 2026 | System-wide filtering on Windows goes through Equalizer APO: an export from `.cfg`, with no Windows audio code of our own | [Equalizer APO export](../use/equalizer-apo.md) |
| 22 October 2026 | Next milestone: an independent user installs and uses an exact preview package by following the documentation | [Status](status.md#next-milestone-an-independent-user-runs-a-preview), [guidance](../review-2026-10/release-readiness-guidance-2026-10-22.md) |
| 22 October 2026 | REAPER and Carla are the suggested first named hosts; jrp will try them (replaced 23 October 2026) | [Host testing](../develop/host-testing.md#first-hosts) |
| 23 October 2026 | jrp's first named hosts, on his Mac: REAPER and Audacity for VST3, GarageBand for the Audio Unit (a smoke test only). Replaces REAPER and Carla | [Host testing](../develop/host-testing.md#first-hosts) |
| 23 October 2026 | The host-facing part of DIAG-1 comes before the named-host smoke tests. VST3 and AU show why they are silent in a read-only Status parameter and write the full explanation to a Convolver log file (and the macOS unified log), detailed enough to send with a report | [Adapter contract](../develop/plugin-contract.md#status-and-log), [plug-ins](../use/plugins.md#status-and-the-convolver-log) |
| 24 October 2026 | jrp asked for a plug-in editor window ("helpful, not least for debugging"), the request the 21 October direction waited for. Its framework, order and contents were open choices in the plan (all answered by 25 October 2026) | [Plug-in editor plan](plans/2026-10-24-plugin-editor.md#decisions-for-jrp) |
| 24 October 2026 | The plug-in editor also configures the plug-in as the old Convolver's windows did, using them "as inspiration for the new setup" rather than copying them; better, more ergonomic ideas are welcome. Which settings and their saved form were open choices in the plan (answered 25 October 2026) | [Plug-in editor plan](plans/2026-10-24-plugin-editor.md#what-the-old-convolver-configured) |
| 24 October 2026 | The plug-in editor uses native views with no framework: one AppKit view on macOS for VST3 and the Audio Unit, a Win32 view on Windows, the content in shared code. Replaces DPF as the fallback for a shared editor; VSTGUI and DPF were set aside for their lack of accessibility (and, for DPF, replacing our adapters). The Linux window's toolkit is chosen in VST3-13 (decided 25 October 2026: VSTGUI as a stop-gap) | [Plug-in editor plan](plans/2026-10-24-plugin-editor.md#framework-choice) |
| 25 October 2026 | Plug-ins built with FFTW offer FFTW planning as an optional setting (Quick by default, or Measured), with a tooltip stating the measured gain (jrp asked for "at most 6%" if that is what was found; the whole-engine tests showed 2–6%, and PLUG-11 measures short filters before the figure is fixed); released plug-ins keep Ooura or Accelerate and no planning setting. Dither is not offered in the plug-ins | [Plug-in editor plan](plans/2026-10-24-plugin-editor.md#settings) |
| 25 October 2026 | The Linux VST3 editor window uses VSTGUI as a stop-gap (a BSD-3-Clause dependency of the Linux VST3 build only); macOS and Windows keep native views. Editor accessibility is highly desirable but not essential: gaps are recorded, not blocking. The window offers drag and drop, recent files (a per-user file beside the log) and reload on change, and does not save the chosen path with the project | [Plug-in editor plan](plans/2026-10-24-plugin-editor.md#decisions-for-jrp) |
| 24 October 2026 | Use the latest stable Clang formatting and analysis tools, replacing the version-18 policy. Local installation on jrp's Mac is deferred. CI going red on a new release is accepted: layout changes get a separate reformat commit, new clang-tidy findings are fixed in the code | [Contributor tooling](../../CONTRIBUTING.md#formatting-and-analysis), [new releases](../../CONTRIBUTING.md#when-a-new-clang-or-ruff-release-turns-ci-red) |
| 29 October 2026 | ruff formats and lints the maintained Python, unpinned like the Clang tools (the latest stable release), with 120 columns, single quotes and as large a rule set as is reasonable (CI-18). As with Clang, Lint going red on a new ruff release is accepted: layout changes get a separate reformat commit, and new rule findings are fixed in the code, not added to the ignore list | [Python formatting and lint](../../CONTRIBUTING.md#python-formatting-and-lint), [new releases](../../CONTRIBUTING.md#when-a-new-clang-or-ruff-release-turns-ci-red) |

## Releases

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | The first releases are unsigned previews on macOS (ad-hoc signature, quarantine removed by the user) and Windows (SmartScreen warning). Signing stays a production gate | [Releasing](../develop/releasing.md#publishing-an-unsigned-preview) |
| 21 October 2026 | Publishing is a manual step owned by jrp, from one candidate run | [Releasing](../develop/releasing.md#publishing-an-unsigned-preview) |
| 21 October 2026 | Plug-in builds use Ooura on Linux and Windows and Accelerate on macOS (PLUG-1) | [FFT guide](../develop/fft-backends.md#plug-in-builds) |
| 22 October 2026 | Preview packages ship `convolver-preset` and `convolver-equalizer-apo`, not the older `convolver-vst3-preset` | [TODO](../../TODO.md) PLUG-5 |
| 23 October 2026 | The independent walkthrough (REL-3) uses the chosen candidate run's exact files before they are uploaded, so the pre-release can say what was demonstrated | [Releasing](../develop/releasing.md#publishing-an-unsigned-preview) |
| 23 October 2026 | The first preview holds VST3, the `convolver` CLI and the preset tool; LV2 and AU join later previews once their own journeys are demonstrated (AU moved into the first preview on 25 October 2026) | [Status](status.md#user-journeys-for-the-first-preview) |
| 24 October 2026 | CI runs each kind of test only where a configuration can change its result; plug-in jobs skip what an identical native or Audio Unit job runs, and the release candidates no longer run the CMake package consumers | [Testing](../develop/testing.md#which-configurations-run-which-tests) |
| 24 October 2026 | REL-4's repository side: a README line for users of the old Windows Convolver, issue forms, and a social preview source, with the steps after publishing | [TODO](../../TODO.md) REL-4, [releasing](../develop/releasing.md#after-publishing-make-it-findable) |
| 24 October 2026 | The old SourceForge website stays as it is for now, without a banner pointing here; the SourceForge project summary points to GitHub | [TODO](../../TODO.md) REL-4 |
| 24 October 2026 | Start the 0.5 series before end-to-end testing, so the first preview is cut from a 0.5.x tag (replaced on 25 October 2026 by the 0.6 series, below) | [Status](status.md#next-milestone-an-independent-user-runs-a-preview), [versioning](../develop/versioning.md#minor-and-major-versions) |
| 25 October 2026 | Start the 0.6 series rather than restore 0.5.x API compatibility, and keep no bridges to older library interfaces until the API is more stable; the project must stay internally consistent. The first preview is cut from a 0.6.x tag (replaced on 31 October 2026 by the 0.7 series, below) | [Versioning](../develop/versioning.md#minor-and-major-versions), [CHANGELOG](../../CHANGELOG.md) |
| 30 October 2026 | On macOS, builds prefer Apple's own libraries and keep the other platforms' libraries as build-time fallbacks. For the FFT this means Accelerate by default for every build, the command-line tools included ("Prioritise Accelerate for the FFT, but allow FFTW to be used instead" for VST3, then "apply the same approach to the cli tools"); FFTW stays available with `-DCONVOLVER_FFT_BACKEND=FFTW` (FFT-3). The tools keep libsndfile for reading and writing sound files: AudioToolbox could do both, but reading alone gains nothing while the tools write with libsndfile, and writing too would mean a second, macOS-only writer whose behaviour differs slightly, while the other platforms and the packages need libsndfile anyway ("agreed") | [Which libraries each build uses](../develop/build.md#which-libraries-each-build-uses), [FFT guide](../develop/fft-backends.md#accelerate-by-default-on-macos) |
| 31 October 2026 | Start the 0.7 series as a milestone, once the Audio Unit's window (AU-6) and the pre-preview fixes (PLUG-12, ROBUST-3) are merged, so the first preview is cut from a 0.7.x tag (replaced on 1 November 2026 by the 0.8 series, below) | [Status](status.md#next-milestone-an-independent-user-runs-a-preview), [versioning](../develop/versioning.md#minor-and-major-versions) |
| 1 November 2026 | Version 0.8.0 for PLUG-10's library change (`HostProcessor` takes its block size from the settings), following the rule that an incompatible API change before 1.0 needs a minor step; the first preview is cut from a 0.8.x tag | [Versioning](../develop/versioning.md#minor-and-major-versions), [CHANGELOG](../../CHANGELOG.md) |

## Plug-in behaviour

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | A failed replacement keeps the running filter in every format, with an explicit error | [Adapter contract](../develop/plugin-contract.md) |
| 21 October 2026 | An empty instance passes audio through unchanged only with equal input and output counts; with unequal counts it plays silence | [Adapter contract](../develop/plugin-contract.md) |
| 21 October 2026 | A failed first load plays silence, never unfiltered audio, with an error shown where the format allows | [Adapter contract](../develop/plugin-contract.md) |
| 22 October 2026 | Bypass with unequal counts fades every output to silence | [Adapter contract](../develop/plugin-contract.md) |
| 22 October 2026 | VST3 counts its configuration as known only after activation (JUCE hosts call `setupProcessing` with placeholder values first); no message for a mismatch found at activation until VST3-3 (the message part replaced 23 October 2026: the Status parameter and Convolver log report it, DIAG-1) | [Adapter contract](../develop/plugin-contract.md) |
| 22 October 2026 | Saving after a failed first load records the failure (host state version 2), so reopening stays silent | [Adapter contract](../develop/plugin-contract.md) |
| 23 October 2026 | VST3 speaker arrangements are checked with a notice only: no enforced layouts, no host state version 3 and no buses above 8 channels; revisit on request | [Plug-in identities](../develop/plugin-identities.md#channel-layouts) |
| 23 October 2026 | Whether saved state records bypass follows each format's convention: VST3 saves its bypass parameter; LV2 and Audio Units leave bypass to the host and ignore the flag in restored state | [Adapter contract](../develop/plugin-contract.md#bypass) |
| 24 October 2026 | Clarification of the audio thread's mute: silence ends only when a filter published after the mute takes over; one published before it but not yet picked up stays silent (PLUG-7, PR #58) | [Adapter contract](../develop/plugin-contract.md#replacement-and-threads) |
| 24 October 2026 | The Status parameter (VST3, AU) and LV2 status port gain a fifth value, Fault, shown when the audio thread silenced output that overflowed, rejected invalid input or caught a processing failure while a filter was loaded, rather than still saying the filter is loaded. Each fault is explained in the Convolver log (DIAG-1) | [Adapter contract](../develop/plugin-contract.md#status-and-log) |
| 25 October 2026 | The first preview waits for the editor's settings (gain, mix, latency) and windows: VST3 on macOS, Windows and Linux (PLUG-9, PLUG-10, VST3-3, VST3-4, VST3-11 to VST3-13). It also holds the Audio Unit, with the same settings and the shared macOS window (AU-3 to AU-7); LV2 still joins later | [Plug-in editor plan](plans/2026-10-24-plugin-editor.md#phases-and-todo-items) |
| 31 October 2026 | The first preview keeps the 25 October scope (all three desktop platforms' windows), not a macOS-only preview as jrp's review of `6920a45` suggested; an earlier, explicitly limited macOS walkthrough of the stereo journey (REL-5) runs first as a usability trial | [TODO](../../TODO.md#user-experience-and-distribution) (REL-5) |
| 31 October 2026 | The Audio Unit keeps reading and preparing a chosen filter on the main thread for now: work on load performance (a worker thread, as LV2 has) is deferred. In GarageBand a 1.3-million-tap filter showed its report after about 1.4 s and a 5.3-million-tap one after about 1.7 s, observation included; whether the host stays responsive and the audio uninterrupted during the load is not qualified | [TODO AU-6](../../TODO.md) |
| 1 November 2026 | A new Audio Unit Latency (PLUG-10, AU-7) plays at once, with a gap in the sound while the new processor starts, rather than at the next `Initialize`: GarageBand starts the unit before it restores a project | [Adapter contract](../develop/plugin-contract.md#settings) |

## Identities and file formats

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | AU codes `aufx Conv CONV` | [Plug-in identities](../develop/plugin-identities.md) |
| 22 October 2026 | LV2 URI scheme `https://github.com/jrp2014/convolver/lv2#discrete-N-M`, the eight fixed LV2 layouts, and `io.github.jrp2014.convolver.*` macOS identifiers | [Plug-in identities](../develop/plugin-identities.md) |
| 22 October 2026 | Host state version 2 only marks a failed first load; everything else is still written as version 1 | [Plug-in identities](../develop/plugin-identities.md) |
| 22 October 2026 | AU presets keep Apple's standard keys and hold the state under `convolver-state`; LV2 presets hold it as an `atom:Chunk` under the base URI plus `state` | [Adapter contract](../develop/plugin-contract.md#preset-files) |
| 23 October 2026 | The LV2 filter parameter and the saved file path share one URI, the base URI plus `filter`, which PipeWire `filter-chain` `config` entries also use | [Plug-in identities](../develop/plugin-identities.md) |
| 24 October 2026 | Plug-in parameter IDs: VST3 Bypass 1000 and Status 1001, Audio Unit Status 1001, leaving 0 to 2 free for the old VST2 parameters (ID 0 replaced 25 October 2026: the editor's Gain) | [Plug-in identities](../develop/plugin-identities.md) |
| 25 October 2026 | The editor's Gain setting uses VST3 parameter ID 0 (the old VST2 attenuation's ID) rather than a new ID, with a new range and mapping instead of the old -200 to +200 dB; the range is approved when PLUG-10 proposes it. The editor also keeps a wet/dry Mix (the old "Effect level") | [Plug-in editor plan](plans/2026-10-24-plugin-editor.md#settings) |
| 25 October 2026 | Gain's range is −100 to +24 dB, linear in decibels, default 0 dB. The settings' parameter IDs are the same in VST3 and the Audio Unit: Gain 0, Gain mode 1002 (Set, Estimate, No clipping), Mix 1003, Latency 1004 (64 to 4096 frames, default 256, not automatable) | [Adapter contract](../develop/plugin-contract.md#settings) |
| 25 October 2026 | Keep the first version of the settings simple: Gain and Mix only, Audio Unit first for GarageBand; the gain modes, the Latency choice and the meters wait | [TODO PLUG-10](../../TODO.md) |
| 25 October 2026 | The editor's settings are saved in each format's own way for now (VST3 host state version 3 only when a setting is not at its default; Audio Unit and LV2 parameters and ports), and `convolver-preset` gains no setting options yet. Where settings can then diverge between formats and hosts is documented | [Plug-in editor plan](plans/2026-10-24-plugin-editor.md#settings) |

## Configuration and sound

The two from 18 October were decided in conversation with jrp before decisions
were recorded in the repository; this log is their first written record.

| Date | Decision | Explained in |
| --- | --- | --- |
| 18 October 2026 | Dither is off unless requested. Replaced on 25 October 2026 | [Command-line reference](../use/command-line.md#integer-formats-dither-and-noise-shaping) |
| 25 October 2026 | The CLI adds triangular (TPDF) dither by default below 20 significant bits; from 20 bits dither stays off but available. Options another always beats (`rectangular`, `second-order`, `sony-sbm`) are dropped, and Convolver warns about a pointless or clearly beaten choice and points to a better one ("Drop options where there is clearly a better one") | [Integer output](../use/command-line.md#integer-formats-dither-and-noise-shaping) (IO-7) |
| 18 October 2026 | `.cfg` paths are not translated or guessed between systems. Relative paths may use `\`; drive, UNC and `\`-rooted paths are rejected on macOS and Linux with a message saying to make them relative | [Other programs](../use/other-programs.md) |
| 24 October 2026 | The x86-64 AVX2 kernels use fused multiply-add ("add FMA, but keep an eye out for significant output divergences"). Output may differ between CPUs with and without AVX2+FMA at float-rounding level; `native.kernel_divergence` fails on anything larger (1e-6 of the peak, -120 dBFS, or a worse oracle error). The baseline kernels stay unfused | [AVX2 kernels](../develop/fft-backends.md#avx2-kernels-chosen-at-run-time) (PERF-4) |
| 24 October 2026 | A headerless `.pcm` or `.raw` filter in a `.cfg` is read as mono little-endian float32 (`f32le`, what DRC and BruteFIR write) at the configuration's rate, unless it starts with a sound-file header. Convolver says which type it used, and `--raw-filter-format` chooses another; `.cfg` syntax is unchanged | [Filter files](../use/configuration.md#filter-files) |
| 24 October 2026 | The 64 Mi limit on decoded filter samples counts a filter file once for every path that names it, although it is decoded only once ("I can't see a use case for different links to the same file in the configuration") | [Resource limits](../develop/library.md#resource-limits) (PERF-6) |

## Diagnostics

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | Messages explain generously: what failed, why and what to change, so users need not read the documentation | [TODO](../../TODO.md) DIAG-1 |
| 31 October 2026 | DIAG-1's proposals: invalid CLI samples and overflows name the file, time and channel and say what to change; damaged sound files and `convolver-preset` messages get a next step and the program's name, and that tool takes options after the file names; plug-ins log host settings they refuse and failed saves. After a failure the CLI only says that its output is incomplete (no deleting it, no temporary file). The CLI keeps replacing an existing output ("normal macOS / Linux behaviour") | [Output safety](../use/command-line.md#output-safety), [Convolver log](../use/plugins.md#status-and-the-convolver-log), [archived plan](../archive/plans/2026-10-24-diagnostics-by-surface.md#gaps-and-proposals) |

## Licences and dependencies

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | The Audio Unit is built with Apple's AudioUnitSDK and distributed under GPL-3.0-or-later with its Apache-2.0 notices; everything else stays GPL-2.0-or-later | [Audio Unit licence](../use/plugins.md#audio-unit-licence) |
| 22 October 2026 | AudioUnitSDK 1.4.0, so the AU target builds as C++23 while everything else stays C++20 (the pin is replaced by CI-6, 24 October 2026; the C++23 choice stands) | [Build from source](../develop/build.md#when-a-new-release-breaks-the-build) |
| 24 October 2026 | Supporting libraries and tools (VST3 SDK, lv2, AudioUnitSDK, FFTW, libsndfile, NSIS, pluginval, lv2lint) follow their newest releases instead of pins, for security and bug fixes, provided every build records exactly what it used (version, commit, checksum) in its summary and manifests, and a recorded build can be repeated exactly (CI-6). lilv follows the newest Ubuntu 24.04 package. A new upstream release can therefore turn CI red; Convolver is then changed to work with it, or a build chooses an older release with `--version`. Replaces the pins of PLUG-6 (22 October 2026) | [Supporting libraries](../develop/build.md#supporting-libraries), [when a new release breaks the build](../develop/build.md#when-a-new-release-breaks-the-build) |
| 24 October 2026 | The critical gate is the latest formal release on the current platform; upstream issues, including those of newer pre-releases, are warnings ("the critical gate is the latest formal release on the current platform, but warn if there are any upstream issues, including formal pre-releases"). So formal tests use the newest full release of each tool, never a beta or release candidate, and a newer pre-release is only tried, with what it signposts reported as a warning (pluginval: formal 1.0.4, advisory 2.0 beta). The exception is the pluginval acceptance test, which needs pluginval 2's `test` command: it stays required on the newest 2.0 beta until 2.0 is released. LV2 tests use Ubuntu's lilv packages formally; an advisory job flags the issues the newest upstream lilv, serd, sord, sratom and zix reveal. Archives without a published checksum are trusted on first download and their checksum recorded (CI-6) | [Pre-releases and newer upstream versions](../develop/build.md#pre-releases-and-newer-upstream-versions) |
| 24 October 2026 | The preview packages do not ship libFLAC, libogg, libvorbis or libopus (IO-6), so they neither read FLAC or Ogg nor write FLAC; the limitation and the workarounds (convert, or build against a libsndfile that has them) are documented | [FLAC and Ogg](../use/command-line.md#flac-and-ogg) |
| 30 October 2026 | The Audio Unit reads the filter files chosen in its window (AU-6) with macOS's own AudioToolbox, not libsndfile, so the component links nothing beyond macOS. Chosen over linking libsndfile into it (built in for packages) and over leaving Choose Filter out; the formats it reads can differ slightly from the command-line tool's | [The Audio Unit's window](../use/plugins.md#the-audio-units-window), [architecture](../develop/architecture.md) |
| 30 October 2026 | On macOS, prefer Apple's own libraries but keep the other platforms' as build fallbacks ("it would be good to be able to fall back to the libraries that the other platforms use, such as libsndfile, fftw"): the Audio Unit reads filter files with AudioToolbox unless built with `CONVOLVER_AU_FILE_READER=LIBSNDFILE`, and the plug-ins use Accelerate unless built with `CONVOLVER_FFT_BACKEND=FFTW` (VST3: "prioritise Accelerate for the FFT, but allow FFTW"). The same for the command-line tools is to follow | [Build: Audio Unit](../develop/build.md#audio-unit), [FFT backends](../develop/fft-backends.md#plug-in-builds) |

## Documentation

| Date | Decision | Explained in |
| --- | --- | --- |
| 18 October 2026 | In repository documents the owner is called jrp | [Documentation guide](../develop/documentation.md#writing-rules) |
| 23 October 2026 | Maintained pages are sorted by reader into `docs/use/`, `docs/develop/` and `docs/project/` | [Documentation by reader](../archive/plans/2026-10-23-documentation-by-reader.md) |
| 23 October 2026 | One plug-in page for all formats; TODO holds open work only; `REVIEW.md` is retired into [status](status.md); all of `docs/use/` ships in packages; this decisions log is kept | [Documentation by reader](../archive/plans/2026-10-23-documentation-by-reader.md#decisions) |
| 24 October 2026 | Decision notes and their dates appear only in this log, not as "(owner's decision, *date*)" on other pages | [Workflow](../develop/workflow.md#while-working) |
| 27 October 2026 | Every finished TODO item gets a changelog entry naming its ID in the pull request that finishes it, contributor-only items included | [Workflow](../develop/workflow.md#recording-progress-in-todo) |
