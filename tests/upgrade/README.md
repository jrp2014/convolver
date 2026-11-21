<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Files from released builds (COMPAT-1)

Round-trip tests with today's writer and reader can agree with each other
while both break yesterday's files. Each folder here, named by version, keeps
files made by one published release and the audio that release rendered from
them. Every later build must load them and reproduce that audio. The `.cfg`
corpus in [`tests/compatibility/`](../compatibility/README.md) is separate:
its expectations are written by hand, while these come from a release.

```sh
python3 tests/upgrade/run_upgrade.py --cli build/convolver
python3 tests/upgrade/run_upgrade.py --vst3-render build-vst3/plugins/vst3/convolver_vst3_render \
  --vst3-bundle build-vst3/VST3/Release/Convolver.vst3 --vst3-backend OOURA --kernels-from build-vst3/convolver \
  --compiler "GNU 11.4.0" --configuration Release "--flags= -O3 -DNDEBUG -g"
```

CTest runs them as `upgrade.cli` (whenever the CLI is built),
`vst3.upgrade` (in VST3 builds that load the bundle, the same condition as
`vst3.validator`) and `lv2.upgrade` (in LV2 builds with tests, which build
the CLI too). By hand, the LV2 check is:

```sh
python3 tests/upgrade/run_upgrade.py --lv2-render build/.../convolver_lv2_render \
  --lv2-bundle build/.../convolver.lv2 --lv2-backend OOURA --kernels-from build/convolver
```

## What each folder holds

- `package/`: the release's example presets (`presets/`) and first-use
  examples (`examples/first-use/`), copied byte for byte from its Linux ZIP.
  The macOS ZIPs hold the same bytes; the Windows ZIP differs only in the
  `.cfg` and `examples.json` files' line endings (CRLF).
