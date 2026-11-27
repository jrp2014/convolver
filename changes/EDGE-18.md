<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# EDGE-18

## Fixed

- Command line and presets (EDGE-18): an IR file refused for as many
  output channels as input channels, such as a four-channel file marked as
  Ambisonics B-format asked for 2 and 2, no longer also suggests omitting
  `--output-channels`, which gave the same refusal. The message keeps the
  reason (a marked B-format response cannot be used as true stereo on
  stereo audio) and what to do about it. A four-channel IR file asked for
  2 inputs and 4 outputs is now told that true stereo has two outputs,
  instead of being told to use a four-channel true-stereo file, which it
  already is. See
  [configurations](../docs/use/configuration.md).

## Done

- [x] **EDGE-18 — No output-count remedy that cannot help.** jrp's review
  of `d6d03ec9`, 27 November 2026, PR #340
  ([configurations](../docs/use/configuration.md)).
