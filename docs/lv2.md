<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# LV2 plug-in

[Documentation index](README.md) · [Audio concepts](concepts.md) · [Plug-in presets](presets.md)

**In short:**

- The LV2 plug-in is optional and, for now, a developer build for Linux. Build
  it with `-DCONVOLVER_BUILD_LV2=ON` (it needs the `lv2` headers, and `lilv`
  unless tests are off), then point `LV2_PATH` at `BUILD/lv2`, or copy the
  `convolver.lv2` folder into `~/.lv2`.
- LV2 ports are fixed, so each channel layout is its own plug-in: 1→1, 2→2,
  1→2, 2→4, 2→6, 2→8, 6→6 and 8→8. Choose the one whose counts match your
  filter.
- Load a filter as a preset: `convolver-preset room.cfg room.preset.lv2`
  writes a preset folder with the filter embedded; copy it into `~/.lv2` and
  choose it in the host's preset menu. A file chooser for the filter
  (`patch:Set` on a path parameter, loaded off the audio thread) is the next
  step, LV2-2.
- Latency is 256 frames, reported on the latency port, so hosts compensate.
  A status port shows Running, No filter or Load failed, and every rejected
  load is explained in the host's log.
- Not yet tested in named hosts (Ardour, Carla, REAPER, Qtractor, PipeWire);
  that is LV2-4, and release bundles are LV2-5.

