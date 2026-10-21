# Building and consuming the portable library

[Documentation index](README.md) · [Implementation map](project-structure.md)

The portable package reads its version from [VERSION](../VERSION). It provides static C++20 libraries. The API is evolving: package
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
dependency. See [VST3 and host lifecycle](vst3.md).

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

## Presets

`CMakePresets.json` uses schema 2, supported by CMake 3.20. Presets require Ninja;
the ordinary `cmake -S/-B` commands also work with Makefiles and Visual Studio.
On Windows, run Ninja presets from a Visual Studio developer shell. The sanitizer
presets require Clang/GCC; the Accelerate presets require Apple platforms.

```sh
cmake --list-presets
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Presets are `release`, `debug-sanitizers`, `core-only`, `accelerate`, and
`accelerate-debug`, and `ooura`. Each has matching configure/build/test presets and a separate
directory under `build/presets/`. Pass `-DCMAKE_PREFIX_PATH=...` during configure
for local dependencies. Keep personal paths/toolchain overrides in the ignored
`CMakeUserPresets.json`, inheriting one of the checked-in presets.

## What verifies the package

`packaging.consumer` uses only CMake and C++, and runs under CTest. It installs the
current build into a temporary prefix, moves that prefix to a path containing a
space, and builds/runs a separate application against the installed package.
That application compares convolution against a direct oracle. A full package
also exercises a real-file I/O round trip; a core-only package verifies that I/O
cannot be requested. Unknown required and optional components, repeated lookup,
and `add_subdirectory` consumption are covered. Core consumers disable Python
and libsndfile package discovery explicitly. The source example is in
[`tests/consumer`](../tests/consumer).

The test builds small secondary projects, so build the primary targets before
running CTest. Disable `CONVOLVER_PACKAGE_TESTS` for cross builds which cannot
execute test applications, or disable all tests. Tests inherit the selected
compiler, generator, SDK, dependency prefixes and Windows toolchain/triplet.
The normal GitHub matrix runs these checks on Linux, Windows and macOS; Apple
also exercises Accelerate. CI results belong to the specific tested commit.
