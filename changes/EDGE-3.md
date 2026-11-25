<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# EDGE-3

## Added

- Filters (EDGE-3): a mono IR file, or any filter with one input and one
  output, now filters every channel of stereo or wider audio on its own, and
  a four-channel IR file on stereo audio plays as true stereo (left to left,
  left to right, right to left, right to right), without a `.cfg`. This is
  how REVerence, SIR3 and other convolution reverbs read these files. A
  filter that matches the channels exactly still wins, in a list too; a
  file marked as Ambisonic B-format is refused with the reason; a `.cfg`
  still routes any other layout. Status, the Convolver log, the window's
  Routing line, the command line's stderr and `--inspect` (`layout` in the
  JSON) say when a default routing plays. Convolver 4.4 refused such files.
  See [mono and true-stereo IR files](../docs/use/configuration.md#mono-and-true-stereo-ir-files).

## Changed

- Plug-ins (EDGE-3): in Ardour, a mono filter on a Flexible-I/O track now
  plays with the VST3 plug-in as Ardour sets it up, without setting its
  channels by hand. See [Ardour](../docs/use/plugins.md#ardour).

## For contributors

- Library (EDGE-3): `on_each_channel()` lays a 1→1 configuration out on
  several channels; `plays_on_each_channel()`, `true_stereo_routing()` and
  `routing_sentence()` in `host.hpp`; `detail::default_layout()` and
  `laid_out()` hold the rule, and `load_configuration()` selects in two
  passes so an exact match beats a default anywhere in a list. Plug-ins keep
  a mono filter 1→1 in their state and lay it out at each start. See the
  [plug-in contract](../docs/develop/plugin-contract.md#default-routings).
- Tests (EDGE-3): `native.io`, `native.filter_file` `default_layouts`, the
  command-line tests, `vst3.adapter`, `lv2.adapter`, `au.editor` `files`
  and a new Ardour case (mono on a Flexible-I/O track with the VST3 plug-in).

## Done

- [x] **EDGE-3 — Reverb file layouts without a configuration.** 25 November
  2026, PR #292 ([configuration](../docs/use/configuration.md#mono-and-true-stereo-ir-files)).

## Decisions: Configuration and sound

- **25 November 2026.** A filter with one input and one output plays on each
  channel of audio with two or more channels (not summed), and a
  four-channel IR file on stereo is true stereo in the order LL, LR, RL, RR,
  the order REVerence, SIR3, IR.lv2 and KlangFalter use. An exact channel
  match always wins, a file marked as Ambisonic B-format is never taken for
  true stereo, and Convolver says which routing plays. jrp asked for documentation
  that is "crystal clear, with rationale". See
  [mono and true-stereo IR files](../docs/use/configuration.md#mono-and-true-stereo-ir-files).
