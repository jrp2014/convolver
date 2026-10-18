# Build the VST3 plug-in

These are developer builds of portable Convolver 0.2.0. Supported operating-system
floors and DAW combinations have not yet been qualified for production.

Use CMake 3.25+, a C++20 compiler and Ninja (or a Visual Studio generator).
The core alone still uses CMake 3.20. Release pins and proposed platforms are in
[the release policy](../../packaging/release-policy.json). Obtain dependencies
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
macOS or provide FFTW. See [the CMake package guide](../cmake-package.md) for
`Convolver::core`, `Convolver::host` and `Convolver::io` consumers.
