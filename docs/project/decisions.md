<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Decisions

[Documentation index](../README.md) · [Status](status.md) · [TODO](../../TODO.md)

For anyone who needs to know what jrp has already decided, so the question is
not asked again. Each row names the decision and links to the page that
explains it and owns the details.

**In short:** jrp decides anything permanent or user-facing: identities, state
and preset formats, `.cfg` syntax, licences, defaults that change the sound,
shipped dependencies and publishing ([workflow](../develop/workflow.md)). When
jrp decides, record it here with its date, and where the work is described as
"(owner's decision, *date*)". Changing a decision is a new decision: add a row
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
| 21 October 2026 | VST3, LV2 and AU are equal first priorities, developed in parallel. CLAP and an editor wait for a request. Adapters use each format's own headers, not a framework (DPF is the fallback if a shared editor is ever needed) | [Plug-in formats plan](plans/2026-10-21-plugin-formats.md) |
| 21 October 2026 | The FFmpeg filter is parked: the pipe covers it, and a built-in filter would mean maintaining a patched FFmpeg | [FFmpeg plan](plans/2026-10-21-ffmpeg-filter.md) |
| 21 October 2026 | Windows-native adapters (Media Foundation, APO) are parked | [Media Foundation plan](plans/2026-10-21-media-foundation-transform.md) |
| 22 October 2026 | System-wide filtering on Windows goes through Equalizer APO: an export from `.cfg`, with no Windows audio code of our own | [Equalizer APO export](../use/equalizer-apo.md) |
| 22 October 2026 | Next milestone: an independent user installs and uses an exact preview package by following the documentation | [Status](status.md#next-milestone-an-independent-user-runs-a-preview), [guidance](../review-2026-10/release-readiness-guidance-2026-10-22.md) |
| 22 October 2026 | REAPER and Carla are the suggested first named hosts; jrp will try them (replaced 23 October 2026) | [Host testing](../develop/host-testing.md#first-hosts) |
| 23 October 2026 | jrp's first named hosts, on his Mac: REAPER and Audacity for VST3, GarageBand for the Audio Unit (a smoke test only). Replaces REAPER and Carla | [Host testing](../develop/host-testing.md#first-hosts) |

## Releases

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | The first releases are unsigned previews on macOS (ad-hoc signature, quarantine removed by the user) and Windows (SmartScreen warning). Signing stays a production gate | [Releasing](../develop/releasing.md#publishing-an-unsigned-preview) |
| 21 October 2026 | Publishing is a manual step owned by jrp, from one candidate run | [Releasing](../develop/releasing.md#publishing-an-unsigned-preview) |
| 21 October 2026 | Plug-in builds use Ooura on Linux and Windows and Accelerate on macOS (PLUG-1) | [FFT guide](../develop/fft-backends.md#plug-in-builds) |
| 22 October 2026 | Preview packages ship `convolver-preset` and `convolver-equalizer-apo`, not the older `convolver-vst3-preset` | [TODO](../../TODO.md) PLUG-5 |
| 23 October 2026 | The independent walkthrough (REL-3) uses the chosen candidate run's exact files before they are uploaded, so the pre-release can say what was demonstrated | [Releasing](../develop/releasing.md#publishing-an-unsigned-preview) |
| 23 October 2026 | The first preview holds VST3, the `convolver` CLI and the preset tool; LV2 and AU join later previews once their own journeys are demonstrated | [Status](status.md#user-journeys-for-the-first-preview) |

## Plug-in behaviour

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | A failed replacement keeps the running filter in every format, with an explicit error | [Adapter contract](../develop/plugin-contract.md) |
| 21 October 2026 | An empty instance passes audio through unchanged only with equal input and output counts; with unequal counts it plays silence | [Adapter contract](../develop/plugin-contract.md) |
| 21 October 2026 | A failed first load plays silence, never unfiltered audio, with an error shown where the format allows | [Adapter contract](../develop/plugin-contract.md) |
| 22 October 2026 | Bypass with unequal counts fades every output to silence | [Adapter contract](../develop/plugin-contract.md) |
| 22 October 2026 | VST3 counts its configuration as known only after activation (JUCE hosts call `setupProcessing` with placeholder values first); no message for a mismatch found at activation until VST3-3 | [Adapter contract](../develop/plugin-contract.md) |
| 22 October 2026 | Saving after a failed first load records the failure (host state version 2), so reopening stays silent | [Adapter contract](../develop/plugin-contract.md) |
| 23 October 2026 | VST3 speaker arrangements are checked with a notice only: no enforced layouts, no host state version 3 and no buses above 8 channels; revisit on request | [Plug-in identities](../develop/plugin-identities.md#channel-layouts) |
| 23 October 2026 | Whether saved state records bypass follows each format's convention: VST3 saves its bypass parameter; LV2 and Audio Units leave bypass to the host and ignore the flag in restored state | [Adapter contract](../develop/plugin-contract.md#bypass) |

## Identities and file formats

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | AU codes `aufx Conv CONV` | [Plug-in identities](../develop/plugin-identities.md) |
| 22 October 2026 | LV2 URI scheme `https://github.com/jrp2014/convolver/lv2#discrete-N-M`, the eight fixed LV2 layouts, and `io.github.jrp2014.convolver.*` macOS identifiers | [Plug-in identities](../develop/plugin-identities.md) |
| 22 October 2026 | Host state version 2 only marks a failed first load; everything else is still written as version 1 | [Plug-in identities](../develop/plugin-identities.md) |
| 22 October 2026 | AU presets keep Apple's standard keys and hold the state under `convolver-state`; LV2 presets hold it as an `atom:Chunk` under the base URI plus `state` | [Adapter contract](../develop/plugin-contract.md#preset-files) |
| 23 October 2026 | The LV2 filter parameter and the saved file path share one URI, the base URI plus `filter`, which PipeWire `filter-chain` `config` entries also use | [Plug-in identities](../develop/plugin-identities.md) |

## Configuration and sound

These two were decided in conversation with jrp before decisions were recorded
in the repository; this log is their first written record.

| Date | Decision | Explained in |
| --- | --- | --- |
| 18 October 2026 | Dither is off unless requested | [Command-line reference](../use/command-line.md#integer-formats-dither-and-noise-shaping) |
| 18 October 2026 | `.cfg` paths are not translated or guessed between systems. Relative paths may use `\`; drive, UNC and `\`-rooted paths are rejected on macOS and Linux with a message saying to make them relative | [Other programs](../use/other-programs.md) |
| 24 October 2026 | A headerless `.pcm` or `.raw` filter in a `.cfg` is read as mono little-endian float32 (`f32le`, what DRC and BruteFIR write) at the configuration's rate, unless it starts with a sound-file header. Convolver says which type it used, and `--raw-filter-format` chooses another; `.cfg` syntax is unchanged | [Filter files](../use/configuration.md#filter-files) |

## Diagnostics

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | Messages explain generously: what failed, why and what to change, so users need not read the documentation | [TODO](../../TODO.md) DIAG-1 |

## Licences and dependencies

| Date | Decision | Explained in |
| --- | --- | --- |
| 21 October 2026 | The Audio Unit is built with Apple's AudioUnitSDK and distributed under GPL-3.0-or-later with its Apache-2.0 notices; everything else stays GPL-2.0-or-later | [Audio Unit licence](../use/plugins.md#audio-unit-licence) |
| 22 October 2026 | AudioUnitSDK 1.4.0, so the AU target builds as C++23 while everything else stays C++20 | [Build from source](../develop/build.md#pinned-format-dependencies) |
| 24 October 2026 | The preview packages do not ship libFLAC, libogg or libvorbis (IO-6), so they neither read FLAC or Ogg nor write FLAC; the limitation and the workarounds (convert, or build against a libsndfile that has them) are documented | [FLAC and Ogg](../use/command-line.md#flac-and-ogg) |

## Documentation

| Date | Decision | Explained in |
| --- | --- | --- |
| 18 October 2026 | In repository documents the owner is called jrp | [Documentation guide](../develop/documentation.md#writing-rules) |
| 23 October 2026 | Maintained pages are sorted by reader into `docs/use/`, `docs/develop/` and `docs/project/` | [Documentation by reader](../archive/plans/2026-10-23-documentation-by-reader.md) |
| 23 October 2026 | One plug-in page for all formats; TODO holds open work only; `REVIEW.md` is retired into [status](status.md); all of `docs/use/` ships in packages; this decisions log is kept | [Documentation by reader](../archive/plans/2026-10-23-documentation-by-reader.md#decisions) |
