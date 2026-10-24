<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver

Convolver applies finite impulse response (FIR) filters to multichannel audio:
digital room correction, crossovers, and routing or mixing channels, using
filters made by tools such as DRC, REW or Audiolense. It applies filters; it
does not measure your room or design them. It runs on macOS, Linux and Windows
as:

- a **command-line tool** that filters WAV files or raw audio streams;
- **plug-ins** for audio programs: VST3 (Windows, macOS, Linux), Audio Unit
  (macOS) and LV2 (Linux), each with up to 8 input and 8 output channels;
- an **Equalizer APO export**, for filtering everything Windows plays;
- a **C++20 library** you can build into your own program.

A filter is a multichannel impulse-response (IR) sound file, or a Convolver
**`.cfg`** file describing filters and routes. Roon, JRiver and Audiolense use
variants of the `.cfg` format ([other programs](docs/use/other-programs.md)).
Coming from the old Windows Convolver? Your `.cfg` files may need a few
edits ([how](docs/use/other-programs.md#configurations-from-the-old-windows-convolver)).
Convolver is a developer preview: packages are unsigned and no named DAW has
been tested yet ([status](docs/project/status.md)).

## Start here

| I want to… | Read |
| --- | --- |
| Use Convolver | [Using Convolver](docs/use/README.md), then [your first filter](docs/use/first-filter.md) |
| Use the plug-ins | [Install](docs/use/install.md) and [your first sound in a host](docs/use/first-sound-in-a-host.md) |
| Build it, embed it or change it | [Build from source](docs/develop/build.md), [library guide](docs/develop/library.md), [implementation map](docs/develop/architecture.md), [contributing](CONTRIBUTING.md) |
| Find any page | [Documentation index](docs/README.md) |
| See what is ready, decided or planned | [Status](docs/project/status.md), [decisions](docs/project/decisions.md), [TODO](TODO.md), [changelog](CHANGELOG.md) |

## Try it

With CMake 3.20 or later, a C++20 compiler, FFTW (single precision),
libsndfile and Python 3.8 or later (`brew install cmake fftw libsndfile`, or
`sudo apt-get install cmake g++ python3 libfftw3-dev libsndfile1-dev`):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Then filter one of the bundled examples:

<!-- first-use-example -->
```sh
./build/convolver --filter examples/first-use/stereo/filter.cfg --input examples/first-use/stereo/input.wav --output stereo.wav
```

`stereo.wav` should match `examples/first-use/stereo/expected.wav`.
[Your first filter](docs/use/first-filter.md) continues from here. If the build
stops, [build from source](docs/develop/build.md#when-a-build-fails) explains
the usual causes; Windows builds use vcpkg ([Windows](docs/develop/build.md#windows)).

## Repository

| Directory | What it contains |
| --- | --- |
| [`include/convolver/`](include/convolver/) | Public C++ APIs |
| [`portable/`](portable/) | Engine, FFT backends, file I/O, configuration loader and the tools |
| [`plugins/`](plugins/) | One folder per plug-in format ([`vst3/`](plugins/vst3/), [`au/`](plugins/au/), [`lv2/`](plugins/lv2/)) |
| [`tests/`](tests/) | Native, plug-in, package, compatibility and historical tests |
| [`docs/`](docs/README.md) | Guides by reader ([`use/`](docs/use/README.md), [`develop/`](docs/develop/), [`project/`](docs/project/)) and dated records ([`archive/`](docs/archive/)) |
| [`examples/`](examples/) | First-use filters with expected output |
| [`cmake/`](cmake/), [`packaging/`](packaging/), [`scripts/`](scripts/) | Build helpers, release packaging, and release and repository checks |
| [`historical/`](historical/) | The preserved 2005–2006 Windows implementation; not built |

## About this project

Convolver began in 2005 as Windows applications by JRP (a Windows Media Player
plug-in, a DirectShow filter, a VST 2 plug-in and command-line tools), released
on [SourceForge](https://sourceforge.net/projects/convolver/) up to version 4.4.
The [original website](https://convolver.sourceforge.net/) describes those
applications, not this portable version. [History](docs/project/history.md)
explains where the code came from and
[how the two differ](docs/project/history.md#how-the-portable-version-differs).

## Licence

GNU General Public License, version 2 or (at your option) any later version;
see [COPYING](COPYING). The Audio Unit is distributed under version 3 or later
([why](docs/use/plugins.md#audio-unit-licence)). If you distribute a modified
plug-in build, first give it its own identities
([how](docs/develop/plugin-identities.md#if-you-distribute-a-modified-build)).
