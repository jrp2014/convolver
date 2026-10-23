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
| Use Convolver inside a DAW or other plug-in host | [VST3 guide](vst3.md); on macOS also the [Audio Unit guide](au.md); on Linux also the [LV2 guide](lv2.md) (developer build) |
| Turn a filter into a preset for a plug-in | [Plug-in presets](presets.md) |
| Filter everything Windows plays (system-wide) | [Equalizer APO export](equalizer-apo.md) |
| Filter the audio of a video or other file with FFmpeg | [FFmpeg pipe example](portable-core.md#with-ffmpeg); a built-in filter was [planned](plans/2026-10-21-ffmpeg-filter.md) and is parked |
| Build Convolver from source, or a plug-in | [Build from source](develop/build.md) |
| Use the C++ libraries in another application | [Library guide](develop/library.md) |
| Find the implementation or change it safely | [Implementation map](develop/architecture.md), then [contributing](../CONTRIBUTING.md) |
| Understand what the tests establish | [Testing](develop/testing.md) |
| Choose an FFT backend or reproduce measurements | [FFT guide](develop/fft-backends.md) |
| See what is ready to use, what is still unproven and what blocks the next release | [Release readiness](release-readiness.md) |
| Find open work or suggest an improvement | [TODO](../TODO.md) |
| Understand version numbers and tags | [Source versioning](develop/versioning.md) |
| Edit these documents | [Documentation guide](develop/documentation.md) |
| Pick up, hand over or review a piece of work | [Taking on and reviewing work](develop/workflow.md) |
| Learn where Convolver came from | [History](history.md) |

## All maintained pages

Pages are grouped by who they are for. The [documentation guide](develop/documentation.md#kinds-of-page)
explains the kinds. The user guides still sit at the top of `docs/`; they move
into `docs/use/` in a later step (DOC-4).

### For people using Convolver

| Page | Kind | What it covers |
| --- | --- | --- |
| [Concepts](concepts.md) | Background | Audio terms, latency, tails, files versus streams |
| [Configuration format](configuration.md) | Reference | `.cfg` and list syntax, routes, paths, troubleshooting |
| [Configuration compatibility](configuration-compatibility.md) | Reference | What is tested, path-portability profile, other readers of the format |
| [Core and CLI reference](portable-core.md) | Reference | Every CLI option, output formats, inspection, library API, limits |
| [VST3 guide](vst3.md) | How-to | Installing the plug-in and loading filters; how it behaves |
| [Audio Unit guide](au.md) | How-to | Installing the macOS Audio Unit and loading filters; its behaviour, licence and tests |
| [LV2 guide](lv2.md) | How-to | Installing the LV2 plug-ins, loading a filter as a preset, ports, behaviour and tests |
| [Plug-in presets](presets.md) | How-to | `convolver-preset`: VST3, AU and LV2 presets from one filter, conversion, what a preset that does not fit plays, file contents |
| [Equalizer APO export](equalizer-apo.md) | How-to | `convolver-equalizer-apo`: system-wide filtering on Windows, how a filter is chosen, silence for devices no filter fits, installation and troubleshooting |
| [Plug-in distribution](plugins/index.md) | How-to | Preview archives and installers: [build](plugins/build.md), [install](plugins/install.md), [first use](plugins/quickstart.md), [host testing](plugins/hosts.md), [troubleshooting](plugins/troubleshooting.md) |

### For people building, embedding or changing Convolver (people and agents)

| Page | Kind | What it covers |
| --- | --- | --- |
| [Build from source](develop/build.md) | How-to | Every build: dependencies, presets, FFT backend, Windows, options, VST3, AU and LV2 SDKs |
| [Library guide](develop/library.md) | How-to | Using the libraries from another CMake project; presets |
| [Implementation map](develop/architecture.md) | Background | Source responsibilities, invariants, change-to-test map |
| [Plug-in adapter contract](develop/plugin-contract.md) | Reference | What every plug-in adapter must do: failed loads, restoration, replacement, bypass, threads, host state |
| [Plug-in identities and channel layouts](develop/plugin-identities.md) | Reference | Permanent IDs for every format, what forks must change, discrete and named layouts, old presets |
| [FFT guide](develop/fft-backends.md) | Background | Backend choice, numerical contract, dated benchmarks |
| [Testing](develop/testing.md) | Reference | What each test establishes and its limits |
| [Taking on and reviewing work](develop/workflow.md) | How-to | Claiming a TODO item, handoff notes, definition of done, independent review, merge, repository checks |
| [Documentation guide](develop/documentation.md) | Reference | Readers and kinds of page, writing rules, pages agents follow, which page owns each topic, link checks |
| [Source versioning](develop/versioning.md) | Reference | `VERSION`, automatic tags, recovery |

### Status, direction and history

| Page | Kind | What it covers |
| --- | --- | --- |
| [Release readiness](release-readiness.md) | Reference | What users can run now at each evidence level, user journeys, next-milestone blockers, compatibility guarantees |
| [Preview release record](releases/developer-preview.md) | Record | Candidate matrix, evidence and remaining production gates |
| [Modernization direction](modernization.md) | Background | Priorities and dependency choices |
| [History](history.md) | Background | Provenance, historical builds, downstream use |
| [convolverPipe](convolverPipe.md) | Reference | The recovered historical streaming tool (Windows only) |

### Moved pages

These pages moved on 23 October 2026 (DOC-4). Dated records
still link to the old paths.

| Old path | New path |
| --- | --- |
| `docs/project-structure.md` | [`docs/develop/architecture.md`](develop/architecture.md) |
| `docs/cmake-package.md` | [`docs/develop/library.md`](develop/library.md) |
| `docs/fft-backends.md` | [`docs/develop/fft-backends.md`](develop/fft-backends.md) |
| `docs/plugin-identities.md` | [`docs/develop/plugin-identities.md`](develop/plugin-identities.md) |
| `docs/testing.md` | [`docs/develop/testing.md`](develop/testing.md) |
| `docs/workflow.md` | [`docs/develop/workflow.md`](develop/workflow.md) |
| `docs/documentation.md` | [`docs/develop/documentation.md`](develop/documentation.md) |
| `docs/versioning.md` | [`docs/develop/versioning.md`](develop/versioning.md) |
| Shared adapter contract in the plug-in formats plan | [`docs/develop/plugin-contract.md`](develop/plugin-contract.md) |

At the top level: [README](../README.md), [TODO](../TODO.md),
[CHANGELOG](../CHANGELOG.md), [current review](../REVIEW.md),
[contributing](../CONTRIBUTING.md) and [agent rules](../AGENTS.md).

These pages describe the active CMake implementation. The package version is in
[`VERSION`](../VERSION); use `convolver --version` to identify a particular binary.
[Changes](../CHANGELOG.md), [source versioning](develop/versioning.md), and the
[current review](../REVIEW.md) provide release and review context.

## Current source versus historical records

- `include/convolver/`, `portable/`, `plugins/vst3/`, `plugins/au/` and `plugins/lv2/` are the active implementation.
- `historical/` contains the old Windows applications. You do not need it to learn
  the current API or build the CLI.
- [History](history.md) explains recovery, provenance and downstream reuse.
  [Historical website files](historical/convolver-4.4/README.md) are preserved
  unchanged. Their installation instructions and restrictions are not current
  instructions for the portable code.
- `review-2026-10/` and `archive/reviews/` contain dated reviews and evidence.
  [`plans/`](plans/) holds design and implementation plans for open work. Plans
  describe intent at the time; use the current guides and source to determine
  what is implemented. [Completed plans](archive/plans/README.md) move to
  `archive/plans/`.

For maintainers and coding agents, [AGENTS.md](../AGENTS.md) supplies repository
rules. The [implementation map](develop/architecture.md#change-and-test-map) connects
changes to source files, invariants and tests without requiring a history lesson.

## Open plans

These dated plans in [`plans/`](plans/) describe work that is not finished;
open work and the current order are tracked in
[TODO](../TODO.md#execution-priorities). The plug-in formats plan is the
current direction; the Media Foundation and FFmpeg plans are parked.

- [Plug-in formats: VST3, LV2 and AU](plans/2026-10-21-plugin-formats.md)
  (PLUG, LV2 and AU items): which hosts load VST3, AU, LV2 and CLAP, framework
  choice, and the plan to develop VST3, LV2 and AU in parallel, including LV2
  for PipeWire system audio.
- [Media Foundation transform](plans/2026-10-21-media-foundation-transform.md)
  (HOST-3, parked): a Windows audio effect for Media Player Legacy, DirectShow players and
  Media Foundation or WinRT applications, with APO and Equalizer APO alternatives
  for system-wide processing. The Equalizer APO route is now the
  [Equalizer APO export](equalizer-apo.md) (HOST-4).
- [FFmpeg filter](plans/2026-10-21-ffmpeg-filter.md) (FFMPEG-1 to
  FFMPEG-5, parked): a libavfilter filter; the pipe already works.
- [Plug-in distribution](plans/2026-10-18-plugin-distribution.md) and its
  [design](plans/2026-10-18-plugin-distribution-design.md) (REL-1, PLUG-5):
  the 18 October release design. The packaging it describes is merged; signing
  and production qualification are open.
