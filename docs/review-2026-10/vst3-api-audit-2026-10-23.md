<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# VST3 API audit: 23 October 2026

Reviewed `plugins/vst3/` on `main` at `3ea689c` (0.4.31) against the VST3
documentation, at jrp's request ("have you scoured the full VST3
documentation?"). Earlier VST3 work had been checked against the Steinberg
validator, pluginval and the [shared adapter contract](../develop/plugin-contract.md),
not against the documentation as a whole. Nothing found changes the filtered
sound. The fixes jrp approved on the same day are TODO VST3-8; the other
findings became VST3-9, VST3-10 and release-checklist items.

## Sources

- The VST 3 Developer Portal as published on 22 October 2026 (read from the
  `gh-pages` branch of
  [steinbergmedia/vst3_dev_portal](https://github.com/steinbergmedia/vst3_dev_portal)):
  Technical Documentation, FAQ, Workflow Diagrams, Change History 3.0.0 to
  3.8.1, Versions 3.7.0 to 3.8.1, the tutorials on silence flags, advanced
  techniques and replacing VST2, and the usage guidelines.
- [vst3sdk](https://github.com/steinbergmedia/vst3sdk) at `3cdf9ca` (SDK 3.8.1
  build 84, as pinned in `.github/workflows/vst3.yml`): `pluginterfaces/vst/`,
  `public.sdk/source/vst/` and the validator's test suite.

## Method

Built the plug-in on Linux (Clang, Release, Ooura) and ran the SDK validator
with three Steinberg tests that exist in the SDK's test suite but are not
created by `validator.cpp`: Check Tail processing, Process Input Overwriting
and Parameter Function Name. All 52 tests passed on `main`. A small program
then drove `Processor` directly to see what the tail test had missed.

After the VST3-8 fixes (same day), Check Tail no longer passes without testing
anything, and fails on float FFT rounding residue: up to 1.9e-7 just after the
reported tail for ±1 noise (about −134 dB), against the test's absolute 1e-7
limit. The tail itself is not cut; `vst3.adapter` checks it with a 1e-6
margin.

## Findings

| # | Finding | Evidence | Outcome |
| --- | --- | --- | --- |
| 1 | `getTailSamples()` left out the 256-sample latency | Hosts use the tail to avoid cutting output (`ivstaudioprocessor.h`). Steinberg's Check Tail test requires silence `tail` samples after the input stops, measured at the plug-in's output. An empty instance at 44.1 kHz reported 0 and kept sounding for 256 samples. | VST3-8 |
| 2 | Non-integer sample rates are refused by `setupProcessing` | The validator logs ERROR for 12345.678 Hz (counted as information). Pull-up/pull-down rates fail the plug-in instead of playing silence with a reason. | VST3-9 (needs jrp's choice of tolerance) |
| 3 | Input silence flags were trusted | The validator's tail test sets input flags on buffers full of noise; the plug-in played silence, so that test passed without testing anything. Hosts must clear flagged buffers, so reading them costs nothing. | VST3-8 |
| 4 | The controller decodes the whole state to read the bypass flag | Up to 64 MB decoded a second time on the UI thread. | Kept: decoding is one linear pass, far cheaper than preparing the filter, and a full decode keeps the controller from showing the bypass flag of a state the processor refused as corrupt |
| 5 | The Bypass text did not convert back | Validator: `getParamValueByString ("Off") failed for the string returned by getParamStringByValue`. | VST3-8 |
| 6 | Subcategory `Fx\|Reverb`, while LV2 declares `lv2:FilterPlugin` | Hosts file plug-ins by the first subcategory. | jrp to choose |
| 7 | Factory class version carries the Git suffix | `PClassInfo2::version` is documented as Major.Minor.Subversion.Build; the suffix is deliberate ([versioning](../develop/versioning.md#identifying-a-build)). | jrp to choose |
| 8 | Controller `getState`/`setState` returned `kNotImplemented` | The persistence FAQ has hosts call both on every save and load. | VST3-8 |
| 9 | 64-bit bypass is rounded to float | The delayed dry path is float; about −144 dB. | Low priority, not scheduled |
| 10 | `setProcessing` clears all spectrum history | The SDK allows `setProcessing` on the audio thread for light work such as clearing delay lines. | PERF-3 measures it |

Not a code finding: Steinberg's [usage guidelines](https://steinbergmedia.github.io/vst3_dev_portal/pages/VST+3+Licensing/Usage+guidelines.html)
ask anyone who uses the word "VST" to add ® on first use, the sentence "VST is
a registered trademark of Steinberg Media Technologies GmbH." and the VST
Compatible logo. The repository had none of these; added to the PLUG-5
release checklist.

Checked and found to follow the documentation: call order and threads,
constant latency, bypass (sample-accurate, ramped, latency-aligned, saved),
bus-arrangement refusal (the documented third option), flush calls, 32- and
64-bit processing, `IProcessContextRequirements` (asks for nothing), state and
preset handling, real-time rules, untouched input buffers, `moduleinfo.json`.

## Features

The list under "VST3 SDK features not yet used" in [TODO](../../TODO.md) was
complete for a convolver. Two cheap additions came out of the audit: marking
silent output once the tail has decayed, as the Audio Unit does (VST3-10), and
the host's track name (`IInfoListener`) in the Convolver log (a DIAG-1
follow-up). Program lists (VST3-2) remain the most valuable larger feature.
