<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Building and consuming the portable library

[Documentation index](../README.md) · [Implementation map](architecture.md)

**In short:** install Convolver (or add it with `add_subdirectory`), then
`find_package(Convolver 0.4 REQUIRED COMPONENTS core)` and link
`Convolver::core`. Add the `host` component for plug-in-style planar processing
and `io` for sound files and configurations (needs libsndfile). The libraries are
static development packages for the same toolchain, not a stable binary ABI.
The core-only build needs only an FFT library (or none, with Ooura).

The portable package reads its version from [VERSION](../../VERSION). It provides static C++20 libraries. The API is evolving: package
version checks accept compatible patch releases within the same minor version,
but this is not a stable binary ABI promise. Rebuild consumers with the same
toolchain/runtime. `BUILD_SHARED_LIBS` does not change these static targets.

## Core-only build

From a fresh build directory:

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release \
  -DCONVOLVER_BUILD_IO=OFF -DCONVOLVER_BUILD_CLI=OFF
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
cmake --install build-core --prefix /path/to/convolver-install
```

This builds the core and its C++/consumer tests without looking for libsndfile or
Python. FFTW's float library remains required, unless selecting
`-DCONVOLVER_FFT_BACKEND=ACCELERATE` on Apple or the built-in
`-DCONVOLVER_FFT_BACKEND=OOURA` anywhere. Set `-DBUILD_TESTING=OFF` for a
library-only top-level build. Dependency discovery never downloads libraries.

The full default build still includes I/O, CLI, native tests, and documentation
tests; Clang on macOS/Linux additionally enables the historical extraction tests.
`CONVOLVER_BUILD_NATIVE=OFF` retains the historical regression-only mode. Set
`CONVOLVER_BUILD_CLI=OFF` to build libraries without the renderer. Enabling CLI
while disabling I/O is an error. In an existing build directory, cached options
remain explicit: disable legacy/documentation tests as well when converting that
directory into a Python-free core build, or use the core-only preset.

## Installed package

An external application's `CMakeLists.txt` can use:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyConvolverApp LANGUAGES CXX)
find_package(Convolver 0.4 REQUIRED COMPONENTS core)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Convolver::core)
```

Configure the application with `-DCMAKE_PREFIX_PATH=/path/to/convolver-install`
and any separate dependency prefixes. CMake supplies the public include path,
C++20 requirement and static FFT/thread link dependencies. No components means
`core`; even a full installation does not look for libsndfile until requested:

```cmake
find_package(Convolver 0.4 REQUIRED COMPONENTS core io)
target_link_libraries(my_app PRIVATE Convolver::core Convolver::io)
```

