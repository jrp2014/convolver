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

CTest runs them as `upgrade.cli` (whenever the CLI is built) and
`vst3.upgrade` (in VST3 builds that load the bundle, the same condition as
`vst3.validator`).

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
files, which 0.13.12's match byte for byte (peaks 0.25 to 0.53): a Clang 18
Release build with Ooura reproduced them (built with GCC 11.4) sample for sample; CI's GCC 15 Release build
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
  alone by the formatters and markdownlint, and any change to them runs the
  full CI ([CI triggers](../../docs/develop/testing.md#ci-triggers)).
  A deliberate incompatibility is jrp's decision, recorded in the
  [decisions log](../../docs/project/decisions.md), and the check it breaks is
  changed in the same pull request, not the file. So is a deliberate
  numerical change that stays within the bound (an optimisation, say) but
  breaks an identical-samples check.
- Capture each published preview once, after publication, with
  [`capture.py`](capture.py) on Linux x86-64 from that release's Linux ZIP
  ([publication steps](../../docs/develop/releasing.md#publishing-an-unsigned-preview)).
  Its docstring gives the command; it refuses to write into an existing
  folder.

## Not yet kept

- A saved REAPER project containing Convolver, made with the release, kept
  for reopening by hand; the runner will take the plug-in state out of it and
  restore it through the adapters (COMPAT-1 in [TODO](../../TODO.md)).
- The Audio Unit and LV2 adapters: the VST3 check runs on each CI system that
  builds the bundle, but no Audio Unit state from a release is kept yet, and
  no release has shipped LV2.
