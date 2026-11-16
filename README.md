<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Convolver

**Download:** [Convolver 0.13.20 for macOS, Windows and Linux](https://github.com/jrp2014/convolver/releases/tag/v0.13.20), an
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
most of the effort here has gone into the refinements around it, below.
Each point links to the guide, test or measurement that backs it, and
[preview status](#preview-status) says what has not yet been tried.

- **Its output is checked, not assumed.** Automated tests on Linux, macOS
  and Windows compare the engine's output with a direct double-precision
  convolution written independently of it, across routings, delays, filter
  lengths and host buffer sizes. For the current engine the recorded worst
  difference was 5.2e-7 of the output's peak, about −126 dB
  ([changelog](CHANGELOG.md#0132---2026-11-09)), and long stress and soak
  runs stayed within 4e-6, about −108 dB
  ([testing](docs/develop/testing.md#native-production-tests),
  [measurements](docs/develop/fft-measurements.md#on-apple-silicon)). Each
  plug-in format renders the bundled examples against that reference and gives the
  same samples as the command-line tool
  ([plug-in comparisons](docs/develop/testing.md#plug-in-comparisons-plug-2)),
  and you can check a host's render yourself with `convolver --compare`
  ([check a stereo filter in a host](docs/use/check-a-stereo-filter.md)).
- **Long reverbs with no added delay.** By default Convolver adds no
  processing delay at any host buffer size: it sums a filter's first taps
  directly and spreads the work of its larger blocks over the callbacks in
  between ([latency and tail](docs/use/plugins.md#latency-and-tail),
  [stress tests](docs/develop/testing.md#native-production-tests)). On
  jrp's Apple silicon Mac (8 November 2026, 0.13.0) this engine needed 15
  to 17 times less processor time for a 30-second filter than the engine
  of the first preview at the same latency
  ([measurements](docs/develop/fft-measurements.md#on-apple-silicon)), and
  on a Linux cloud machine a 30-second stereo reverb at zero latency took
  under 2% of one core
  ([threads and cores](docs/develop/architecture.md#threads-and-cores)).
  When you are not monitoring live, a Latency setting trades delay for
  processor time.
- **A level that suits each filter.** The plug-ins' Gain mode plays your
  own Gain, an estimate of how far to turn the filter down found by playing
  noise through it, or the gain at which no input within full scale can clip,
  worked out from the filter's taps. Each filter plays at its own figure,
  shown in Convolver's window where it has one
  ([settings](docs/use/plugins.md#settings)). The command-line tool offers
  the same with `--gain-db auto` and `no-clip`, and for integer output
  `no-clip` also leaves room for rounding, dither and noise shaping
  ([gain](docs/use/command-line.md#gain-and-attenuation)).
- **It fails safe and says why.** A first filter that cannot be loaded
  plays silence, not your unfiltered audio; a replacement that does not
  fit leaves the playing filter in place. For filters with different input
  and output counts, such as a 2→6 crossover, Convolver's own Bypass fades
  to silence; a host that takes the plug-in out of the signal path may
  still route unfiltered audio around it, so check which kind of bypass
  your host's button uses ([bypass](docs/use/plugins.md#bypass)). In VST3 and the Audio Unit, a
  Status parameter and the Convolver log say why audio is silent and what to do
  ([status and log](docs/use/plugins.md#status-and-the-convolver-log)).
- **Smooth changes during playback.** Gain and Mix changes fade over
  1.3 ms. A filter loaded during playback crossfades in over 5.3 ms, though
  the new filter starts without the old one's history. A preset or undo
  that holds the filter already playing, at the same Latency, keeps it and
  lets its reverb tail ring on
  ([replacing a filter](docs/use/plugins.md#replacing-a-filter)). A
  Latency change restarts processing, which leaves a gap and cuts a tail
  that is still sounding ([latency and tail](docs/use/plugins.md#latency-and-tail)).
  The host is told the exact latency, and Mix delays the dry signal to line
  up with the filtered one.
- **The room-correction files people have.** Convolver reads the `.cfg`
  format of the old Windows Convolver (version 3.0 and later), with matrix
  routing, fractional gains and delays; Roon, JRiver and Audiolense use
  variants of it, though exchanging files with them has not yet been tested
  ([other programs](docs/use/other-programs.md)). Coming from the old
  Windows Convolver? Your `.cfg` files may need a few edits
  ([how](docs/use/other-programs.md#configurations-from-the-old-windows-convolver)).
  A configuration list holds filters for several sample rates and channel
  counts: the command-line tool takes the first entry that fits, and a
  plug-in selects a compatible entry for the host's rate and channels when
  the filter is loaded ([channels](docs/use/plugins.md#channels)). `convolver --validate` checks a configuration, and
  `--profile portable-paths` whether its paths will work on another
  computer ([check a configuration](docs/use/other-programs.md#check-a-configuration)).
- **Dither where it can be heard, and nowhere else.** For integer output the
  command-line tool dithers by default only below 20 significant bits,
  where measurements showed rounding could reach the threshold of hearing,
  and warns when a setting adds noise for nothing
  ([dither](docs/use/command-line.md#integer-formats-dither-and-noise-shaping)).
  On the same build and machine, with the default FFT planning, the same
  input and seed give byte-identical samples, whatever the buffering.
- **Updates that do not quietly change the sound.** From 0.13.2 on, each
  published preview's example presets and the audio it rendered from them
  are kept in the repository, and every later build's command-line tool
  and VST3 plug-in must reproduce that audio within a stated tolerance
  ([upgrade tests](tests/upgrade/README.md)).
- **Free, on macOS, Windows and Linux.** GPL source, no copy protection or
  account, and the same engine in every format.

### Problems users have reported

People choosing a convolver have described the problems below in public
forums and vendors' help pages, gathered on 7 November 2026 in the
[edges plan](docs/project/plans/2026-11-06-edges-over-other-convolvers.md#limitations-users-cite).
They are general to convolvers rather than to one product. This is how
Convolver handles each:

| Reported | What Convolver does |
| --- | --- |
| Long reverbs cause processor spikes or dropouts at small host buffers | Spreads the work of its large blocks over the callbacks between them: with a 30-second filter at 64-frame callbacks, the 99th-percentile callback stayed under a tenth of the period ([measurements](docs/develop/fft-measurements.md#the-non-uniform-engine)) |
| Zero latency costs processor time and cannot be turned off | Zero latency is the default; the Latency setting (64 to 4096 frames) trades delay for processor time, and its cost is published ([latency and tail](docs/use/plugins.md#latency-and-tail)) |
| A bounce faster than real time drops out when convolution runs on a background thread | Processes within the host's own call, on the host's audio thread, so a bounce cannot outrun it ([threads and cores](docs/develop/architecture.md#threads-and-cores)) |
| A convolver does not say what went wrong | The Status parameter and the Convolver log say why audio is silent and what to do ([status and log](docs/use/plugins.md#status-and-the-convolver-log)) |
| The length shown is not the filter's | The window and `convolver --inspect` show the decoded length of what is loaded ([window](docs/use/plugins.md#convolvers-window), [inspection](docs/use/command-line.md#inspecting-and-validating-configurations)) |
| A filter resampled to another rate changes level or clips | Never resamples a filter: a configuration list holds one per sample rate, and a filter for another rate is refused with a message rather than resampled ([configuration lists](docs/use/configuration.md#paths-and-configuration-lists)) |

Other reported problems are not yet addressed: memory per instance,
true-stereo reverb files without a `.cfg`, and the delay of long
linear-phase filters. They are planned in [TODO](TODO.md#edges-over-other-convolvers).

Because it plays the response you give it as it is, Convolver brings no
library of sampled spaces and no controls that reshape a reverb's decay or
tone; you bring the filter.

## Preview status

Convolver is a developer preview: the latest, [0.13.20](https://github.com/jrp2014/convolver/releases/tag/v0.13.20), is an
unsigned pre-release for macOS, Windows and Linux. Its VST3 plug-in has a
window on macOS and Windows and uses the host's controls on Linux, and the
Linux package holds the LV2 plug-in. 0.13.20 adds the Gain mode and opening
a Convolver preset from the window. It was published before anyone listened
to the new audio engine (since 0.13) or tried the Windows window, LV2 or
0.13.20's new features in a host, and before an independent user's
walkthrough, which are still to come ([status](docs/project/status.md)).
Processing is single precision; [limits](docs/use/limits.md) lists what
Convolver does not do.

## Start here

| I want to… | Read |
| --- | --- |
| Use Convolver | [Using Convolver](docs/use/README.md), then [your first filter](docs/use/first-filter.md) |
| Download the preview (macOS, Windows, Linux) | [0.13.20 pre-release](https://github.com/jrp2014/convolver/releases/tag/v0.13.20) |
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
