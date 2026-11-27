<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# EDGE-18

## Fixed

- Command line and presets (EDGE-18): an IR file refused for 2 input and
  2 output channels, such as a four-channel file marked as Ambisonics
  B-format, no longer also suggests omitting `--output-channels`, which
  gave the same refusal. The message keeps the reason (a marked B-format
  response cannot be used as true stereo on a stereo track) and what to do
  about it. See
  [configurations](../docs/use/configuration.md).

## Done

- [x] **EDGE-18 — No output-count remedy that cannot help.** jrp's review
  of `d6d03ec9`, 27 November 2026, PR #340
  ([configurations](../docs/use/configuration.md)).
