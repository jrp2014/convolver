# VST3 adapter and host lifecycle

The optional Convolver VST3 adapter uses **VST SDK 3.8.1 build 84**, the latest
published release checked on 18 October 2026. Builds pin SDK commit
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96` and its submodules. Ordinary core/CLI
builds do not need the SDK. Compatibility and numerical correctness take priority
over CPU-specific optimization.

## Build

The optional SDK build needs CMake 3.25+, a C++20 compiler and the usual FFT
backend. The core alone retains its CMake 3.20 minimum. Obtain the SDK separately:

```sh
git clone --branch v3.8.1_build_84 --depth 1 https://github.com/steinbergmedia/vst3sdk.git /path/to/vst3sdk
git -C /path/to/vst3sdk submodule update --init --depth 1 base cmake pluginterfaces public.sdk
cmake -S . -B build-vst3 -DCMAKE_BUILD_TYPE=Release \
  -DCONVOLVER_BUILD_VST3=ON -DCONVOLVER_VST3_SDK=/path/to/vst3sdk
cmake --build build-vst3 --config Release --parallel 2
ctest --test-dir build-vst3 -C Release --output-on-failure
```

On Windows, use static libraries with the dynamic MSVC runtime so the plug-in
can load independently of the DAW's DLL search path:

```powershell
vcpkg install fftw3:x64-windows-static-md 'libsndfile[core]:x64-windows-static-md'
cmake -S . -B build-vst3 -A x64 -DCONVOLVER_BUILD_VST3=ON `
  -DCONVOLVER_VST3_SDK=C:/path/to/vst3sdk `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
```

Use a fresh build directory when changing triplets. On Apple,
`-DCONVOLVER_FFT_BACKEND=ACCELERATE` removes the plug-in's FFTW dependency.
The preset-writing tool still needs libsndfile. Core-only plug-in builds can
turn I/O and CLI off; the preset writer is then omitted.

The SDK creates `build-vst3/VST3/Release/Convolver.vst3`. No link is created in
user/system plug-in folders. This is a developer build, not a signed/notarized
installer or self-contained distribution. Shared FFTW dependencies must be
available at runtime; compatibility with older OS versions also depends on how
those libraries were built. Local libraries built for macOS 27 do not establish
macOS 11 compatibility just because a bundle has an older deployment target.
macOS may block ad-hoc-signed developer bundles; use an appropriate developer
signing/approval workflow. Do not disable system security checks.

## Loading filters

The first version uses the DAW's standard preset loader and generic bypass
control, without a custom editor. Convert an impulse WAV, legacy `.cfg`, or
configuration list into a new preset:

```sh
./build-vst3/plugins/vst3/convolver-vst3-preset room.cfg room.vstpreset
```

With Visual Studio the executable is in `plugins/vst3/Release/`. The output must
not already exist. Presets embed coefficients, gains, routes and delays; the DAW
project's saved state therefore does not depend on the original filter paths.
Select a configuration matching the DAW's sample rate and mono/stereo layout.
A configuration list uses the importer's ordinary first-match behavior when no
rate/layout was specified; prepare a preset from the intended individual config
when the list contains multiple alternatives.

Supported buses are one mono input/output pair or one stereo input/output pair,
including cross-channel filter routes. Other arrangements are rejected. 32-bit
and 64-bit host buffers are supported, with **single-precision internal DSP**.
Sample-rate mismatches are rejected; there is no implicit IR resampling or
pitch/timing change. This plug-in has a new VST3 identity: old VST2 project state
is not claimed to migrate automatically.

## Processing and lifecycle

Latency is always 256 samples, independent of the DAW callback length. The adapter
handles variable blocks, in-place host buffers, zero-length/parameter-only calls,
inactive channels and silence flags. Silent input still drains the wet tail.
Bypass uses a 64-sample ramp and a dry delay matching the reported latency. Wet
processing continues during bypass so returning to wet does not lose history.

Filters are prepared outside processing and transferred through a bounded
single-producer/single-consumer pointer handoff. A replacement runs for 256 samples
to fill its latency, then crossfades for 256 samples. It starts with fresh input
history: it cannot reconstruct audio that preceded loading. Old engines are
retired on the control thread. Pending changes coalesce to the newest; at most
one transition and one pending replacement are retained, with up to two old
engines awaiting control-thread collection after the last publication. Collection
does not hold up pending changes. The tail is available before activation and
includes routed delays; it can grow conservatively during a session, and
reactivation recomputes it.

`Convolver::host` exports `HostProcessor` and the state codec without SDK or file
I/O dependencies. Prepare/replace/collect/destruct calls must be serialized on the
control side. One audio thread calls process/reset/set_bypass. Destruction and
reconfiguration require audio to have stopped. Reset clears history without
allocation, but its work scales with prepared storage. Filter preparation can be
expensive; neither measured FFT planning nor file access belongs in a callback.

Finite input outside float range and nonfinite input become silence. Nonfinite
output is contained and history reset. The processing boundary is nonthrowing;
invalid dimensions return failure without advancing audio. State uses a bounded,
versioned little-endian format (64 MiB encoded limit), validated transactionally.

## Compatibility evidence

Tests cover direct numerical results, planar/in-place float/double buffers,
callback sizes crossing partition boundaries, silence/tails, bypass automation,
reset, concurrent replacement, C++ allocation/deallocation guards, corrupted
state, and presets after source-file deletion. The SDK validator loads the actual
bundle; separate tests call the real processor/controller SDK interfaces.

The VST3 CI matrix runs on Linux, Windows and macOS. These automated checks are
not proof of hard real-time deadlines or compatibility with every DAW. Commercial
DAW testing, installers, dependency bundling, signing/notarization and a custom
file-picker editor remain separate release work. PCM dither remains deferred;
VST3 exchanges floating-point audio.
