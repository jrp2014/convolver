<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# EDGE-3 (review)

## Fixed

- Plug-ins (EDGE-3): a four-channel WAVE file marked as Ambisonics B-format,
  chosen on a stereo track, is refused with the reason, also while the host
  is stopped or when no other filter plays. Before, it could be kept as a
  four-channel filter, silent, with advice to choose it again. See
  [mono and true-stereo IR files](../docs/use/configuration.md#mono-and-true-stereo-ir-files).
- Filters (EDGE-3): when no entry of a configuration list matches, the
  explanation no longer says mono or true-stereo entries would be used for
  audio with unequal input and output counts, where they are not, and it
  says when a four-channel entry is marked as Ambisonics B-format on
  stereo audio, where it would otherwise play as true stereo.

## For contributors

- Inspection (EDGE-3): `--inspect`'s second pass stops at entries a list
  gained after the first pass read it, instead of reading past its record.
  Found in jrp's review of `a404df0`; tests in `native.filter_file`
  `default_layouts` and `native.io`.
