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
| See every CLI option and its behavior | [Core and CLI reference](portable-core.md) |
| Use Convolver inside a DAW or other plug-in host | [VST3 guide](vst3.md) |
| Use the C++ libraries in another application | [CMake package guide](cmake-package.md) |
| Find the implementation or change it safely | [Implementation map](project-structure.md), then [contributing](../CONTRIBUTING.md) |
| Understand what the tests establish | [Testing](testing.md) |
| Choose an FFT backend or reproduce measurements | [FFT guide](fft-backends.md) |
| Find open work or suggest an improvement | [TODO](../TODO.md) |

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
  use the current guides and source to determine what is implemented.

For maintainers and coding agents, [AGENTS.md](../AGENTS.md) supplies repository
rules. The [implementation map](project-structure.md#change-and-test-map) connects
changes to source files, invariants and tests without requiring a history lesson.
