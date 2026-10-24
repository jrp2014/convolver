<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Build from source

[Documentation index](../README.md) · [Implementation map](architecture.md) · [Testing](testing.md)

For anyone, person or coding agent, who builds Convolver from source: the
command-line tool, the libraries, the tests, a plug-in or a preview package.
Build recipes live on this page; other pages link here instead of repeating
them.

**In short:**

- Install CMake 3.20 or later, a C++20 compiler, FFTW (single precision),
  libsndfile and Python 3.8 or later, then run the three commands under
  [everyday build](#everyday-build).
- Contributors and agents use the [sanitizer build](#sanitizer-build-for-changes)
  for changes, and a Release build for measurements.
- A plug-in build adds one option and that format's SDK: see
  [plug-ins](#plug-ins). Plug-ins use a built-in FFT, so they need no FFTW.
- Configuration never downloads anything. Missing dependencies stop CMake with a
  message naming what to install.

Contents: [choose a build](#choose-a-build) ·
[dependencies](#dependencies) · [everyday build](#everyday-build) ·
[sanitizer build](#sanitizer-build-for-changes) · [presets](#cmake-presets) ·
[FFT backend](#fft-backend) · [Windows](#windows) · [options](#build-options) ·
[targets and installing](#targets-and-installing) · [plug-ins](#plug-ins) ·
[pinned format dependencies](#pinned-format-dependencies) ·
[pinned preview recipe](#pinned-preview-recipe) ·
[when a build fails](#when-a-build-fails)

## Choose a build

| You want | Use |
| --- | --- |
| The `convolver` command-line tool | [Everyday build](#everyday-build) |
| To change the code (people and agents) | [Sanitizer build](#sanitizer-build-for-changes), then [testing](testing.md) |
| Only the C++ libraries, for your own program | [Library guide](library.md#core-only-build) |
| No FFT library at all | Add `-DCONVOLVER_FFT_BACKEND=OOURA` ([FFT backend](#fft-backend)) |
| A VST3, Audio Unit or LV2 plug-in | [Plug-ins](#plug-ins) |
| The exact recipe used for preview packages | [Pinned preview recipe](#pinned-preview-recipe) |

## Dependencies

| Platform | Command |
| --- | --- |
| macOS (Homebrew) | `brew install cmake fftw libsndfile` |
| Debian or Ubuntu | `sudo apt-get install cmake g++ python3 libfftw3-dev libsndfile1-dev` |
| Windows | vcpkg; see [Windows](#windows) |

Contributors on Ubuntu also need the tools the checks use:
`sudo apt-get install libclang-rt-18-dev` for the default Clang 18 compiler.
Match the runtime package to your compiler if it differs. Install the latest
stable formatting and analysis tools using the
[contributor instructions](../../CONTRIBUTING.md#formatting-and-analysis).
Claude Code cloud sessions install all of these through
`.claude/hooks/session-start.sh`; add any new build or test dependency there
and to the CI workflow in the same pull request.

Use `-DCMAKE_PREFIX_PATH=/path/to/dependencies` for libraries installed
somewhere CMake does not look. CMake finds FFTW3f and SndFile package configs,
then falls back to searching for the libraries and headers (and pkg-config for
libsndfile). The default FFTW backend needs FFTW's **single-precision** library
even if the double-precision one is installed.

## Everyday build

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/convolver --version
```

`--version` names the FFT backend and the linked library versions. To check
the result, run the [first-use examples](../use/first-filter.md) and
compare its output with the expected file beside it.

If Clang is your default compiler on macOS or Linux, the default build also
runs the historical regression tests, which need Clang's sanitizer runtimes.
When configuring stops with "Clang with ASan/UBSan runtimes is required for
legacy tests", install them (`libclang-rt-18-dev` for Clang 18 on Ubuntu) or
add `-DCONVOLVER_LEGACY_TESTS=OFF`.

## Sanitizer build for changes

Build and test every change this way before pushing. It instruments the native
targets with AddressSanitizer and UndefinedBehaviorSanitizer and runs the
historical regression tests too:

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCONVOLVER_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure --parallel 4
```

Use a separate Release build for timing and benchmarks. A plug-in built with
sanitizers can be loaded only by programs built with the same sanitizers (the
tests), not by a host or a validator such as `auval`.

What the tests establish, and which ones a change needs, is in
[testing](testing.md); the lint and formatting checks are in
[CONTRIBUTING](../../CONTRIBUTING.md#lint-checks).

## CMake presets

`CMakePresets.json` names the common configurations. Presets use the Ninja
generator, so install Ninja first; on Windows run them from a Visual Studio
developer shell. The ordinary `cmake -S . -B build` commands also work with
Makefiles and Visual Studio. Each preset has matching configure, build and test
presets and builds into `build/presets/NAME`:

| Preset | What it builds |
| --- | --- |
| `release` | Everything in the default configuration, Release |
| `debug-sanitizers` | The same, Debug with ASan and UBSan (GCC or Clang) |
| `core-only` | Core library and its C++ and package-consumer tests; no libsndfile or Python |
| `accelerate`, `accelerate-debug` | Apple's Accelerate FFT instead of FFTW (macOS) |
| `ooura` | The built-in Ooura FFT, so no FFT library is needed |
| `release-vst3` | The VST3 distribution; needs the SDK path ([VST3](#vst3)) |

```sh
cmake --list-presets
cmake --preset debug-sanitizers
cmake --build --preset debug-sanitizers
ctest --preset debug-sanitizers
```

The sanitizer presets need GCC or Clang; the Accelerate presets need macOS. Pass
`-DCMAKE_PREFIX_PATH=...` at configure time for local dependencies. Keep personal
paths and toolchain overrides in `CMakeUserPresets.json` (ignored by Git),
inheriting one of the checked-in presets.

## FFT backend

One FFT library serves every target in a build, chosen with
`-DCONVOLVER_FFT_BACKEND`:

| Value | Meaning |
| --- | --- |
| `AUTO` (default) | FFTW, except in a plug-in build: Ooura, or Accelerate on macOS |
| `FFTW` | FFTW's single-precision library |
| `ACCELERATE` | Apple's Accelerate framework (macOS only); searches for and links no FFTW |
| `OOURA` | Ooura's FFT, built into the library; the only remaining dependency is libsndfile for the CLI |

The choice is fixed when building, not at run time. Plug-in builds avoid FFTW
because a plug-in shares its process with the host and other plug-ins, which
may use the same FFTW, and FFTW's planner is not thread-safe; selecting FFTW for
a plug-in build makes CMake warn. The [FFT guide](fft-backends.md) has the
numerical contract and the measurements.

## Windows

With an existing vcpkg installation, in a Visual Studio developer terminal:

```powershell
vcpkg install fftw3:x64-windows 'libsndfile[core]:x64-windows'
cmake -S . -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DCONVOLVER_LEGACY_TESTS=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

With a Visual Studio generator, executables are in `Release/` below the build
folder. Plug-in builds on Windows use static libraries with the dynamic MSVC
runtime ([VST3](#vst3)). Use a fresh build folder when changing triplets.

Package-manager versions change over time; `--version` records the ones
linked. Pin dependencies, as the [preview recipe](#pinned-preview-recipe)
does, before producing a reproducible binary.

## Build options

| Option | Default | Purpose |
| --- | --- | --- |
| `CONVOLVER_BUILD_NATIVE` | ON | Enable portable targets; OFF keeps only the historical regression tests |
| `CONVOLVER_BUILD_IO` | ON | libsndfile and configuration adapter |
| `CONVOLVER_BUILD_CLI` | Same as I/O on first configure | Command-line renderer; requires I/O |
| `CONVOLVER_FFT_BACKEND` | AUTO | See [FFT backend](#fft-backend) |
| `CONVOLVER_BUILD_VST3` | OFF | VST3 adapter and, with I/O, the older `convolver-vst3-preset` tool; see [VST3](#vst3) |
| `CONVOLVER_VST3_SDK` | empty | VST3 SDK source folder, with its submodules |
| `CONVOLVER_VST3_RUNTIME_VALIDATION` | ON | Run the SDK tools that load the bundle; turn off where local policy rejects ad-hoc-signed code |
| `CONVOLVER_PLUGINVAL`, `CONVOLVER_PLUGINVAL_ARGS` | empty | Optional pluginval executable and extra options for the host-compatibility tests |
| `CONVOLVER_BUILD_AU` | OFF | Audio Unit adapter (macOS); see [Audio Unit](#audio-unit) |
| `CONVOLVER_AU_SDK` | empty | AudioUnitSDK 1.4.0 source folder |
| `CONVOLVER_BUILD_LV2` | OFF | LV2 adapter; see [LV2](#lv2) |
| `CONVOLVER_PACKAGE_PLUGINS` | OFF | Stage the VST3 bundle, tools and examples for distribution |
| `CONVOLVER_DISTRIBUTION_LICENSE_DIR` | empty | Dependency notices from `build_dependencies.py`, for packaging |
| `CONVOLVER_BUILD_FUZZERS` | OFF | Clang libFuzzer targets; requires I/O |
| `CONVOLVER_BUILD_BENCHMARKS` | OFF | Build `convolver_benchmark` |
| `CONVOLVER_AVX2_KERNELS` | ON when targeting x86-64 | Also compile the multiply-accumulate kernels for AVX2 with FMA and use them on CPUs that have both; output then differs from the baseline kernels by float rounding only, below -120 dBFS ([measurements](fft-backends.md#compiler-flags-and-avx2-kernels)) |
| `BUILD_TESTING` | ON at top level | Master test switch |
| `CONVOLVER_BUILD_TESTS` | ON at top level; OFF as a subproject | Convolver test targets, subject to `BUILD_TESTING` |
| `CONVOLVER_PACKAGE_TESTS` | ON | Installed, relocated and source-tree C++ consumer tests |
| `CONVOLVER_DOCUMENTATION_TESTS` | Same as I/O on first configure | Historical documentation checks; requires Python |
| `CONVOLVER_SANITIZERS` | OFF | ASan and UBSan for native targets with GCC or Clang |
| `CONVOLVER_LEGACY_TESTS` | ON with I/O and native Clang on macOS/Linux; OFF elsewhere | Historical extraction harnesses |
| `CONVOLVER_EQUALIZER_APO_BENCHMARK`, `CONVOLVER_EQUALIZER_APO_CONFIG` | empty | Optional Equalizer APO `Benchmark.exe` and its config folder, for `export.equalizer_apo.runtime` on Windows |

Cached options stay set in an existing build folder. When turning a full build
folder into a Python-free core build, turn the legacy and documentation tests
off too, or use the `core-only` preset.

## Targets and installing

| Target | Builds |
| --- | --- |
| `convolver_core`, `convolver_host`, `convolver_io` | The static libraries |
| `convolver_cli` | The `convolver` executable |
| `convolver_preset_tool`, `convolver_equalizer_apo` | The `convolver-preset` and `convolver-equalizer-apo` tools; built with the CLI |
| `check` | The native tests, then runs CTest |

`cmake --install build --prefix /desired/prefix` installs the three tools,
the libraries, headers and CMake package. The libraries are static and their shared
dependencies must stay installed: this is a development install, not a
self-contained application. Using the libraries from another project is in the
[library guide](library.md).

## Plug-ins

Each format is optional and needs its own SDK, fetched separately. The core
alone never needs one. In every plug-in build:

- the default FFT backend is Ooura, or Accelerate on macOS, so the plug-in links
  no FFTW ([FFT backend](#fft-backend));
- `convolver-preset`, which makes presets for every format, is built with the
  CLI as usual; a VST3 build with I/O also builds the older
  `convolver-vst3-preset`. Both need libsndfile;
- do not use `CONVOLVER_SANITIZERS=ON` for a bundle you will load in a host;
- the result is a developer build: not signed, notarized or packaged. Installing
  it for a host is in [install](../use/install.md#where-each-plug-in-goes).

Every SDK and test tool is pinned by commit and checksum; the table, and how to
move a pin, are in [pinned format dependencies](#pinned-format-dependencies).

### VST3

Needs CMake 3.25 or later and the Steinberg VST3 SDK 3.8.1 build 84 (commit
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`, which the CI workflow pins):

```sh
git clone --branch v3.8.1_build_84 --depth 1 https://github.com/steinbergmedia/vst3sdk.git /path/to/vst3sdk
git -C /path/to/vst3sdk submodule update --init --depth 1 base cmake pluginterfaces public.sdk
cmake -S . -B build-vst3 -DCMAKE_BUILD_TYPE=Release \
  -DCONVOLVER_BUILD_VST3=ON -DCONVOLVER_VST3_SDK=/path/to/vst3sdk
cmake --build build-vst3 --config Release --parallel 2
ctest --test-dir build-vst3 -C Release --output-on-failure
```

The bundle is `build-vst3/VST3/Release/Convolver.vst3`. Nothing is copied into
plug-in folders. Shared libsndfile (and FFTW, if you chose it) must be present
at run time; libraries built for a newer macOS do not run on an older one just
because the bundle names an older deployment target.

On Windows, use static libraries with the dynamic MSVC runtime, so the plug-in
loads regardless of the host's DLL search path:

```powershell
vcpkg install 'libsndfile[core]:x64-windows-static-md'
cmake -S . -B build-vst3 -A x64 -DCONVOLVER_BUILD_VST3=ON `
  -DCONVOLVER_VST3_SDK=C:/path/to/vst3sdk `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
```

An FFTW override on Windows needs static FFTW too
(`fftw3:x64-windows-static-md`). A core-only plug-in build can turn I/O and the
CLI off; the preset tools are then omitted. The `release-vst3` preset is the
packaged variant; its recipe is the [pinned preview recipe](#pinned-preview-recipe).

macOS may refuse to load ad-hoc-signed developer bundles. Use a developer
signing or approval workflow, or set `-DCONVOLVER_VST3_RUNTIME_VALIDATION=OFF`
to skip the tests that load the bundle (this cannot qualify a release). Do not
disable system security checks.

### Audio Unit

Needs macOS 11 or later, CMake 3.20+, Xcode's command-line tools (C++23, for
AudioUnitSDK 1.4.0) and the pinned AudioUnitSDK. Fetch and unpack it, then
configure with it:

```sh
python3 scripts/release/sources.py --fetch --cache build/downloads \
  --destination build/sources audiounitsdk
cmake -S . -B build-au -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_BUILD_AU=ON \
  -DCONVOLVER_AU_SDK="$PWD/build/sources/AudioUnitSDK"
cmake --build build-au --parallel
ctest --test-dir build-au --output-on-failure
```

The component is `build-au/plugins/au/Convolver.component`. It links no FFT
library (Accelerate is part of macOS); libsndfile is used only by the tools and
tests. The AU target builds as C++23 because AudioUnitSDK 1.4.0 needs it; the
core and other formats stay C++20.

### LV2

Needs `pkg-config` and the `lv2` headers 1.18 or later; the tests also need
`lilv` 0.24 or later. On Debian or Ubuntu:

```sh
sudo apt-get install pkg-config lv2-dev liblilv-dev lilv-utils
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_BUILD_LV2=ON
cmake --build build --parallel
ctest --test-dir build -L lv2 --output-on-failure
```

CMake requires `lilv` while `CONVOLVER_BUILD_TESTS` is on (the default at top
level); configure with `-DCONVOLVER_BUILD_TESTS=OFF` to build the plug-in
without it. The bundle is `build/lv2/convolver.lv2/`: `convolver.so`,
`manifest.ttl` and `convolver.ttl`. `plugins/lv2/generate_ttl.py` writes both
Turtle files from `plugins/identities.json`, so the URIs and layouts have one
source ([identities](plugin-identities.md)). The library exports only
`lv2_descriptor`, so it cannot clash with a VST3 build or another version in the
same host.

## Pinned format dependencies

Every plug-in format's SDK is pinned by commit and archive SHA-256 in
`packaging/release-policy.json`, and every test tool by version and checksum in
CI (PLUG-6), so a build or a validator result can be repeated. The workflows
are `.github/workflows/vst3.yml`, `lv2.yml` and `au.yml`:

| Dependency | Pin | Used by | Where pinned |
| --- | --- | --- | --- |
| VST3 SDK (`vst3sdk` and four submodules) | 3.8.1 build 84, commit `3cdf9ca` | VST3 builds | Release policy; the VST3 workflow checks out the same commit |
| `lv2` headers | 1.18.10, commit `0bcde33` | LV2 builds | Release policy |
| AudioUnitSDK | 1.4.0, commit `bd98b31` | AU builds | Release policy |
| `lilv` (test host) and its libraries | Ubuntu 24.04 packages by exact version (`0.24.22-1build1`; serd, sord, sratom, zix too) | LV2 tests | LV2 workflow |
| `lv2lint` | 0.16.2, archive SHA-256 | LV2 validation | LV2 workflow |
| pluginval | v2.0.0-beta3, archive SHA-256 per platform | VST3 validation, later LV2 and AU | VST3 workflow |
| `auval` | ships with macOS; the image label is `macos-15`, and each run logs the macOS and Xcode versions | AU validation | Audio Unit workflow |

AudioUnitSDK 1.4.0 needs C++23 (for `std::expected`), so the AU target builds
as C++23 while the core and the other formats stay C++20. jrp chose 1.4.0 over
the C++20 1.3.0. Its licence is Apache-2.0, which is why the
AU component is distributed under GPL-3.0-or-later
([AU licence](../project/plans/2026-10-21-plugin-formats.md#au-licence)).

Each SDK entry in the policy lists the plug-in `formats` that use it.
`build_dependencies.py --formats vst3 lv2 au` unpacks those formats' SDKs and
ships their notices; the default is `vst3`, the only format in preview
packages. An artifact manifest lists only the dependencies its build used,
while the corresponding source bundle carries every pinned archive (about
0.4 MB more for lv2 and AudioUnitSDK), so one bundle serves every format. To
fetch, verify and unpack chosen pins on their own:

```sh
python3 scripts/release/sources.py --fetch --cache build/downloads \
  --destination build/sources lv2 audiounitsdk
```

To move a pin, change the policy entry (or the workflow) in its own pull
request and say why. A GitHub archive's checksum can be computed without
downloading it, from a clone at that commit:
`git archive --format=tar --prefix=REPO-COMMIT/ COMMIT | gzip -n | sha256sum`
(for example `--prefix=lv2-0bcde338…/`); the `sources` job of the candidate
workflow downloads every pinned archive and fails on a mismatch.

The Dependency updates workflow (`.github/workflows/updates.yml`) looks for
newer releases every Monday. `scripts/release/check_updates.py` reads each pin
above except `auval`, which comes with macOS (plus FFTW and libsndfile from the
release policy), lists the upstream tags and reports which pins have a newer
release. The VST3 SDK is checked by the `vst3sdk` tag, whose commit fixes its
four submodules. The check also flags a policy pin whose tag no longer points
at the pinned commit, and a VST3 workflow checkout that differs from the
policy. On `main`, one open issue titled "Newer versions of pinned
dependencies" carries the report; it gets a comment when the findings change
and is closed when every pin is current. If the repository has issues turned
off, the run fails instead when something needs attention. An upstream host
that does not answer after three tries is a warning in the run, not a reason
to open the issue. `lilv` is listed for information only, because its version
follows the `ubuntu-24.04` image. The check moves nothing: each update is its
own pull request. Run it locally with
`python3 scripts/release/check_updates.py`. GitHub Actions versions are
updated by Dependabot instead.

## Pinned preview recipe

Preview packages are built this way. It needs CMake 3.25 or later and Ninja (or
a Visual Studio generator). The recipe builds static libsndfile (built-in
formats only; optional FLAC, Ogg and MP3 codecs are disabled) and uses Ooura
for Linux and Windows plug-ins and Accelerate for macOS. It builds static FFTW
only when `--backend FFTW` is given, and ships FFTW's notice only then. The
commands work in a Unix shell or a Visual Studio developer terminal with Python
and CMake on `PATH` (use one line per command in PowerShell):

```sh
python3 scripts/release/build_dependencies.py --cache build/downloads --work build/deps --fetch
cmake --preset release-vst3 -DCONVOLVER_VST3_SDK="$PWD/build/deps/sources/vst3sdk" \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps/prefix" \
  -DCONVOLVER_DISTRIBUTION_LICENSE_DIR="$PWD/build/deps/licenses"
cmake --build --preset release-vst3
ctest --preset release-vst3
cmake --install build/presets/release-vst3 --config Release --prefix stage --component PluginDistribution
```

On macOS add `-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0`. Use `$PWD` in PowerShell or
absolute paths in cmd. `--fetch` explicitly permits network acquisition;
omitting it requires the already supplied, hash-checked sources. No optional CPU
instruction set is enabled in an FFTW build. Windows uses static libraries and
the dynamic MSVC runtime consistently. The staged package ships `docs/use/` as
its guide (`scripts/release/package_docs.py` points links to other pages at
the source repository at the packaged commit).

`build_sources.py --cache build/downloads --output build/Convolver-source.tar.gz`
assembles **committed** source and the pinned dependency archives. Then
`python3 tests/release/source_build.py --archive build/Convolver-source.tar.gz`
rebuilds dependencies, core, tools and SDK from a fresh extraction without
fetching, and runs their tests (the CMake package consumers, which nothing
shipped uses, run in native CI instead). Its `--backend` defaults to `AUTO`, as above;
`--sdk`, `--prefix` and `--backend` also check a fresh build against an SDK and
dependencies you supply. A failed validator remains a release blocker.

On a Mac that rejects ad-hoc bundles, set `-DCONVOLVER_VST3_RUNTIME_VALIDATION=OFF`
(or `--exclude-validator` for the source smoke). This disables **both** the SDK
validator and the moduleinfo tool's bundle loading. It leaves signature-integrity
and standalone adapter tests enabled, but cannot produce release qualification.
Use CI for actual bundle loading. Do not repeatedly retry a rejected local
bundle, and do not disable system security checks.

## When a build fails

| Message or symptom | Cause and fix |
| --- | --- |
| "Clang with ASan/UBSan runtimes is required for legacy tests" | Install Clang's sanitizer runtimes (`libclang-rt-18-dev`), or add `-DCONVOLVER_LEGACY_TESTS=OFF` |
| CMake cannot find FFTW although it is installed | Install FFTW's single-precision (`fftw3f`) library, or choose another [backend](#fft-backend) |
| CMake cannot find a dependency in a custom location | Add `-DCMAKE_PREFIX_PATH=/path/to/prefix` |
| An option seems to be ignored | It is cached from an earlier configure; set it explicitly or use a fresh build folder |
| A plug-in loads in the tests but not in a host | Sanitizers are on, a runtime library is missing, or macOS rejected the signature; see [VST3](#vst3) |
| Enabling the CLI fails with I/O off | The CLI needs `CONVOLVER_BUILD_IO=ON` |
