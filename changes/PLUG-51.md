<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-51

## Added

- Plug-ins (PLUG-51): the window's folded details and Copy Diagnostics now
  give a line for each output, with the inputs that reach it through the
  filter, their route gains and the channels' delays
  (`Out 3: (In 1 + In 2) ×0.5; output delayed 48 frames (1.0 ms)`), so a
  crossover's paths past the Routing line's first four show. An output that
  no path feeds says so and what plays there instead: silence, also in
  bypass, or, where there is a dry signal, its own input, only as Mix's dry
  share and in bypass. See
  [Convolver's window](../docs/use/plugins.md#convolvers-window).

## Done

- [x] **PLUG-51 — Every output's routes.** 26 November 2026, PR #TBD
  ([Convolver's window](../docs/use/plugins.md#convolvers-window)).
