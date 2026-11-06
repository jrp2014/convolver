<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edges over the leading convolution plug-ins

Status: written on 6 November 2026 against Convolver `f6895f3` (0.10.x), at
jrp's request: "Make it a goal of this project to be superior to the existing
most popular reverb plug-ins. Identify those edges and move towards them."
Nothing here is implemented. Optional features wait until the first
independent walkthrough (REL-3, [decided](../decisions.md#plug-in-behaviour)),
so this plan names the edges, ranks them and turns them into TODO items
(EDGE-1 to EDGE-9, with PERF-2 and PERF-3) to take up afterwards in the
order given here, unless jrp [decides otherwise](#decisions-for-jrp). Revised
the same day after jrp's reply: accuracy beyond what can be heard is not a
priority, long reverbs matter more than expected, and faster convolution and
more precise lag are worth investigating once the framework is more stable.

For jrp, deciding which edges to pursue and in what order, and for whoever
then takes up one of the EDGE items.

**In short:**

- **Which plug-ins.** Most of the most popular reverb plug-ins are
  algorithmic (13 of the 15 in LANDR's 2026 list); they use no impulse
  response, so Convolver cannot meet them on their own ground. This plan
  reads jrp's goal as the reverbs that play impulse responses (Altiverb,
  LiquidSonics Reverberate and Seventh Heaven, Logic's Space Designer, Waves
  IR) and the convolvers Convolver's room-correction users choose between
  (Hang Loose Convolver, CamillaDSP, X-MCFX), leaving algorithmic reverb
  out, as jrp [chose](../decisions.md#direction).
- **Where Convolver can lead.** Not in libraries of sampled spaces or in
  algorithmic sound design, nor in precision beyond what can be heard, but
  in long reverbs at low CPU and low latency, output that is correct and
  checkable, failing safely, explaining itself, free source on every
  platform, and the `.cfg` files of Roon, JRiver and Audiolense; and, after
  the work below, sample-rate changes, more precise delays, showing what a
  filter does and common reverb file layouts.
- **Edges Convolver already has** (keep and state them): output tested
  against an independent double-precision reference, which catches the
  faults that can be heard (wrong routing, a misplaced block, a lost tail);
  silence rather
  than unfiltered audio when a first load fails (at the default 100% Mix,
  and always for a crossover, which has no dry signal); a Status and log that
  say why it is silent; free GPL source for macOS, Windows and Linux in
  VST3, Audio Unit and LV2, plus a command-line tool and a library (nothing
  is published yet, and the first preview is macOS-only).
- **The ranked edges to build** (each a TODO item, after REL-3): long
  reverbs at low CPU, with lower and finer latency (measured in PERF-3, then
  PERF-2's non-uniform partitions), filters for every sample rate in one
  plug-in state (EDGE-1), delays finer than a sample (EDGE-9), showing a
  filter's response (EDGE-2), reverb file layouts such as
  true stereo without writing a `.cfg` (EDGE-3), level-matched comparison of
  two filters (EDGE-4), published workloads and evidence (EDGE-5), a reverb's pre-delay, start, length and fades (EDGE-6), decay and
  damping (EDGE-7) and immersive layouts beyond 8 channels (EDGE-8).
- **What every edge keeps.** The rules in [non-negotiables](#non-negotiables)
  apply to each item: the oracle tests, no allocation or locks on the audio
  thread, failing safe, clear diagnostics, each platform's guidelines, `.cfg`
  compatibility, and jrp's decision on anything permanent.

Contents: [the products compared](#the-products-compared) ·
[edges Convolver already has](#edges-convolver-already-has) ·
[ranked edges to build](#ranked-edges-to-build) ·
[not pursued](#not-pursued) · [non-negotiables](#non-negotiables) ·
[decisions for jrp](#decisions-for-jrp) · [sources](#sources)

## The products compared

Popularity comes from LANDR's 2026 list of the best reverb plug-ins; the
room-correction convolvers are the ones Convolver's
[other programs](../../use/other-programs.md) page and its users' forums
name. The features come from each vendor's page or a review, as dated in
[sources](#sources), checked on 6 November 2026. Features not stated there
are left blank rather than guessed. The algorithmic reverbs that lead
LANDR's list (Valhalla VintageVerb first) use no impulse responses, so they
are not compared here, but they set what users expect of CPU, latency and
ease of use.

| Product | What it is | Platforms and formats | Channels | Latency | Impulse-response handling | Price and licence |
| --- | --- | --- | --- | --- | --- | --- |
| Altiverb 8 (Audio Ease) | Convolution reverb with a library of sampled spaces | macOS only (Windows planned); VST3, AU, AAX | Regular: mono and stereo; XL: up to 9.1.6, Dolby Atmos | | Decay, three-band damping, size, attack, pre-delay, positioner, EQ; an IR tool for making responses | €499, XL €849; iLok |
| Reverberate 3 (LiquidSonics) | Convolution reverb with modulation and layered "Fusion-IR" responses | Windows, macOS; VST2, VST3, AU, AAX | Stereo and true stereo | Reverberate 2: zero, or up to 8192 samples to save CPU | Re-decay 0.25–1.75×, two-crossover contour, crop, envelope, resampling stretch, EQ | US$99; iLok |
| Seventh Heaven (LiquidSonics) | Fusion-IR convolution reverb | | | | | US$69 (LANDR) |
| Space Designer (Apple; Logic Pro 10.2 help) | Convolution and synthesised-IR reverb | Logic Pro only | Mono to surround | 128 samples at the original rate; delays the dry signal to match | IR start, perceived-volume compensation, synthesised responses | With Logic Pro |
| Waves IR-1, IR-L, IR-360 (2005 news item) | Convolution reverbs | IR-360 then Pro Tools HD only | IR-360: mono or stereo to 5.0, 5.1 to 5.1 | | IR-1: start, early-reflection build-up, wet and dry gain; IR-L: reverb time, start, length, low-CPU mode, up to 96 kHz | Commercial |
| Hang Loose Convolver (Accurate Sound) | Room-correction convolver | Windows, macOS; standalone, VST3, AU | Stereo and 7.1 reviewed | Block 256 by default, up to 8192 | A `.cfg` listing rate, channels and per-path WAV files with level and delay; several filter sets switched in real time, with level matching; filters fitted to the playback rate | US$129 |
| CamillaDSP | Audio-device DSP engine, not a plug-in | Linux, macOS, Windows | Any, through mixers | Set by its chunk size (segmented convolution) | WAV, raw and text filters; `$samplerate$` in a filter's path picks a file per rate; resamples the signal, not the filter | Free, GPL-3.0 |
| X-MCFX (fork of mcfx_convolver) | Multichannel matrix convolver for ambisonics and arrays | Windows, macOS; VST2, VST3 | Up to 128 × 128 | | Whole matrix from one multichannel WAV | Free (GPL-3.0, per its repository) |

## Edges Convolver already has

These are true of `main` today; the work is to keep them and say so. An
edge here means Convolver does it and shows it, not that the others are
known to do worse: none of them publishes figures to compare.

| Edge | Evidence | Who it matters to |
| --- | --- | --- |
| **Correct, and checkable.** The engine's tests compare its output with a direct double-precision convolution computed independently of it, which catches audible faults: wrong routing, a misplaced block, a lost tail or delay. Precision beyond that is not the point: the float engine's worst error, 4.4e-7 of the output peak (−127 dB), cannot be heard, and any careful float convolver should reach a similar figure. | `native.engine` on 6 November 2026 (`f6895f3`, Linux x86-64, FFTW, Release): 4.4e-7 of the peak with the AVX2+FMA kernels, 3.8e-7 with the baseline kernels. Renders reproduce their samples ([decisions](../decisions.md)). | Room correction and crossovers, where a wrong filter is worse than no filter |
| **Fails safe.** A failed first load plays silence rather than unfiltered audio (at Mix below 100% the dry share still plays); a filter with different input and output counts, such as a crossover, has no dry signal, and bypass fades it to silence. | [Adapter contract](../../develop/plugin-contract.md), lifecycle tests (PLUG-7) | Crossovers: full-range audio never reaches a tweeter |
| **Explains itself.** A read-only Status parameter and the Convolver log say why audio is silent and what to do. | [Status and log](../../use/plugins.md#status-and-the-convolver-log), DIAG-1 to DIAG-4 | Anyone whose host shows no error |
| **Open and everywhere, in source.** Free, GPL; VST3, Audio Unit and LV2; macOS, Windows and Linux; command line, library and an Equalizer APO export; no copy protection. Nothing is published yet, and the first preview is macOS-only ([status](../status.md)). CamillaDSP and X-MCFX are free too; neither is a plug-in in all three formats. | [README](../../../README.md) | Linux users (none of the commercial products above run there), anyone without iLok |
| **Reads the files people have.** The `.cfg` format of Roon, JRiver and Audiolense (Hang Loose Convolver reads a `.cfg` too), with matrix routing, delays and fractional gains, up to 256 channels in the command line and 8 × 8 in the plug-ins. | [Configuration](../../use/configuration.md), [other programs](../../use/other-programs.md) | Room-correction users moving between players |

Two things are parity, not edges, and are kept: subnormal numbers are
flushed while processing, as is common practice (JUCE's
`ScopedNoDenormals` does the same), so a reverb tail fading to silence no
longer slows every callback (PERF-15: the longest callbacks with a 30 s
filter fell from 37–43 ms to 8–12 ms, though its replacement still misses
the 5.3 ms half-period budget on that machine,
[archived record](../../archive/todo/2026-11-06-done-entries.md)); and the
host is told the exact latency, with Mix delaying the dry signal to match
([settings](../../use/plugins.md#settings)), as Space Designer does.

## Ranked edges to build

Ranked by value to Convolver's users (room correction and crossovers, and
long reverbs, which matter more than first expected), how clearly the gap
shows in the comparison, and effort.
Effort: **S** about a pull request, **M** a few, **L** a design and several.
Nothing starts before REL-3 unless jrp lifts the freeze.

| Rank | Edge | The gap today | Effort | TODO | jrp decides first |
| --- | --- | --- | --- | --- | --- |
| 1 | **Long reverbs at low CPU, with lower and finer latency.** First measure long reverbs (10, 30 and 60 s; stereo and true stereo; 48 and 96 kHz) in PERF-3. Then, in PERF-2: non-uniform partitions (short blocks at the start for latency, ever longer blocks for the tail); spreading the long blocks' work evenly over callbacks, or onto a worker thread without locks, so no callback spikes; and a replacement that crossfades the start of the filter rather than running two whole engines. Latency can then be any value the partitions allow, not only a power of two, down to zero with a direct (time-domain) head. | Every block of a filter is processed on every callback: at 256 frames, a 30 s filter at 48 kHz is about 5,600 blocks, about 1.4 million complex multiply-adds and 12 MB of filter spectra per path per callback, and the multiply-accumulate dominates (the FFTs are a few per cent of the work). With a 30 s filter the longest 512-frame callbacks took 8–12 ms of a 10.7 ms period on a cloud machine (PERF-15), and a replacement misses its budget. Latency is a power of two from 64 to 4096 frames in VST3 and the Audio Unit (LV2 stays at 256); Reverberate 2 offers zero, Space Designer 128 samples. Published comparisons put non-uniform partitions about an order of magnitude ahead of uniform ones for filters this long; PERF-3 checks that on our machines first. | L | PERF-3, then PERF-2 | Whether "zero" means zero samples or one small block; whether a worker thread is acceptable |
| 2 | **A filter for every sample rate in one plug-in state.** Keep each entry of a configuration list in the state and play the one matching the host's rate; switch when the rate changes. | A list already picks the entry for the host's rate, but the plug-in keeps only that entry, so a project opened at another rate plays silence. Hang Loose Convolver loads the matching-rate filter when the rate changes, and CamillaDSP picks a file per rate; room-correction tools write one filter per rate. | M | EDGE-1 | The saved-state format (a new host state version) |
| 3 | **Delays finer than a sample.** An opt-in fractional part for each delay, built into the filter when it loads (a band-limited fractional delay), so playing costs nothing more. | `.cfg` delays are truncated to whole samples, a compatibility rule that stays. One sample at 48 kHz is about 7 mm of speaker distance, which matters when aligning a crossover's drivers. | S–M | EDGE-9 | How the finer delay is given (a setting or an option, never a change to `.cfg` syntax) |
| 4 | **Show what a filter does.** Magnitude, phase, group delay and the decay (energy-time) of each path, as data from `--inspect` and as a plot in the window. | The report gives rate, lengths, delay and peak, not the response. Room-correction users check filters in REW; reverb users expect to see the decay. | M (data S, window M) | EDGE-2 | The window's layout (an addition to the editor plan) |
| 5 | **Reverb file layouts without a `.cfg`.** Mono on stereo, true stereo as one four-channel file or two stereo files. | A sound file filters channel by channel only; true stereo needs a hand-written `.cfg`. Reverb responses are commonly distributed in these layouts. | S–M | EDGE-3 | The channel order assumed for four-channel files, and how the layout is chosen |
| 6 | **Compare two filters, level-matched.** Hold two filters and switch between them with the existing crossfade, at matched loudness. | Hang Loose Convolver switches filter sets in real time, with level matching. Convolver replaces one filter with another. | M | EDGE-4 | The saved state (two filters double its size against the 64 MiB limit), and the loudness measure used for matching |
| 7 | **Publish the evidence.** The supported workloads, long reverbs included, with CPU measured beside open convolvers (CamillaDSP, BruteFIR, mcfx) on the same machines; and a user page saying the output is correct, with a null test anyone can run with `--compare` against a shipped reference render. | The evidence exists only in test logs; none of the products above publishes CPU figures for stated workloads. | S (page), M (benchmarks) | EDGE-5 | Which claims the user guides make |
| 8 | **A reverb's pre-delay, start, length and fades**, computed when the filter loads, not on the audio thread. | Every reverb above has them; Convolver has delays in `.cfg` only. | M | EDGE-6 | Parameter IDs and how they are saved |
| 9 | **Decay and damping.** Shorten or lengthen the decay, per band, by reshaping the response when it loads. | Altiverb (decay, three-band damping) and Reverberate (re-decay, contour) have it. | L | EDGE-7 | Whether to do it at all, after EDGE-6 is used |
| 10 | **Immersive layouts beyond 8 channels** (7.1.4, 9.1.6, 16 × 16). | Plug-ins stop at 8 × 8 ([channel layouts](../../develop/plugin-identities.md#channel-layouts): VST3 buses up to 8 channels, revisit on request, and LV2's eight fixed layouts); Altiverb XL reaches 9.1.6 and X-MCFX 128 × 128. | M–L | EDGE-8 | New layouts and identities in every format |

Two edges already have open items and need no new one: a window that a
screen reader can use (VoiceOver, in VST3-11 and AU-6), which most
custom-drawn plug-in windows lack; and CLAP (HOST-2), on request.

Measured performance comes first for rank 1: PERF-3's measurements decide
the partition sizes, and the result must pass the same oracle tests at every
partition boundary, with the worst error reported, and keep everything
`.cfg` defines (routing, delays, tail, reset). Ranks 2, 5 and 6 are mostly
control-side work and keep the engine as it is; rank 3 changes only the
filter that is prepared.

## Not pursued

- **A library of sampled spaces or hardware reverbs.** It is what Altiverb and
  LiquidSonics sell; it needs recordings and licences, not code. Convolver
  plays any response the user has.
- **Algorithmic reverb, modulation and layered responses.** They change the
  sound away from the measured response, which is the opposite of what room
  correction needs. Reverb users who want them have Valhalla, FabFilter and
  Reverberate.
- **Copying any product's look or design.** Convolver's window keeps its own
  native design ([editor plan](2026-10-24-plugin-editor.md)).
- **AAX (Pro Tools).** Not proposed: it would need a licence assessment
  against Avid's SDK and signing terms first.
- **Double-precision processing.** Not pursued (jrp: accuracy is not a
  priority where differences cannot be heard). The float engine's worst
  error, −127 dB relative to the peak, is far below audibility, and double
  precision would roughly double the memory and the multiply-accumulate
  cost, which works against long reverbs.

## Non-negotiables

Every EDGE item, and PERF-2, keeps these; a change that cannot is not done.

- **Exact audio.** The direct double-precision oracle tests pass, with the
  worst error reported ([workflow](../../develop/workflow.md#definition-of-done)).
  Shaping a reverb's response (EDGE-6, EDGE-7) is checked against the shaped
  response computed independently, and with every control at its neutral
  value the output is the same as without the feature.
- **Real-time safety.** Nothing new on the audio thread allocates, locks or
  does I/O; reshaping and re-preparing happen when the filter loads.
- **Fails safe.** A refused or failed change keeps the running filter or plays
  silence, as the [contract](../../develop/plugin-contract.md) says.
- **Clear diagnostics.** Every refusal names the problem, the cause and the
  fix, in the Status, the log and the command line.
- **Platform guidelines.** Each format follows Apple's Audio Unit, Steinberg's
  VST3 and LV2's own rules; the window stays native and accessible.
- **Compatibility.** `.cfg` syntax and files from released builds keep working;
  a new saved-state version is read by the builds that write it and refused
  clearly by older ones.
- **jrp decides** anything permanent or user-facing: state formats,
  parameter IDs, `.cfg` syntax, defaults that change the sound.

## Decisions for jrp

1. **Which plug-ins does the goal mean?** Answered: the reverbs that play
   impulse responses and the room-correction convolvers, leaving algorithmic
   reverb out ([decisions](../decisions.md#direction)).
2. **Adopt this order?** The ranking puts long reverbs and latency (rank 1)
   and the room-correction gains (ranks 2 to 4) before reverb shaping (ranks
   8, 9). It also weighs the standing direction to favour stability and
   longevity over features ([decisions](../decisions.md#direction)): each
   item keeps the [non-negotiables](#non-negotiables), and the reverb-shaping
   items wait for users to ask.
3. **Does the freeze hold for all of it?** EDGE-5's user page states only
   what is already true and could be written before REL-3; everything else
   waits.
4. Each item's own question, listed in its row above, when it starts.

## Sources

Checked on 6 November 2026.

- Altiverb 8: [Sound On Sound news](https://www.soundonsound.com/news/audio-ease-release-altiverb-8-reverb-plug),
  [manual](https://audioease.com/altiverb/files/Altiverb-8-manual.pdf)
- Reverberate 3: [LiquidSonics](https://www.liquidsonics.com/software/reverberate/)
- Popularity and prices: [LANDR, the 15 best reverb plugins in 2026](https://blog.landr.com/best-reverb-plugins)
- Space Designer: [Logic Pro 10.2 effects help](https://help.apple.com/logicpro-effects/mac/10.2/en.lproj/lgcee0be400c.html)
- Waves IR series: [Sound On Sound news, 17 January 2005](https://www.soundonsound.com/news/waves-release-three-new-convolution-reverb-plug-ins)
- Hang Loose Convolver: [Archimago's review, 12 November 2022](https://archimago.blogspot.com/2022/11/accurate-sounds-hang-loose-convolver.html)
- CamillaDSP: [documentation, v3.0](https://henquist.github.io/3.0.x)
- X-MCFX: [readme](https://www.angelofarina.it/Public/X-MCFX_convolver/X-MCFX-readme.txt),
  [repository](https://github.com/JB-Luke/X-MCFX)
