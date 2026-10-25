<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in formats plan: VST3, LV2 and AU

Status: written on 21 October 2026 against Convolver `75f3924` (package 0.4.3)
and revised the same day against `4e1bb1b` (0.4.6) after the owner's review.
It revises the format order in the
[FFmpeg filter plan](2026-10-21-ffmpeg-filter.md) and the
[plug-in distribution plan](../../archive/plans/2026-10-18-plugin-distribution.md).
**Current state (24 October 2026, `9fa13f7`, 0.4.44):** the shared
groundwork is done (PLUG-1 to PLUG-4 and PLUG-6 to PLUG-8), and so are the
first steps of each stream: VST3-1, VST3-7, VST3-8 and VST3-10; LV2-1 and
LV2-2; AU-1 and AU-2. The [shared adapter contract](../../develop/plugin-contract.md)
now lives in the developer guides; it applies to every format and takes
precedence over a per-format section below that seems to disagree. Still open: the release pipeline for all
three formats (PLUG-5), LV2-3 to LV2-5, AU-3 to AU-5 and the named-host tests
(HOST-1, AU-4). The first preview holds VST3 only; LV2 and AU join later
previews. Open work and its order are in [TODO](../../../TODO.md#execution-priorities).
The sections below are the plan as revised on 21 October 2026.

**Priority:** develop VST3, LV2 and AU in
parallel, as equal first priorities. The FFmpeg filter and a Windows-native
adapter (Media Foundation, APO) are parked: FFmpeg because maintaining a
patched FFmpeg source tree adds uncomfortable coupling, Windows because it needs
more thought before any work starts.

**In short:**

- **Three formats, three parallel streams.** Each adapter depends only on
  `Convolver::host`, so they can proceed independently once a little shared
  groundwork (FFT safety, a common test harness, one preset tool, one layout
  and identity list, pinned format dependencies) is in place. All three follow
  one [contract](#shared-adapter-contract) for failed loads, initial
  restoration and channel layouts. See [parallel development](#parallel-development).
- **LV2** is the native plug-in format on Linux: Ardour, Qtractor, Carla,
  REAPER and Audacity load it, and so does PipeWire's `filter-chain`, which can
  apply it to all system audio. That last host matters most for room
  correction. On Windows and macOS only a few hosts (Ardour, Mixbus, REAPER,
  Carla) load LV2. Audacity 4.0 loads it on Linux only.
- **VST3** stays the main cross-platform format. Almost every DAW except Logic
  and Pro Tools loads it, and its SDK has been MIT-licensed since 3.8. Its
  stream is finishing what exists: layouts, host tests and release.
- **AU (AUv2)** is needed on the Mac for Logic, GarageBand and system-wide
  effects in SoundSource.
- **CLAP adds no reach.** Every CLAP host found (Bitwig, REAPER, FL Studio,
  Studio One/Fender Studio Pro, Qtractor) also loads VST3. Add it only if users
  ask.
- **FFmpeg is parked.** The documented pipe covers it. FFmpeg's `lv2` filter
  cannot pass a file path to a plug-in today, so LV2 does not help there yet.
- **Framework: none.** Write each adapter directly against the format's own
  C or C++ headers, sharing `HostProcessor`, as the VST3 adapter already does.
  These headers outlive any framework built on them. DPF was the fallback if a
  shared editor across formats was ever needed; on 24 October 2026 jrp chose
  native views instead ([editor plan](2026-10-24-plugin-editor.md#framework-choice)).
  JUCE is not recommended.

Contents: [recommendations](#revised-recommendations) ·
[who loads which format](#who-loads-which-format) ·
[how widely used](#how-widely-each-format-is-used) ·
[frameworks](#development-frameworks) ·
[parallel development](#parallel-development) ·
[shared contract](#shared-adapter-contract) ·
[test harness](#shared-test-harness-plug-2) · [LV2 design](#lv2-adapter-design) ·
[AU design](#au-adapter-design) ·
[PipeWire example](#system-wide-use-with-pipewire) · [testing](#testing) ·
[build and distribution](#build-and-distribution) · [milestones](#milestones) ·
[decisions](#decisions-for-the-owner)

## Revised recommendations

| Priority | Format | Why | Platforms |
| --- | --- | --- | --- |
| First (exists; finish) | **VST3** | Widest host coverage on Windows, macOS and Linux | All |
| First (parallel) | **LV2** | Native Linux format; the only plug-in route into PipeWire system audio, MOD/Zynthian hardware and GStreamer. Small C API; can be built and tested headless in Linux CI. A file-path parameter gives hosts a file chooser without us writing an editor. | Linux first; macOS/Windows builds for Ardour, Mixbus, REAPER and Carla |
| First (parallel) | **AU (AUv2)** | Logic and GarageBand load nothing else; SoundSource applies AU effects to Mac system audio. First releases unsigned; signing and notarization later. | macOS |
| On request | CLAP | No host found that loads CLAP but not VST3 | All |
| Parked | FFmpeg filter | The pipe works today; a built-in filter would mean maintaining a patched FFmpeg | All |
| Parked | Windows-native (MFT, APO) | Needs more thought first; see the [Media Foundation plan](2026-10-21-media-foundation-transform.md) | Windows |
| Not planned | AAX | Pro Tools only; licence and signing burden for a room-correction tool | Windows, macOS |

## Who loads which format

Checked against the sources listed below the table, 21 October 2026. The table
records which formats each host says it loads. It says nothing about whether
Convolver works there: no host has been tested yet (HOST-1, LV2-4, AU-4). A gap
means "not supported or not found". Several rows rest on secondary summaries;
recheck each against the vendor's own documentation before a user guide repeats
it.

| Host | VST3 | AU | LV2 | CLAP | Notes |
| --- | --- | --- | --- | --- | --- |
| Logic Pro, GarageBand | | Yes | | | AU only |
| Pro Tools | | | | | AAX only |
| Cubase 15 | Yes | | | | No new formats in 15 (Nov 2025) |
| Ableton Live 12 | Yes | Yes | | | |
| FL Studio 2024+ | Yes | macOS | | Yes | 64-bit AU on macOS; CLAP added June 2024 (vendor manual) |
| Studio One 7 / Fender Studio Pro 8 | Yes | Yes | | Yes | CLAP since 7.0 (Oct 2024); Linux beta since 6.5 |
| Bitwig Studio | Yes | | | Yes | Windows, macOS, Linux |
| REAPER | Yes | Yes | Yes | Yes | CLAP since 6.71 (Nov 2022) |
| Ardour, Mixbus | Yes | Yes | Yes | | LV2 on all platforms ("VST2, VST3, LV2 and LADSPA on Windows"); Ardour sees no gain in CLAP over LV2 |
| Audacity 4.0 | Yes | Yes | Linux only | | |
| Qtractor | Yes | | Yes | Yes | Linux |
| Carla | Yes | Yes | Yes | | Plug-in host/rack; no released CLAP support as of Nov 2024 |
| PipeWire `filter-chain` | | | Yes | | Also LADSPA and its own built-in convolver |
| SoundSource (macOS) | | Yes | | | System-wide effects |
| FFmpeg, GStreamer | | | Yes | | FFmpeg's `lv2` host sets numbers only (see below) |
| MOD Duo/Dwarf, Zynthian | | | Yes | | LV2-only hardware hosts |

Sources: [LV2 on Wikipedia](https://en.wikipedia.org/wiki/LV2),
[Ardour on Windows](https://ardour.org/windows.html),
[REAPER](https://en.wikipedia.org/wiki/REAPER),
[Audacity](https://en.wikipedia.org/wiki/Audacity_(audio_editor)),
[Fender Studio Pro](https://en.wikipedia.org/wiki/Studio_Pro),
[FL Studio supported formats](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins_supported.htm)
(vendor manual: VST, VST3 and CLAP on Windows; VST, VST3, AU and CLAP on macOS),
[FL Studio 2024](https://rekkerd.org/image-line-launches-fl-studio-2024-incl-clap-plugin-support/),
[Cubase 15](https://sonicstate.com/news/2025/11/06/steinberg-releases-cubase-15/),
[CLAP two years later](https://librearts.org/2024/11/clap-api-two-years-later/),
[PipeWire filter-chain](https://docs.pipewire.org/page_module_filter_chain.html),
and FFmpeg's `af_lv2.c` (see the [FFmpeg plan](2026-10-21-ffmpeg-filter.md#integration-options)).
A vendor's 2026 overview
([Antares](https://www.antarestech.com/blog/vst3-au-aax-clap-plugin-formats-2026))
gives the same picture for the big DAWs but misses FL Studio's CLAP support,
and this table first missed FL Studio's AU support, so treat secondary
summaries with care.

## How widely each format is used

No public data counts plug-in installs by format. The available evidence:

- **DAW popularity.** Production Expert's 2025 survey (over 2500 professionals,
  self-selected) ranks Pro Tools first, then Logic, Studio One, Cubase, Live and
  REAPER ([results](https://www.production-expert.com/production-expert-1/2025-daw-survey-the-results)).
  Plug-in developers on the JUCE forum report very different shares from
  their own users (FL Studio about 32%, Pro Tools about 2%), with macOS and
  Windows roughly equal ([thread](https://forum.juce.com/t/daw-market-share-statistics/69310)).
  Whichever is closer, the leaders need VST3, AU or AAX. None of them loads LV2.
- **Linux share.** Linux was just over 4% of Steam users in July 2026
  ([report](https://www.digitalcitizen.life/linux-usage-on-steam-climbs-back-above-4-in-july-2026/)).
  There is no comparable figure for audio users; distribution packages are
  missing from vendors' download counts.
- **Convolver's own users.** Historical downloads with a recorded operating
  system were mostly Windows (about 30,000), and the `.cfg` readers that keep the
  format alive (Roon, JRiver, Audiolense) are Windows and macOS products. See the
  [downstream-use record](../history.md#downstream-use).
- **CLAP.** About 99% of CLAP plug-ins also ship in another format, per the
  [CLAP review](https://librearts.org/2024/11/clap-api-two-years-later/).

So LV2 reaches a small share of DAW users, but on Linux it is often the only
plug-in route, especially for system-wide filtering, where Convolver's room
correction use belongs.

## Development frameworks

jrp asked to favour stability and longevity over features. Convolver needs no
custom editor, so a framework's main benefit (GUI and cross-format parameter
plumbing) is small, and the existing VST3 adapter is about 500 lines over
`HostProcessor`.

| Option | Formats | Licence | Longevity and risk | Verdict |
| --- | --- | --- | --- | --- |
| **Format headers directly** (LV2 1.18, VST3 SDK, Apple AudioUnitSDK, CLAP) | One per adapter | LV2 ISC; VST3 MIT since 3.8 ([announcement](https://sonicstate.com/news/2025/10/30/vst-3-now-available-under-mit-license)); AudioUnitSDK Apache-2.0 ([repo](https://github.com/apple/AudioUnitSDK)); CLAP MIT | These are the standards themselves. LV2's C API has been compatible since 2008; every framework depends on them anyway | **Recommended**, matching the VST3 adapter |
| DPF (DISTRHO Plugin Framework) | LV2, VST2, VST3, CLAP, AU, LADSPA, DSSI, JACK standalone | ISC | Active since about 2012 and used by many free plug-ins, but largely one maintainer; its own build system | Was the fallback for a shared editor; replaced by native views on 24 October 2026 |
| JUCE 8 | VST3, AU, AUv3, AAX, LV2, standalone | AGPLv3 or commercial (free tier up to $20k revenue) | Commercially owned; licence changed from GPLv3 to AGPLv3 in JUCE 8; large dependency | Not recommended: AGPL binaries and vendor control |
| iPlug2 | VST2, VST3, AUv2, AUv3, AAX, CLAP, WAM | zlib-like | No Linux or LV2 | Excluded for this goal |
| clap-wrapper | CLAP wrapped as VST3, AUv2, AUv3, AAX, standalone | MIT | Young (2023); no LV2 | Only relevant for a CLAP-first rewrite, which we do not need |

The LV2 adapter therefore uses the `lv2` headers (C, ISC, Ubuntu 24.04 ships
1.18.10) and, for tests only, `lilv` (0.24.22) to load it as a host would.
PLUG-6 pinned these versions (22 October 2026); see
[pinned format dependencies](../../develop/build.md#pinned-format-dependencies).

Apache-2.0, AudioUnitSDK's licence, is compatible with GPLv3 but not with
GPLv2. That affects how the AU component may be distributed; see
[AU licence](#au-licence).

## Parallel development

The three adapters share the engine, host lifecycle and state codec already.
Doing the shared pieces once, early, keeps the streams independent and their
behaviour identical:

| ID | Shared groundwork | Why it comes first |
| --- | --- | --- |
| PLUG-1 | A safe FFT backend for every plug-in build (see [FFT backend](#lv2-adapter-design) below). **Done 21 October 2026.** | Affects the existing VST3 build already |
| PLUG-2 | One adapter test harness, specified under [shared test harness](#shared-test-harness-plug-2): the independent convolution oracle, CLI equivalence, fixed tolerances and lifecycle cases | Each format's tests then only drive its own host API |
| PLUG-3 | One preset tool, `convolver-preset`, writing `.vstpreset`, `.aupreset` and LV2 preset bundles from the same encoded host state. It builds without the VST3 SDK, so AU and LV2 builds do not need it; `.vstpreset` output either uses the SDK when present or a small writer of the documented format, tested against the SDK's reader. `convolver-vst3-preset` keeps working with the same arguments and output, for existing scripts. Implemented 22 October 2026 (PR #32); the [presets guide](../../use/presets.md) describes the files, including the AU key and LV2 state property chosen that day | Users convert a `.cfg` the same way for every format |
| PLUG-4 | One list of supported channel layouts, with [channel meaning](../../develop/plugin-identities.md#channel-layouts) defined, and a registry of permanent identities (VST3 class IDs, AU type/subtype/manufacturer codes, LV2 URIs) | Identities can never change after release, and stricter layout checks must not break saved projects |
| PLUG-5 | One release pipeline (REL-1) producing VST3, LV2 and AU together. First comes an [unsigned preview](#unsigned-previews-and-production) with a defined publication step; signing and notarization, done once for both Mac formats, are the later production gate | Avoids three release processes |
| PLUG-6 | Pin format dependencies: AudioUnitSDK (commit and checksum, like the VST3 SDK), the `lv2` headers, and the test-only `lilv` and `lv2lint`, in `packaging/release-policy.json` for shipped code and in CI for test tools. **Done 22 October 2026:** AudioUnitSDK 1.4.0 (C++23; jrp chose it over the C++20 1.3.0), `lv2` 1.18.10, `lilv` 0.24.22, `lv2lint` 0.16.2 ([pins](../../develop/build.md#pinned-format-dependencies)) | Builds and validator results must be reproducible before adapters depend on them |

After that the streams are separate and can be worked on at the same time, by
different people or threads:

- **VST3:** the existing VST3-1 to VST3-6 items (layouts first), the HOST-1 host
  matrix and release.
- **LV2:** LV2-1 to LV2-5 below. Linux CI only, until bundles for other
  platforms.
- **AU:** AU-1 to AU-5 below. macOS CI only.

Each stream must pass the same PLUG-2 comparisons before release, so a filter
sounds the same in every format.

## Shared adapter contract

These rules hold for VST3, LV2 and AU alike. Since 23 October 2026 (DOC-4) they
are maintained in the [plug-in adapter contract](../../develop/plugin-contract.md),
which holds the text this section used to: [loading and failure](../../develop/plugin-contract.md#loading-and-failure),
[failure reasons](../../develop/plugin-contract.md#failure-reasons),
[initial restoration and live replacement](../../develop/plugin-contract.md#initial-restoration-and-live-replacement)
and [bypass](../../develop/plugin-contract.md#bypass). Channel layouts and old
presets are in [plug-in identities](../../develop/plugin-identities.md#channel-layouts).
Change the rules there, not here.

## Shared test harness (PLUG-2)

Implemented 22 October 2026 (PR #28) in `tests/plugins/`; the
[testing guide](../../develop/testing.md#plug-in-comparisons-plug-2) describes the cases
as built. The fixtures are the DOC-1 first-use examples, written by
`tests/plugins/fixtures.py`, whose helpers the release example generator and
the pluginval acceptance test now share.

Comparing an adapter with the CLI render shows that the adapter is wired up
correctly, but both use the same engine and could share a defect. So the
harness has two references, and every adapter's tests use both:

1. **Independent oracle:** direct double-precision time-domain convolution, as
   `tests/native/engine_tests.cpp` and the pluginval acceptance test already
   do (now `tests/native/oracle.hpp` and `tests/plugins/fixtures.py`). This
   stays the authority for correctness.
2. **CLI equivalence:** the CLI render of the same input and filter, with the
   same backend and block size.

Fixed criteria:

- **Tolerance:** at most 1e-5 absolute against the oracle for full-scale noise
  (the existing pluginval acceptance tolerance); report the worst error
  observed. Against the CLI, identical frames once latency is aligned; any
  difference must be explained and bounded in the test.
- **Latency alignment:** the host-reported latency (256 frames) must be exactly
  the offset at which output matches the oracle. AU reports it in seconds
  (`256 / sample rate`), so the test converts.
- **Tails:** for a freshly constructed processor (first activation, or
  reactivation), the reported tail must equal the exact value: the longest
  routed IR length minus one plus that route's input and output delays (the
  formula in `tests/native/oracle.hpp`). After live replacements, `HostProcessor`
  deliberately reports a conservative bound: the largest tail of any filter
  used since construction, which never shrinks during a session
  (`portable/host.cpp`). The test therefore requires the reported tail to be
  at least the current filter's exact tail and equal to that maximum, and a
  long-filter → short-filter replacement followed by reactivation must report
  the long tail until reactivation and the short tail after it. AU reports it in
  seconds too. Output beyond the current filter's tail is within tolerance of
  zero, and silence after a reset is exactly zero.
- **Channel order:** every route gets a distinct IR, so a swapped or misrouted
  channel fails instead of passing by symmetry.

Cases, run for every adapter:

- initial restoration (no crossfade: matches the oracle from the first block),
  and a long → short replacement followed by reactivation (tail reporting),
  live replacement (fresh history, 256-frame warm-up, 256-frame crossfade),
  failed initial load and failed replacement (see the
  [contract](../../develop/plugin-contract.md#loading-and-failure)), and an empty instance;
- bypass (64-sample ramp, dry path delayed by the reported latency), reset,
  silence flags, and nonfinite input;
- variable block sizes (including 1, 61, 300 and the host maximum) and
  in-place buffers;
- mono, stereo and 2→6 routing.

These cases test one operation at a time. Their combinations (for example a
load after a reset during a transition, the defect found in the review of
0.4.19) are PLUG-7: deterministic sequences checked after every operation
against a small reference model of this contract.

Fixtures: one generator, not several. As built, `tests/plugins/fixtures.py`
writes the DOC-1 fixtures, including the 2→6 crossover, and holds the WAV,
noise and oracle helpers; `scripts/release/generate_examples.py` (the release
presets) and `tests/vst3/pluginval_tests.py` import them instead of keeping
their own copies.

## Unsigned previews and production

The first releases are unsigned previews.
Signing must not become a hidden prerequisite for them. The two gates are
separate:

| | Unsigned preview | Signed production release |
| --- | --- | --- |
| Signing | macOS: ad-hoc signature, documented quarantine removal. Windows: unsigned, SmartScreen warning | Developer ID, notarization, Authenticode with timestamps |
| Testing | All CI gates, including validators and PLUG-2; a smoke test in at least one host per format | The full [host qualification](../../develop/host-testing.md) procedure, clean machines, upgrades, and upgrade tests against files kept from earlier releases (COMPAT-1) |
| Label | "unsigned developer preview" in the release title and guide | Ordinary release |
| `production_enabled` | stays false | true only after every production gate |

The next milestone (TODO, "Execution priorities") adds an independent user
following the REL-2 journeys from the first preview's exact files (REL-3).
It happens with the chosen candidate run's files before they are uploaded
(see REL-3).

The candidate workflow builds and checks artifacts but cannot publish (its
permissions are read-only). Publishing a preview is a manual step owned by jrp:
run the candidate workflow on the chosen tag by hand (the version bot pushes
tags with the workflow token, which does not trigger other workflows), take
exactly the files from that run's `complete-preview-candidate` artifact (made
by its `collect` job), check them against the recorded SHA-256 sums, and
upload them unchanged to a GitHub pre-release on that tag, with the sums and
download instructions in the release notes. Assets are never replaced after
publication; a fix gets a new version.

## LV2 adapter design

`plugins/lv2/`, a bundle `convolver.lv2/` with `manifest.ttl`, a generated
`convolver.ttl` and one shared library. It links `Convolver::host` and
`Convolver::io`.

**Identity.** Stable URIs under `https://github.com/jrp2014/convolver/lv2#`,
never changed after release (like the VST3 class IDs). State keys and the
parameter URI live under the same base.

**Channels.** LV2 ports are fixed in the `.ttl`, so each channel layout is a
separate plug-in URI generated from one template: 1→1, 2→2, 1→2, 2→4,
2→6, 2→8, 6→6 and 8→8, all discrete, within the VST3 adapter's 1–8 limit.
Named layouts (port groups and designations) are added per the
[layout rules](../../develop/plugin-identities.md#channel-layouts). A configuration whose counts
differ from the instance's is rejected with an explanation (nothing is
remapped), and the [failure rules](../../develop/plugin-contract.md#loading-and-failure) decide what plays:
a failed replacement keeps the running filter, a failed initial load is silent.

**Ports.**

| Port | Purpose |
| --- | --- |
| Audio in/out, `lv2:inPlaceBroken` not set | Planar float buffers to `HostProcessor::process`, which supports in-place use |
| `lv2:enabled` control input | Host bypass, using the existing 64-sample ramp |
| Latency control output (`lv2:reportsLatency`) | Reports B frames (256), so hosts compensate |
| Status control output | 0 running, 1 no filter, 2 last load failed, 3 loading; visible in generic UIs |
| Atom input/output (`patch:Message`) | `patch:Set`/`patch:Get` for the filter path |

**Loading a filter.**

- A `filter` parameter: `lv2:Parameter` with range `atom:Path`, listed as
  `patch:writable`. Ardour and Jalv show a file chooser for these (Carla and
  REAPER to be confirmed in HOST-1), so
  users can pick a `.cfg`, list or IR file without a custom editor. The plug-in
  must answer `patch:Get` or Ardour shows "(None)" after reload
  ([report](https://discourse.ardour.org/t/lv2-plugins-file-selectors-not-updating-in-ardour/113420)).
- Loading uses `load_configuration` with the instance's sample rate and
  channel counts, so lists choose an entry exactly as in the CLI and nothing is
  resampled.
- Preparation runs in the LV2 worker; the worker and state lifecycle below
  defines the threads, locking and failure cases.
- Errors go to the host log (`log:log`) with the same explanatory text as the
  CLI, and to the status port. Messages are logged from `work()`, `restore()`
  or another non-real-time call, never from `run()`.

**Worker and state lifecycle.** The LV2 specifications constrain who may call
what. The scheduler passed at instantiation may be used only from `run()`
([worker](https://lv2plug.in/ns/ext/worker)). `work()` runs in a non-real-time
context, normally another thread, but when the host is free-wheeling (offline
rendering) it may run immediately inside the `run()` call that scheduled it.
Its responses arrive through `work_response()` in the `run()` context
([worker API](https://lv2plug.in/c/html/group__worker.html)). `restore()` may run
concurrently with `run()` only if the plug-in declares `state:threadSafeRestore`,
and then the host passes a separate scheduler to `restore()`
([state](https://lv2plug.in/ns/ext/state#threadSafeRestore)). Nothing stops
`work()` from running at the same time as `restore()` or `save()`. The adapter
contract:

- **One control lock.** An adapter mutex serializes every `HostProcessor`
  control call (construction, `replace`, `collect`, destruction) and the
  adapter's record of the last accepted filter. `work()`, `restore()`, `save()`,
  `activate()`, `deactivate()` and `cleanup()` take it. `run()` and
  `work_response()` never do. The only exception is free-wheeling, where
  `work()` runs inside `run()`; there are no real-time requirements then.
- **No `state:threadSafeRestore` at first.** Without it, the host never calls
  `restore()` during `run()`, which keeps initial restoration simple. Revisit
  only if a target host needs concurrent restore; then `restore()` must
  schedule its work with the scheduler it was given, not the instantiation one.
- **Initial restoration** before the first audio block (session load, PipeWire
  `config`) prepares the filter in `restore()` and constructs the
  `HostProcessor` with it directly, with no crossfade, per the
  [contract](../../develop/plugin-contract.md#initial-restoration-and-live-replacement). If audio has already
  run, `restore()` uses `replace` instead.
- **Live loads.** `run()` and `work()` share no request buffer, because
  `run()` never takes the lock. A `patch:Set` arriving in `run()` builds a
  bounded request on the stack (a generation number, the path length and the
  path; too long a path sets "load failed") and passes it to `schedule_work` as
  its payload, which the host copies
  ([worker API](https://lv2plug.in/c/html/group__worker.html)). The generation
  comes from an atomic counter that `run()` and `restore()` increment, so the
  newest request is always known without a lock.
- **Stale requests.** `work()` takes the lock and skips a request whose
  generation is older than the newest one, so rapid loads coalesce. Loading a
  file and preparing its FFT engine take time, and a newer request can arrive
  at any point during them. So preparation and publication are separate steps,
  with the last generation check between them:
  1. Load and validate the file. If the request is already stale, stop here;
     this saves FFT planning. Otherwise prepare the replacement filter (engine
     construction and FFT planning), which is everything `replace` does today
     before it hands the filter to the audio thread.
  2. Check the generation again, after preparation. Publish only if the
     request is still the newest and newer than the generation already
     installed (kept under the lock, so it only increases). Otherwise discard
     the prepared filter, which off the audio thread is safe.
  3. Publish: hand the prepared filter to the audio thread, raise the tail
     bound and record the installed generation and path.

  Checking before a single `replace` call is not enough, because `replace`
  prepares and publishes in one call. A request that arrives while it prepares
  would still have the stale filter installed and briefly audible, and if that
  newer request then failed, the stale filter would keep playing.
  `HostProcessor` therefore needs a two-step replacement (prepare, then
  publish), added with LV2-2 and covered by the core host tests. `replace`
  remains prepare followed by publish, so VST3 and AU are unchanged.

  A request B that arrives between the final check and publication of A is
  treated as arriving after A was installed. The remaining gap is the atomic
  hand-over, which a `run()` that never takes the lock cannot close. A really
  was the newest when it was committed, and B's own `work()` call runs next
  under the lock and replaces A or reports as usual.
- **Responses.** Every `work()` call sends a response, including skipped and
  discarded ones. It carries the request's generation, its outcome
  (installed, superseded, or load failed), and the installed generation and
  path, read under the lock. `work_response()` handles the two parts
  separately:
  - **Status follows the newest request.** `work_response()` reads the newest
    requested generation from the atomic counter, which is exact because it
    runs in the `run()` context that increments it. It applies a response's
    outcome only if the response is for that generation. Otherwise the status
    stays "loading" until the newest request answers. Checking against the
    last response applied is not enough.
  - **The path follows the installed generation.** If a response reports a
    newer installed generation than the last one reported, `work_response()`
    sends `patch:Set` with that path. So `patch:Get` always names the filter
    that is playing, even when the response that installed it is for an older
    request.
  - **Requests that never reach `work()` still get an answer.** If
    `schedule_work` fails, `run()` answers that generation itself with
    "load failed" (see below). `restore()` updates the installed record under
    the lock and leaves its generation, outcome and path for the next `run()`
    to apply as that generation's response. Without
    `state:threadSafeRestore`, `restore()` never overlaps `run()`, so the
    host's own ordering makes that hand-over safe.
- **Scheduling failure.** If `schedule_work` returns
  `LV2_WORKER_ERR_NO_SPACE` or `LV2_WORKER_ERR_UNKNOWN`, the running filter
  stays, `run()` answers the request at once with "load failed",
  `patch:Get` still reports the filter that is playing, and the message is logged from the next non-real-time call. The request
  is dropped, not silently retried.
- **Saving during a load.** `save()` takes the lock, so it records the state
  either before or after a load, never half of one. It saves the last accepted
  filter (mapped path and embedded state). A request still queued is not saved;
  the status shows it is loading.
- **Teardown.** `deactivate()` and `cleanup()` take the lock, so a `work()` call
  in progress finishes first. A closing flag makes later responses do nothing.
  The `HostProcessor` is destroyed only there, when the host guarantees that
  `run()` is not executing. `collect()` runs in `work()` and `save()`.
- **Tests** (in the `lilv` host test): a worker with an injectable delay,
  synchronous (free-wheel) work, injected scheduling failure, restore and save
  while a load is in progress, several loads in quick succession, a newer
  request arriving while an older file is still loading (the older completion
  must not change the filter, status or saved path), and cleanup with work
  outstanding, all under sanitizers (ThreadSanitizer for the races).
  **Regression tests for the preparation window.** Test hooks pause `work()`
  at two points, and request B arrives while request A is paused. Distinct
  gains for the original filter, A and B make each outcome visible in the
  output.
  - *After preparation, before the final check:* if B names a valid filter,
    the output never carries A's gain, and the end state is B. If B fails
    validation, the original filter keeps running, the status is "load
    failed", and `patch:Get` and `save()` report the original path. A's
    response (superseded) leaves the status at "loading" until B answers.
  - *After the final check, before publication:* A is published. A's
    response, delivered after B was requested, leaves the status at "loading"
    but sends `patch:Set` with A's path. If B is valid, the end state is B.
    If B fails, A keeps playing, and the status, `patch:Get` and `save()`
    report "load failed" and A's path.
  - *Scheduling failure for B* in the second case: the status becomes "load
    failed" at once, and the path follows A when A's response arrives.
  - *`restore()` with a response outstanding:* the older response does not
    overwrite the status or path that `restore()` installed.

  Implemented 23 October 2026 by LV2-2 (PR #48), as above, with these
  details. `HostProcessor::prepare()` and `publish()` are the two steps, and
  an install callback throws `SupersededRequest` so that `HostFilter` records
  nothing for an overtaken request. LV2 numbers every request with its
  generations, so failure records carry them. The atom ports follow the
  audio ports (`control`, then `notify`, 8192-byte buffers), so the earlier
  port numbers are unchanged. A request that never reaches `work()` (no
  worker, or a failed `schedule_work`) shows "load failed" at once and is
  recorded and logged, without its path, by the next control call; since
  jrp's review of 0.4.30 an instance with no accepted filter also mutes at
  once (`HostProcessor::mute()`), so it does not play unfiltered audio
  meanwhile. Paths
  longer than 4096 bytes are refused. A file that is readable but made for
  another rate or other channels is recorded as `sample_rate` or `channels`
  with the CLI's explanation. The tests pause `work()` through a test-only
  extension, as PLUG-8's failure record is read, and also run the worker
  synchronously with answers delivered inside `run()`.

**State.** Save the mapped path (`state:mapPath`, so sessions can move) and the
encoded host state (the VST3 preset format), so a session still sounds the same
if the file later changes or disappears. Restore prefers the embedded copy; an
explicit path change reloads from the file. If neither the embedded copy nor
the file can be used, this is a failed initial load (or a failed replacement if
a filter is already running). Restore also accepts a bare path
string, because PipeWire passes `config` values with type 0 and no terminating
NUL (seen in its `plugin_lv2.c` at `b359cb0`).

**Audio thread.** `run()` only calls `HostProcessor::process`, `set_bypass` and
reads atoms: no allocation, locks or I/O. The maximum block comes from the
`bufsz:maxBlockLength` option (PipeWire passes 8192); without it, process in
chunks of a preallocated size.

**FFT backend.** Plug-in hosts load many plug-ins into one process. With a
shared system FFTW, another plug-in's planner calls are not serialized with
ours, and FFTW's planner is not thread-safe by default. Our own planner mutex
does not cover the host or other plug-ins. Recommended for plug-in builds: Ooura
on Linux and Windows and Accelerate on macOS; Ooura measured 0.9–1.45 times
FFTW's time per frame. The alternatives are FFTW linked statically with its
symbols hidden and bound locally (static linking alone is not enough on Linux or
macOS), or `fftwf_make_planner_thread_safe()`. This applies to the existing
VST3 build and to AU as well; it is PLUG-1.

**Not in scope:** tail reporting (LV2 core has none), a custom UI, and ports
beyond eight channels.

## System-wide use with PipeWire

PipeWire's `filter-chain` passes `config` entries to the plug-in's state on
load and reads its latency port. A stereo room correction would look like this
(URIs from the [PLUG-4 registry](../../develop/plugin-identities.md)):

```text
context.modules = [
  { name = libpipewire-module-filter-chain
    args = {
      node.description = "Room correction"
      filter.graph = {
        nodes = [
          { type = lv2  name = room
            plugin = "https://github.com/jrp2014/convolver/lv2#discrete-2-2"
            config = {
              "https://github.com/jrp2014/convolver/lv2#filter" = "/home/me/room/room.cfg"
            }
          }
        ]
        inputs  = [ "room:in_1" "room:in_2" ]
        outputs = [ "room:out_1" "room:out_2" ]
      }
      capture.props  = { node.name = "room_correction" media.class = Audio/Sink audio.position = [ FL FR ] }
      playback.props = { node.name = "room_correction.out" audio.position = [ FL FR ] }
    }
  }
]
```

PipeWire also has a built-in `convolver` node that reads one IR file per node,
resamples it and picks the closest rate from a list. Convolver's plug-in adds
the existing `.cfg` files, routing matrices, delays and rate/channel list
selection; the documentation should say so plainly.

## AU adapter design

`plugins/au/`, an AUv2 effect (`aufx`) built on Apple's AudioUnitSDK
(Apache-2.0) and packaged as `Convolver.component`. AUv3 is not planned: it
needs an app-extension container, and Logic, GarageBand and SoundSource load
AUv2 effects.

- **Identity:** type `aufx`, manufacturer `CONV` and subtype `Conv`. Every AU needs these codes, open source or not,
  but nothing checks a registry: Apple's archived guide asked developers to
  register a "creator code", and developers report that the registration page
  no longer offers it. A manufacturer code needs at least one uppercase letter
  (all-lowercase codes are Apple's) and must not be `DEMO`. Hosts identify an AU
  by the type, subtype and manufacturer together; no other use of `CONV` was
  found in public sources, which cannot prove it is unused. The subtype repeats
  the historical VST2 ID `'Conv'`. Forks should change the manufacturer code
  (see [plug-in identities](../../develop/plugin-identities.md#if-you-distribute-a-modified-build)). Record them with
  the other permanent IDs in PLUG-4 and never change them after release.
- **Channels:** advertise the PLUG-4 channel counts through
  `kAudioUnitProperty_SupportedNumChannels`, including unequal pairs such as
  2→6. Those are counts only; named layouts go through
  `kAudioUnitProperty_AudioChannelLayout` and
  `kAudioUnitProperty_SupportedChannelLayoutTags` as the
  [layout rules](../../develop/plugin-identities.md#channel-layouts) describe. A filter whose
  counts differ is rejected, as VST3 does, under the
  [failure rules](../../develop/plugin-contract.md#loading-and-failure).
- **Timing:** AU reports both values in **seconds**, as `Float64`
  ([`kAudioUnitProperty_Latency`](https://developer.apple.com/documentation/audiotoolbox/kaudiounitproperty_latency)):
  latency is `latency_frames() / sample_rate` (256 frames at 48 kHz is about
  0.00533 s) and `kAudioUnitProperty_TailTime` is `tail_frames() / sample_rate`
  (AU, unlike LV2, can report a tail; `SupportsTail()` returns true). Both are
  recomputed when the sample rate or filter changes, and the property-change
  notification is sent.
- **Base class and bypass:** do not inherit `AUEffectBase`'s processing.
  When `ShouldBypassEffect()` is true its `Render` just copies input to output
  (or leaves in-place buffers alone), which would skip Convolver's delayed dry
  path and 64-sample ramp, and its source warns that it "does not deal as well as
  it should with N-M effects", bypass included
  ([AUEffectBase.cpp](https://github.com/apple/AudioUnitSDK/blob/main/src/AudioUnitSDK/AUEffectBase.cpp)).
  Derive from `ausdk::AUBase` (or override `Render` and the bypass property
  completely). Setting `kAudioUnitProperty_BypassEffect` happens on a host
  thread, and `HostProcessor::set_bypass` is an audio-thread call that writes a
  plain flag, so the property setter only stores the requested value in an
  atomic; `Render` applies it with `set_bypass` before processing each block,
  as the VST3 adapter does with its bypass parameter. Wet processing keeps
  running underneath, as in VST3. Set `kAudioUnitRenderAction_OutputIsSilence` only when the output
  really is silent, which is not until the tail has drained.
- **Filters and state:** the encoded host state is saved in the
  `kAudioUnitProperty_ClassInfo` dictionary, so `.aupreset` files written by the
  PLUG-3 tool load through the host's preset menu, with no editor. A
  file-choosing editor stays a HOST-2 decision for all formats.
- **Threads:** preparation happens in `SetProperty`/`RestoreState` on the
  host's main thread: construction for an initial restore, `replace` once audio
  has run. `Render` only calls `process`. A rejected `RestoreState` returns an
  error and keeps the running filter.
- **Tests (AU-1):** through PLUG-2, with AU-specific checks for latency and tail
  in seconds, bypass (ramp and delayed dry path, never plain pass-through;
  toggled from another thread while rendering, under ThreadSanitizer),
  tails after silent input, silence flags, and 2→6 processing.
- **Validation:** `auval` and pluginval on the `.component` in macOS CI.
- **Hosts (AU-4):** Logic, GarageBand, SoundSource, Live, REAPER and FL Studio:
  scan, preset load, save/reload, latency and tail, bypass, filter change.

### AU licence

AudioUnitSDK is Apache-2.0. The Apache Software Foundation considers Apache-2.0
compatible with GPLv3 but not GPLv2
([guidance](https://www.apache.org/licenses/GPL-compatibility.html)). Convolver is
GPL-2.0-or-later, so a component combining the two can be distributed under
GPL-3.0-or-later; the core, CLI, VST3 and LV2 builds and the source files stay
GPL-2.0-or-later. **Decided:** use AudioUnitSDK and
distribute the AU component under GPL-3.0-or-later. Ship the Apache-2.0 licence
and any SDK `NOTICE` in the component's `licenses/`, and state the combined
licence in its notices and the AU guide. The rejected alternative was writing
the AUv2 entry points against Apple's system headers without AudioUnitSDK.

## Testing

- **Headless host test (CTest, Linux CI):** a small `lilv` program in
  `tests/lv2/` that discovers the bundle, instantiates each URI, restores state
  (embedded, path, and PipeWire-style bare string), sets the path by
  `patch:Set`, and runs the [PLUG-2 cases](#shared-test-harness-plug-2) against
  both the independent oracle and the CLI render. It also checks the latency
  port, status port, mismatched channel counts, explanatory log text and the
  [worker lifecycle](#lv2-adapter-design) cases, with sanitizers.
- **Real-time checks:** the existing allocation guard around `run()`.
- **Validators:** `lv2lint` (Turtle and symbol hygiene) and pluginval, which
  already runs for VST3 and also loads LV2.
- **PipeWire:** a manual or scheduled job that loads the example above with
  `pw-cat` input and compares the recorded output.
- **Host matrix (HOST-1):** Ardour (Linux, macOS, Windows), REAPER, Carla,
  Qtractor and PipeWire: scan, file chooser, save/reload, latency
  compensation, filter change during playback.

## Build and distribution

- CMake option `CONVOLVER_BUILD_LV2` (off by default), needing only the pinned
  `lv2` headers (PLUG-6, done); `lilv` only when tests are on. The `.ttl` files are
  generated from `VERSION` and the layout list. Neither LV2 nor AU builds need
  the VST3 SDK, and the shared preset tool (PLUG-3) builds without it.
- Linux first: a release tarball of `convolver.lv2` for `~/.lv2`, then distro
  packages and possibly the Flathub LV2 extension. macOS and Windows bundles
  follow from the same CI, first as
  [unsigned previews](#unsigned-previews-and-production) like the VST3 bundle.
- GPL-2.0-or-later. A bundle with libsndfile and Ooura (or FFTW, when chosen)
  needs the usual corresponding-source offer. The AU component's licence is the
  [separate question](#au-licence) above.

## Milestones

Shared groundwork PLUG-1 to PLUG-5 is described under
[parallel development](#parallel-development). PLUG-7 (lifecycle sequences)
and PLUG-8 (failure reasons), added on 22 October 2026, are shared too and
are described in TODO. The streams then run side by side:

| Step | VST3 | LV2 | AU |
| --- | --- | --- | --- |
| 1 | VST3-1: speaker arrangements and the PLUG-4 layouts | LV2-1: adapter with ports, `run()`, latency and bypass; headless `lilv` test against PLUG-2 | AU-1: adapter with channel configurations, latency and tail in seconds, and explicit bypass; test against PLUG-2 |
| 2 | Preset tool moved to PLUG-3 | LV2-2: file parameter, worker loading, state (embedded and path), PipeWire-style restore | AU-2: ClassInfo state and `.aupreset` loading |
| 3 | pluginval (exists) on PLUG-2 fixtures | LV2-3: generated layouts, `lv2lint` and pluginval in CI | AU-3: `auval` and pluginval in macOS CI |
| 4 | HOST-1 host matrix | LV2-4: user guide (LV2 in `docs/use/plugins.md` and `install.md`) with PipeWire example; host matrix | AU-4: user guide (the Audio Unit in `docs/use/plugins.md` and `install.md`); Logic, GarageBand, SoundSource, Live, REAPER |
| 5 | Unsigned preview under PLUG-5 | LV2-5: release bundles (Linux, then macOS/Windows), unsigned previews first | AU-5: unsigned preview component under PLUG-5; Developer ID signing and notarization later, at the production gate |

FFMPEG-1 (testing the pipe in CI) stays cheap and useful. FFMPEG-2 to FFMPEG-5
and HOST-3 are parked. If FFmpeg is revisited, prefer routes that do not mean
maintaining a patched FFmpeg: the pipe, a filter accepted upstream, or state
support added upstream to FFmpeg's own generic `lv2` filter, which would let
the LV2 plug-in serve FFmpeg too.

## Decisions for the owner

- The plug-in FFT backend (PLUG-1). **Decided and implemented:** Ooura on Linux and Windows, Accelerate on macOS, overridable with
  `CONVOLVER_FFT_BACKEND`. LV2 and AU builds inherit it.
- Signing identities for releases (PLUG-5). **Decided:**
  start unsigned on macOS and Windows. Mac bundles are ad-hoc signed and users
  remove the download quarantine as the install guide explains; Windows
  installers show a SmartScreen warning. An Apple Developer ID (Gatekeeper and
  notarization) and Windows Authenticode come later. The two gates are
  separated under [unsigned previews and production](#unsigned-previews-and-production).
- The AU component's licence ([AU licence](#au-licence)). **Decided:** use AudioUnitSDK and distribute the AU component under
  GPL-3.0-or-later, as GPL-2.0-or-later permits, with the Apache-2.0 notices;
  everything else stays GPL-2.0-or-later.
- What an empty instance with unequal channel counts and a failed initial
  load play ([loading and failure](../../develop/plugin-contract.md#loading-and-failure)). **Decided:** silence for both, with an error indication, so an unloaded
  crossover never sends full-range audio to a driver; identity stays for an
  empty instance with equal counts. This changes current VST3 behaviour for
  unequal counts.
- Whether to enforce named layouts or offer VST3 buses above 8 channels
  (VST3-1). **Decided:** neither for now. A filter's
  output mask is compared with the host's arrangement and gives a notice only;
  no host state version 3; buses stay at 1 to 8 channels. Revisit on request.
- What bypass plays with unequal channel counts ([bypass](../../develop/plugin-contract.md#bypass)).
  **Decided:** silence on every output, including a
  1→2 filter that forks one input; equal counts keep input *c* to output *c*.
  `HostProcessor` implements it, so VST3 already follows it.
- Whether saved state records bypass ([bypass](../../develop/plugin-contract.md#bypass)). **Decided:** follow each format's convention. VST3 saves its
  bypass parameter; LV2 and AU leave bypass to the host and ignore the flag in
  restored state.
- Failed replacement (raised in jrp's review): keep the
  running filter in every adapter, with an explicit error indication. Adopted
  in the contract above; VST3 needs a fix for presets loaded while inactive.
- Which channel layouts to publish as LV2 URIs. Recommendation: the eight
  above, all discrete, added to only on request, as
  `https://github.com/jrp2014/convolver/lv2#discrete-2-6` and so on.
  **Decided:** as recommended, recorded in the PLUG-4
  registry.
- The LV2 URI base and the macOS identifier namespace (PLUG-4). Recommendation:
  `https://github.com/jrp2014/convolver/lv2#`, and `io.github.jrp2014.convolver.*`
  in place of `org.convolver.*` (a domain the project does not control) before
  the first release. **Decided:** as recommended.
- Whether restore prefers the embedded filter or the file. Recommendation:
  embedded, as VST3 presets do, with an explicit reload.
- Whether to publish macOS/Windows LV2 bundles at all. Recommendation: build
  them in CI, publish after Linux has users.
