<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver

**Download:** [Convolver 0.18.10 for macOS, Windows and Linux](https://github.com/jrp2014/convolver/releases/tag/v0.18.10), an
unsigned developer preview; read [opening an unsigned preview](docs/use/install.md#unsigned-previews)
first.

Convolver applies finite impulse response (FIR) filters to multichannel audio:
digital room correction, crossovers, convolution reverb, and routing or mixing
channels, using filters made by tools such as DRC, REW or Audiolense, or a
reverb's impulse response. It applies filters; it does not measure your room
or design them. It runs on macOS, Linux and Windows as:

- a **command-line tool** that filters WAV files or raw audio streams;
- **plug-ins** for audio programs: VST® 3 (Windows, macOS, Linux), Audio Unit
  (macOS) and LV2 (Linux), each with up to 8 input and 8 output channels;
- an **Equalizer APO export**, for filtering everything Windows plays;
- a **C++20 library** you can build into your own program.

A filter is an IR file (a multichannel impulse response in a sound file such
as WAV), a Convolver **`.cfg`** configuration describing filters and routes, or
a configuration list naming several of them.

## What sets it apart

Convolver's niche is playing a filter faithfully, to within float
rounding: a room correction, a crossover or a reverb's impulse response,
with no colouring of its own. Convolution itself is a well-known technique;
the effort here has gone into making it accurate, light on the system,
robust and unsurprising. Each point links to the guide, test or measurement
behind it, and [preview status](#preview-status) says what has not yet been
tried.

- **Accurate.** Automated tests on Linux, macOS and Windows compare the
  engine's output with a double-precision convolution written independently
  of it. The recorded results for the current engine are a worst difference
  of 5.2e-7 of the output's peak (about −126 dB) in those tests
  ([changelog](CHANGELOG.md#0132---2026-11-09)), and within 4e-6 (about
  −108 dB) in long stress and soak runs
  ([testing](docs/develop/testing.md#native-production-tests)). Each
  plug-in format gives the command-line tool's samples for the bundled
  examples, and `convolver --compare` checks a host's render against the
  expected file ([check a stereo filter in a host](docs/use/check-a-stereo-filter.md)).
- **Light on the system.** By default Convolver adds no processing delay:
  it sums a filter's first taps directly and spreads the work of its larger
  blocks over the callbacks in between
  ([latency and tail](docs/use/plugins.md#latency-and-tail)). In a
  benchmark on a Linux cloud machine at Latency 0, a 30-second stereo
  reverb used under 2% of one core and a 30-second 2→6 crossover, with six
  paths, about 5.5%
  ([measurements](docs/develop/fft-measurements.md#the-non-uniform-engine)).
  In jrp's recorded comparison on an Apple silicon Mac (8 November 2026,
  0.13.0), at a Latency of 256 frames, the engine used 15 to 17 times less
  processor time for 30-second filters than the first preview's engine
  ([measurements](docs/develop/fft-measurements.md#on-apple-silicon)). The
  Latency setting trades delay for processor time when you are not
  monitoring live.
- **Robust.** Every plug-in format is held to the same lifecycle tests:
  loading before and after the host starts, restoring saved projects,
  replacing and refusing filters, bypass, reset, variable buffer sizes and
  input that is not a finite number
  ([plug-in comparisons](docs/develop/testing.md#plug-in-comparisons-plug-2)).
  A first filter that cannot be loaded plays silence, not your unfiltered
  audio, and a replacement that does not fit leaves the playing filter in
  place. For filters with different input and output counts, such as a
  2→6 crossover, Convolver's own bypass fades to silence instead of
  passing unfiltered audio; a host that takes the plug-in out of the signal
  path can still route audio around it, so check which kind of bypass your
  host's button uses ([bypass](docs/use/plugins.md#bypass)). From 0.13.2
  on, each published preview's example presets and renders are kept, and
  every later build's command-line tool and VST3 plug-in must reproduce
  them within a stated tolerance ([upgrade tests](tests/upgrade/README.md)).
- **Behaves as you'd expect.** Changes are smoothed, with tails preserved
  for settings-only updates: Gain and Mix ramp over 1.3 ms, and a new
  filter crossfades in over 5.3 ms, starting with fresh history; a Latency
  change restarts processing and leaves a gap
  ([replacing a filter](docs/use/plugins.md#replacing-a-filter)). The host
  is told the exact latency, and Mix delays the dry signal to line up with
  the filtered one ([settings](docs/use/plugins.md#settings)). In VST3 and
  the Audio Unit, a Status parameter and the Convolver log say why audio is
  silent and what to do
  ([status and log](docs/use/plugins.md#status-and-the-convolver-log)).

Also:

- **Routing for room correction and crossovers.** Convolver reads the
  `.cfg` format of the old Windows Convolver (version 3.0 and later), with
  matrix routing, fractional gains and delays; Roon, JRiver and Audiolense
  use variants of it, though exchanging files with them has not yet been
  tested, and old files may need a few edits
  ([other programs](docs/use/other-programs.md)). Configuration lists
  select a suitable filter for the host's sample rate and channels,
  preferring an exact channel match ([channels](docs/use/plugins.md#channels)).
  `convolver --validate` checks a configuration, and `--profile
  portable-paths` warns about common portability problems
  ([check a configuration](docs/use/other-programs.md#check-a-configuration)).
- **A level that suits each filter.** The Gain mode plays your Gain, a
  noise-based estimate, or the gain at which no input within full scale can
  clip, worked out from the filter's taps ([settings](docs/use/plugins.md#settings));
  the command-line tool has `--gain-db auto` and `no-clip`
  ([gain](docs/use/command-line.md#gain-and-attenuation)).
- **Dither defaults informed by measurement** for the command-line tool's
  integer output ([dither](docs/use/command-line.md#integer-formats-dither-and-noise-shaping)).
- **Free, on macOS, Windows and Linux:** GPL source, no copy protection or
  account, and the same engine in every format.

Convolver plays the response you give it as it is, so it brings no library
of sampled spaces and no controls that reshape a reverb's decay or tone;
you bring the filter.

## Preview status

Convolver is a developer preview: the latest, [0.18.10](https://github.com/jrp2014/convolver/releases/tag/v0.18.10), is an
unsigned pre-release for macOS, Windows and Linux. Its VST3 plug-in has a
window on macOS and Windows and uses the host's controls on Linux, and the
Linux package holds the LV2 plug-in. 0.13.20 added the Gain mode and
opening a Convolver preset from the window; 0.13.24 reduces the risk of an
LV2 crash in hosts built on JUCE after a Gain mode change; 0.14.0 restores
the playing filter more quickly, uses less CPU once the sound has stopped
and reports clipping after a command-line render; 0.18.10 keeps a filter
chosen before the host plays, waits for a start that suits a filter and
words the window and Status more plainly. These previews
were published before anyone listened to the new audio engine (since 0.13)
or tried the Windows window, LV2 or the features new since 0.13.20 in a host, and
before an independent user's walkthrough, which are still to come
([status](docs/project/status.md)).
Processing is single precision; [limits](docs/use/limits.md) lists what
Convolver does not do.

## Start here

| I want to… | Read |
| --- | --- |
| Use Convolver | [Using Convolver](docs/use/README.md), then [your first filter](docs/use/first-filter.md) |
| Download the preview (macOS, Windows, Linux) | [0.18.10 pre-release](https://github.com/jrp2014/convolver/releases/tag/v0.18.10) |
| Use the plug-ins | [Install](docs/use/install.md) and [your first sound in a host](docs/use/first-sound-in-a-host.md) |
| Build it, embed it or change it | [Build from source](docs/develop/build.md), [library guide](docs/develop/library.md), [implementation map](docs/develop/architecture.md), [contributing](CONTRIBUTING.md) |
| Find any page | [Documentation index](docs/README.md) |
| See what is ready, decided or planned | [Status](docs/project/status.md), [decisions](docs/project/decisions.md), [TODO](TODO.md), [changelog](CHANGELOG.md) |

## Try it

With CMake 3.20 or later, a C++20 compiler, libsndfile, Python 3.8 or later
and, except on macOS, which uses its own Accelerate framework, single-precision
FFTW (`brew install cmake libsndfile`, or
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
| [`plugins/`](plugins/) | One folder per plug-in format ([`vst3/`](plugins/vst3/), [`au/`](plugins/au/), [`lv2/`](plugins/lv2/)), and the macOS window they share ([`editor/`](plugins/editor/)) |
| [`tests/`](tests/) | Native, plug-in, package, compatibility and historical tests |
| [`docs/`](docs/README.md) | Guides by reader ([`use/`](docs/use/README.md), [`develop/`](docs/develop/), [`project/`](docs/project/)) and dated records ([`archive/`](docs/archive/)) |
| [`examples/`](examples/) | First-use filters with expected output |
| [`cmake/`](cmake/), [`packaging/`](packaging/), [`scripts/`](scripts/) | Build helpers, release packaging, and release and repository checks |
| [`changes/`](changes/README.md) | Each open or recently merged pull request's entries for the changelog, TODO and decisions, until they are folded in |
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

<!-- VST trademark notice: written by scripts/trademarks.py from packaging/trademarks/vst.json -->

<img src="docs/use/images/VST_Compatible_Logo_Steinberg.png" alt="VST Compatible logo" width="120">

Convolver supports VST® 3. VST is a registered trademark of Steinberg Media Technologies GmbH.

<!-- End of the VST trademark notice -->
