<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-10

## Changed

- Packages (REL-10): the `echo` example presets and filters now sound like an
  echo: each sound comes back three times, 0.3 s apart, each repeat 6 dB
  quieter, where before a single copy 5 ms later only made the tone hollow.
  The three examples (identity, echo and repeat) now sound clearly different
  and show differently on a level meter. See
  [the plug-in examples](../examples/plugin-presets/README.md).

## For contributors

- Tests (REL-10): the package probe checks every tap of each example against
  its own independent list, so it follows the echo's four taps.
- Packages (REL-10): `examples.json` gives each example's latency as 0, the
  default since PERF-2, where it said 256. The echo examples are longer
  (0.9 s), so the generated `presets/` folder grows from about 1.2 MB to
  5.2 MB, and each future upgrade capture (`tests/upgrade/X.Y.Z/`) to about
  13 to 15 MB, from 4.4 MB for 0.14.0.

## Done

- [x] **REL-10 — Make the example echo clearly audible.** 21 November 2026,
  PR #266 ([plug-in examples](../examples/plugin-presets/README.md)).

## Decisions: Configuration and sound

- **21 November 2026.** jrp: "Ensure that the generated test filters create
  a clearly audible effect, eg a distinct echo, rather that just a slight
  smear", and he often judges by the level meters. The `echo` examples
  became three fading repeats 0.3 s apart. See
  [the plug-in examples](../examples/plugin-presets/README.md).