`Convolver::host` adds the prepared planar host lifecycle and embedded-state codec.
Request `COMPONENTS host` to use it; it depends only on core and has no SDK/file I/O
dependency. Its threading rules are in the [plug-in adapter contract](plugin-contract.md#replacement-and-threads).

`Convolver::io` provides file/configuration functions and configuration inspection
(`convolver/inspection.hpp`), links libsndfile, and depends on `Convolver::core`.
Missing required components fail clearly; optional components can be inspected
through `Convolver_core_FOUND` and `Convolver_io_FOUND`.
`Convolver_FFT_BACKEND` reports the package's fixed backend. A consumer cannot
switch it without rebuilding the library.

Install exports use relative Convolver paths and recreate dependencies on the
consumer's machine. The installation prefix can move; compatible FFTW/libsndfile
dependencies must still be installed and discoverable. These are development
packages, not self-contained application bundles. Shared dependency DLLs/dylibs
must also be available at runtime. Debug sanitizer packages propagate their
sanitizer link options and are for development with a compatible compiler.

## Source-tree use

```cmake
set(CONVOLVER_BUILD_IO OFF CACHE BOOL "" FORCE)
set(CONVOLVER_BUILD_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/convolver)
target_link_libraries(my_app PRIVATE Convolver::core)
```

The namespaced targets are identical to installed consumption. Convolver's tests
default to off when included as a subproject, even if the parent uses CTest.
The parent can explicitly enable `CONVOLVER_BUILD_TESTS` if desired. Dependencies
must be provided by installed packages or suitable pre-existing imported targets.

## Using the engine

How to call the libraries once they are linked. The command-line tool's
behaviour is in the [command-line reference](../use/command-line.md).

### Engine API

[engine.hpp](../../include/convolver/engine.hpp) exposes Configuration, FilterPath, Route and Engine.
Construction prepares FFT plans and all processing buffers. `process` takes
interleaved float spans and any frame count, producing the same number of output
frames. Empty input means silence for drain. Input/output spans must not overlap;
there is no in-place mode. `latency_frames()` and `tail_frames()` are separate;
feed their sum as silence after the final nonempty input to drain fully.
`reset()` clears history and delays. Instances have separate state; an individual
instance must not be processed/reset concurrently.

This implementation uses uniform partitioned overlap-add with input-spectrum
history, as described in [the implementation map](architecture.md#processing-data-flow). All three backends expose the same spectrum layout and
scaling; 1/N normalization is applied once when preparing IR spectra. The first
backend measurements are recorded in [the FFT guide](fft-backends.md).

Prepared processing/reset performs no C++ heap allocation; a test guards that
contract. This is not a hard real-time guarantee or instrumentation of every
possible library/system allocation. FFTW planning/destruction is serialized between
these engine instances, outside processing. Accelerate owns an independent setup
per instance. Other FFTW users in the same process
must coordinate their planning too, which is why plug-in builds default to Ooura
or Accelerate ([plug-in builds](fft-backends.md#plug-in-builds)).

### Resource limits

Resource limits are deliberate: 1..256 channels, 1..768000 Hz, B up to 1048576,
4096 paths/routes, and about 1 GiB of prepared float-equivalent storage. Core IRs
and individual delays are bounded to 67108864 frames. The importer additionally
limits each text file to 1 MiB and all text read per operation to 16 MiB, list nesting to 16, visited entries to 4096, total decoded IR
samples to 64 Mi, and each delay to 16 Mi frames. Invalid dimensions, arithmetic
overflow and nonfinite inputs/IRs/gains are rejected. DSP overflow is detected by
the CLI's output check; library callers choose their own output policy.

### FFT plans and wisdom

Library callers can pass `FftOptions` to `Engine`, or call `tune_fft`, then
`export_fft_wisdom`/`import_fft_wisdom`. Core APIs use strings, with file handling
kept in the CLI. Wisdom is global to the FFTW library in the process. Convolver
serializes its own planning, destruction and wisdom access; applications making
other FFTW planner calls must coordinate those too. All preparation belongs
outside the audio callback.

### Inspecting configurations

`convolver::inspect_configuration` in `convolver/inspection.hpp`
(`Convolver::io`) returns the same report as `convolver --inspect`
([inspection](../use/command-line.md#inspecting-and-validating-configurations)),
and the core's `convolver::analyze_configuration` returns latency, tail, FFT
length, accounted storage and partitions for an in-memory `Configuration`.

### Writing files

`AudioWriter` takes either an `OutputEncoding` (a `Container` and a
`SampleType`, for example `{Container::flac, SampleType::pcm24}`) or one of the
original `OutputFormat` shorthands, which `output_encoding()` maps to the same
pairs; `supported()` says which pairs exist ([output formats](../use/command-line.md#output-formats)),
and `available()` whether the libsndfile in use can write a container (only
FLAC can be missing; the writer then throws `std::runtime_error`).
`AudioReader` takes an optional `RawSpec` and a `RawEncoding` for headerless
input ([RAW streams](../use/command-line.md#raw-streams)); the encoding is
ignored for files with a header. `parse_raw_encoding()` and
`raw_encoding_name()` convert between a `RawEncoding` and the names `s16le` to
`f64be`. `sound_file_header()` says whether a file's content is a sound file
(libsndfile recognises it, or it starts with a container marker), opening
regular files only. Headerless `.pcm` and `.raw` filters in a configuration are read with
`ConfigurationSelection::raw_filter_encoding` (`f32le` by default;
[filter files](../use/configuration.md#filter-files)); `load_configuration`'s
optional `raw_filters` argument lists those it read, and inspection takes the
same choice in `InspectionOptions` and reports it per path.

`AudioWriter` defaults to plain rounding for integer formats; pass
`QuantizationOptions` to enable dither, noise shaping or valid bits, as the
command line's [integer output](../use/command-line.md#integer-formats-dither-and-noise-shaping)
describes. The noise-shaping coefficients are in
[io.hpp](../../include/convolver/io.hpp). `AudioWriter(path, info, format, {}, OutputMetadata{mask})`
writes [speaker labels](../use/command-line.md#speaker-labels); the default
`OutputMetadata{}` keeps the plain header. `estimate_attenuation` in
[engine.hpp](../../include/convolver/engine.hpp) is the calculation behind
`--gain-db auto` and `no-clip` ([attenuation.cpp](../../portable/attenuation.cpp)).

## Presets

`CMakePresets.json` names the common configurations, including `core-only`.
Their prerequisites and commands are in [build from source](build.md#cmake-presets).

## What verifies the package

`packaging.consumer` uses only CMake and C++, and runs under CTest. It installs the
current build into a temporary prefix, moves that prefix to a path containing a
space, and builds/runs a separate application against the installed package.
That application compares convolution against a direct oracle. A full package
also exercises a real-file I/O round trip; a core-only package verifies that I/O
cannot be requested. Unknown required and optional components, repeated lookup,
and `add_subdirectory` consumption are covered. Core consumers disable Python
and libsndfile package discovery explicitly. The source example is in
[`tests/consumer`](../../tests/consumer).

The test builds small secondary projects, so build the primary targets before
running CTest. Disable `CONVOLVER_PACKAGE_TESTS` for cross builds which cannot
execute test applications, or disable all tests. Tests inherit the selected
compiler, generator, SDK, dependency prefixes and Windows toolchain/triplet.
The normal GitHub matrix runs these checks on Linux, Windows and macOS; Apple
also exercises Accelerate. CI results belong to the specific tested commit.

## Optional plug-in distributions

The `release-vst3` preset enables the separate `PluginDistribution` installation
component. It stages the SDK bundle, `convolver-preset`, `convolver-equalizer-apo`, presets,
notices and user guide (with the presets and Equalizer APO guides);
it does not replace the C++ library exports above. See the
[pinned source recipe](build.md) and [preview qualification record](releasing.md).
