<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Status

[Documentation index](../README.md) · [TODO](../../TODO.md) · [Decisions](decisions.md) ·
[Releasing](../develop/releasing.md)

For jrp and anyone asking what is ready, what is not, and where the project is
going. [TODO](../../TODO.md) is the backlog and owns the order of work; the
linked guides own the behaviour.

**In short:** The latest unsigned preview is
[0.19.21](https://github.com/jrp2014/convolver/releases/tag/v0.19.21)
for macOS, Windows and Linux, published on 27 November 2026, from `main`
as it then was. Its changes since 0.18.29, and 0.18.29's VST3 pairing
fixes, have automated coverage only; the checks left still run on the
0.18.29 files, the baseline jrp chose. The
most recent
[Mac host session](../archive/reviews/2026-11-22-live-session-0.18.10.md)
used a local build of the 0.18.10 tag. A packaged LV2 run and a Windows
window test in a named host are still open.

The [next milestone](#next-milestone-an-independent-user-runs-a-preview)
is an independent user completing the walkthrough with the published
0.18.29 package. The [checks table](#checks-left-before-publication) gives
the build and result for each host test and identifies the retests due on
the next preview.

The **oracle** named below is a direct double-precision convolution computed
independently of the engine, the project's reference for correct output
([testing](../develop/testing.md)). This page gives each feature's level and
each check's status in a sentence and links the TODO item that holds the
open work ([recording progress](../develop/workflow.md#recording-progress-in-todo)).
Update it when a level, a check or the scope changes, and keep the date and
commit above current.

Contents: [evidence levels](#evidence-levels) ·
[what users can run now](#what-users-can-run-now) ·
[next milestone](#next-milestone-an-independent-user-runs-a-preview) ·
[checks left](#checks-left-before-publication) ·
[performance](#performance-what-is-measured) ·
[compatibility guarantees](#compatibility-guarantees) ·
[what the tests do not establish](#what-the-tests-do-not-establish) ·
[direction](#direction)

## Evidence levels

Each feature is judged on four separate levels. A higher level does not follow
from a lower one.

| Level | Means | Evidence to cite |
| --- | --- | --- |
| **Implemented** | The code is on `main` | The guide that describes it |
| **Automatically tested** | A CTest test or CI workflow checks it, against an independent reference where one exists | The test name and workflow; a particular run for a particular commit |
| **Tested in a named host** | A person ran it in a named program (host name and version, OS, package SHA-256) and recorded the result | The record under HOST-1 or the format's host-matrix item |
| **Published** | It is in an asset of a GitHub pre-release or release that jrp published | The release page and its SHA-256 sums |

A user journey adds a fifth: **demonstrated**, when someone unfamiliar with
the repository completed it from the exact preview files using the
documentation alone. None is demonstrated yet.

## What users can run now

The last column gives availability in published previews. Each host result
names the build used where it matters.

| Feature | Implemented | Automatically tested | Named host | Published |
| --- | --- | --- | --- | --- |
| Command-line renderer `convolver` ([reference](../use/command-line.md)) | Yes | Yes: "Native and historical tests" on Linux (Clang, GCC), macOS (FFTW, Accelerate) and Windows (MSVC), against the oracle | Not applicable | Yes |
| Configuration inspection and validation ([compatibility](../use/other-programs.md)) | Yes | Yes: parser tests, a corpus of configurations and replay of fuzzing inputs | Other `.cfg` readers not checked (CFG-1) | Yes |
| Preset tool `convolver-preset` ([guide](../use/presets.md)) | Yes | Yes: `native.presets`, `plugins.presets`, `vst3.preset_format` | Not applicable | Yes |
| Equalizer APO export ([guide](../use/equalizer-apo.md)) | Yes | Yes: against a model of Equalizer APO 1.4.2, and in its own `Benchmark.exe` on Windows CI | Not yet on a real Windows device (HOST-4) | Yes |
| Non-uniform engine, Latency 0 by default (PERF-2, [measurements](../develop/fft-measurements.md#the-non-uniform-engine)) | Yes | Yes: the engine oracle tests (worst error 5.2e-7 of the peak) and `native.engine_stress` | REAPER played a 30-second filter at Latency 0 on the 0.18.10 source build. Listening and the remaining steps are open ([check 9](#checks-left-before-publication)). | Since 0.13.2 |
| Idle once silent input has outlasted the tail (EDGE-11, [measurements](../develop/fft-measurements.md#idle-after-the-tail)) | Yes | Yes: `native.engine` (`idle_after_tail`) checks the output is bit-identical to processing the silence; measured on Linux only | Not yet | Since 0.14.0 |
| VST3 plug-in ([guide](../use/plugins.md)) | Yes: 1–8 in, 1–8 out; Gain, Mix, Latency, Gain mode; a window on macOS (VST3-11) and Windows (VST3-12), not on Linux | Yes: SDK validator, pluginval at strictness 10, `vst3.harness` (against the oracle and the CLI) and `vst3.adapter` on all three systems, under sanitizers on Linux; `vst3.editor` on macOS and Windows; a headless Ardour 9 session plays the stereo example at Latency 0 and 1024, and the mono example on a Strict-I/O track, on a Flexible-I/O track as Ardour sets it up (on each channel, EDGE-3) and with its channels set by hand, reopened and exported against the oracle, on Linux (HOST-9, HOST-10) | REAPER 7.86 rendered and reopened the project on the 0.18.10 source build. In Audacity 4.0.2 a single filter chosen before playback worked, but a list chosen then produced silent exports. See [checks 2 and 10](#checks-left-before-publication) for the refusal, fixes and retests. | macOS since 0.12.12; Windows and Linux since 0.13.2; the Windows window since 0.13.12 |
| LV2 plug-in ([guide](../use/plugins.md)) | Yes: layouts (LV2-1), filter files through the worker (LV2-2), settings as control ports (LV2-7) | Yes: `lv2.harness`, `lv2.adapter`, `lv2lint` and pluginval on Linux; a headless Ardour 9 session against the oracle, sessions saved at Latency 256 and 1024 included (HOST-5, HOST-9) | Not yet from a package; a source build of Convolver ran in PipeWire 1.6.8 ([LV2 hosts](../develop/host-testing.md#lv2-hosts)) | Linux since 0.13.12 |
| Audio Unit ([guide](../use/plugins.md)) | Yes: state and `.aupreset` loading (AU-1, AU-2), settings (AU-7), the window (AU-6) | Yes: `au.harness`, `au.adapter` (also under ThreadSanitizer), `au.lifecycle`, `au.editor`, `au.bundle`, `auval -strict`, pluginval and an AVAudioEngine render against the oracle (AU-15), also in GarageBand's order at 44.1 kHz, with Latency chosen while running and after a `ClassInfo` round trip (AU-18), on macOS | GarageBand 10.4.15 exports aligned at Latency 0 and 1024 on the 0.18.10 source build ([check 8](#checks-left-before-publication)). Earlier [window and preset tests](../archive/reviews/2026-11-08-au4-garageband.md) used GarageBand 10.4.14. | macOS since 0.12.12 |
| C++ libraries ([CMake package](../develop/library.md)) | Yes | Yes: installed and source-tree consumer tests | Not applicable | Source only |
| FFmpeg pipe ([example](../use/command-line.md#with-ffmpeg)) | Documented | Yes: `native.ffmpeg_pipe` with Ubuntu's FFmpeg (FFMPEG-1) | Not applicable | Not applicable |

## Next milestone: an independent user runs a preview

From jrp's [guidance of 22 October 2026](../review-2026-10/release-readiness-guidance-2026-10-22.md):
an independent user installs and uses an exact preview package, following
only the documentation (REL-3). jrp published the previews before it
([decisions](decisions.md#releases)), so the walkthrough uses the published
0.18.29 files. It covers installing, loading the tester's own filter, saving
and reopening, then the REL-2 journeys from the package: the
[stereo example](../use/check-a-stereo-filter.md) and the
[2→6 crossover](../use/check-a-crossover-in-a-host.md) in a host, and
[file rendering](../use/check-files-with-the-command-line.md) with the
command line. The release tests run every packaged command of those guides.
How the first preview was qualified (its gates, journeys and the defects
jrp's reviews found) is [archived](../archive/releases/2026-11-08-first-preview-gates.md).

Not part of this milestone, but tracked: signing and notarization (REL-1),
other `.cfg` readers (CFG-1) and Equalizer APO on a real device (HOST-4).

<a id="what-still-blocks-the-first-preview"></a><a id="user-journeys-for-the-first-preview"></a>

## Checks left before publication

Check 6 runs on the exact candidate before each preview is published. The
other checks are follow-up work. Their agreed baseline is the published
0.18.29 package, which jrp chose on 26 November 2026. When a later fix changes the behaviour under test, repeat
that check on the next published preview. Record the build used and the
SHA-256 for a package run.
[Release decisions](decisions.md#releases) ·
[Host test steps](../develop/host-testing.md#smoke-test-in-each-first-host)

| # | Check | Steps | Time | Latest result |
| --- | --- | --- | --- | --- |
| 1 | REAPER, by ear: scan, the identity, echo and repeat presets, a click on bypass, Gain and Mix; where REAPER shows the Status parameter (HOST-1, VST3-11) | [REAPER](../develop/host-testing.md#reaper-vst3) steps 1 to 5 and the listening in 11 | 10 min | v0.12.8: Status display passed. Listening open |
| 2 | REAPER: a render, save, quit and reopen with the echo playing, a refused preset, the wrong host rate | REAPER steps 6 to 9 | 10 min | On a build of the v0.18.10 tag, 22 November 2026 ([record](../archive/reviews/2026-11-22-live-session-0.18.10.md)): the render, save and reopen passed (HOST-11's run); the refused preset was refused and kept playing, but the shown Latency changed to 0 (PLUG-48, fixed after); the wrong host rate not reported. Runs again on 0.18.29. v0.12.8: passed |
| 3 | REAPER: the stereo journey and the 2→6 crossover, each following its guide, with **Choose File…** on a six-channel track | REAPER steps 10 and 12 | 25 min | Not run since the guides were rewritten (DOC-10) |
| 4 | Audacity, through its preset menu only ([decision](decisions.md#plug-in-behaviour)): packaged presets and one of your own, listening, an export compared with the CLI, save and reopen, a refused preset, a mono track, a Latency change, the window's layout (HOST-1, VST3-11) | [Prepare the Mac](../develop/host-testing.md#prepare-the-mac) step 3; [Audacity](../develop/host-testing.md#audacity-vst3) steps 1 to 8 and 10; step 9 only for the window's layout, Gain, Mix and Latency | 30 min | v0.12.8: failed through **Choose…** (VST3-17), which passed on 0.16.14 in check 10; the rest open |
| 5 | GarageBand: a packaged preset and save and reopen, listening, a click on bypass, the track in time after a Latency change, the recent files and **Reload** in the Choose Filter menu, VoiceOver (AU-4, AU-6) | [GarageBand](../develop/host-testing.md#garageband-audio-unit) steps 4 to 6, 9 and 10 | 20 min | v0.12.8: the preset, save and reopen, **Recent** and **Reload** passed; the rest open |
| 6 | The exact candidate: the candidates workflow on the preview's tag, and the draft release's clean-machine checks | [Releasing](../develop/releasing.md#candidates) | CI | **Passed** for 0.19.21: [run 46072020640](https://github.com/jrp2014/convolver/actions/runs/46072020640), then [run 46075411986](https://github.com/jrp2014/convolver/actions/runs/46075411986), with the native, VST3, LV2, Audio Unit, fuzz and lint workflows and the everyday load budget run by hand on its tag; for 0.18.29, [run 45795263785](https://github.com/jrp2014/convolver/actions/runs/45795263785), then [run 45799318398](https://github.com/jrp2014/convolver/actions/runs/45799318398) (one clean machine re-run after an Ubuntu mirror fault), with the native, VST3, LV2, Audio Unit, fuzz and lint workflows and the everyday load budget run by hand on its tag; for 0.18.12, [run 45030498591](https://github.com/jrp2014/convolver/actions/runs/45030498591), then [run 45033419474](https://github.com/jrp2014/convolver/actions/runs/45033419474), with the native, VST3, LV2, Audio Unit, fuzz and lint workflows run by hand on its tag; for 0.18.10, runs [44971449967](https://github.com/jrp2014/convolver/actions/runs/44971449967) and [44973857366](https://github.com/jrp2014/convolver/actions/runs/44973857366); for 0.14.0, runs [44611563339](https://github.com/jrp2014/convolver/actions/runs/44611563339) and [44616436091](https://github.com/jrp2014/convolver/actions/runs/44616436091) |
| 7 | The independent walkthrough of 0.18.29 (REL-3) | [Above](#next-milestone-an-independent-user-runs-a-preview) | The tester | Not started |
| 8 | GarageBand with the window open: Latency changed stopped and while playing, then save and reopen (AU-17 changed how the unit reports latency) | GarageBand steps 4 to 6 | 5 min | Exports in time at Latency 0 and 1024 on a build of the v0.18.10 tag, 22 November 2026, the pending latency clearing while playing ([record](../archive/reviews/2026-11-22-live-session-0.18.10.md)); the window, a change while playing and save and reopen not reported |
| 9 | PERF-2's session: a long filter at Latency 0 by ear in REAPER and GarageBand, Latency changes and replacement while playing, projects from 0.12.12 reopening at the Latency the [plug-in guide](../use/plugins.md#settings) gives, and a saved Latency of 1024 reopening in time | PERF-2 in [TODO](../../TODO.md) | 30 min | A 30-second filter at Latency 0 played a full loop in REAPER without underruns on a build of the v0.18.10 tag, 22 November 2026 ([record](../archive/reviews/2026-11-22-live-session-0.18.10.md)); listening and the rest not run |
| 10 | Audacity, VST3-17's fix: choose `filter.cfg` with **Choose File…**, export without playing, compare with the CLI; save, reopen and export again; a list chosen again after one playback | [Audacity](../develop/host-testing.md#audacity-vst3) step 11 | 10 min | A single filter chosen before playback worked on the 0.18.10 source build. A list chosen then produced silent exports until chosen again after playback ([record](../archive/reviews/2026-11-22-live-session-0.18.10.md)). PLUG-49 shipped in 0.18.12. Retest on 0.18.29. On the [0.16.14 candidate](../archive/reviews/2026-11-21-vst3-17-audacity-pass.md), all four exports matched within 7.45e-8 of the reference, including save and reopen with one filter and choosing a list after playback. The [0.14.0 run](../archive/reviews/2026-11-20-vst3-17-audacity.md) failed. |

Accepted on earlier builds and not repeated
([decisions](decisions.md#plug-in-behaviour)): VST3-16's latency
compensation in REAPER; REAPER's window controls and saved settings on
0.12.5, and its packaged presets with save and reopen after PLUG-31
(`f11a3dd`); GarageBand's controls, saved settings, refusal and exports on
0.12.5; and REL-6's loading qualification of the 0.12.5 source. Candidates
for narrowing, for jrp to decide, required until he does: VoiceOver once,
in GarageBand, since VST3 and the Audio Unit share the window; and a tail
longer than an Audacity clip's silent ending, recorded as unverified.

**Checked only by automated tests so far:** keeping
the playing filter and its tail when restored state or a preset holds it
(PLUG-38, 0.13.20); **Choose…** accepting a filter with fewer inputs than the
host's bus (PLUG-33, 0.13.20); a preset saved at the defaults resetting the
settings when chosen in the window (PLUG-35, 0.13.20); the quicker restore
of the playing filter (PLUG-39 step 1a) and the idle after the tail
(EDGE-11), both 0.14.0; the rest of PLUG-39's consolidation of the load
and restore path (0.15.0 to 0.17.x), whose REAPER and GarageBand check is
checks 2, 5 and 8 on 0.18.29; and, in 0.18.x, a
configuration list loading by itself once the host plays (PLUG-40), a
chosen file waiting for audio in the current start (PLUG-41), a filter
that does not suit the host kept until it does (PLUG-42), Status
naming a setting not in use (DIAG-10), the window's plainer wording
(PLUG-44), the stopped tail at the filter's own rate (PLUG-46), a
Latency chosen while stopped (AU-19) and a list loaded by itself joining
Recent (PLUG-47). **Unverified, not defects:** listening in every
host, VoiceOver, playback while REAPER refuses a preset, and the Audacity
steps not yet run; the required ones among them need running or a scope
decision from jrp. The Audio Unit in REAPER, other hosts and two instances
side by side are follow-up work under HOST-1 and AU-4.

## Performance: what is measured

The figures and their sources are in
[FFT measurements](../develop/fft-measurements.md),
[load timing](../develop/load-timing.md) and TODO's
[loading responsiveness](../../TODO.md#loading-responsiveness);
none qualifies a workload by itself ([responsiveness qualification](../develop/releasing.md#responsiveness-qualification)).

- **Loading** (PERF-9 to PERF-11, 5 November 2026, before PERF-2): how
  long a load holds the host's thread. With a 30 s filter, CI held it 28 to
  154 ms and the Linux cloud VM 227 to 254 ms, against jrp's 250 ms limit.
  A run whose callbacks cannot be judged counts as not qualified.
- **Release qualification** (REL-6, 8 November 2026): on jrp's MacBook Pro,
  all 24 workload and entry-point combinations passed for the 0.12.5 source;
  longest hold 43.2 ms, longest judged callback 2.95 ms
  ([record](../archive/reviews/2026-11-08-rel6-qualification.md)). It
  predates the non-uniform engine and has not been repeated.
- **The non-uniform engine** (PERF-2): 10 to 17 times less CPU than
  `v0.12.12` at the same latency for 10 and 30 s filters on an Apple M5 Max;
  Latency 0 costs 16 to 96% more than 256, most for short filters. Block
  sizes are not yet tuned for Apple silicon.
- **Idle after the tail** (EDGE-11): silence after the tail costs 93 to 99%
  less on the Linux cloud VM, music unchanged within the spread; Apple
  silicon and Windows not yet measured.
- **Supported workloads** (PERF-3, 21 November 2026): on the Linux cloud
  VM and GitHub's Linux, macOS arm64 and Windows runners, every filter that
  fits the saved state, except a full 7.1 matrix at 96 kHz, used at most 20%
  of one core with its 99th-percentile callback under half its period;
  shared machines, so indicative only. The [limits
  guide](../use/limits.md#filter-length-channels-and-processor-load) states
  them; jrp's Mac has not run them. Stereo room correction and the 2-to-6
  crossover have an everyday budget of 3% of one core at 48 kHz, Latency 0
  and 64-frame callbacks (0.6 to 2.1% then), checked on the runners before
  each preview (PERF-21,
  [the everyday budget](../develop/fft-measurements.md#the-everyday-budget)).

## Compatibility guarantees

Four promises, kept separate because each breaks in a different way. Changing
any of them is an owner's decision.

| Guarantee | What is promised | Owning guide | Checked by |
| --- | --- | --- | --- |
| `.cfg` and list syntax | Existing files keep loading with the same routes, gains, delays, paths and list selection; syntax changes need a recorded decision | [Configuration](../use/configuration.md), [compatibility](../use/other-programs.md) | Compatibility corpus, parser and fuzz replay |
| Serialized plug-in state and presets | Every version-1 preset whose rate and counts fit keeps loading (except one made before PLUG-13 within 256 bytes of 64 MiB; none was published); version 2 only marks a failed first load | [Presets](../use/presets.md#old-and-new-versions), [presets without a layout](../develop/plugin-identities.md#presets-without-a-layout) | `native.state`, `vst3.adapter`, `plugins.presets`; presets and audio kept from 0.13.2, 0.13.12, 0.13.20, 0.13.24, 0.14.0 and 0.18.29, and 0.14.0's and 0.18.29's LV2 and Audio Unit state (`upgrade.cli`, `vst3.upgrade`, `lv2.upgrade`, the Audio Unit workflow's state check, COMPAT-1) |
| Plug-in identity | VST3 class IDs, AU codes, LV2 URIs and bundle IDs never change after release | [Identities](../develop/plugin-identities.md) | `identities` (frozen values), `vst3.identities` |
| C++ API and CMake package | Before 1.0, incompatible changes only in a new minor version; consumers require a matching minor version | [Versioning](../develop/versioning.md#minor-and-major-versions), [package guide](../develop/library.md) | Installed and source-tree consumer tests |

## What the tests do not establish

[Testing](../develop/testing.md) maps each test to the behaviour it checks.
Neither a workflow file nor a source tag proves a pass: check
[Actions](https://github.com/jrp2014/convolver/actions) for a revision's
results. Automated tests do not establish compatibility in a named
commercial DAW, hard real-time deadlines, signed or notarized distribution,
or native builds of the historical Windows applications. Performance
measurements apply to their stated source, hardware and builds; no FFT
backend is established as fastest for every workload. Adoption of the `.cfg`
format by another program does not establish identical selection, path or
routing behaviour ([other programs](../use/other-programs.md)).

## Direction

1. Finish a dependable product: someone unfamiliar with the repository can
   download, install and use an exact preview package by following the
   documentation, in named hosts, with reproducible packages.
2. Preserve configuration and stream behaviour, tested against independent
   references and against files kept from released builds.
3. Improve speed only for measured workloads, keeping latency, memory and
   callback behaviour visible beside throughput; resource budgets first.
4. Keep maintenance cost down: move code into shared layers only when two
   formats need the same behaviour, and do not generalise host lifecycle or
   release machinery beyond the requirements in hand.
5. Be better than the most popular reverb plug-ins where it can be measured
   and checked, in the ranked order of the
   [edges plan](plans/2026-11-06-edges-over-other-convolvers.md): the
   limitations users cite first, after the walkthroughs. Precision beyond
   what can be heard is not a goal.

The plug-in direction is in the [decisions log](decisions.md#direction):
VST3, LV2 and AU in parallel, CLAP on request. The dependency choices behind
the build are in the
[implementation map](../develop/architecture.md#dependency-choices), and
where the code came from is in [history](history.md).
