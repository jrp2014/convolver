<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver documentation

Convolver applies filters you supply to audio. It can correct a loudspeaker's
response, split audio into frequency bands for a crossover, or mix and route
channels. It does not measure a room or design the filters.

A filter is stored as an **impulse response (IR)**: a sequence of numbers, usually
in a WAV file, describing how an input sample affects later output samples.
Convolver applies that response continuously to every input sample.

## Choose a starting point

| I want to… | Start here |
| --- | --- |
| Build Convolver and filter a file | [Quick start](../README.md#build-and-try-it) |
| Understand the audio terms and timing | [Concepts](concepts.md) |
| Write a filter configuration or move one from Windows | [Configuration format](configuration.md) |
| Check whether a configuration works in Roon, JRiver or elsewhere | [Configuration compatibility](configuration-compatibility.md) |
| See every CLI option and its behavior | [Core and CLI reference](portable-core.md) |
| Use Convolver inside a DAW or other plug-in host | [VST3 guide](vst3.md) |
| Filter the audio of a video or other file with FFmpeg | [FFmpeg pipe example](portable-core.md#with-ffmpeg); a built-in filter is [planned](superpowers/plans/2026-10-21-ffmpeg-filter.md) |
| Use the C++ libraries in another application | [CMake package guide](cmake-package.md) |
| Find the implementation or change it safely | [Implementation map](project-structure.md), then [contributing](../CONTRIBUTING.md) |
| Understand what the tests establish | [Testing](testing.md) |
| Choose an FFT backend or reproduce measurements | [FFT guide](fft-backends.md) |
| Find open work or suggest an improvement | [TODO](../TODO.md) |
| Understand version numbers and tags | [Source versioning](versioning.md) |
| Edit these documents | [Documentation maintenance](documentation.md) |
| Learn where Convolver came from | [History](history.md) |

## All maintained pages

| Page | What it covers |
| --- | --- |
| [Concepts](concepts.md) | Audio terms, latency, tails, files versus streams |
| [Configuration format](configuration.md) | `.cfg` and list syntax, routes, paths, troubleshooting |
| [Configuration compatibility](configuration-compatibility.md) | What is tested, path-portability profile, other readers of the format |
| [Core and CLI reference](portable-core.md) | Build options, every CLI option, output formats, inspection, library API, limits |
| [VST3 guide](vst3.md) | Building, installing and loading the plug-in; host lifecycle |
| [CMake package guide](cmake-package.md) | Using the libraries from another CMake project; presets |
| [FFT guide](fft-backends.md) | Backend choice, numerical contract, dated benchmarks |
| [Testing](testing.md) | What each test establishes and its limits |
| [Implementation map](project-structure.md) | Source responsibilities, invariants, change-to-test map |
| [Modernization direction](modernization.md) | Priorities and dependency choices |
| [Source versioning](versioning.md) | `VERSION`, automatic tags, recovery |
| [Documentation maintenance](documentation.md) | Which page owns each topic; link checks |
| [History](history.md) | Provenance, historical builds, downstream use |
| [convolverPipe](convolverPipe.md) | The recovered historical streaming tool (Windows only) |

At the top level: [README](../README.md), [TODO](../TODO.md),
[CHANGELOG](../CHANGELOG.md), [current review](../REVIEW.md),
[contributing](../CONTRIBUTING.md) and [agent rules](../AGENTS.md).

These pages describe the active CMake implementation. The package version is in
[`VERSION`](../VERSION); use `convolver --version` to identify a particular binary.
[Changes](../CHANGELOG.md), [source versioning](versioning.md), and the
[current review](../REVIEW.md) provide release and review context.

## Current source versus historical records

- `include/convolver/`, `portable/` and `plugins/vst3/` are the active implementation.
- `convolver/` contains the old Windows applications. You do not need it to learn
  the current API or build the CLI.
- [History](history.md) explains recovery, provenance and downstream reuse.
  [Historical website files](historical/convolver-4.4/README.md) are preserved
  unchanged. Their installation instructions and restrictions are not current
  instructions for the portable code.
- `review-2026-10/`, `archive/reviews/` and `superpowers/` contain dated reviews,
  evidence and design/implementation plans. Plans describe intent at the time;
  use the current guides and source to determine what is implemented. Plans for
  open work stay in `superpowers/`; [completed plans](archive/plans/README.md)
  move to `archive/plans/`.

For maintainers and coding agents, [AGENTS.md](../AGENTS.md) supplies repository
rules. The [implementation map](project-structure.md#change-and-test-map) connects
changes to source files, invariants and tests without requiring a history lesson.

## Plans for further adapters

These dated plans propose adapters that do not exist yet; open work is tracked
in [TODO](../TODO.md).

- [Media Foundation transform](superpowers/plans/2026-10-21-media-foundation-transform.md)
  (HOST-3): a Windows audio effect for Media Player Legacy, DirectShow players and
  Media Foundation or WinRT applications, with APO and Equalizer APO alternatives
  for system-wide processing.
- [Plug-in formats and LV2](superpowers/plans/2026-10-21-lv2-plugin.md)
  (LV2-1 to LV2-6): which hosts load VST3, AU, LV2 and CLAP, the recommended
  order (LV2 next, then AU), framework choice, and an LV2 plug-in for Linux
  hosts and PipeWire system audio.
- [FFmpeg filter](superpowers/plans/2026-10-21-ffmpeg-filter.md) (FFMPEG-1 to
  FFMPEG-5, deferred behind LV2): a libavfilter filter; the pipe already works.
