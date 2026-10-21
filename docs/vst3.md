# VST3 adapter and host lifecycle

[Documentation index](README.md) · [Audio concepts](concepts.md)

**In short:**

- The plug-in is optional. Build it with `-DCONVOLVER_BUILD_VST3=ON` and a
  separately downloaded Steinberg VST3 SDK, then copy the whole
  `Convolver.vst3` bundle into a folder your host scans.
- There is no editor. Turn a WAV, `.cfg` or configuration list into a
  `.vstpreset` with `convolver-vst3-preset`, then load it from the host's preset
  menu. The preset embeds the filter, so the original files are not needed later.
- One input bus and one output bus of 1 to 8 channels each; the counts may
  differ. The preset must match the host's sample rate and channel counts.
- Latency is always 256 samples. Processing is single-precision with 32- or
  64-bit host buffers; integer dither does not apply.
- These are developer builds: not signed, notarized or packaged, and not yet
  tested in named commercial DAWs.

Use this guide to build the plug-in and load a filter through a host's preset
menu. The later lifecycle section is for contributors and host integrators.

Contents: [build](#build), [install for a host](#make-the-plug-in-discoverable),
[loading filters](#loading-filters), [processing and lifecycle](#processing-and-lifecycle)
and [compatibility evidence](#compatibility-evidence).

The optional adapter uses [VST SDK 3.8.1 build 84](https://github.com/steinbergmedia/vst3sdk/releases/tag/v3.8.1_build_84).
The CI workflow pins SDK commit
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

## Make the plug-in discoverable

For a local developer build, close the host and copy the complete
`Convolver.vst3` bundle into a plug-in folder it scans. Do not copy only the
executable inside the bundle. Common per-user locations are:

| Platform | Folder |
| --- | --- |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` |
| Windows | `%LOCALAPPDATA%\Programs\Common\VST3\` |
| Linux | `~/.vst3/` |

See Steinberg's [plug-in locations](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BLocations.html)
for system-wide alternatives. Reopen the host, enable VST3 scanning if necessary,
and use its plug-in rescan command. Add Convolver as an audio effect, then use
the host's preset loader as described below. Exact menu names depend on the host.

If Convolver does not appear, check the host's scan log, matching CPU architecture,
runtime libraries and the signing requirements above. A successful build or SDK
validator run alone does not prove that a relocated bundle loads in a particular
DAW; named-host installation tests remain tracked in [TODO](../TODO.md).

## Loading filters

The adapter uses the DAW's standard preset loader and generic bypass
control, without a custom editor. Convert an impulse WAV, [`.cfg`](configuration.md), or
configuration list into a new preset:

```sh
./build-vst3/plugins/vst3/convolver-vst3-preset room.cfg room.vstpreset
```

With Visual Studio the executable is in `plugins/vst3/Release/`. The output must
not already exist. Presets embed coefficients, gains, routes and delays; the DAW
project's saved state therefore does not depend on the original filter paths.
Select a configuration matching the DAW's sample rate and bus layout. For a
configuration list, `--sample-rate HZ`, `--input-channels N` and
`--output-channels N` (before the file names) choose the first matching entry,
as the CLI does; without them the first entry is used:

```sh
./build-vst3/plugins/vst3/convolver-vst3-preset --sample-rate 48000 \
  --input-channels 2 --output-channels 6 filters.txt upmix.vstpreset
```

The plug-in has one input bus and one output bus. Each may have 1 to 8 channels,
and the counts may differ (for example stereo in, 5.1 out). Any speaker
arrangement with that many channels is accepted; channels are numbered in the host's bus order, which is the order the
configuration's routes refer to. A preset must match the bus channel counts and
the sample rate, or activation fails. Without a preset the plug-in passes input
*c* to output *c* and leaves outputs without a matching input silent; bypass uses
the same mapping. More than 8 channels, or more than one bus, is rejected. 32-bit
and 64-bit host buffers are supported, with **single-precision internal DSP**.
Sample-rate mismatches are rejected; there is no implicit IR resampling or
pitch/timing change. This plug-in has a new VST3 identity: old VST2 project state
is not claimed to migrate automatically.

## Processing and lifecycle

The host owns the audio device, clock and scheduling. Each callback supplies the
next section of one continuous stream; convolution history persists between
callbacks. Processing produces one output frame per input frame, with the delay
reported below. The plug-in performs no file I/O in this streaming path.

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
file-picker editor remain separate release work. VST3 exchanges floating-point
audio, so the CLI/file writer's integer quantization and dither does not apply to it.

Source entry points and the change/test map are in [the implementation guide](project-structure.md).
Release gaps and intentional constraints are tracked in [TODO](../TODO.md#documented-limitations).
