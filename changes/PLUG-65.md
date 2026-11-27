<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-65

## For contributors

- Plug-in windows (PLUG-65): the Gain mode and Latency menus' tooltips come
  from one place, `choice_help()` in `portable/window_model.cpp`, beside
  the sliders' `slider_help()`. The Audio Unit and VST3 macOS windows (in
  the shared `setting_rows.mm`) and the VST3 Windows window each kept their
  own copy of the two texts, so a change to one window's wording could miss
  the others. The words are unchanged. `native.window_model` checks that
  the Gain mode help names each mode and Latency's says the host is told
  the delay; the macOS editor tests check that both menus show it.

## Done

- [x] **PLUG-65 — One source for the Gain mode and Latency help.**
  27 November 2026, from jrp's review of `ca12f773`
  ([architecture](../docs/develop/architecture.md#the-shared-macos-window)).
