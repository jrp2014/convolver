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

On macOS add `-DCONVOLVER_FFT_BACKEND=ACCELERATE`; the converter requires libsndfile.
On Linux provide FFTW single precision and libsndfile development libraries.
These development libraries do not establish a portable binary release.

On Windows run in a Visual Studio developer terminal. For the optional vcpkg
recipe, check out the baseline recorded in the policy before installing:

```powershell
vcpkg install fftw3:x64-windows-static-md 'libsndfile[core]:x64-windows-static-md'
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

This needs neither Python, libsndfile nor the VST3 SDK. Choose Accelerate on
macOS or provide FFTW. See `docs/cmake-package.md` in that source bundle for
`Convolver::core`, `Convolver::host` and `Convolver::io` consumers.

## Pinned preview recipe

The release recipe builds static FFTW and libsndfile (built-in formats only;
optional FLAC/Ogg/MP3 codecs are disabled), and uses Accelerate for macOS plug-ins.
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

On macOS add `-DCONVOLVER_FFT_BACKEND=ACCELERATE -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0`.
Use `$PWD` in PowerShell or absolute paths in cmd. `--fetch` explicitly permits
network acquisition; omitting it requires the already supplied, hash-checked sources.
No optional CPU instruction set is enabled in release FFTW. Windows uses static
libraries and the dynamic MSVC runtime consistently.

`build_sources.py --cache build/downloads --output build/Convolver-source.tar.gz`
assembles **committed** source and the pinned dependency archives. Then
`python tests/release/source_build.py --archive build/Convolver-source.tar.gz`
rebuilds dependencies, core, converter, SDK and external consumers from a fresh
extraction without fetching. Add `--backend ACCELERATE` on macOS.

On a Mac that rejects ad-hoc bundles, set `-DCONVOLVER_VST3_RUNTIME_VALIDATION=OFF`
(or `--exclude-validator` for the source smoke). This disables **both** the SDK
validator and moduleinfo tool's bundle loading. It leaves signature-integrity and
standalone adapter tests enabled, but cannot produce release qualification.
Use CI for actual bundle loading. Do not repeatedly retry a rejected local bundle.
