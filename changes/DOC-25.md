<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DOC-25

## Changed

- Documentation (DOC-25): the README no longer says that any mono or
  true-stereo reverb needs a `.cfg`. An IR file with one channel for each
  of the track's plays as it is, a mono file on a mono track included; a
  mono response on a stereo track, or a four-channel true-stereo response,
  needs a short `.cfg`, which the new
  [reverb responses](../docs/use/configuration.md#reverb-responses) section
  shows. The README's preview status now says what jrp ran in hosts on
  0.18.10, and the user guides no longer say the plug-ins are untried in
  named hosts, that every tool refuses to replace an existing output
  (`convolver` replaces it), or that a configuration list chosen before
  Audacity first plays always needs choosing again. Notes about how
  Windows previews before 0.13.12 had no window are gone, and the long
  paragraph on when a chosen file is checked is split by case.

## Done

- [x] **DOC-25 — Correct and tighten the README and user guides.**
  25 November 2026 ([configuration](../docs/use/configuration.md#reverb-responses)).
