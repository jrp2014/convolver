<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Build the VST3 plug-in

These are developer builds of portable Convolver. Supported operating-system
floors and DAW combinations have not yet been qualified for production.

Use CMake 3.25+, a C++20 compiler and Ninja (or a Visual Studio generator).
The core alone still uses CMake 3.20. Release pins and proposed platforms are in
`packaging/release-policy.json` in the matching source bundle. Obtain dependencies
explicitly before configuring; normal core builds never download them.

## Existing development dependencies

Obtain the SDK at commit `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`, including
its pinned `base`, `cmake`, `pluginterfaces` and `public.sdk` submodules:

```sh
git clone https://github.com/steinbergmedia/vst3sdk.git build-sdk
git -C build-sdk checkout 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96
git -C build-sdk submodule update --init base cmake pluginterfaces public.sdk
cmake --preset release-vst3 -DCONVOLVER_VST3_SDK="$PWD/build-sdk"
cmake --build --preset release-vst3
ctest --preset release-vst3
```

Plug-in builds need no FFT library: the default backend is the built-in Ooura FFT
on Linux and Windows and Accelerate on macOS, because a plug-in must not share an
FFTW planner with its host. Override it with `-DCONVOLVER_FFT_BACKEND=FFTW` (or
`OOURA`, `ACCELERATE`) if you need to; CMake warns for FFTW. The converter
requires libsndfile, so on Linux provide its development library.
These development libraries do not establish a portable binary release.

On Windows run in a Visual Studio developer terminal. For the optional vcpkg
recipe, check out the baseline recorded in the policy before installing:

```powershell
vcpkg install 'libsndfile[core]:x64-windows-static-md'
cmake --preset release-vst3 -DCONVOLVER_VST3_SDK=C:/src/vst3sdk `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build --preset release-vst3
ctest --preset release-vst3
```

A fresh source build can be checked using `tests/release/source_build.py` with
`--sdk`, `--prefix` and `--backend`. It uses a new CMake cache and runs core,
consumer, adapter, preset and actual SDK bundle validation. macOS signature
policy may reject an ad-hoc bundle; a failed validator remains a release blocker.
Do not disable system security checks.

## Core only

```sh
cmake -S . -B build-core -DBUILD_TESTING=OFF -DCONVOLVER_BUILD_IO=OFF \
  -DCONVOLVER_BUILD_CLI=OFF -DCONVOLVER_BUILD_VST3=OFF
cmake --build build-core
```

This needs neither Python, libsndfile nor the VST3 SDK. It uses FFTW by default;
choose Accelerate on macOS or Ooura anywhere to avoid it. See `docs/cmake-package.md` in that source bundle for
`Convolver::core`, `Convolver::host` and `Convolver::io` consumers.

## Pinned format dependencies

Every plug-in format's SDK is pinned by commit and archive SHA-256 in
`packaging/release-policy.json`, and every test tool by version and checksum in
CI (PLUG-6), so a build or a validator result can be repeated exactly:

| Dependency | Pin | Used by | Where pinned |
| --- | --- | --- | --- |
| VST3 SDK (`vst3sdk` and four submodules) | 3.8.1 build 84, commit `3cdf9ca` | VST3 builds | Release policy; the [VST3 workflow](../../.github/workflows/vst3.yml) checks out the same commit |
| `lv2` headers | 1.18.10, commit `0bcde33` | LV2 builds (LV2-1) | Release policy |
| AudioUnitSDK | 1.4.0, commit `bd98b31` | AU builds (AU-1) | Release policy |
| `lilv` (test host) | Ubuntu 24.04 `0.24.22-1build1` | LV2 tests | [LV2 workflow](../../.github/workflows/lv2.yml) |
| `lv2lint` | 0.16.2, archive SHA-256 | LV2 validation (LV2-3) | LV2 workflow |
| pluginval | v2.0.0-beta3, archive SHA-256 per platform | VST3 validation, later LV2 and AU | VST3 workflow |
| `auval` | ships with macOS; the image is fixed at `macos-15` | AU validation (AU-3) | [Audio Unit workflow](../../.github/workflows/au.yml) |

AudioUnitSDK 1.4.0 needs C++23 (for `std::expected`), so the AU target builds as
C++23; the core and the other formats stay C++20. Its licence is Apache-2.0,
which is why the AU component is distributed under GPL-3.0-or-later (see the
[plug-in formats plan](../plans/2026-10-21-plugin-formats.md#au-licence)).

Each SDK entry in the policy lists the plug-in `formats` that use it.
`build_dependencies.py --formats vst3 lv2 au` unpacks those formats' SDKs and
ships their notices; the default is `vst3`, today's only adapter. To fetch,
verify and unpack chosen pins on their own:

```sh
python scripts/release/sources.py --fetch --cache build/downloads \
  --destination build/sources lv2 audiounitsdk
```

The LV2 workflow builds LV2's example plug-ins against the pinned headers, runs
one through `lilv` on the shared mono fixture and checks it with `lv2lint`; the
Audio Unit workflow compiles the pinned AudioUnitSDK as the AU target will and
runs `auval` on Apple's AUDelay. The adapters add their own bundles to these
workflows.

To move a pin, change the policy entry (or the workflow) in its own pull
request and say why. A GitHub archive's checksum can be computed without
downloading it, from a clone at that commit:
`git archive --format=tar --prefix=REPO-COMMIT/ COMMIT | gzip -n | sha256sum`
(for example `--prefix=lv2-0bcde338…/`); the `sources` job of the candidate
workflow downloads every pinned archive and fails on a mismatch.

## Pinned preview recipe

The release recipe builds static libsndfile (built-in formats only; optional
FLAC/Ogg/MP3 codecs are disabled) and uses Ooura for Linux and Windows plug-ins
and Accelerate for macOS. It builds static FFTW only when `--backend FFTW` is
given, and ships FFTW's notice only then.
The following commands work in a Unix shell or a Visual Studio developer terminal
with Python and CMake on `PATH` (use one line per command in PowerShell):

```sh
python scripts/release/build_dependencies.py --cache build/downloads --work build/deps --fetch
cmake --preset release-vst3 -DCONVOLVER_VST3_SDK="$PWD/build/deps/sources/vst3sdk" \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps/prefix" \
  -DCONVOLVER_DISTRIBUTION_LICENSE_DIR="$PWD/build/deps/licenses"
cmake --build --preset release-vst3
ctest --preset release-vst3
cmake --install build/presets/release-vst3 --config Release --prefix stage --component PluginDistribution
```

On macOS add `-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0`.
Use `$PWD` in PowerShell or absolute paths in cmd. `--fetch` explicitly permits
network acquisition; omitting it requires the already supplied, hash-checked sources.
No optional CPU instruction set is enabled in an FFTW build. Windows uses static
libraries and the dynamic MSVC runtime consistently.

`build_sources.py --cache build/downloads --output build/Convolver-source.tar.gz`
assembles **committed** source and the pinned dependency archives. Then
`python tests/release/source_build.py --archive build/Convolver-source.tar.gz`
rebuilds dependencies, core, converter, SDK and external consumers from a fresh
extraction without fetching. Its `--backend` defaults to `AUTO`, as above.

On a Mac that rejects ad-hoc bundles, set `-DCONVOLVER_VST3_RUNTIME_VALIDATION=OFF`
(or `--exclude-validator` for the source smoke). This disables **both** the SDK
validator and moduleinfo tool's bundle loading. It leaves signature-integrity and
standalone adapter tests enabled, but cannot produce release qualification.
Use CI for actual bundle loading. Do not repeatedly retry a rejected local bundle.
