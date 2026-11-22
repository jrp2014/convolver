<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-49

## Fixed

- VST3 (PLUG-49, jrp's live session of 22 November 2026): in Audacity
  4.0.2, a configuration list chosen before the first playback now loads
  by itself when Audacity plays with Convolver's window open, as PLUG-40
  intended, and the export holds it. Before, Audacity handed the plug-in
  its own saved state back as playback started, which forgot the list:
  Status stayed Load failed and every export was silent until the list was
  chosen again. See [loading a filter](../docs/use/plugins.md#loading-a-filter).
- VST3 (PLUG-48, jrp's live session of 22 November 2026): a preset the
  plug-in refuses, such as one for 44.1 kHz chosen from REAPER's preset
  menu while a 48 kHz filter plays, no longer changes the Gain, Gain mode,
  Mix and Latency the host and the window show. Before, they took the
  refused preset's values (Latency 0) while the plug-in went on playing its
  own (Latency 1024).

## For contributors

- Tests (PLUG-49, PLUG-48): `vst3.adapter` `list_read_again` replays
  Audacity's playback start (its own state handed back, then a start)
  before the window reads the report; `refused_state_keeps_parameters`
  restores a preset for another rate while a filter plays at Latency 1024
  and checks the parameters, then an accepted preset's. Each failed before
  its fix. `HostFilter::list_waits()` and `Processor::unchanged_state()`;
  `ConvolverStatus` carries the settings that play and `restore refused`
  ([adapter contract](../docs/develop/plugin-contract.md)).

## Done

- [x] **PLUG-49 — Keep a waiting configuration list when Audacity starts
  playback.** 22 November 2026, PR #271, jrp's live session on 0.18.10
  ([loading a filter](../docs/use/plugins.md#loading-a-filter)).
- [x] **PLUG-48 — Keep the shown settings when a restored preset is
  refused.** 22 November 2026, PR #271, jrp's live session on 0.18.10
  ([adapter contract](../docs/develop/plugin-contract.md)).
