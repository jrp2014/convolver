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
[supporting libraries](#supporting-libraries) ·
[preview recipe](#preview-recipe) ·
[when a build fails](#when-a-build-fails)

## Choose a build

| You want | Use |
| --- | --- |
| The `convolver` command-line tool | [Everyday build](#everyday-build) |
| To change the code (people and agents) | [Sanitizer build](#sanitizer-build-for-changes), then [testing](testing.md) |
| Only the C++ libraries, for your own program | [Library guide](library.md#core-only-build) |
| No FFT library at all | Add `-DCONVOLVER_FFT_BACKEND=OOURA` ([FFT backend](#fft-backend)) |
| A VST3, Audio Unit or LV2 plug-in | [Plug-ins](#plug-ins) |
| The exact recipe used for preview packages | [Preview recipe](#preview-recipe) |

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
linked. The [preview recipe](#preview-recipe) builds its libraries from
source and records exactly which releases it used.

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
| `CONVOLVER_AU_SDK` | empty | AudioUnitSDK source folder |
| `CONVOLVER_BUILD_LV2` | OFF | LV2 adapter; see [LV2](#lv2) |
| `CONVOLVER_PACKAGE_PLUGINS` | OFF | Stage the VST3 bundle, tools and examples for distribution |
| `CONVOLVER_DISTRIBUTION_LICENSE_DIR` | empty | Dependency notices from `build_dependencies.py`, for packaging |
| `CONVOLVER_DEBUG_SYMBOLS` | OFF | Compile Release code with debug information and keep the packaged binaries' symbols in `symbols/` in the build folder, outside everything installed; release candidates set it ([debugging symbols](releasing.md#debugging-symbols)) |
| `CONVOLVER_BUILD_FUZZERS` | OFF | Clang libFuzzer targets; requires I/O |
| `CONVOLVER_BUILD_BENCHMARKS` | OFF | Build `convolver_benchmark`, and `convolver_callback_benchmark` when the I/O adapter is built |
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

Builds use the newest release of each SDK and test tool and record which
([supporting libraries](#supporting-libraries)).

### VST3

Needs CMake 3.25 or later and the newest Steinberg VST3 SDK release with four
of its submodules, which `sources.py` fetches, checks and unpacks (the
summary it prints names the release):

```sh
python3 scripts/release/sources.py --latest --fetch --cache build/downloads \
  --destination build/sources vst3sdk vst3-base vst3-cmake vst3-pluginterfaces vst3-public-sdk
cmake -S . -B build-vst3 -DCMAKE_BUILD_TYPE=Release \
  -DCONVOLVER_BUILD_VST3=ON -DCONVOLVER_VST3_SDK="$PWD/build/sources/vst3sdk"
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
packaged variant; its recipe is the [preview recipe](#preview-recipe). A Git
checkout of the SDK works too: clone the newest `v3.*` tag of
`https://github.com/steinbergmedia/vst3sdk` and run
`git submodule update --init --depth 1 base cmake pluginterfaces public.sdk`.

macOS may refuse to load ad-hoc-signed developer bundles. Use a developer
signing or approval workflow, or set `-DCONVOLVER_VST3_RUNTIME_VALIDATION=OFF`
to skip the tests that load the bundle (this cannot qualify a release). Do not
disable system security checks.

### Audio Unit

Needs macOS 11 or later, CMake 3.20+, Xcode's command-line tools (C++23, which
AudioUnitSDK has needed since 1.4.0) and the newest AudioUnitSDK release. Fetch
and unpack it, then configure with it:

```sh
python3 scripts/release/sources.py --latest --fetch --cache build/downloads \
  --destination build/sources audiounitsdk
cmake -S . -B build-au -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_BUILD_AU=ON \
  -DCONVOLVER_AU_SDK="$PWD/build/sources/AudioUnitSDK"
cmake --build build-au --parallel
ctest --test-dir build-au --output-on-failure
```

The component is `build-au/plugins/au/Convolver.component`. It links no FFT
library (Accelerate is part of macOS); libsndfile is used only by the tools and
tests. The AU target builds as C++23 because AudioUnitSDK needs it; the core
and other formats stay C++20.

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

## Supporting libraries

Builds use the **newest release** of every SDK, library and tool they depend
on, not a pinned one, so security and bug fixes arrive without anyone moving a
pin. In exchange, every build records exactly what it used, and a recorded
build can be repeated exactly. The release policy
([`packaging/release-policy.json`](../../packaging/release-policy.json)) says
only where each dependency comes from: its Git repository, how its tags name
releases, and where its archive is downloaded.

| Dependency | Newest release taken from | Used by | Recorded in |
| --- | --- | --- | --- |
| VST3 SDK (`vst3sdk` and its `base`, `cmake`, `pluginterfaces` and `public.sdk` submodules) | `vst3sdk` tags `v3.8.1_build_84` and so on; the submodule commits are the ones that tag records | VST3 builds, preview packages | Job summary, source and artifact manifests, Convolver log |
| `lv2` headers | `lv2` tags | LV2 builds | Job summary, source manifest |
| AudioUnitSDK | `AudioUnitSDK-` tags | AU builds | Job summary, source manifest, Convolver log |
| FFTW | `fftw3` tags; the archive from fftw.org | Source builds with the FFTW backend (none of the preview packages) | Job summary, source manifest |
| libsndfile | `libsndfile` tags; the release archive, checked against the digest GitHub publishes | Preview packages and their candidates | Job summary, source and artifact manifests, `convolver --version` |
| NSIS | `kichik/nsis` tags (`v313` is 3.13); the archive from SourceForge | The Windows installer | Job summary, candidate record |
| pluginval | `Tracktion/pluginval` tags (1.0.4 in October 2026); the newest 2.0 beta in an advisory run | VST3 validation, later LV2 and AU | Job summary |
| `lv2lint` | Its own Git host; when that does not answer, the newest version FreeBSD's ports list | LV2 validation | Job summary |
| `lilv` (test host) and its libraries | The runner image's Ubuntu 24.04 packages, with updates; the newest upstream releases in an advisory job | LV2 tests | Job summary |
| `auval` | Ships with macOS on the `macos-15` image | AU validation | Each run logs the macOS and Xcode versions |

The other CI jobs (native, VST3, LV2 and regression) take FFTW and libsndfile
from the runner's package manager (apt, Homebrew or vcpkg) at its newest
version, as a source build would. Their versions appear only in the job logs
and in `convolver --version`, not in a manifest.

For each dependency a build records the version, the tag, the commit the tag
points at and the SHA-256 of the archive it downloaded:

- every CI job lists them in its summary;
- the corresponding source bundle's `source-manifest.json` holds the full
  records, and the bundle carries the archives themselves;
- each preview package's `artifact-manifest.json` and
  `licenses/DEPENDENCIES.json` list the ones it was built with;
- the Convolver log's session line names the plug-in SDK, for example
  "built with VST3 SDK 3.8.1_build_84, commit 3cdf9ca5d1f5"
  ([status and the Convolver log](../use/plugins.md#status-and-the-convolver-log)).
  CMake reads it from the `.convolver-source.json` that `sources.py` leaves in
  each SDK it unpacks, or from `git describe` in an SDK checkout.

`scripts/release/resolve.py` finds the newest releases (with `git ls-remote`,
so no API token is needed) and `scripts/release/sources.py` fetches, checks and
unpacks them. To get the newest SDKs yourself:

```sh
python3 scripts/release/sources.py --latest --fetch --cache build/downloads \
  --destination build/sources lv2 audiounitsdk
```

`--version audiounitsdk=1.3.0` takes another release instead of the newest,
and `--record lock.json` writes the records to a file. To repeat a recorded
build exactly, pass the records back with `--lock`: a lock file, or the
`source-manifest.json` of a source bundle. The archives are then fetched by
their recorded URL and refused if their checksum differs:

```sh
python3 scripts/release/sources.py --lock source-manifest.json --fetch \
  --cache build/downloads --destination build/sources vst3sdk vst3-base vst3-cmake \
  vst3-pluginterfaces vst3-public-sdk
```

Each SDK entry in the policy lists the plug-in `formats` that use it.
`build_dependencies.py --formats vst3 lv2 au` unpacks those formats' SDKs and
ships their notices; the default is `vst3`, the only format in preview
packages. An artifact manifest lists only the dependencies its build used,
while the corresponding source bundle carries every dependency's archive (about
0.4 MB more for lv2 and AudioUnitSDK), so one bundle serves every format.

The VST3, LV2, Audio Unit and candidate workflows also run every Monday, so a
new upstream release is built and tested within a week even when nothing in
Convolver changes. GitHub Actions versions are updated by Dependabot instead.

### Pre-releases and newer upstream versions

The critical gate is the newest full release on the current platform, never a
beta or release candidate. What a newer pre-release or upstream version
signposts is still looked at, but only as advice: it is reported as a GitHub
Actions warning and in the job summary, and does not fail a run (with the one
exception below).

- Every summary names a newer pre-release of a dependency or tool when there
  is one (for example a `v1.20.0-rc1` beside lv2 1.18.10), without using it.
  Where no advisory run tries it, the job also raises a warning asking for
  its release notes to be read. Lock files and manifests leave the note out,
  since it describes upstream, not the build.
- pluginval: the formal tests use the newest full release. When a newer
  pre-release exists (2.0.0-beta3 in October 2026), the VST3 jobs also run
  `vst3.pluginval` with it, `--rtcheck` included on macOS, and report any
  problems as warnings, including a failed download. The acceptance test is
  the exception: it needs pluginval 2's `test` command, which no full release
  has yet, so it runs with the pre-release and must pass (a skip counts as a
  failure there)
  ([pluginval](testing.md#pluginval)).
- lilv: the formal LV2 tests use Ubuntu 24.04's packages. The **Newest upstream
  lilv (advisory)** job builds the newest versions of lilv, zix, serd, sord
  and sratom (`resolve.py checkout`, from the policy's `test_sources`; a
  pre-release when it is newer than the newest release) with the newest lv2
  headers, runs the same LV2 tests against them and reports any failure,
  including a failed download or build, as a warning.

### When a new release breaks the build

Because builds follow upstream, a new release can turn CI red with no change
in Convolver; a scheduled Monday run is usually where it shows first. Each
failed job ends with a note saying how to tell. Compare the versions in its
summary with the last green run's. To confirm a suspect, build with the
previous release (`--version NAME=OLD_VERSION`, as above), then change
Convolver to work with the new release in a pull request. Nothing pins the old
release in the meantime: a new SDK release that needs code changes keeps CI red
until that pull request is merged. AudioUnitSDK is the likeliest: 1.4.0 needed
C++23 (for `std::expected`), so the AU target builds as C++23 while the core
and the other formats stay C++20. Its licence is Apache-2.0, which is why the
AU component is distributed under GPL-3.0-or-later
([AU licence](../project/plans/2026-10-21-plugin-formats.md#au-licence)).

If an upstream host does not answer, the job fails and names the host; run it
again later. `lv2lint`'s small host has a fallback (FreeBSD's ports list and
distfile caches), and the summary says when it was used. When an upstream
project moves or renames its repository or tags, update its entry in the
release policy.

## Preview recipe

Preview packages are built this way. It needs CMake 3.25 or later and Ninja (or
a Visual Studio generator). The recipe builds static libsndfile (built-in
formats only; optional FLAC, Ogg and MP3 codecs are disabled) and uses Ooura
for Linux and Windows plug-ins and Accelerate for macOS. It builds static FFTW
only when `--backend FFTW` is given, and ships FFTW's notice only then. The
commands work in a Unix shell or a Visual Studio developer terminal with Python
and CMake on `PATH` (use one line per command in PowerShell):

```sh
python3 scripts/release/build_dependencies.py --latest --cache build/downloads --work build/deps --fetch
cmake --preset release-vst3 -DCONVOLVER_VST3_SDK="$PWD/build/deps/sources/vst3sdk" \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps/prefix" \
  -DCONVOLVER_DISTRIBUTION_LICENSE_DIR="$PWD/build/deps/licenses"
cmake --build --preset release-vst3
ctest --preset release-vst3
cmake --install build/presets/release-vst3 --config Release --prefix stage --component PluginDistribution
```

On macOS add `-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0`. Use `$PWD` in PowerShell or
absolute paths in cmd. `--latest` takes the newest release of each library
and `--fetch` explicitly permits downloading them; `--lock FILE` instead
builds the releases a lock file or source manifest records, and without
`--fetch` they must already be in the cache, where their checksums are
checked. No optional CPU
instruction set is enabled in an FFTW build. Windows uses static libraries and
the dynamic MSVC runtime consistently. The staged package ships `docs/use/` as
its guide (`scripts/release/package_docs.py` points links to other pages at
the source repository at the packaged commit).

`build_sources.py --latest --fetch --cache build/downloads --output build/Convolver-source.tar.gz`
assembles **committed** source and the archives of each dependency's newest
release, and records them in its `source-manifest.json` (`--lock` assembles
recorded ones again). Then
`python3 tests/release/source_build.py --archive build/Convolver-source.tar.gz`
rebuilds dependencies, core, tools and SDK from a fresh extraction without
fetching, using exactly the releases its manifest records, and runs their tests (the CMake package consumers, which nothing
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
