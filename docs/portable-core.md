# Portable core and CLI

The new `portable/` implementation builds normal C++20 translation units against
real FFTW (single precision) and libsndfile. It is independent of Windows/ATL and
the historical plug-in wrappers. The original sources and their regression tests
remain in place.

## Build

Install CMake 3.20+, a C++20 compiler, FFTW's single-precision development library
and libsndfile's development library. For example:

```sh
# macOS with Homebrew
brew install cmake fftw libsndfile
# Debian/Ubuntu
sudo apt-get install cmake g++ libfftw3-dev libsndfile1-dev
```

Then, from the repository root:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
./build-native/convolver --version
```

Use `-DCMAKE_PREFIX_PATH=/path/to/dependencies` for a separate installation.
Configuration never downloads dependencies. CMake finds FFTW3f/SndFile package
configs, with library/header discovery fallbacks (and pkg-config for libsndfile).
The single-precision FFTW library is required even if double FFTW is installed.

Windows/MSVC configuration, using an existing vcpkg installation:

```powershell
vcpkg install fftw3:x64-windows 'libsndfile[core]:x64-windows'
cmake -S . -B build-native -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DCONVOLVER_LEGACY_TESTS=OFF
cmake --build build-native --config Release
ctest --test-dir build-native -C Release --output-on-failure
```

These Windows instructions and Linux/MSVC CI jobs are provided for validation;
local verification so far is macOS Apple Silicon with AppleClang. The historical
Windows hosts have not been ported. Dependency package versions in CI follow the
runner's package manager; `--version` records the linked versions. Pin a package
baseline before making a reproducible binary release.

Targets: `convolver_core`, `convolver_io`, `convolver_cli` (executable name
`convolver`). `cmake --build build-native --target check` builds native tests and
runs CTest. `cmake --install build-native --prefix /desired/prefix` installs the
CLI, libraries and headers. Libraries can also be consumed with `add_subdirectory`
and linked through their CMake target names. Installed CMake package exports and
self-contained binary bundles are not supplied yet. Shared dependencies must
remain installed; this is not a redistributable app bundle.

Build options:

| Option | Default | Purpose |
| --- | --- | --- |
| `CONVOLVER_BUILD_NATIVE` | ON | Core, I/O and CLI; requires real dependencies |
| `BUILD_TESTING` | ON | Native tests and documentation checks; requires Python 3.8+ |
| `CONVOLVER_SANITIZERS` | OFF | ASan/UBSan instrumentation of native targets with GCC/Clang |
| `CONVOLVER_LEGACY_TESTS` | ON for native Clang on macOS/Linux; OFF elsewhere | Existing sanitized extraction harnesses |

## File rendering

```sh
./build-native/convolver --filter room.cfg --input music.wav --output corrected.wav
./build-native/convolver --filter impulse.wav --input music.wav --output corrected.wav --block-size 128 --gain-db -6 --output-format wav-pcm24
```

The filter may be an IR sound file, a historical configuration, or a configuration
list. A direct multichannel IR maps each IR channel to the corresponding input
and output; a mono IR requires mono input. Use a config to apply one mono IR to
several channels. Lists select the first entry matching input rate and channel
count. There is no resampling or nearest-rate substitution.

`--block-size` is the uniform partition payload B, not the historical partition
count. FFT length is 2B; positive non-power-of-two B values also work. The core
has B frames of algorithmic latency. The CLI trims that leading latency by
default and always drains the complete FIR/routing tail. `--keep-latency` retains
it, matching the historical Pipe timing convention. No input frames means no
output audio frames. Gain is a finite decibel value; finite float output can
exceed unity. PCM output clips to its representable range via libsndfile.

Outputs: `wav-float` (default), `wav-pcm16`, `wav-pcm24`, `wav-pcm32`, `raw-float`.
There is no dither/noise-shaping option yet. The historical channel mask is
parsed and retained in Configuration, but the current WAV writer does not encode
that mask; output channel order is preserved. Do not rely on WAV speaker labels.

Output paths must differ from input, configuration/list files and referenced IRs,
including filesystem aliases. A regular file redirected to stdin is protected
from being opened as output by the CLI. Shell redirection itself happens before
the program starts: never redirect stdout onto an input/configuration/IR file.
Failures return a nonzero exit code and a diagnostic on stderr. Output may be
partial after an I/O or numerical failure; writing is not transactional.

## RAW streams

Standard streams use explicitly described little-endian IEEE float32, interleaved
by frame. No WAV header needs rewriting on a non-seekable pipe:

```sh
producer | ./build-native/convolver --filter room.cfg --input - --raw-input 48000 2 --output - --output-format raw-float | consumer
```

`--raw-input RATE CHANNELS` also works on a RAW file. The output rate and channel
count come from the selected configuration. No diagnostics are written to binary
stdout during rendering. Incomplete RAW frames, nonfinite samples, broken pipes,
short writes and detected finalization failures are errors. Caller-owned standard
descriptors are not closed by the I/O wrappers.

Ordinary RIFF/WAVE inputs receive extra chunk-boundary and PCM/float alignment
validation, because libsndfile can otherwise silently accept truncation. Other
input containers, including RF64, retain libsndfile's validation semantics; this
is not a complete validator for every supported codec. Additional codecs depend
on how the installed libsndfile was built. Output is currently ordinary WAV or
RAW; use RAW for streams that can exceed WAV's 4 GiB container limit.

## Compatibility and library API

Configuration syntax follows the [archived specification](historical/convolver-4.4/config.html):
header, input delays, output delays, then four lines per path (IR filename,
selected IR channel, input routes, output routes). `//` comments and blank lines
are accepted. The fractional part of a route is its weight; an integer route has
unit magnitude, and `-0` means inverted channel zero. Delays use the historical
float parsing and truncation from milliseconds to integer frames.

