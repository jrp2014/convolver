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

## Done

- [x] **REL-10 — Make the example echo clearly audible.** 21 November 2026
  ([plug-in examples](../examples/plugin-presets/README.md)).

## Decisions: Configuration and sound

- **21 November 2026.** jrp: the package's example filters must have a
  clearly audible effect, such as a distinct echo, rather than a slight
  smear, and distinct examples must sound different. The `echo` examples
  became three fading repeats 0.3 s apart. See
  [the plug-in examples](../examples/plugin-presets/README.md).
