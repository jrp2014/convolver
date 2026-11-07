<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edges over the leading convolution plug-ins

Status: written on 6 November 2026 against Convolver `f6895f3` (0.10.x), at
jrp's request: "Make it a goal of this project to be superior to the existing
most popular reverb plug-ins. Identify those edges and move towards them."
Nothing here is implemented. Optional features wait until the first
independent walkthrough (REL-3, [decided](../decisions.md#plug-in-behaviour)),
and platform- and chip-level micro-optimisation waits for the walkthroughs
(jrp, 7 November 2026). This plan names the edges, ranks them and turns them
into TODO items (EDGE-1 to EDGE-14, with PERF-2, PERF-3 and IO-2) to take
up afterwards in the order given here, unless jrp
[decides otherwise](#decisions-for-jrp). Last revised on 7 November 2026
after jrp's review of `ecd087a`; his decisions along the way are in
[decisions](../decisions.md#direction), and the earlier versions in git.
Rationale and measurements live here; TODO holds each item's acceptance
criteria.

For jrp, deciding which edges to pursue and in what order, and for whoever
then takes up one of the EDGE items.

**In short:**

- **Which plug-ins.** Most of the recommended reverb plug-ins are
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
- **What users complain about** ([limitations users cite](#limitations-users-cite)).
  CPU spikes and dropouts with long responses at small host buffers; a
  filter recommended for every sample rate, with resampled filters that
  changed level; memory per instance; strict true-stereo file rules;
  room-correction delay that breaks lip sync; and bounces that drop out with
  threaded convolvers. jrp's rule: remove the limitations users cite first,
  once an item passes the [gate](#before-an-item-starts): a reproducible
  Convolver limitation, an affected intended workflow and a measurable
  acceptance criterion.
- **The ranked edges to build** (each a TODO item, after REL-3). First
  those that answer a complaint: a filter for every sample rate in one
  plug-in state, with opt-in resampling (EDGE-1, IO-2); savings in today's
  engine, measured first (PERF-3, EDGE-11); long reverbs at low CPU with
  lower and finer latency (PERF-2, with EDGE-10); true-stereo and other file
  layouts without a `.cfg` (EDGE-3); and an opt-in report of a filter's own
  delay, which answers the lip-sync complaint in part (EDGE-13). Then edges
  nobody was found citing: filters larger than the saved-state limit that
  Convolver itself sets (EDGE-12), and the easy room-correction wins,
  delays finer than a sample (EDGE-9), showing a filter's response (EDGE-2),
  level-matched comparison (EDGE-4) and published evidence (EDGE-5). Then
  the reverb features: pre-delay, start, length and fades (EDGE-6), decay
  and damping (EDGE-7) and layouts beyond 8 channels (EDGE-8).
- **What every edge keeps.** The rules in [non-negotiables](#non-negotiables)
  apply to each item: the oracle tests, no allocation or locks on the audio
  thread, failing safe, clear diagnostics, each platform's guidelines, `.cfg`
  compatibility, and jrp's decision on anything permanent.

Contents: [the products compared](#the-products-compared) ·
[edges Convolver already has](#edges-convolver-already-has) ·
[limitations users cite](#limitations-users-cite) ·
[before an item starts](#before-an-item-starts) ·
[ranked edges to build](#ranked-edges-to-build) ·
[automatic gain](#automatic-gain) ·
[other convolvers and algorithms](#other-convolvers-and-algorithms) ·
[sound-aware optimisation](#sound-aware-and-platform-aware-optimisation) ·
[not pursued](#not-pursued) · [non-negotiables](#non-negotiables) ·
[decisions for jrp](#decisions-for-jrp) · [sources](#sources)

## The products compared

These are representative products, not a market-share ranking: the
reverbs come from LANDR's 2026 list of recommended reverb plug-ins (an
editorial list), and the room-correction convolvers are the ones Convolver's
[other programs](../../use/other-programs.md) page and its users' forums
name. The features come from each vendor's page or a review, as dated in
[sources](#sources), checked on 6 or 7 November 2026. Rows from a review or
news item describe the product at that date, not necessarily now (Waves,
2005; Hang Loose Convolver, 2022; Reverberate's latency, from Reverberate
2). Features not stated there are left blank rather than guessed. The algorithmic reverbs that lead
LANDR's list (Valhalla VintageVerb first) use no impulse responses, so they
are not compared here, but they set what users expect of CPU, latency and
ease of use.

| Product | What it is | Platforms and formats | Channels | Latency | Impulse-response handling | Price and licence |
| --- | --- | --- | --- | --- | --- | --- |
| Altiverb 8 (Audio Ease) | Convolution reverb with a library of sampled spaces | macOS and Windows (Windows since 8.2.3, 7 April 2025); VST3, AU (macOS), AAX | Regular: mono and stereo, up to 96 kHz; XL: up to 9.1.6, Dolby Atmos, up to 384 kHz | | Decay, three-band damping, size, attack, pre-delay, positioner, EQ; an IR tool for making responses | €499, XL €849; iLok |
| Reverberate 3 (LiquidSonics) | Convolution reverb with modulation and layered "Fusion-IR" responses | Windows, macOS; VST2, VST3, AU, AAX | Stereo and true stereo | Reverberate 2: zero, or up to 8192 samples to save CPU | Re-decay 0.25–1.75×, two-crossover contour, crop, envelope, resampling stretch, EQ | US$99; iLok |
| Seventh Heaven (LiquidSonics) | Fusion-IR convolution reverb | | | | | US$69 (LANDR) |
| Space Designer (Apple; Logic Pro 10.2 help) | Convolution and synthesised-IR reverb | Logic Pro only | Mono to surround | 128 samples at the original rate; delays the dry signal to match | IR start, perceived-volume compensation, synthesised responses | With Logic Pro |
| Waves IR-1, IR-L, IR-360 (2005 news item) | Convolution reverbs | IR-360 then Pro Tools HD only | IR-360: mono or stereo to 5.0, 5.1 to 5.1 | | IR-1: start, early-reflection build-up, wet and dry gain; IR-L: reverb time, start, length, low-CPU mode, up to 96 kHz | Commercial |
| Hang Loose Convolver (Accurate Sound) | Room-correction convolver | Windows, macOS; standalone, VST3, AU | Stereo and 7.1 reviewed | Block 256 by default, up to 8192 | A `.cfg` listing rate, channels and per-path WAV files with level and delay; several filter sets switched in real time, with level matching; filters fitted to the playback rate | US$129 |
| CamillaDSP | Audio-device DSP engine, not a plug-in | Linux, macOS, Windows | Any, through mixers | Set by its chunk size (segmented convolution) | WAV, raw and text filters; `$samplerate$` in a filter's path picks a file per rate; resamples the signal, not the filter | Free, GPL-3.0 |
| KlangFalter (HiFi-LoFi; checked 7 November 2026) | Open-source convolution reverb, dormant since 2019 | macOS AU, VST2, VST3; Linux via DISTRHO Ports | Mono, stereo, true stereo | Zero (two-stage partitions, tail on a thread that the audio thread waits for) | Pre-delay, start, end, attack and decay envelope, reverse, stretch, two-band EQ, energy auto gain (on by default) | Free, GPL-3.0 |
| REEV-R (tiagolr; checked 7 November 2026) | Open-source convolution reverb with rhythmic modulation | Windows, Linux, macOS; VST3, LV2, AU | Stereo, true stereo | Zero (KlangFalter's engine) | Stretch, trim, reverse, attack, decay, EQ and per-frequency decay; energy normalisation always on, never boosting; replacement warmed with recent input | Free, GPL-3.0 |
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

## Limitations users cite

jrp's rule (7 November 2026): where Convolver can remove a limitation that
users cite, that comes first. The complaints below come from public forums
and vendors' own help pages, gathered on 7 November 2026. They are
anecdotes, not a survey; some are years old, each concerns the product
named, and the JUCE and HISE threads come from plug-in developers rather
than end users. Each is matched with what Convolver does today and the ranked edge
that could answer it. These are reported problems, not evidence that a
solution deserves priority: that takes the [gate](#before-an-item-starts).

| What users cite | Where (when) | Convolver today | Candidate edge |
| --- | --- | --- | --- |
| CPU spikes or dropouts with long responses at small host buffers; only one or two instances before the host overloads | Ableton's Hybrid Reverb FAQ advises a larger buffer or a shorter decay; JUCE forum: uniform partitions are "not really suited" to responses over a second (April 2018), and tiny host buffers force frequent transforms (August 2018); Vienna Suite users: "Two instances MAX" (undated); HISE forum dropouts (2021–22) | Uniform partitions. With a host callback smaller than the Latency block, one callback in every few does all the work. A 30 s filter's longest 512-frame callbacks took 8–12 ms of a 10.7 ms period (PERF-15), and silent input costs as much as music | Ranks 2 and 3 |
| Zero latency costs a lot of CPU; a Hang Loose Convolver user asked to turn its zero-latency filter switching off to save CPU | The Gear Forum (January 2023); AV NIRVANA (1 November 2023) | Latency is a setting from 64 to 4096 frames, so users already trade latency for CPU; zero latency is not offered | Rank 3 |
| Bounces drop out with convolvers that use a background thread, which a faster-than-real-time render outruns | HISE forum: Cubase Direct Offline Processing and Pro Tools (2021–22) | No thread on the audio path, so offline renders are exact | Rank 3's [scheduling rule](#3-long-reverbs-at-low-cpu-and-latency) for any worker thread |
| Roon recommends a filter for every sample rate, and users lack some rates; resampled responses changed level (up to +9.6 dB at 192 kHz in JUCE's convolution), and one user's upsampled filters clipped; pops when the rate changes | Roon's guidance and users (October 2021); JUCE forum (March 2021 to June 2022); Audirvana with the Audiolense and Acourate convolvers (undated) | A configuration list plays the entry for the host's rate, but a plug-in keeps only that entry; a missing rate plays silence with a clear Status; nothing is resampled | Rank 1 |
| Memory per instance: hundreds of megabytes each for Steinberg's REVerence | Steinberg forum (February–March 2011) | Each path holds its own filter spectra, even when two paths hold the same response; a filter over the 64 MiB saved-state limit is refused | Rank 2 (identical responses held once) |
| True-stereo files must be two stereo files of equal length named Left and Right; users are unsure how to give separate left and right filters | Ableton's Hybrid Reverb FAQ; Roon forum, where the guide is "woefully inadequate" for newcomers (April 2024) | True stereo needs a hand-written `.cfg` | Rank 4 |
| Long linear-phase room-correction filters delay the sound far beyond lip sync: about 0.7 s for 65,536 symmetric taps at 48 kHz | WiiM forum (5 April 2024) and miniDSP forum (undated), both about players and streamers | Reports the engine's latency only, not the filter's own delay | Rank 5, in part: it helps hosts that compensate plug-in latency, not players |
| A convolver says nothing about what went wrong, and `.cfg` variations need editing by hand | AV NIRVANA, a user of Hang Loose Convolver (1 November 2023; another user said it does log errors) | The Status and log explain each refusal; `.cfg` files are read as Roon, JRiver and Audiolense write them | Already an [edge](#edges-convolver-already-has) |
| The tap count shown is not the filter's | Roon forum (May 2023 to September 2024) | `--inspect` and the window report the rate, lengths, delay and peak of what is loaded | Already an edge; EDGE-2 adds the response |

This search found nobody citing the lack of delays finer than a sample, a
response display or level-matched comparison, nor the saved-state limit,
which is Convolver's own. Those edges stay, ranked below the ones that
answer a complaint.

## Before an item starts

The complaints above show where to look. They do not show how common a
problem is among Convolver's users: an old complaint about another product
is a lead, not evidence that a fix deserves priority. So every EDGE item,
and PERF-2, starts only when it passes this gate (jrp, 7 November 2026),
besides waiting for REL-3 under the freeze:

1. **A reproducible Convolver limitation.** Shown with Convolver itself, by
   a test, a benchmark or a recorded host session, not only by a complaint
   about another product.
2. **An affected intended workflow.** The user journey it blocks or
   degrades, such as room correction in a player that hosts plug-ins,
   mixing with a long reverb in a DAW, or a bounce.
3. **A measurable acceptance criterion.** In the item's TODO entry, checked
   by an automated test or a stated measurement.

An item that does not pass stays listed and does not start. This keeps the
roadmap from turning into another long round of speculative hardening.

## Ranked edges to build

Ranked by jrp's priorities. First the edges that answer a limitation users
cite ([above](#limitations-users-cite)) (ranks 1 to 5); then edges nobody
was found citing: the saved-state limit Convolver itself sets on long
reverbs (rank 6) and the easy room-correction wins (ranks 7 to 10); then
the reverb features (ranks 11 to 13). Within each group, by value to users,
how clearly the gap shows and effort. The rank is an order of work once an
item passes the [gate](#before-an-item-starts), not a promise to build it.
Effort: **S** about a pull request, **M** a few, **L** a design and several.
Nothing starts before REL-3 unless jrp lifts the freeze
([decisions for jrp](#decisions-for-jrp)); platform- and chip-level
micro-optimisation waits for the walkthroughs (jrp, 7 November 2026). Each
row links to its section, which holds the gap, the approach and jrp's
question; TODO holds the acceptance criteria.

| Rank | Edge | Answers a complaint | Effort | TODO |
| --- | --- | --- | --- | --- |
| 1 | [A filter for every sample rate](#1-a-filter-for-every-sample-rate) | Yes | M | EDGE-1, IO-2 |
| 2 | [Savings in today's engine](#2-savings-in-todays-engine) | Yes | S–M each | PERF-3, EDGE-11 |
| 3 | [Long reverbs at low CPU and latency](#3-long-reverbs-at-low-cpu-and-latency) | Yes | L | PERF-2, EDGE-10 |
| 4 | [Reverb file layouts without a `.cfg`](#4-reverb-file-layouts-without-a-cfg) | Yes | S–M | EDGE-3 |
| 5 | [Report a filter's own delay](#5-report-a-filters-own-delay) | In part | S–M | EDGE-13 |
| 6 | [Filters larger than the saved-state limit](#6-filters-larger-than-the-saved-state-limit) | No | M | EDGE-12 |
| 7 | [Delays finer than a sample](#7-delays-finer-than-a-sample) | No | S–M | EDGE-9 |
| 8 | [Show what a filter does](#8-show-what-a-filter-does) | No | M | EDGE-2 |
| 9 | [Compare two filters, level-matched](#9-compare-two-filters-level-matched) | No | M | EDGE-4 |
| 10 | [Publish the evidence](#10-publish-the-evidence) | No | S–M | EDGE-5 |
| 11 | [Pre-delay, start, length and fades](#11-pre-delay-start-length-and-fades) | No | M | EDGE-6 |
| 12 | [Decay and damping](#12-decay-and-damping) | No | L | EDGE-7 |
| 13 | [Layouts beyond 8 channels](#13-layouts-beyond-8-channels) | No | M–L | EDGE-8 |

Two edges already have open items and need no new one: a window that a
screen reader can use (VoiceOver, in VST3-11 and AU-6), which most
custom-drawn plug-in windows lack; and CLAP (HOST-2), on request.

### 1. A filter for every sample rate

**Approach.** Keep each entry of a configuration list in the plug-in state
and play the one matching the host's rate; switch when the rate changes,
with the usual crossfade so there is no pop. For a rate with no filter, an
opt-in resampling of the filter when it loads (IO-2), with its level
preserved and its error stated, instead of silence.

**Gap.** A list already picks the entry for the host's rate, but the
plug-in keeps only that entry, so a project opened at another rate plays
silence. Hang Loose Convolver loads the matching-rate filter when the rate
changes, and CamillaDSP picks a file per rate; room-correction tools write
one filter per rate. Users who lack a rate resample the filter themselves,
and the level faults reported for JUCE's resampled responses are what IO-2
must avoid. Mostly control-side work: the engine stays as it is, and
resampling changes only the filter that is prepared.

**jrp decides first.** The saved-state format (a new host state version),
with all the rates together inside the [saved-state
limit](#6-filters-larger-than-the-saved-state-limit); whether Convolver
resamples at all (today it never does; refusal stays the default).

### 2. Savings in today's engine

**Approach.** The first six candidates in the [experiments
table](#sound-aware-and-platform-aware-optimisation), four exact and two
rounding-only, each measured on PERF-3's workloads and kept only for the
gain stated there; sharing identical spectra first.

**Gap.** With a 64-frame host callback and the default 256-frame block,
every fourth callback does all the work, and that peak is what hosts and
users see. Silent input costs as much as music. A leading silence or zero
padding is multiplied like any other block. A mono reverb on both channels,
or one correction on left and right, is stored twice. True stereo reads
each input history twice and inverts four transforms for two outputs. All
of these are arithmetic estimates until PERF-3 measures them.

**jrp decides first.** Only the gains needed ([decision
4](#decisions-for-jrp)): the exact changes keep the output bit-identical,
and the rounding-only ones stay within the kernels' divergence bound.

### 3. Long reverbs at low CPU and latency

**Approach.** First measure long reverbs (10, 30 and 60 s; stereo and true
stereo; 48 and 96 kHz) in PERF-3, and take rank 2's savings. Then, in
PERF-2: non-uniform partitions (short blocks at the start for latency, ever
longer blocks for the tail); spreading the long blocks' work evenly over
callbacks, or onto a worker thread without locks, so no callback spikes;
and a cheaper replacement than running two whole engines together, for
example crossfading only the start of the filter, once what happens to the
old filter's sounding tail is specified (left to ring out, which keeps its
tail work running until it decays, or faded, which cuts it). Latency can
then be any value the partitions allow, not only a power of two, down to
zero with a direct (time-domain) head. The result passes the oracle tests
at every partition boundary, with the worst error reported, and keeps
everything `.cfg` defines (routing, delays, tail, reset).

**Scheduling rule for work off the calling thread.** It must be correct
under every way a host may call the plug-in: VST3's realtime, prefetch and
offline process modes, including a switch between them without another
`setupProcessing` call ([Steinberg's process
modes](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/namespaceSteinberg_1_1Vst.html#details)),
the Audio Unit's offline render and LV2 hosts' free-wheeling. Prefetch and
offline calls can come faster than real time, so a worker's results must be
ready when their samples are needed, never assumed from elapsed wall-clock
time. Any call, flagged or not, may come faster than real time (REAPER's
anticipative FX processing does so without a flag), so a mode flag may
tune scheduling but never ensures correctness. Otherwise bounces
drop out, as reported on the HISE forum. Today's engine is synchronous, so
this is a requirement on a future design, not a defect.

**Gap.** Every block of a filter is processed for every engine block: with
256-frame engine blocks, a 30 s filter at 48 kHz is about 5,600 blocks, so
each engine block costs about 1.4 million complex multiply-adds and reads
12 MB of filter spectra per path (a 512-frame host callback runs two engine
blocks); the multiply-accumulate dominates (the FFTs were a few per cent of
the work in the October FFTW measurements). These are arithmetic estimates.
With a 30 s filter the longest 512-frame callbacks took 8–12 ms of a
10.7 ms period on a cloud machine (PERF-15), and a replacement misses its
budget. Latency is a power of two from 64 to 4096 frames in VST3 and the
Audio Unit (LV2 stays at 256); Reverberate 2 offers zero, Space Designer
128 samples. That non-uniform partitions would cut this by about an order
of magnitude for filters this long is a hypothesis from the general
literature, not an established result here; PERF-3's measurements and a
prototype in the benchmark harness test it.

**Experiment (7 November 2026).** A prototype on the PERF-2 branch
([measurements](../../develop/fft-measurements.md#long-reverbs-and-zero-latency))
answers the hypothesis on a cloud VM: non-uniform partitions (blocks of 64,
256, 1024 and 4096 frames, then 8192 for the rest), with a direct sum of the
first 64 taps for zero latency, took about 15 to 35 times less CPU than
today's engine at Latency 256 for 30 and 60 s stereo reverbs (a 60 s filter:
2.7–4.8% of a core against 90–136%), passed the double-precision oracle
(worst −131 dB), and cost no more at zero latency than at 256. Spreading
each large block's work over the callbacks of its slack, on the calling
thread, kept the worst 64-frame callback near 0.2 ms; without spreading, or
with KlangFalter's inline tail, single callbacks took 3 to 8 ms. A worker
thread cost two to three times the CPU and did not help, so the
recommendation is no thread: the spread schedule is synchronous, so it is
correct in realtime, prefetch and offline processing without the
scheduling rule's machinery.

**Other convolvers and algorithms.** The design combines what WDL (Cockos),
LSP and zita-convolver each do, and no algorithm beyond partitioned
convolution beats it with exact output; block sizes still need tuning on
each platform ([review](#other-convolvers-and-algorithms)).

**Recommendation for usability.** Make zero latency the plug-ins' default
once this engine is in, as reverb users expect (KlangFalter and REEV-R are
zero-latency, and Reverberate offers zero): no delay to compensate, nothing for Mix to
align, and live monitoring works. Keep larger Latency values only as a
compatibility choice; they no longer save meaningful CPU. When a filter is
replaced, warm the new engine with the recent input on the preparation
thread before the crossfade (as REEV-R does, though on its audio thread),
so a long reverb's tail does not drop out.

**jrp decides first.** Zero latency as the default, and what the Latency
setting keeps; adopting non-uniform partitions in the engine (a DSP change
under the non-negotiables, with the oracle at every partition boundary);
what happens to the old tail on replacement (warm the new engine, or let
the old one ring out).

### 4. Reverb file layouts without a `.cfg`

**Approach.** Mono on stereo, and true stereo as one four-channel file or
two stereo files, load without a hand-written configuration.

**Gap.** A sound file filters channel by channel only; true stereo needs a
hand-written `.cfg`. Reverb responses are commonly distributed in these
layouts; Ableton's true stereo needs two stereo files of equal length named
Left and Right.

**jrp decides first.** The channel order assumed for four-channel files,
and how the layout is chosen.

### 5. Report a filter's own delay

**Approach.** An opt-in setting adds the filter's delay to the latency
reported to the host, as linear-phase equalisers report theirs, so a host
that compensates plug-in latency keeps picture and sound in sync. The delay
is, at first, either given by the user or a common linear-phase delay that
Convolver verifies across the paths concerned: the centre of symmetry of
each symmetric path, checked to be the same for all of them. A filter's
largest coefficient is not its delay: `[1, 0, 1]` has one sample of group
delay while its peaks are at samples 0 and 2, because a symmetric filter's
group delay is its centre of symmetry ([Smith, linear-phase
filters](https://www.dsprelated.com/freebooks/filters/Filters_Preserving_Phase.html)).
Proposed handling, for jrp to confirm:

- **Each path's delay is its whole delay:** its centre of symmetry plus the
  input and output delays the configuration gives it, because a `.cfg`
  delay moves the centre.
- **Symmetry is checked within a stated tolerance**, for example every
  coefficient pair equal to within 1e-6 of the path's peak; a path outside
  it is not linear-phase, and only a user-given delay applies.
- **Paths whose delays differ:** the option adds nothing and the Status says
  why, offering a user-given delay instead.
- **A fractional delay:** an even-length symmetric filter's centre lies
  half-way between two samples, and the plug-in formats report latency in
  whole samples, so the delay is rounded down and the half sample left over
  is stated in the report. The plan's own example (65,536 taps) is such a
  filter.

The wet convolution is unchanged. Mix and bypass delay the dry signal by
the reported latency, so a Mix between 0 and 100% sounds different with the
option on, and the dry path needs a buffer as long as the reported delay
(for example about 0.7 s per channel for 65,536 symmetric taps at 48 kHz;
an example, not a maximum) instead of one block. The latency is fixed for a
processor's life ([adapter contract](../../develop/plugin-contract.md)),
so a replacement filter with a different delay needs the host to restart
processing.

**Gap.** Long linear-phase room-correction filters delay the sound far
beyond lip sync. Convolver reports its engine latency only, so the host
cannot compensate for the rest. Players that do not host plug-ins do not
benefit, so this answers the lip-sync complaint only in part.

**jrp decides first.** Whether to offer it; user-given only or also
verified linear-phase; the proposed handling above; what a replacement with
a different delay does.

### 6. Filters larger than the saved-state limit

**Approach.** Let the plug-ins accept a long filter whose samples exceed
64 MiB, by a storage the owner chooses: for example the samples compressed
without loss, or a reference to the file with its hash and a clear refusal
when the file is missing or changed.

**Gap.** A plug-in refuses a filter it cannot save, and the saved state
holds the filter's samples, up to 64 MiB ([adapter
contract](../../develop/plugin-contract.md#host-state)). A 60 s
true-stereo filter at 96 kHz is four responses of 5.76 million float
samples, about 88 MiB: the command line renders it, but no plug-in accepts
it. PERF-3 therefore keeps core and command-line benchmarks apart from
supported plug-in workloads and records the encoded state size beside CPU
and memory. The same budget covers every rate kept by EDGE-1 and both
filters held by EDGE-4. Nobody was found citing this limit; it is
Convolver's own, and long reverbs reach it.

**jrp decides first.** The storage, and what a project carries when it
moves to another machine.

### 7. Delays finer than a sample

**Approach.** An opt-in fractional part for each delay, baked into the
prepared filter (a band-limited fractional delay), so no separate delay
stage runs while playing. Baking may lengthen the filter and so add blocks;
the added taps, processing cost and integer delay are measured, and the
supported bandwidth and approximation error stated.

**Gap.** `.cfg` delays are truncated to whole samples, a compatibility rule
that stays. One sample at 48 kHz is about 7 mm of speaker distance, which
matters when aligning a crossover's drivers.

**jrp decides first.** How the finer delay is given (a setting or an
option, never a change to `.cfg` syntax).

### 8. Show what a filter does

**Approach.** Magnitude, phase, group delay and the decay (energy-time) of
each path, as data from `--inspect` (S) and as a plot in the window (M).

**Gap.** The report gives rate, lengths, delay and peak, not the response.
Room-correction users check filters in REW; reverb users expect to see the
decay.

**jrp decides first.** The window's layout (an addition to the editor
plan).

### 9. Compare two filters, level-matched

**Approach.** Hold two filters and switch between them with the existing
crossfade, at matched loudness.

**Gap.** Hang Loose Convolver switches filter sets in real time, with level
matching. Convolver replaces one filter with another.

**jrp decides first.** The saved state (two filters together must fit the
saved-state limit), and the loudness measure used for matching.

### 10. Publish the evidence

**Approach.** The supported plug-in workloads (CPU, memory and saved-state
size), long reverbs included (from ranks 2 and 3), with CPU measured beside
open convolvers (CamillaDSP, BruteFIR, mcfx) on the same machines (M); and
a user page saying the output is correct, with a null test anyone can run
with `--compare` against a shipped reference render (S).

**Gap.** The evidence exists only in test logs; none of the products above
publishes CPU figures for stated workloads.

**jrp decides first.** Which claims the user guides make.

### 11. Pre-delay, start, length and fades

**Approach.** A reverb's pre-delay, start, length and fades, computed when
the filter loads, not on the audio thread.

**Gap.** Every reverb above has them; Convolver has delays in `.cfg` only.

**jrp decides first.** Parameter IDs and how they are saved.

### 12. Decay and damping

**Approach.** Shorten or lengthen the decay, per band, by reshaping the
response when it loads.

**Gap.** Altiverb (decay, three-band damping) and Reverberate (re-decay,
contour) have it.

**jrp decides first.** Whether to do it at all, after EDGE-6 is used.

### 13. Layouts beyond 8 channels

**Approach.** Immersive layouts such as 7.1.4, 9.1.6 and 16 × 16.

**Gap.** Plug-ins stop at 8 × 8 ([channel
layouts](../../develop/plugin-identities.md#channel-layouts): VST3 buses up
to 8 channels, revisit on request, and LV2's eight fixed layouts); Altiverb
XL reaches 9.1.6 and X-MCFX 128 × 128.

**jrp decides first.** Which layouts; existing identities and saved
sessions are preserved, with additional identities only where a separate
variant requires them (such as further fixed-layout LV2 variants).

## Automatic gain

jrp asked on 7 November 2026 what automatic gain would bring. Reverbs that
play impulse responses scale a loaded response so the wet signal has a
consistent level: KlangFalter (energy, on by default), REEV-R (energy,
always, never boosting), quantumVerb (peak, always), VSL's Hybrid Reverb
("Auto Gain"), JUCE's `dsp::Convolution` (energy) and Space Designer
(perceived-volume compensation). Convolver plays every filter at its own
level. [`auto_gain.py`](../../../tests/native/benchmarks/auto_gain.py)
measured, from the frequency response, 20 synthetic reverbs (RT60 0.4 to
12 s, bright and dark, with and without direct sound, peak-normalised as
responses are usually distributed) and three room-correction and crossover
filters ([output](../../benchmarks/2026-11-07-auto-gain.txt)):

| Method | Wet level spread across the reverbs (bright / pink / dark input) | Loudest wet vs dry | Room correction and crossovers |
| --- | --- | --- | --- |
| None (Convolver today) | 31.5 / 30.4 / 29.5 dB | +34 dB | Unchanged, as designed |
| Peak to 0.5 (quantumVerb) | 31.5 / 30.4 / 29.5 dB | +28 dB | −5 to +28 dB |
| Energy (KlangFalter, JUCE) | 1.8 / 4.6 / 6.9 dB | +4.6 dB | +1 to +17 dB |
| Energy, never boosting (REEV-R) | 1.8 / 4.6 / 6.9 dB | +4.6 dB | Unchanged |
| K-weighted loudness (BS.1770, pink noise) | 2.8 / 0.0 / 2.6 dB | 0 dB | +2 to +7 dB |

- **For reverbs the benefit is large.** A peak-normalised long reverb at
  100% Mix plays up to 34 dB louder than its input today, a risk to ears
  and speakers when it loads. Automatic gain brings every response to
  within a few dB, so switching responses, comparing them (EDGE-4) and
  setting Mix work as users expect.
- **Loudness beats energy.** Energy normalisation weights all frequencies
  up to half the sample rate equally, so dark reverbs end up 4 to 7 dB
  louder than bright ones, and the same response resampled to 96 kHz ends
  up 2.9 dB louder than at 48 kHz (half its band is empty, so its energy
  halves): a cause of the level changes users report after resampling.
  K-weighted loudness over 20 Hz to 20 kHz is the same at every rate.
- **For room correction and crossovers it is harmful.** These filters are
  designed with a calibrated level and headroom; any automatic gain moves
  that level (a crossover leg by up to +28 dB with peak normalisation) and
  can push the response above 0 dB, so it would clip.
- **Peak normalisation does not help**: it leaves the spread unchanged.

**Proposal (EDGE-14).** A Level setting with two values: "As designed"
(today's behaviour, and always for a `.cfg` configuration) and "Match
loudness" (K-weighted loudness of the wet signal matched to the dry signal,
computed when the filter loads, then a fixed gain). The default needs
jrp's decision: "As designed" keeps every existing project and room
correction unchanged; "Match loudness" is what reverb users expect. Either
way, a Status warning when a loaded filter plays more than 12 dB louder
than its input at 100% Mix, offering "Match loudness" or Gain, removes the
loud surprise without changing anyone's sound.

## Other convolvers and algorithms

jrp asked on 7 November 2026 for a review of other open-source convolvers and
of algorithms beyond partitioned convolution, before choosing an approach for
[rank 3](#3-long-reverbs-at-low-cpu-and-latency). Each engine below was read
from source at the commit given.

| Engine (commit, licence) | Used in | Partitions | Latency | Long blocks' work |
| --- | --- | --- | --- | --- |
| zita-convolver 4.0.3 ([GrandOrgue mirror](https://github.com/GrandOrgue/ZitaConvolver) `b60b4a3`, GPL-3.0) | jconvolver, Ardour's convolver, x42 convoLV2, Guitarix | Blocks from 64 to 8192 that double or quadruple, with the count per size chosen from a model of FFT and multiply-add cost | The host period at best; no direct head, so never zero | One thread per block size, with real-time priorities that fall as blocks grow. A late result is a gap in the output, flagged; a synchronous mode waits instead, for offline use |
| [BruteFIR](https://github.com/atorger/brutefir) 1.x (`051e7af`, ISC since 2025, GPL before) | Linux room correction | Uniform | Twice the block | Spread over several processes on several cores |
| [CamillaDSP](https://github.com/HEnquist/camilladsp) 3 (`05e9cfc`, GPL-3.0 or MPL-2.0) | Room correction on Linux, macOS, Windows | Uniform, one block per chunk | The chunk size | On the processing thread; 64-bit floats by default |
| PipeWire's filter-chain convolver ([`convolver.c`](https://github.com/PipeWire/pipewire), `46d9214`, MIT, adapted from FFTConvolver) | Linux desktop audio | Doubling stages from the block (64 to 256 by default) to the tail size (4096) | The block | Each stage runs whole at its block boundary; the thread code is there but switched off |
| JUCE `dsp::Convolution` ([JUCE](https://github.com/juce-framework/JUCE) `d60b7a7`, AGPL-3.0 or commercial) | Many JUCE plug-ins | Uniform by default; optionally a head and one tail stage | Zero by default (a transform per call), or a fixed latency | Inline |
| WDL convolution engine ([WDL](https://github.com/justinfrankel/WDL) `03c6e56`, zlib) | Cockos's own library (REAPER's maker) | A direct 64-tap head, then stages of three blocks each, the blocks growing fourfold (the code notes this was about 10% faster than doubling) up to 16384 | Zero | Each stage whole at its boundary, one thread |
| LSP `Convolver` ([lsp-dsp-units](https://github.com/lsp-plugins/lsp-dsp-units) `b7a2475`, LGPL-3.0) | LSP Impulse Reverb and Impulse Responses | A direct 128-tap head, doubling stages, then uniform tail blocks | Zero | The tail's multiply-adds are spread over the callbacks of each frame |
| OpenAL Soft convolution effect ([openal-soft](https://github.com/kcat/openal-soft) `2fc9933`, LGPL-2.0) | Games | Uniform 128-sample blocks, the first applied directly | Zero | Inline |
| SuperCollider `PartConv` ([supercollider](https://github.com/supercollider/supercollider) `97e4952`, GPL-3.0) | SuperCollider | Uniform | One block | Multiply-adds spread over the spare blocks |
| libHybridConv 0.1.1 (Christian Borss, 2009, GPL-2.0-or-later; in [Equalizer APO](https://github.com/TheFireKahuna/equalizerAPO64)) | Equalizer APO | One, two or three uniform stages | The first stage's block | Spread over steps; Equalizer APO uses one stage at the device period |
| [Steam Audio](https://github.com/ValveSoftware/steam-audio) (`480dd64`, Apache-2.0) | Games | Uniform | One block | Optionally on an AMD GPU (TrueAudio Next); a "hybrid" mode replaces the late tail with a parametric reverb |
| KlangFalter's FFTConvolver (above) | KlangFalter, REEV-R | A head and one tail stage | Zero | The tail on a thread that the audio thread waits for |

**What this shows.**

- **The experiment's design is the established one, not KlangFalter's.** A
  direct head for zero latency (WDL, LSP, OpenAL Soft), stages that grow
  (zita-convolver, WDL, PipeWire) and the large blocks' work spread over
  callbacks on the calling thread (LSP's tail, SuperCollider,
  libHybridConv) each appear in a maintained engine; the prototype combines
  them. KlangFalter's two stages and waiting tail thread are what it
  avoids.
- **Threads.** zita-convolver depends on Linux real-time priorities and
  plays a gap when a thread is late; PipeWire switched its thread off; JUCE
  and WDL use none. Battenberg and Avizienis (DAFx 2011) found a
  multi-threaded scheduler faster on a desktop, but the time-distributed one
  better suited to a plug-in inside a host. The prototype's measurement
  agrees for plug-ins, and the [scheduling
  rule](#3-long-reverbs-at-low-cpu-and-latency) would make any thread
  costly to get right.
- **Block sizes are tuned, not derived.** zita-convolver picks doubling or
  quadrupling from a cost model; WDL's comment says growing fourfold was
  about 10% faster than doubling; BruteFIR's manual says only trial and error finds the
  best partitioning on given hardware; Wefers and Vorländer (DAFx 2011)
  found no closed form and recommend benchmarking on the target. So the
  sizes (64, then fourfold to 8192 in the prototype) are measured again on
  each platform before they are fixed, Apple silicon first.
- **Smaller ideas already listed.** Holding identical responses once
  (zita-convolver links them) is in [sound-aware
  optimisation](#sound-aware-and-platform-aware-optimisation); per-rate file
  choice and resampling (PipeWire, JUCE, CamillaDSP) are rank 1. BruteFIR
  can stop if the output exceeds a set level; EDGE-14's warning covers the
  common case (a loud filter), so it is not proposed.

**Algorithms beyond partitioned convolution.**

| Algorithm | What it offers | Verdict for Convolver |
| --- | --- | --- |
| Optimal non-uniform partitions by search (Garcia, AES 2002; Wefers, 2015) | The cheapest sizes and counts for a filter length and latency | A refinement of the prototype: the tuning step above |
| Optimal FFT size other than twice the block (Wefers and Vorländer, DAFx 2011) | Up to 1.65 times faster for uniform partitions with long filters and short blocks | Needs sizes that are not powers of two; Ooura has none, and Accelerate only 3, 5 and 15 times a power of two. Not pursued: the non-uniform stages already make the tail cheap |
| A time-distributed FFT (Hurchalla, AES 2010) | Splits one large transform across several callbacks | The prototype spreads whole transforms and multiply-adds instead, and its worst 64-frame callback is already about 0.2 ms; adopt only if one transform becomes the worst callback |
| Fast direct-form FIR (Winograd, Toom-Cook) for the head | Fewer multiplies for short filters | The 64-tap head is a small share of the work; not pursued |
| Number-theoretic transforms | Exact integer convolution, no rounding | Needs integer input and wide words, has no platform libraries and is slower than a SIMD float FFT; float error is already about −131 dB. Not pursued, as double precision was not |
| GPU convolution (GPU Audio; Steam Audio; an [auralisation study](https://arxiv.org/abs/2509.04390), 2025) | Up to 60 times a CPU for 32 to 128 channels on a workstation GPU | Not portable (CUDA, one vendor), transfers add latency and a plug-in shares the GPU with the host; a 60 s stereo reverb already needs under 5% of a core. Not pursued |
| Models of the late tail: a feedback-delay-network tail after a convolved head (Steam Audio's hybrid mode; Carpentier and others, DAFx 2014), velvet-noise tails (Välimäki and others, 2017), modal or IIR fits, subband tails | Much less work for long tails | They change the sound away from the response, so they are algorithmic reverb, [not pursued](#not-pursued). A lower-rate tail, lower-precision storage and stopping at the audible end are already listed as bounded approximations for jrp to judge |

No algorithm found beats non-uniform partitions with a spread schedule while
keeping the output exact. What remains is tuning the block sizes on each
platform, and the exact savings in rank 2.

## Sound-aware and platform-aware optimisation

jrp's direction: take each platform's floating-point characteristics fully
into account, as the subnormal flushing did, and use what is known about
sound to make processing cheaper; that is the value a DSP project adds over a
general convolution library. Each candidate is an experiment with
permission to stop (jrp, 7 November 2026). It is measured on PERF-3's
workloads, the long reverbs included. It is kept only if it reaches the
gain stated for it, which is what justifies its maintenance cost. One that
falls short stops: its numbers go into the dated measurements and it is set
aside, with no further tuning round unless there is a new hypothesis. The
[platform-optimisation rules](../../develop/architecture.md#platform-optimisation)
apply to every candidate.

Order: sharing identical spectra and removing unnecessary work first (rank
2's exact savings), then the other rounding-only and exact changes; worker
threads, reduced-precision storage, lower-rate tails and the other bounded
approximations only after those have been measured.

The gains needed are proposals, set before measuring so that a result
cannot move them; jrp may change them. "p99" is the 99th-percentile
callback time on the PERF-3 workload the candidate targets, and "no
regression" means no worse than the run-to-run spread of repeated
measurements.

| Candidate | Kind | What it gains | Gain needed to keep it | What it costs or risks |
| --- | --- | --- | --- | --- |
| Hold identical responses once | Exact | Halves the spectra of a mono reverb on two channels or one correction on left and right, and their memory traffic | 25% less filter memory on such a workload, with no p99 regression | A lookup at preparation |
| Skip input history that is exactly silent, and go idle once silent input has outlasted the tail | Exact | Gaps, fades and silence between tracks cost almost nothing; the engine already skips history from before a reset (PERF-5) | 50% less CPU on silent input, with no p99 regression on music | Bookkeeping per block; must stay allocation-free |
| Skip all-zero filter blocks | Exact | Leading silence and zero padding cost nothing | 10% less CPU on a 30 s reverb with 0.5 s of leading silence | The same caveat as PERF-5 for an infinite spectrum |
| Read each input history once for all the paths that use it | Exact | About a quarter less memory traffic for true stereo (an estimate) | 10% lower p99 for true stereo | A fused multiply-accumulate kernel per path count |
| Do the older blocks' work in the callbacks between engine blocks when host callbacks are smaller than the Latency block | Rounding only | Lower callback peaks at small host buffers | 30% lower maximum callback time at 64-frame callbacks with the default block | A changed summation order within `native.kernel_divergence`'s bound; bookkeeping across callbacks |
| One inverse transform per output where outputs are fewer than paths | Rounding only | Two inverse transforms instead of four for true stereo; most for short filters and many channels | 10% lower p99 on a short true-stereo or matrix workload | A changed accumulation order within the divergence bound |
| FFT sizes each backend handles well: Accelerate and Ooura round 2B up to a power of two | Exact | Choosing B (and non-uniform block sizes) so that 2B is a power of two avoids nearly doubling the transform | A constraint on PERF-2, adopted with it | Constrains PERF-2's block sizes |
| Large memory pages for the filter spectra (huge pages on Linux, superpages on macOS, large pages on Windows, which need a privilege there) | Exact | Fewer address-translation misses over spectra of 10 MB and more | 10% lower p99 on a 30 s filter | Ordinary pages where unavailable |
| Floating-point environment checks on every platform: flush mode (done, PERF-15), contraction and fused multiply-add per compiler, MSVC's floating-point model, and denormal behaviour inside FFT libraries | Exact or rounding only | No hidden slow paths or output differences between platforms | A correctness check, not a gain: kept where it finds a difference | Tests per compiler and processor in CI |
| Parallelism where it is safe: filter preparation and the command line's offline render across paths and channels; on the audio thread only through each platform's real-time mechanism (an audio workgroup joined to the host's on macOS, MMCSS on Windows), never a general thread pool such as OpenMP's, and under rank 3's [scheduling rule](#3-long-reverbs-at-low-cpu-and-latency) | Exact | Faster loads of long filters and faster multichannel renders; on the audio thread, the tail's long blocks on a second core (with PERF-2) | Offline: 30% less wall-clock time for an 8-path load or render; on the audio thread, a supported workload that fails today fits its budget | OpenMP's runtime can block, allocate and wake threads late, so it suits only the offline paths, where jrp wants it used as long as it causes no cross-platform incompatibility: optional, MSVC's OpenMP 2.0 subset only, a serial build with byte-identical output where it is missing (Apple's compiler ships none); hosts already spread tracks across cores, so extra audio threads compete with them |
| Explicit platform kernels for Apple silicon and other AArch64 processors (NEON intrinsics or Accelerate's vDSP) | Rounding only | Already tried: the [24 October 2026 vDSP experiment](../../archive/reviews/2026-10-24-apple-vdsp.md) on an M5 Max found no substantial whole-engine gain and failed the divergence gates, and showed that the baseline kernels are already compiled to NEON with FMA. Revisit only with a new workload (such as long reverbs), hardware target or implementation hypothesis, measured against that experiment. Compiler options were measured on x86-64 only (PERF-4: `-O3` is already on, AVX2 alone helped); MSVC is unmeasured | 15% whole-engine gain within the divergence bound | A second kernel set to keep within `native.kernel_divergence`'s bound |
| Stop at a filter's audible end: blocks whose whole contribution is below a stated level (for example below the output's least significant bit) are not processed | Bounded approximation | Long reverb tails often hold seconds of decay far below audibility; cost falls with them | 25% lower p99 on a 30 s reverb, within jrp's bound | Changes the output below the bound; the bound and the report of what was cut are jrp's decision |
| Stop each late block at its audible bandwidth: high frequencies decay first, so a late block keeps only the frequency bins still above the stated level | Bounded approximation | Work and memory fall along the tail, beyond what the whole-block audible end cuts | 15% beyond the whole-block cut, within jrp's bound | The bound is jrp's decision; a bin range per block |
| Store a long filter's late blocks in 16-bit floating point (F16C on x86-64, FP16 on AArch64) | Bounded approximation | Halves the storage of the late filter spectra (about 12 MB per path at 30 s), and with it their share of memory traffic; the input history and outputs stay in 32-bit floats. The late tail is already tens of dB down, so the error may land below audibility | 20% lower p99 on a 30 s reverb, within jrp's bound | Not measured: the audible error, against the oracle and scaled to the tail's level, and the conversion cost on each processor |
| Process the late tail at a lower sample rate | Bounded approximation, research | A quarter-rate tail would cost about a quarter (an estimate) | 30% lower p99 on a 30 s reverb, within jrp's bound | Resampling filters add error and latency of their own; research first |

An exact candidate keeps the output the same; a rounding-only one stays
within the kernels' existing divergence bound; a bounded approximation
changes the output by a stated amount below audibility, and needs jrp's
decision on the bound, judged as the dither defaults were, against the
threshold of hearing at a stated playback level.

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
- **Larger blocks during offline renders.** The reported latency must stay
  fixed for the host's compensation, so the block size cannot change when a
  host bounces.
- **Double-precision processing.** Not pursued (jrp: accuracy is not a
  priority where differences cannot be heard). The float engine's worst
  error, −127 dB relative to the peak, is far below audibility, and double
  precision would roughly double the memory and the multiply-accumulate
  cost (an estimate), which works against long reverbs.

## Non-negotiables

Every EDGE item, and PERF-2, keeps these; a change that cannot is not done.

- **Exact audio.** The direct double-precision oracle tests pass, with the
  worst error reported ([workflow](../../develop/workflow.md#definition-of-done)).
  The only exception is a bounded approximation jrp has approved
  ([sound-aware optimisation](#sound-aware-and-platform-aware-optimisation)),
  tested against its stated bound.
  Shaping a reverb's response (EDGE-6, EDGE-7) is checked against the shaped
  response computed independently, and with every control at its neutral
  value the output is the same as without the feature.
- **Real-time safety.** Nothing new on the audio thread allocates, locks or
  does I/O; reshaping and re-preparing happen when the filter loads. Work
  moved off the calling thread is correct under realtime, prefetch and
  offline scheduling and the switches between them (rank 3's
  [scheduling rule](#3-long-reverbs-at-low-cpu-and-latency)).
- **Fails safe.** A refused or failed change keeps the running filter or plays
  silence, as the [contract](../../develop/plugin-contract.md) says.
- **Clear diagnostics.** Every refusal names the problem, the cause and the
  fix, in the Status, the log and the command line.
- **Platform guidelines.** Each format follows Apple's Audio Unit, Steinberg's
  VST3 and LV2's own rules; the window stays native and accessible.
- **Saved-state budget.** Whatever a plug-in holds, every rate (EDGE-1),
  both filters (EDGE-4) or a long reverb, fits the saved-state limit or is
  refused clearly; raising the limit is a storage decision for jrp.
- **Compatibility.** `.cfg` syntax and files from released builds keep working;
  a new saved-state version is read by the builds that write it and refused
  clearly by older ones.
- **jrp decides** anything permanent or user-facing: state formats,
  parameter IDs, `.cfg` syntax, defaults that change the sound.

## Decisions for jrp

1. **Which plug-ins does the goal mean?** Answered: the reverbs that play
   impulse responses and the room-correction convolvers, leaving algorithmic
   reverb out ([decisions](../decisions.md#direction)).
2. **Adopt this order?** Re-ranked on 7 November 2026 by jrp's rule that
   limitations users cite come first ([limitations users
   cite](#limitations-users-cite)): ranks 1 to 5 answer complaints; rank 6
   (the saved-state limit) and the easy room-correction wins (ranks 7 to
   10) answer none that was found; ranks 11 to 13 are the reverb features.
   It also weighs the standing direction to favour stability and longevity
   over features ([decisions](../decisions.md#direction)): each item keeps
   the [non-negotiables](#non-negotiables), and the reverb-shaping items
   wait for users to ask.
3. **Does the freeze hold for all of it?** Platform- and chip-level
   micro-optimisation waits for the walkthroughs (jrp, 7 November 2026).
   Open: whether putting the limitations users cite first lifts the freeze
   for any of them, such as EDGE-1 or EDGE-11. Either way, an item starts
   only after the [gate](#before-an-item-starts). EDGE-5's user page states
   only what is already true and could be written before REL-3; a dropout that PERF-3 measures would be a
   fix the freeze allows, not an optional feature.
4. **The gains needed to keep each optimisation.** The figures in the
   [experiments table](#sound-aware-and-platform-aware-optimisation) are
   proposals, to be confirmed or changed before the measurements.
5. Each item's own question, listed in its section above, when it starts.
6. **Long reverbs and zero latency** (7 November 2026 experiment, [rank
   3](#3-long-reverbs-at-low-cpu-and-latency)): adopt non-uniform
   partitions with the spread schedule; zero latency as the default; warm a
   replacement with recent input.
7. **Automatic gain** ([EDGE-14](#automatic-gain)): whether to offer
   "Match loudness", its default, and the loud-filter warning.

## Sources

Checked on 6 November 2026 unless stated.

- Altiverb 8: [Audio Ease specifications and release history](https://www.audioease.com/altiverb/) (checked 7 November 2026: Windows since 8.2.3, 7 April 2025),
  [Sound On Sound news](https://www.soundonsound.com/news/audio-ease-release-altiverb-8-reverb-plug),
  [manual](https://audioease.com/altiverb/files/Altiverb-8-manual.pdf)
- Reverberate 3: [LiquidSonics](https://www.liquidsonics.com/software/reverberate/)
- Popularity and prices: [LANDR, the 15 best reverb plugins in 2026](https://blog.landr.com/best-reverb-plugins)
- Space Designer: [Logic Pro 10.2 effects help](https://help.apple.com/logicpro-effects/mac/10.2/en.lproj/lgcee0be400c.html)
- Waves IR series: [Sound On Sound news, 17 January 2005](https://www.soundonsound.com/news/waves-release-three-new-convolution-reverb-plug-ins)
- Hang Loose Convolver: [Archimago's review, 12 November 2022](https://archimago.blogspot.com/2022/11/accurate-sounds-hang-loose-convolver.html)
- CamillaDSP: [documentation, v3.0](https://henquist.github.io/3.0.x)
- Complaints, checked on 7 November 2026:
  [Ableton, Hybrid Reverb FAQ](https://help.ableton.com/hc/en-us/articles/360019484980-Hybrid-Reverb-FAQ);
  [JUCE forum, convolution CPU (2018–21)](https://forum.juce.com/t/convolution-reverb-with-juce-convolution-class-high-cpu-usage-why/27315);
  [JUCE forum, level changing with the host rate (2021–22)](https://forum.juce.com/t/dsp-convolution-changing-level-depending-on-the-host-sample-rate/44751);
  [Vienna Symphonic Library forum, instances](https://forum.vsl.co.at/post/148419);
  [HISE forum, dropouts (2021–22)](https://forum.hise.audio/post/49183);
  [HISE forum, offline bounces (2021–22)](https://forum.hise.audio/post/36269?page=2);
  [The Gear Forum, long responses (January 2023)](https://thegearforum.com/threads/why-isnt-there-a-modeler-allowing-for-usage-of-longer-irs.1399/latest);
  [AV NIRVANA, Audiolense Convolver and Hang Loose Convolver (2023)](https://avnirvana.com/threads/audiolense-convolver-vs-hang-loose-convolver.12696/post-95801);
  [Roon forum, a filter per rate (October 2021)](https://community.roonlabs.com/t/how-to-up-sample-a-convolution-filter/173555);
  [Roon forum, left and right filters (April 2024)](https://community.roonlabs.com/t/independent-convolution-filters/269959);
  [Roon forum, tap counts (2023–24)](https://community.roonlabs.com/t/roon-adjusting-convolution-filter-tap-length-fix-in-progress/161188?page=2);
  [Audirvana forum, pops after a rate change](https://community.audirvana.com/t/pops-after-samplerate-change-using-audiolense-convolver/29952);
  [Steinberg forum, REVerence memory (2011)](https://forums.steinberg.net/t/reverence-ram-consumption-opinions-welcome/599701);
  [WiiM forum, FIR latency (April 2024)](https://forum.wiimhome.com/threads/convolver-for-digital-room-correction-and-eq-fir-filters.390/page-2);
  [miniDSP forum, latency](https://www.minidsp.com/community/threads/latency.19880/page-3)
- Open-source reverbs, read from source on 7 November 2026:
  [KlangFalter](https://github.com/HiFi-LoFi/KlangFalter) (`a91da61`) and its
  engine [FFTConvolver](https://github.com/HiFi-LoFi/FFTConvolver) (MIT,
  `f2cdeb0`); [REEV-R](https://github.com/tiagolr/reevr) (`e4d553a`);
  [quantumVerb](https://github.com/QVbDev/quantumVerb) (`6801e01`);
  [VSL Hybrid Reverb manual](https://www.vsl.co.at/manuals/vienna-suite/hybrid-reverb)
- Other open-source convolvers, read from source on 7 November 2026: the
  commits in the [table](#other-convolvers-and-algorithms); BruteFIR's
  manual (`brutefir.html` in its repository)
- Algorithms: Gardner, "Efficient convolution without input-output delay",
  JAES 43(3), 1995; Garcia, [optimal filter partition for efficient
  convolution with short input/output delay](https://www.aes.org/e-lib/browse.cfm?elib=11275),
  AES 113th Convention, 2002; Hurchalla, [a time-distributed FFT for
  efficient low-latency convolution](https://www.aes.org/e-lib/browse.cfm?elib=15679),
  AES 129th Convention, 2010; Wefers and Vorländer, [optimal filter
  partitions for uniformly-partitioned convolution](https://dafx.de/paper-archive/details/fWi-8K36aSnULRLT-W7z2A),
  DAFx 2011; Battenberg and Avizienis, [real-time partitioned convolution on
  conventional operating systems](https://dafx.de/paper-archive/details/S8X2gFWs-CsSUhrhJ7KeWQ),
  DAFx 2011; Wefers, *Partitioned convolution algorithms for real-time
  auralization*, Logos, 2015; Carpentier and others, [hybrid reverberation
  processor with perceptual control](https://dafx.de/paper-archive/details/ar34-t6tsn7TfoIpzLzu7Q),
  DAFx 2014; Välimäki and others, [late reverberation synthesis using
  filtered velvet noise](https://mdpi.com/2076-3417/7/5/483), Applied
  Sciences, 2017
- X-MCFX: [readme](https://www.angelofarina.it/Public/X-MCFX_convolver/X-MCFX-readme.txt),
  [repository](https://github.com/JB-Luke/X-MCFX)
