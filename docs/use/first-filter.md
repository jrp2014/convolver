<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Your first filter

For anyone trying the command-line tool for the first time. You will filter
three example recordings that come with Convolver and check the results, then
filter your own file.

**In short:** build `convolver`, run it on `examples/first-use/stereo`, and
compare the output with the expected file. Then give it your own IR or `.cfg`
and your own WAV file.

## 1. Get the command-line tool

Preview packages contain the plug-in and the preset tools, not `convolver`
itself, so build it from the source. On macOS with Homebrew, or Debian and
Ubuntu:

```sh
brew install cmake fftw libsndfile
sudo apt-get install cmake g++ python3 libfftw3-dev libsndfile1-dev
```

Then, from the folder holding the source:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/convolver --version
```

The last command prints the version. If a command fails, or you are on Windows,
see [build from source](../develop/build.md).

## 2. Filter the examples

The [first-use examples](../../examples/first-use/README.md) are three small
filters, each with an input recording and the output it should give:

- `mono/`: one IR file used directly, with no configuration;
- `stereo/`: a `.cfg` that filters each side differently, feeds some of the
  left into the right, and delays the right by 1 ms;
- `crossover/`: a `.cfg` that splits stereo into six outputs, low, mid and high
  bands for each side.

Run all three from the source folder:

<!-- first-use-example -->
```sh
./build/convolver --filter examples/first-use/mono/impulse.wav --input examples/first-use/mono/input.wav --output mono.wav
./build/convolver --filter examples/first-use/stereo/filter.cfg --input examples/first-use/stereo/input.wav --output stereo.wav
./build/convolver --filter examples/first-use/crossover/filter.cfg --input examples/first-use/crossover/input.wav --output crossover.wav
```

Each command writes a 32-bit float WAV. Convolver removes its own processing
delay and keeps the whole filter tail, so `stereo.wav` lines up with the input
and matches `examples/first-use/stereo/expected.wav` sample for sample, apart
from rounding differences smaller than 0.00001.
The inputs are white noise at half full scale: turn the volume down before you
listen.

## 3. Look inside a configuration

Ask Convolver what a configuration does without filtering anything:

<!-- first-use-example -->
```sh
./build/convolver --validate examples/first-use/crossover/filter.cfg
./build/convolver --inspect examples/first-use/stereo/filter.cfg
```

`--validate` prints one line saying whether the file can be used. `--inspect`
lists the routes, delays, IRs, latency and tail. The example `.cfg` files are
commented line by line; [configuration files](configuration.md) explains every
field.

## 4. Filter your own file

With a multichannel IR, each IR channel filters the matching input channel:

```sh
./build/convolver --filter impulse.wav --input music.wav --output corrected.wav
```

With a configuration, and 24-bit output:

```sh
./build/convolver --filter room.cfg --input music.wav --output corrected.wav --output-format wav-pcm24
```

The input's sample rate and channel count must match the filter. If they do
not, the message lists each candidate filter and what differs; for a mono IR
with stereo input it prints a configuration you can save.

## If it did not work

- **"cannot find" an IR:** paths in a `.cfg` are relative to the `.cfg`
  file's folder. Check the spelling, including upper and lower case.
- **The output is too loud or clips:** lower it with `--gain-db -6`, or let
  Convolver estimate the gain with `--gain-db no-clip`
  ([gain](command-line.md#gain-and-attenuation)).
- Anything else: [troubleshooting](troubleshooting.md).

Next: every option is in the [command-line reference](command-line.md); to use
the same filter in a DAW, see [install](install.md).
