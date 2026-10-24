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
| Use Convolver: filter files, use the plug-ins, or filter everything Windows plays | [Using Convolver](use/README.md) |
| Filter my first file | [Your first filter](use/first-filter.md) |
| Hear the plug-in in a host | [Install](use/install.md), then [your first sound in a host](use/first-sound-in-a-host.md) |
| Write a filter configuration | [Configuration files](use/configuration.md) |
| Use a configuration from Roon, JRiver, Audiolense or the old Windows Convolver | [Other programs](use/other-programs.md) |
| Fix a filter that does not load or play | [Troubleshooting](use/troubleshooting.md) |
| Filter the audio of a video with FFmpeg | [FFmpeg pipe example](use/command-line.md#with-ffmpeg); a built-in filter was [planned](project/plans/2026-10-21-ffmpeg-filter.md) and is parked |
| Build Convolver from source, or a plug-in | [Build from source](develop/build.md) |
| Use the C++ libraries in another application | [Library guide](develop/library.md) |
| Find the implementation or change it safely | [Implementation map](develop/architecture.md), then [contributing](../CONTRIBUTING.md) |
| Understand what the tests establish | [Testing](develop/testing.md) |
| See what is ready to use, what is still unproven and what blocks the next release | [Status](project/status.md) |
| Check what jrp has already decided | [Decisions](project/decisions.md) |
| Find open work or suggest an improvement | [TODO](../TODO.md) |
| Pick up, hand over or review a piece of work | [Taking on and reviewing work](develop/workflow.md) |
| Edit these documents | [Documentation guide](develop/documentation.md) |
| Learn where Convolver came from | [History](project/history.md) |

## All maintained pages

Pages are grouped by who they are for. The [documentation guide](develop/documentation.md#kinds-of-page)
explains the kinds.

### For people using Convolver

These pages are in `docs/use/` and are shipped in preview packages.

| Page | Kind | What it covers |
| --- | --- | --- |
| [Using Convolver](use/README.md) | How-to | Choosing how to run it; the tools in a preview package |
| [Concepts](use/concepts.md) | Background | Audio terms, latency, tails, files versus streams |
| [Your first filter](use/first-filter.md) | Tutorial | The bundled examples with the command-line tool |
| [Your first sound in a host](use/first-sound-in-a-host.md) | Tutorial | The example presets in a plug-in host |
| [Install](use/install.md) | How-to | Preview packages and source builds, for every plug-in format and system; unsigned previews |
| [Plug-in presets](use/presets.md) | How-to | `convolver-preset`: VST3, AU and LV2 presets from one filter, conversion, messages |
| [Equalizer APO export](use/equalizer-apo.md) | How-to | `convolver-equalizer-apo`: system-wide filtering on Windows, how a filter is chosen, installation and troubleshooting |
| [Other programs](use/other-programs.md) | How-to | Path portability, what other programs read, configurations from the old Windows Convolver |
| [Troubleshooting](use/troubleshooting.md) | How-to | Symptoms, causes and fixes for plug-ins, configurations and tools |
| [Command-line reference](use/command-line.md) | Reference | Every `convolver` option, output formats, streams, inspection, FFTW tuning |
| [Configuration files](use/configuration.md) | Reference | `.cfg` and list syntax, routes, paths |
| [How the plug-ins behave](use/plugins.md) | Reference | VST3, AU and LV2: channels, loading, failures, latency, bypass, LV2 ports, licence |
| [Limits](use/limits.md) | Reference | What Convolver does not do, and what to do instead |

### For people building, embedding or changing Convolver (people and agents)

| Page | Kind | What it covers |
| --- | --- | --- |
| [Build from source](develop/build.md) | How-to | Every build: dependencies, presets, FFT backend, Windows, options, VST3, AU and LV2 SDKs, pinned dependencies and the preview recipe |
| [Library guide](develop/library.md) | How-to | Using the libraries from another CMake project; the engine API and resource limits |
| [Implementation map](develop/architecture.md) | Background | Source responsibilities, invariants, change-to-test map |
| [Plug-in adapter contract](develop/plugin-contract.md) | Reference | What every plug-in adapter must do: failed loads, restoration, replacement, bypass, threads, host state, preset files |
| [Plug-in identities and channel layouts](develop/plugin-identities.md) | Reference | Permanent IDs for every format, what forks must change, discrete and named layouts, old presets |
| [FFT guide](develop/fft-backends.md) | Background | Backend choice, numerical contract, dated benchmarks |
| [Testing](develop/testing.md) | Reference | What each test establishes and its limits, including each plug-in format and the Equalizer APO export |
| [Host testing](develop/host-testing.md) | How-to | Testing in real hosts: the preview smoke test, production qualification, Equalizer APO on a device |
| [Taking on and reviewing work](develop/workflow.md) | How-to | Claiming a TODO item, handoff notes, definition of done, independent review, merge, repository checks |
| [Documentation guide](develop/documentation.md) | Reference | Readers and kinds of page, writing rules, pages agents follow, which page owns each topic, link checks |
| [Source versioning](develop/versioning.md) | Reference | `VERSION`, automatic tags, recovery |
| [Releasing](develop/releasing.md) | How-to | Candidate packages, publishing an unsigned preview, production gates, licences |

### Status, direction and history

These pages are in `docs/project/`.

| Page | Kind | What it covers |
| --- | --- | --- |
| [Status](project/status.md) | Reference | What users can run now at each evidence level, user journeys, next-milestone blockers, compatibility guarantees, what the tests do not establish, direction |
| [Decisions](project/decisions.md) | Reference | jrp's decisions, dated, with the page that explains each |
| [History](project/history.md) | Background | Provenance, historical builds, downstream use |
| [convolverPipe](project/convolverPipe.md) | Reference | The recovered historical streaming tool (Windows only) |

### Moved pages

These pages moved on 23 October 2026. Dated records still link to the old
paths.

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
| `docs/concepts.md`, `configuration.md`, `equalizer-apo.md`, `presets.md` | The same names in [`docs/use/`](use/README.md) |
| `docs/portable-core.md` | [`docs/use/command-line.md`](use/command-line.md); the engine API and limits are in the [library guide](develop/library.md#using-the-engine) |
| `docs/configuration-compatibility.md` | [`docs/use/other-programs.md`](use/other-programs.md) |
| `docs/vst3.md`, `docs/au.md`, `docs/lv2.md` | [`docs/use/plugins.md`](use/plugins.md) and [`docs/use/install.md`](use/install.md); their tests are in [testing](develop/testing.md#plug-in-format-tests) |
| `docs/plugins/index.md`, `install.md`, `quickstart.md`, `troubleshooting.md` | [`docs/use/README.md`](use/README.md), [`install.md`](use/install.md), [`first-sound-in-a-host.md`](use/first-sound-in-a-host.md), [`troubleshooting.md`](use/troubleshooting.md) |
| `docs/plugins/build.md`, `docs/plugins/hosts.md` | [`docs/develop/build.md`](develop/build.md#preview-recipe), [`docs/develop/host-testing.md`](develop/host-testing.md) |
| `REVIEW.md`, `docs/release-readiness.md`, `docs/modernization.md` | [`docs/project/status.md`](project/status.md); dependency choices in the [implementation map](develop/architecture.md#dependency-choices); the old review is [archived](archive/reviews/2026-10-20-repository-review.md) |
| `docs/releases/developer-preview.md` | [`docs/develop/releasing.md`](develop/releasing.md); the record is [archived](archive/releases/README.md) |
| `docs/history.md`, `docs/convolverPipe.md`, `docs/plans/` | The same names in `docs/project/` |

At the top level: [README](../README.md), [TODO](../TODO.md),
[CHANGELOG](../CHANGELOG.md), [contributing](../CONTRIBUTING.md) and [agent rules](../AGENTS.md).

These pages describe the active CMake implementation. The package version is in
[`VERSION`](../VERSION); use `convolver --version` to identify a particular binary.
[Changes](../CHANGELOG.md), [source versioning](develop/versioning.md) and
[status](project/status.md) provide release context.

## Current source versus historical records

- `include/convolver/`, `portable/`, `plugins/vst3/`, `plugins/au/` and `plugins/lv2/` are the active implementation.
- `historical/` contains the old Windows applications. You do not need it to learn
  the current API or build the CLI.
- [History](project/history.md) explains recovery, provenance and downstream reuse.
  [Historical website files](historical/convolver-4.4/README.md) are preserved
  unchanged. Their installation instructions and restrictions are not current
  instructions for the portable code.
- `review-2026-10/` and `archive/` contain dated reviews, release records and evidence.
  [`project/plans/`](project/plans/) holds design and implementation plans for open work. Plans
  describe intent at the time; use the current guides and source to determine
  what is implemented. [Completed plans](archive/plans/README.md) move to
  `archive/plans/`.

For maintainers and coding agents, [AGENTS.md](../AGENTS.md) supplies repository
rules. The [implementation map](develop/architecture.md#change-and-test-map) connects
changes to source files, invariants and tests without requiring a history lesson.

## Open plans

These dated plans in [`project/plans/`](project/plans/) describe work that is not finished;
open work and the current order are tracked in
[TODO](../TODO.md#execution-priorities). The plug-in formats plan is the
current direction; the Media Foundation and FFmpeg plans are parked.

- [Plug-in formats: VST3, LV2 and AU](project/plans/2026-10-21-plugin-formats.md)
  (PLUG, LV2 and AU items): which hosts load VST3, AU, LV2 and CLAP, framework
  choice, and the plan to develop VST3, LV2 and AU in parallel, including LV2
  for PipeWire system audio.
- [Media Foundation transform](project/plans/2026-10-21-media-foundation-transform.md)
  (HOST-3, parked): a Windows audio effect for Media Player Legacy, DirectShow players and
  Media Foundation or WinRT applications, with APO and Equalizer APO alternatives
  for system-wide processing. The Equalizer APO route is now the
  [Equalizer APO export](use/equalizer-apo.md) (HOST-4).
- [FFmpeg filter](project/plans/2026-10-21-ffmpeg-filter.md) (FFMPEG-1 to
  FFMPEG-5, parked): a libavfilter filter; the pipe already works.
- [Plug-in distribution](project/plans/2026-10-18-plugin-distribution.md) and its
  [design](project/plans/2026-10-18-plugin-distribution-design.md) (REL-1, PLUG-5):
  the 18 October release design. The packaging it describes is merged; signing
  and production qualification are open.
