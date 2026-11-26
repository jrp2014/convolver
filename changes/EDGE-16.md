<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# EDGE-16

## Fixed

- Filter files (EDGE-16): a four-channel file marked as Ambisonics B-format
  is refused on stereo audio wherever the marking is: in an RF64 or Wave64
  file, and in a WAV whose format chunk comes after 64 or more other
  chunks. Until now such a file played as true stereo with the wrong
  routing, `--inspect` reported it as true stereo and `convolver-preset`
  saved that routing without a warning. See
  [mono and true-stereo IR files](../docs/use/configuration.md#mono-and-true-stereo-ir-files).

## For contributors

- Filter files (EDGE-16): each reader asks `ambisonic_b_format()` once, as
  it opens the file, and selection, inspection and the explanations read
  `SoundFile::ambisonic()`. The check walks every chunk of a RIFF, RF64 or
  BW64 file (sizes from `ds64`) and of a Wave64 file, up to the 65,536
  chunks `check_wave()` allows. Tests: `native.io` and `native.inspection`
  load and inspect the marking in six layouts, among them RF64 with the
  audio before the format chunk and Wave64 with a chunk of odd size, and
  `native.audio_toolbox` (macOS) checks that the Audio Unit's reader never
  plays them as true stereo (the shared fixture is
  `tests/native/b_format.hpp`).

## Done

- [x] **EDGE-16 — Ambisonics B-format recognised in RF64, Wave64 and after
  many chunks.** jrp's review of `7bd024f`, 26 November 2026, PR #307
  ([mono and true-stereo IR files](../docs/use/configuration.md#mono-and-true-stereo-ir-files)).
