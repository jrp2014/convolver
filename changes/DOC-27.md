<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DOC-27

## Fixed

- Documentation (DOC-27): the plug-in guide no longer says an instance with
  no filter passes its input "unchanged" whatever the settings: it plays at
  the level Gain and Mix give it, unchanged at their defaults or with
  bypass on. A kept filter that does not suit the track lets Mix's dry share
  and bypass through where the track and the filter each have as many
  inputs as outputs (a kept 4-in, 4-out filter on a 2-in, 2-out track
  does), not only where all four counts are equal; and in VST3 and Audio
  Unit a first filter whose rate or channel counts do not fit is kept
  rather than failed, while one too large to save is refused in every
  format. The quick-start says such a kept filter shows Waiting, not Load
  failed. See
  [when a filter does not fit](../docs/use/plugins.md#when-a-filter-does-not-fit)
  and [bypass](../docs/use/plugins.md#bypass).

## For contributors

- Plug-in contract (DOC-27): the table of four states (empty, active, kept
  but unsuited, failed) in
  [states and transitions](../docs/develop/plugin-contract.md#states-and-transitions)
  is the authoritative account of what plays, what is saved and what Status
  shows; the earlier table of three situations is now a short introduction
  to it, and the kept state's dry-signal rule is worded as the code
  applies it. The table now says a file chosen while the host is not
  playing is Kept when it does not suit a configuration the host is known
  to have (PLUG-27), and the PLUG-27 bullet names the answer
  `HostSettling::check()` gives while the host plays,
  `keep_unless_one_plays`. The lifecycle tests' reference model still writes its
  expected answers from the contract, not from production code.

## Done

- [x] **DOC-27 — One authoritative table for what plays, is saved and
  shows.** jrp's review of `547e2dd8`, 26 November 2026
  ([states and transitions](../docs/develop/plugin-contract.md#states-and-transitions)).