- `inputs/`: the input for each preset rate and channel count, 1,024 frames of
  the [fixtures' noise](../plugins/fixtures.py). The first-use examples use
  their own `input.wav`.
- `expected/`: the audio the release rendered. `NAME.wav` is the release's
  `convolver` CLI at its default block size (0, no latency) and, when the two
  agree sample for sample, also its VST3 plug-in restoring the preset; a
  `NAME.vst3.wav` would hold the plug-in's render if they differed.
  `NAME.block256.wav` is the CLI with `--block-size 256` (first-use examples).
- `manifest.json`: how each file was made. The packages' names and SHA-256,
  the tag, source commit and candidate run; the capture's date, machine, the
  release CLI's `--version` lines and build; every retained file's SHA-256;
  and each case: the preset, the filter the CLI is given, the input, rate and
  channel counts, and each check with its expected file.
- `lv2-state/` and `lv2.json`, for a release that shipped the LV2 plug-in
  (0.14.0 so far): one folder per case, each a session the release's own LV2
  plug-in saved through lilv (`state.ttl` and `manifest.ttl`, as Jalv and
  other lilv hosts save one), with a copy of the case's filter files, which
  the plug-in chose through its filter parameter as a host's file chooser
  does, so the state holds the filter's path relative to the folder. Each
  case at the default settings, and the first-use stereo and crossover
  examples again with settings away from their defaults (`NAME-settings`:
  Gain −6 dB, Mix 50 % and Latency 256; Latency 1024 and Gain mode Estimate),
  so that a renamed port or a setting no longer restored shows. `lv2.json`
  records how they were made, each file's SHA-256 and, for each case, its
  settings, the latency the release reported and the expected render: the
  CLI's file where the release's plug-in, restoring the state, rendered the
  same samples (every case at the default settings did), otherwise
  `expected/NAME.lv2.wav`.

## The checks

For each case, `run_upgrade.py`:

- checks every retained file against its SHA-256 in the manifest;
- renders the input with this build's CLI, at each recorded block size, from
  the case's filter;
- restores the case's `.vstpreset` in this build's VST3 bundle, loaded from
  its file by [`vst3_render.cpp`](vst3_render.cpp) as a host loads it (an
  offline host, host blocks of 1, 73, 137, 3, 129, 512, 64 and 137 frames,
  repeated), and renders the input, dropping the latency the plug-in
  reports, which must be the latency the release reported (a host delays
  the other tracks by it);
- restores each LV2 state in this build's LV2 bundle with
  [`lv2_render.cpp`](lv2_render.cpp): lilv reads `state.ttl` and restores it,
  ports included, into a new instance of the layout for the case's channel
  counts, as a host reopening a session does; the instance plays the input
  in the same irregular host blocks, and the latency its port reports, which
  must be the release's, is dropped;
- requires the same rate, channel count and frame count, and samples within
  the bound: 1e-5 (the PLUG-2 oracle tolerance), times the expected file's
  peak when it is louder than full scale, since float rounding grows with the
  level. A newer compiler may change the samples within it (jrp's
  [decision](../../docs/project/decisions.md#releases)).
- requires identical samples only when this build compiles and runs as the
  capture did: the same compiler and version, configuration, compiler flags
  (as the package manifest records them), FFT backend and CPU kernels
  (PERF-4); the block sizes always are. CTest passes the compiler,
  configuration and flags; run by hand without all three, the check uses the
  bound. Each run prints which rule applies.
- prints, for each check that is not identical, the first differing sample,
  the largest difference as a share of the bound and in float epsilons of the
  peak, and at the end the largest share of any check, so a drift towards
  the bound shows before it fails.

How far builds fall from the bound, on Linux x86-64 against the 0.13.2
files, which 0.13.12's, 0.13.20's, 0.13.24's and 0.14.0's match byte for byte (peaks 0.25 to 0.53): a Clang 18
Release build and a GCC 13.3 Release build with Ooura reproduced the capture's
renders (made by GCC 11.4 Release builds) sample for sample; CI's GCC 15 Release build
with Ooura differed by up to 1.3e-7, and a Clang 18 build with FFTW by up to
2.1e-7 (2% of the bound). The capture renders and the double-precision
references in `examples/first-use/` agree within 1.3e-7.

An identical-samples failure that stays within the bound means the audio is
still compatible but something that changes rounding differs from the
capture without the check knowing (a different libm, which computes Ooura's
twiddle factors, or a per-target flag), or the DSP changed; the message says
so.

## Rules

- Never regenerate or edit a retained file; the runner fails if one changes.
  The version folders are checked out byte for byte (`.gitattributes`), left
  alone by the formatters and markdownlint, and any change in this folder
  runs the native, VST3 and LV2 workflows, the three whose tests read it
  ([CI triggers](../../docs/develop/testing.md#pull-requests-for-one-plug-in-format)).
  A deliberate incompatibility is jrp's decision, recorded in the
  [decisions log](../../docs/project/decisions.md), and the check it breaks is
  changed in the same pull request, not the file. So is a deliberate
  numerical change that stays within the bound (an optimisation, say) but
  breaks an identical-samples check.
- Capture each published preview once, after publication, with
  [`capture.py`](capture.py) on Linux x86-64 from that release's Linux ZIP
  ([publication steps](../../docs/develop/releasing.md#publishing-an-unsigned-preview)),
  then its LV2 state with [`capture_lv2.py`](capture_lv2.py) from the same
  ZIP. Their docstrings give the commands; each refuses to write files it
  would replace.

## Not yet kept

- A saved REAPER project containing Convolver, made with the release, kept
  for reopening by hand; the runner will take the plug-in state out of it and
  restore it through the adapters (COMPAT-1 in [TODO](../../TODO.md)).
- Audio Unit state from a release: the VST3 check runs on each CI system
  that builds the bundle and the LV2 check in the LV2 workflow, but no Audio
  Unit state is kept yet.
- LV2 state from 0.13.12, 0.13.20 and 0.13.24, which shipped the LV2
  plug-in before 0.14.0; only 0.14.0's is kept.
