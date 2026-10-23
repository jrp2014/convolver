<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# VST3 adapter and host lifecycle

[Documentation index](README.md) · [Audio concepts](concepts.md)

**In short:**

- The plug-in is optional. Build it with `-DCONVOLVER_BUILD_VST3=ON` and a
  separately downloaded Steinberg VST3 SDK, then copy the whole
  `Convolver.vst3` bundle into a folder your host scans.
- There is no editor. Turn a WAV, `.cfg` or configuration list into a
  `.vstpreset` with `convolver-preset` (or the older `convolver-vst3-preset`),
  then load it from the host's preset menu. The preset embeds the filter, so the
  original files are not needed later.
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

The optional SDK build needs CMake 3.25+ and a C++20 compiler. The core alone
retains its CMake 3.20 minimum. A plug-in build needs no FFT library: with
`CONVOLVER_BUILD_VST3=ON` the default backend (`AUTO`) is the built-in Ooura FFT,
or Apple's Accelerate on macOS. FFTW is avoided because a plug-in shares its
process with the host and other plug-ins, which may use the same FFTW, and
FFTW's planner is not thread-safe. Pass `-DCONVOLVER_FFT_BACKEND=FFTW` to
override (CMake then warns; see [backend selection](fft-backends.md#plug-in-builds)).
One backend serves every target in a build, so the CLI built alongside the
plug-in uses it too. Obtain the SDK separately:

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
vcpkg install 'libsndfile[core]:x64-windows-static-md'
cmake -S . -B build-vst3 -A x64 -DCONVOLVER_BUILD_VST3=ON `
  -DCONVOLVER_VST3_SDK=C:/path/to/vst3sdk `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
```

Use a fresh build directory when changing triplets. An FFTW override on
Windows needs static FFTW too (`fftw3:x64-windows-static-md`).
The preset-writing tool still needs libsndfile. Core-only plug-in builds can
turn I/O and CLI off; the preset writer is then omitted.

The SDK creates `build-vst3/VST3/Release/Convolver.vst3`. No link is created in
user/system plug-in folders. This is a developer build, not a signed/notarized
installer or self-contained distribution. Shared libsndfile (and, with an FFTW
override, FFTW) dependencies must be available at runtime; compatibility with older OS versions also depends on how
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
configuration list into a new preset with `convolver-preset`, which makes
presets for every plug-in format ([presets guide](presets.md)):

```sh
./build-vst3/convolver-preset room.cfg room.vstpreset
```

The original VST3-only tool still works, with the same arguments and output:

```sh
./build-vst3/plugins/vst3/convolver-vst3-preset room.cfg room.vstpreset
```

With Visual Studio the executables are in `Release/` and `plugins/vst3/Release/`. The output must
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
configuration's routes refer to. VST3 orders a bus's channels by speaker, so
the order follows from the arrangement: 5.1 (`k51`) is L R C LFE Ls Rs, the same
order as the WAV mask `0x3F`.

**Speaker check (VST3-1).** If the `.cfg` names its output speakers (the channel
mask at the end of its first line, which presets keep) and the host's output
arrangement names different ones, the plug-in still plays the filter, routed by
channel number, and records a notice that says which outputs land on which
speakers: for example a 7.1 filter (`0x63F`, side speakers on outputs 7 and 8)
on a 7.1 Cine bus, whose channels 7 and 8 are the left and right of centre
speakers. VST3 has no standard way to show that notice without an editor yet
(DIAG-1, VST3-3). A mask of 0, a mask that does not name one speaker per output,
one-channel buses and arrangements with speakers outside the shared table
(`kMono`, ambisonics, Lw/Rw) are not compared. Channels are compared in bus
order; in a layout with side speakers and no back ones (5.1 written as `0x60F`,
for example), the side speakers count as the back pair, so `0x60F` suits `k51`.
The check
never rejects a preset ([layout rules](plugin-identities.md#channel-layouts)). A preset must match the bus channel counts and
the sample rate; there is no implicit IR resampling or pitch/timing change. Bypass
passes input *c* to output *c* only when the counts are equal; with unequal
counts it plays silence on every output
([shared adapter contract](plans/2026-10-21-plugin-formats.md#bypass)). More
than 8 channels, or more than one bus, is rejected. 32-bit and 64-bit host
buffers are supported, with **single-precision internal DSP**.

Loading follows the
[shared adapter contract](plans/2026-10-21-plugin-formats.md#shared-adapter-contract)
(VST3-7):

- **No preset loaded:** with equal input and output counts the plug-in passes
  input *c* to output *c*. With unequal counts it plays silence, so an unloaded
  2→6 crossover never sends full-range audio to a driver.
- **A preset that does not match:** once the plug-in has been activated, and
  while the host keeps that sample rate and those buses, `setState` checks the
  preset against them, whether or not the plug-in is active now. A mismatch returns `kResultFalse` and
  the last accepted preset keeps playing and stays the saved state. If no preset
  was accepted before, the plug-in plays silence, never its unfiltered input.
- **State restored before the first activation** (a project being opened) is
  kept as it is, even if `setupProcessing` has already been called: JUCE-based
  hosts call it with placeholder values before restoring state at the real
  rate. If it does not suit the rate or buses at activation, the plug-in
  still activates but plays silence; correcting the host's rate or layout brings
  the filter back, and saving the project keeps it. VST3 has no standard way to
  show the reason without an editor, so the host shows only silence (the
  explanation is held for a later editor or status display, VST3-3).
- **A preset loaded after activation but before any audio** (or while the host
  has stopped processing) is installed directly; later loads crossfade as
  described below.
- **Saving after a failed first load** saves that failure (host state version
  2) but not the rejected preset, so reopening the project plays silence again
  rather than the unfiltered input. Load a matching preset to replace it.
  Restoring that state over a working filter also silences it, because it
  restores what was saved. Builds before VST3-7 reject that state, so a fresh
  instance there plays as an empty instance (identity with equal counts).

This plug-in has a new VST3 identity: old VST2 project state
is not claimed to migrate automatically. The class IDs and bundle identifier are
listed, with every other format's, in
[plug-in identities](plugin-identities.md), which also explains why presets
saved today keep loading on any arrangement with the same channel counts.

## Processing and lifecycle

The host owns the audio device, clock and scheduling. Each callback supplies the
next section of one continuous stream; convolution history persists between
callbacks. Processing produces one output frame per input frame, with the delay
reported below. The plug-in performs no file I/O in this streaming path.

Latency is always 256 samples, independent of the DAW callback length. The adapter
handles variable blocks, in-place host buffers, zero-length/parameter-only calls,
inactive channels and silence flags. Silent input still drains the wet tail.
Bypass uses a 64-sample ramp and a dry delay matching the reported latency. With
unequal channel counts it ramps to silence instead. Wet processing continues
during bypass so returning to wet does not lose history.

Filters are prepared outside processing and transferred through a bounded
single-producer/single-consumer pointer handoff. A filter that arrives before any
audio has been processed since activation or reset is installed directly. A
reset (the host stopping and restarting processing) also ends a crossfade in
progress, so the newer filter plays at once, and a filter published before audio
resumes is installed directly. Later,
a replacement runs for 256 samples
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

`vst3.harness` drives the processor through the shared
[PLUG-2 comparisons](testing.md#plug-in-comparisons-plug-2): the first-use
fixtures (mono, stereo and 2→6) against a direct double-precision convolution
and, sample for sample, against the CLI's renders, at several block sizes with
separate and in-place buffers, plus replacement, bypass, reset and failure
cases. Since VST3-7 the adapter follows the
[shared adapter contract](plans/2026-10-21-plugin-formats.md#shared-adapter-contract)
and lists no known deviations.

The SDK validator runs in default and extensive (`-e`) modes. Extensive mode
pairs 15 speaker arrangements (the adapter must accept or refuse each cleanly)
and adds automation patterns. The validator, the adapter tests and the plug-in
all use Steinberg's SDK code, so CI also loads the bundle in an independent host.

### Independent host tests (pluginval)

[pluginval](https://github.com/Tracktion/pluginval) (Tracktion, GPL-3.0) is a
JUCE-based host that runs headless. It is downloaded separately, never linked
into Convolver. Configure with `-DCONVOLVER_PLUGINVAL=/path/to/pluginval` to add:

- `vst3.pluginval`: `pluginval validate --strictness-level 10 --skip-gui-tests`.
  Open/close, state save and restore, bus layouts, parameter fuzzing and thread
  safety, allocations during processing and oversized blocks, at several sample
  rates and block sizes. `CONVOLVER_PLUGINVAL_ARGS` adds options; CI adds
  `--rtcheck;relaxed` on macOS, where pluginval can flag real-time-unsafe calls
  in process callbacks.
- `vst3.pluginval.acceptance`: makes presets from a stereo impulse WAV (block
  size 300) and a cross-feed `.cfg` (block size 61), loads each through the
  host's state call, renders noise with `pluginval test`, and compares the
  output with a direct double-precision convolution delayed by 256 samples
  (tolerance 1e-5). A second render must match the first exactly.

[`tests/vst3/pluginval_tests.py`](../tests/vst3/pluginval_tests.py) runs both.
On Linux it uses `xvfb-run` when there is no display (JUCE needs an X server).
JUCE's host only accepts state in its own wrapper (an XML document holding the
component state in JUCE's base64 variant), so the script converts the preset's
component chunk; given raw component state, JUCE leaves the plug-in empty and
it passes delayed dry audio. CI pins pluginval 2.0.0-beta3 by checksum.

The VST3 CI matrix runs on Linux, Windows and macOS. These automated checks are
not proof of hard real-time deadlines or compatibility with every DAW. The
[plug-in distribution guide](plugins/index.md) covers pinned, unsigned preview
archives, installer templates and corresponding source. Commercial DAW testing,
signing, clean-machine installation and a custom file-picker editor remain
separate release work. VST3 exchanges floating-point audio, so the CLI/file writer's integer quantization and dither does not apply to it.

Source entry points and the change/test map are in [the implementation guide](project-structure.md).
Release gaps and intentional constraints are tracked in [TODO](../TODO.md#documented-limitations).