Relative paths resolve beside the configuration/list that contains them, not
from the process working directory. Text paths use UTF-8. Absolute paths remain
platform-specific; archived Windows paths need adapting on macOS/Linux. Unequal
IR lengths are a supported extension; each path is independently zero-padded,
and the longest connected route determines the tail.

`include/convolver/engine.hpp` exposes Configuration, FilterPath, Route and Engine.
Construction prepares FFT plans and all processing buffers. `process` takes
interleaved float spans and any frame count, producing the same number of output
frames. Empty input means silence for drain. Input/output spans must not overlap;
there is no in-place mode. `latency_frames()` and `tail_frames()` are separate;
feed their sum as silence after the final nonempty input to drain fully.
`reset()` clears history and delays. Instances have separate state; an individual
instance must not be processed/reset concurrently.

This implementation uses uniform partitioned overlap-add with an input-spectrum
history. It preserves the reviewed engine's linear convolution and timing
semantics while making ownership and lengths explicit. It is new portable
production code, not an extraction test or an assertion that every historical
host feature has been ported. FFTW scaling is applied once when preparing IR
spectra. There is no FFT-backend selection or performance tuning yet.

Prepared processing/reset performs no C++ heap allocation; a test guards that
contract. This is not a hard real-time guarantee or instrumentation of every
possible library/system allocation. Planning/destruction is serialized between
these engine instances, outside processing. Other FFTW users in the same process
must coordinate their planning too.

Resource limits are deliberate: 1..256 channels, 1..768000 Hz, B up to 1048576,
4096 paths/routes, and about 1 GiB of prepared float-equivalent storage. Core IRs
and individual delays are bounded to 67108864 frames. The importer additionally
limits text to 1 MiB, list nesting to 16, visited entries to 4096, total decoded IR
samples to 64 Mi, and each delay to 16 Mi frames. Invalid dimensions, arithmetic
overflow and nonfinite inputs/IRs/gains are rejected. DSP overflow is detected by
the CLI's output check; library callers choose their own output policy.

## Verification record

The native tests link actual production translation units and actual libraries:
independent double time-domain convolution, seeded matrices/delays, awkward IR
lengths/block sizes, chunk invariance, reset, tails, file round trips, full-width
PCM32 endpoints, legacy parsing and actual RAW descriptors/subprocess pipelines.
Fault tests cover source overwrite protection, truncation, fragmented pipes,
write failure and WAV-header finalization. Existing historical tests and archived
website integrity checks remain enabled on supported Clang hosts.

Local dependency sources used for verification:

- FFTW 3.3.11, archive SHA256
  `5630c24cdeb33b131612f7eb4b1a9934234754f9f388ff8617458d0be6f239a1`.
- libsndfile 1.2.2, archive SHA256
  `3799ca9924d3125038880367bf1468e53a1b7e3686a934f098b7e1d286cdb80e`;
  optional external codecs disabled for this local test installation.

FFTW's 3.3.11 CMake sources still set their reported version to 3.3.10. The final
local verification uses the official configure/make build of that source archive,
which reports 3.3.11. Convolver itself is built with CMake in both cases.
See [test coverage](testing.md) for the remaining legacy-host and platform limits.
