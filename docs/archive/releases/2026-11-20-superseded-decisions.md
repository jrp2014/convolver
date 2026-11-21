<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Superseded decisions

> **Archived 20 November 2026.** Entries of the [decisions log](../../project/decisions.md)
> that later decisions wholly replaced, moved here unchanged when the log was
> rewritten as short dated entries; only relative links are adjusted. Each
> replacement is in the log. The earlier baselines for the checks left
> (0.13.2, 0.13.12, 0.13.20 and 0.13.24) gave way to the published 0.14.0 on
> 19 November 2026.

## Direction

| Date | Decision | Explained in |
| --- | --- | --- |
| 22 October 2026 | REAPER and Carla are the suggested first named hosts; jrp will try them (replaced 23 October 2026) | [Host testing](../../develop/host-testing.md#first-hosts) |

## Releases

| Date | Decision | Explained in |
| --- | --- | --- |
| 16 November 2026 | The published 0.13.24 replaces 0.13.20 as the baseline for the checks left, REL-3's independent walkthrough and COMPAT-1's REAPER project: they run on its exact files. It differs from 0.13.20 only in the LV2 plug-in, which none of the checks left involves, so no result is lost. 0.13.20's kept presets and audio stay in COMPAT-1 beside 0.13.24's; this supersedes the patch-preview row's "the checks left stay on 0.13.20" (jrp: "Wait and get my Mac session to 13.24", before 0.13.24 was published) | [Status: checks left](../../project/status.md#checks-left-before-publication), [TODO](../../../TODO.md#execution-priorities) |
| 16 November 2026 | The published 0.13.20 replaces 0.13.12 as the baseline for the checks left, REL-3's independent walkthrough and COMPAT-1's REAPER project: they run on its exact files, which hold the PLUG-33 and PLUG-38 fixes and the Gain mode, and no check had yet been run on 0.13.12. Results recorded on earlier builds stay as records of those builds; 0.13.2's and 0.13.12's kept presets and audio stay in COMPAT-1 (jrp chose "Move to 0.13.20" on a decision card after publishing 0.13.20) | [Status: checks left](../../project/status.md#checks-left-before-publication), [TODO](../../../TODO.md#execution-priorities) |
| 11 November 2026 | The published 0.13.12 replaces 0.13.2 as the baseline for the checks left and REL-3's independent walkthrough: they run on its exact files, since VST3-17's Audacity fix, the Windows window and LV2 exist only from 0.13.12. Results recorded on 0.13.2 or earlier stay as records of those builds. 0.13.2's kept presets and audio stay in COMPAT-1 as a reference for later builds (jrp chose "Move to 0.13.12" on a decision card after publishing 0.13.12) | [Status: checks left](../../project/status.md#checks-left-before-publication), [TODO](../../../TODO.md#execution-priorities) |
| 9 November 2026 | The published 0.13 preview (0.13.2) is the baseline for the checks left and REL-3's independent walkthrough: they run on its exact files, and results recorded on 0.12.x builds (v0.12.5, v0.12.8, 0.12.12) stay as records of those builds rather than standing for 0.13, since PERF-2 changed the engine (jrp: "Use 0.13 as the baseline"). This replaces, from 0.13, the 8 November rule that let the v0.12.8 results stand while no engine, loading or plug-in code changed | [Status: checks left](../../project/status.md#checks-left-before-publication), [TODO](../../../TODO.md#execution-priorities) |

## Configuration and sound

| Date | Decision | Explained in |
| --- | --- | --- |
| 18 October 2026 | Dither is off unless requested. Replaced on 25 October 2026 | [Command-line reference](../../use/command-line.md#integer-formats-dither-and-noise-shaping) |