Contents: [build](#build), [install for a host](#make-the-plug-in-discoverable),
[loading filters](#loading-filters), [ports](#ports),
[behaviour](#processing-and-lifecycle) and [tests](#tests).

## Build

```sh
sudo apt-get install lv2-dev liblilv-dev lilv-utils
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCONVOLVER_BUILD_LV2=ON
cmake --build build --parallel
ctest --test-dir build -L lv2 --output-on-failure
```

The plug-in needs `lv2` 1.18 or later (headers only). Its tests need `lilv`
0.24 or later, which CMake therefore requires while `CONVOLVER_BUILD_TESTS`
is on (the default for a top-level build); configure with
`-DCONVOLVER_BUILD_TESTS=OFF` to build the plug-in without it. As for VST3, a plug-in build uses the built-in Ooura
FFT by default rather than FFTW, whose planner a host process may share with
other plug-ins ([backend selection](fft-backends.md)). The library exports
only `lv2_descriptor`, so it cannot clash with the VST3 build or another
version loaded in the same host.

The bundle is written to `BUILD/lv2/convolver.lv2/`: the library
`convolver.so`, `manifest.ttl` and `convolver.ttl`.
[`plugins/lv2/generate_ttl.py`](../plugins/lv2/generate_ttl.py) writes both
Turtle files from [`plugins/identities.json`](../plugins/identities.json), so
the plug-in URIs and layouts have one source
([identities](plugin-identities.md)). A build with `CONVOLVER_SANITIZERS=ON`
can be loaded only by programs built with the same sanitizers, such as the
tests; use a build without them for a host.

## Make the plug-in discoverable

Hosts find LV2 bundles through `LV2_PATH`, which defaults to
`~/.lv2:/usr/local/lib/lv2:/usr/lib/lv2` on Linux. Either start the host with
`BUILD/lv2` added in front of that list, for example
`LV2_PATH="$PWD/build/lv2:$HOME/.lv2:/usr/local/lib/lv2:/usr/lib/lv2"`, or copy
the bundle: `cp -R build/lv2/convolver.lv2 ~/.lv2/`. `lv2ls` lists
what lilv-based hosts will see; Convolver's plug-ins appear as
`https://github.com/jrp2014/convolver/lv2#discrete-N-M`.

## Loading filters

Until LV2-2 adds a filter parameter, a filter reaches the plug-in through
state: a preset, or a session saved by a host.

1. Write a preset for your filter, at the sample rate the host runs at:

   ```sh
   convolver-preset room.cfg room.preset.lv2
   ```

   The tool picks the LV2 plug-in for the filter's channel counts and says
   which ([presets guide](presets.md)).
2. Copy the folder into `~/.lv2/` (any folder on `LV2_PATH`).
3. In the host, insert the plug-in with the same counts and choose the preset
   by its name.

The preset embeds the whole filter, so the original files are not needed
later. Hosts save the same state in their sessions. The filter is prepared
when the state is restored; restoring before audio starts installs it
directly, and restoring while audio has already run crossfades to it as the
[VST3 guide](vst3.md#processing-and-lifecycle) describes.

A filter that does not suit the instance (another sample rate, or other
channel counts) is rejected: the host's restore call fails, the status port
shows Load failed and the log explains what differs and what to change. A
rejected replacement keeps the running filter, which the session still saves.
A rejected first filter plays silence, never the unfiltered input, and saving
the session then records the failure, so reopening it stays silent. These are
the rules of the
[shared adapter contract](plans/2026-10-21-plugin-formats.md#shared-adapter-contract).

## Ports

Every layout has the same three control ports, then its audio ports:

| Index | Symbol | Kind | Meaning |
| --- | --- | --- | --- |
| 0 | `enabled` | Control input, `lv2:enabled` | 1 processes, 0 bypasses (a 64-sample ramp) |
| 1 | `latency` | Control output, `lv2:reportsLatency` | 256 frames |
| 2 | `status` | Control output, enumeration | 0 Running, 1 No filter, 2 Load failed (3 Loading is used from LV2-2) |
| 3… | `in_1` … `in_N` | Audio input | Input channels in order |
| then | `out_1` … `out_M` | Audio output | Output channels in order |

Load failed stays shown until a filter loads successfully, also after the
host reactivates the plug-in. The plug-in needs `urid:map`; it uses `log:log`
for messages (otherwise they go to standard error) and
`bufsz:maxBlockLength` from `options` when the host gives it. Without that
option, or for longer `run()` calls, it processes in pieces of at most 4096
frames, with the same output.

## Processing and lifecycle

- **Nothing loaded:** equal input and output counts play the input unchanged
  (after the latency); unequal counts play silence.
- **Bypass:** ramps to the input delayed by the latency (input *c* to output
  *c*); with unequal counts every output ramps to silence. Bypass is the
  `enabled` port, which the host saves itself, so the plug-in's state does
  not record it, and a bypass flag in a preset from another format is ignored.
- **Activation** is also LV2's reset: it starts a fresh processor, with the
  current filter, no history and its exact tail.
- **Tails:** LV2 core cannot report a tail, so hosts that stop processing at
  the end of a region may cut the filter's ring-out; render with a little
  silence after the audio.
- **Threads:** `run()` does not allocate, lock or do I/O. State restore, save,
  activation and cleanup share a lock that `run()` never takes. The plug-in
  does not declare `state:threadSafeRestore`, so hosts do not restore state
  while audio runs.

## Tests

`ctest -L lv2` runs:

- `lv2.harness`: the [PLUG-2 comparisons](testing.md#plug-in-comparisons-plug-2)
  with the plug-in hosted by lilv. All cases pass; the restore-before-setup
  cases are skipped (LV2 knows its rate and channels from instantiation) and
  the tail checks are skipped (no tail reporting).
- `lv2.adapter`: the bundle's plug-ins and ports through lilv; instantiation
  refusals; the status port, log messages and saved state after good and bad
  loads; presets written by `convolver-preset` and lilv's own state round
  trip; long `run()` calls; and lifecycle sequences, one scripted and several
  seeded (loads, failed loads, audio, bypass, reset, reopening a saved
  session). A small model
  checks the saved state, status and latency after every step, and the exact
  output at every reset and at the end; the transitions during audio
  (crossfades, a rejection while playing) are the harness's cases.
- `lv2.exports` and `lv2.fft_symbols`: the library exports only
  `lv2_descriptor` and does not use a shared FFTW.

The [LV2 workflow](../.github/workflows/lv2.yml) also runs `lv2lint` on every
plug-in in the bundle, with the pinned packages
([dependency pins](plugins/build.md#pinned-format-dependencies)). lilv's
`lv2apply` cannot host Convolver: it offers plug-ins no features, and Convolver
needs `urid:map` for its state (lv2apply 0.24.22 then crashes rather than
reporting the refusal).
