<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Finished TODO items, 8 to 20 November 2026

> **Archived 20 November 2026.** The entries of the
> [done](../../../TODO.md#done) list in `TODO.md` as they stood at `549ec9c`
> (0.16.8), moved here unchanged so that TODO holds open work only; only
> relative links were changed, so they resolve from this folder. The
> [repository checks](../../develop/workflow.md#automated-checks) treat an
> ID listed here as defined and done. Earlier finished items are in the
> [list to 8 November 2026](2026-11-08-done-items.md). Use the
> [current documentation](../../README.md) for how Convolver behaves now.

- [x] **HOST-7 — Run the Ardour test with Ardour 9.**
  20 November 2026, PR #229, from CI-43: Ardour 9's Lua tool crashed in
  `Session:new_audio_track`, which takes an empty `ARDOUR.RouteGroup()`
  where Ardour 8 took `nil`; Ardour 9.0's `new_session` also aborts as it
  exits, after writing the session, which the test accepts with a warning
  on 9.0 and with that message only, checked by `ardour_harness_tests.py`.
  The job runs on `ubuntu-latest` with Ardour 9 and no longer needs an older
  image ([Ardour, headless](../../develop/testing.md#ardour-headless)).
- [x] **CI-45 — Build our own sources without compiler warnings.**
  20 November 2026, PR #228, from the independent review of CI-43: the
  newer compilers CI-43 brought in warned about Convolver's own code. GCC
  12 to 15 flagged the tests' replacement allocators and an `std::optional`
  refill (false positives, now silenced beside them), copies in range loops and a possibly dangling
  reference; Xcode 26.6 flagged mixed enumerations and a nonblocking
  function that may throw in the Audio Unit; Visual Studio 2022 flagged
  narrowing, shadowed names and `getenv`. Each is fixed or, where the
  compiler is wrong or its advice does not apply, silenced at that line with
  the reason. Output is unchanged
  ([compilers and CI machines](../../develop/build.md#compilers-and-ci-machines)).
- [x] **PLUG-36 — The loading-and-failure table covers each transition.**
  20 November 2026, PR #225, in PLUG-39's sequence after step 1b: the contract's
  [states and transitions](../../develop/plugin-contract.md#states-and-transitions)
  give the empty, active, kept-but-unsuited and failed states against every
  request and host event, with what plays, what is saved, what Status shows
  and the tests for each row. One reproduced defect fixed: with no filter
  heard, Status and the window's report ignored Bypass.
- [x] **ROBUST-8 — Fault record after a failed Latency change.**
  20 November 2026, PR #223, from the independent review of VST3-16 (PR
  #134), in PLUG-39's sequence right after step 3c: `HostFilter::activate()`
  cleared the fault count, the notice and an activation failure before the
  new processor was built, so a Latency that ran out of memory could bring
  back Fault for faults a load had cleared. `activation()` now finds what to
  play and changes nothing, and every adapter calls `activate()` only once
  the new processor will play
  ([status and log](../../develop/plugin-contract.md#status-and-log)).
- [x] **CI-44 — Check the Linux package's libstdc++ versions.**
  20 November 2026, PR #227, from the independent review of CI-43:
  `verify_artifact.py` refused glibc symbols newer than Ubuntu 22.04's but
  not libstdc++ ones, so a newer GCC on the build image would have shipped
  a package that does not load there or on Debian 12. It now refuses
  GLIBCXX and CXXABI versions above GCC 12's (`max_glibcxx`, `max_cxxabi`)
  ([compilers and CI machines](../../develop/build.md#compilers-and-ci-machines)).
- [x] **CI-43 — Keep compilers and CI machines on their newest releases.**
  19 November 2026, PR #222, from jrp's question whether Convolver uses the
  latest SDKs: the libraries already followed their newest releases, but the
  compilers and runner images did not. CI now takes the newest generally
  available images, GCC and Xcode, the newest LLVM release branch and the newest
  stable Python, and the LV2 tests use the newest Ubuntu image's lilv; only
  the Linux and Windows packages and the Ardour test (HOST-7) keep older
  images, for recorded reasons ([compilers and CI machines](../../develop/build.md#compilers-and-ci-machines)).
- [x] **CI-42 — Run only the workflows that read the files kept from
  releases.** 19 November 2026, PR #221, from jrp's audit of v0.14.0:
  `ci_scope.py` counted `tests/upgrade/` as code, so changing one retained file ran every
  workflow; it now runs the native and VST3 ones, whose `upgrade.cli` and
  `vst3.upgrade` read them, and `tests/ci_scope_tests.py` checks that no
  other build reads them
  ([CI triggers](../../develop/testing.md#pull-requests-for-one-plug-in-format)).
- [x] **PERF-20 — Compare engine revisions that differ outside
  `engine.cpp`.** 19 November 2026, PR #220, from jrp's audit of v0.14.0:
  `compare_engines.py` refused two revisions with the same `engine.cpp`, so a
  change to the partitioner, kernels or an FFT backend got "nothing to
  compare". It now compares any two commits, warns when nothing under
  `portable/` or `include/`, nor the CMake files or the benchmark's source,
  differs (without concluding that the timings are noise, since the
  latencies may differ), and refuses the same commit before building
  ([the script](../../../tests/native/benchmarks/compare_engines.py)).
- [x] **CI-41 — Run the timing harness's self-test once per build type.**
  19 November 2026, from jrp's review of `212fe6e`: `native.load_timing_harness`
  carries the CTest label `backend-independent`, which the native jobs'
  Accelerate and Ooura builds, and the g++ and MSVC core-only builds,
  leave out
  ([which configurations run which tests](../../develop/testing.md#which-configurations-run-which-tests)).
- [x] **DIAG-8 — Say why +1.0 clips without claiming it always does.** 19
  November 2026, from jrp's review of `212fe6e`: DIAG-5's clipping report
  said "+1.0 always clips", untrue with dither; it now says +1.0 is above
  the largest representable positive integer sample, so leave headroom for
  quantization ([file rendering](../../use/command-line.md#file-rendering)).
- [x] **DOC-17 — Keep product claims current and substantiated.** 19
  November 2026, PR #216, from jrp's edits on `codex/maintain-product-claims`
  (`c623b23`), reconciled with DOC-16: claims in the README, user guides and
  release notes rest on dated evidence, keep its scope and are refreshed in
  the change that affects them
  ([documentation](../../develop/documentation.md#current-claims-and-dated-evidence)).
- [x] **DOC-16 — Reconcile the contributor guidance with current
  decisions.** 18 November 2026, PR #206, from jrp's review of `e89a9dc`: the
  [execution priorities](../../../TODO.md#execution-priorities) sort open work by what it
  waits for instead of by the lifted freeze; the
  [verification table](../../develop/workflow.md#verification-for-each-kind-of-change)
  decides what to run when starting, resuming and finishing; later review
  rounds read the new commits; automatic reload is described as a debounce
  ([plug-ins guide](../../use/plugins.md#convolvers-window)); source comments state
  their constraint ([documentation](../../develop/documentation.md#current-claims-and-dated-evidence)).
- [x] **CI-40 — Static analysis covers every portable source.** 18
  November 2026, PR #206, from jrp's review of `e89a9dc`: CI and CONTRIBUTING ran
  clang-tidy on a hand-kept list that had missed `portable/partitions.cpp`;
  both now take the portable sources from the build's
  `compile_commands.json`, leaving out the vendored `portable/third_party/`
  ([lint checks](../../../CONTRIBUTING.md#formatting-and-analysis)).
- [x] **CI-34 — Reduce duplicate CI scheduling, preserving coverage.** 18
  November 2026, PR #207 (the first step, the candidates, in PR #154): pull
  requests build the candidates and run the fuzz targets only when they
  change what those are built from; `main`, weekly and manual runs keep
  both ([CI triggers](../../develop/testing.md#when-pull-requests-run-the-fuzz-targets),
  [plan](../../archive/plans/2026-11-08-ci-scheduling.md)).
- [x] **DIAG-5 — Report clipping after a command-line render.** 18
  November 2026, PR #208, from the BruteFIR comparison: after a render, stderr gives
  each affected channel's clipped-sample count and peak in dBFS and
  suggests `--gain-db no-clip`; float output above full scale gets a note;
  the samples and exit status are unchanged
  ([file rendering](../../use/command-line.md#file-rendering)).
- [x] **DOC-15 — Say what sets Convolver apart in the README.** 17
  November 2026, PR #201: the README's "What sets it apart" section, under
  jrp's four emphases (accurate, light on the system, robust, behaves as
  you'd expect), each claim linked to its evidence
  ([README](../../../README.md#what-sets-it-apart)).
- [x] **CI-39 — Windows core-only steps fail when their build fails.** 17
  November 2026, from CI-38's independent review: the two core-only steps
  of the native Windows job stop at a failed configure or build
  (`$PSNativeCommandUseErrorActionPreference`), shown on its pull request by a
  deliberate build failure.
- [x] **CI-38 — Safe CI speedups without restructuring.** 16 November
  2026, from jrp's request that day: the longest core tests first
  ([test order](../../develop/testing.md#build-targets-and-test-order))
  and the advisory pluginval pre-release off pull requests
  ([pre-releases](../../develop/build.md#pre-releases-and-newer-upstream-versions)).
- [x] **LV2-9 — Closing waits for a queued Gain mode request.** 16
  November 2026, from pluginval failures on `main`
  ([run 43910524162](https://github.com/jrp2014/convolver/actions/runs/43910524162)):
  JUCE's LV2 host ran a Gain mode request after freeing the instance
  (an AddressSanitizer build under pluginval shows the use after free in
  `work()`); glibc's assertion, which only robust or priority-inheritance
  mutexes reach, points to JUCE's own per-instance work lock, freed with
  it and locked before `work()` is called. It may also explain the "Fuzz
  parameters" timeout in
  [run 43912718333](https://github.com/jrp2014/convolver/actions/runs/43912718333);
  that is not reproduced or confirmed.
  `cleanup()` waited for a queued Latency alone; it now waits for every
  request `run()` queues one at a time, PR #198
  ([format notes](../../develop/plugin-contract.md#format-notes)).
- [x] **PLUG-5 — One release pipeline.** Done for the previews, 12 November
  2026 (DOC-14): one candidate pipeline packages the VST3 plug-in, the Audio
  Unit, LV2 on Linux, the CLI, the tools and the examples for macOS,
  Windows and Linux (PRs #38, #101, #115, #163, #179;
  [what a package holds](../../develop/releasing.md#what-a-package-holds)).
  Signing and the other production gates continue in REL-1.
- [x] **PLUG-9 — Editor report and loading from a file.** 12 November 2026
  (DOC-14): the report, `report_text()` and Copy Diagnostics, and the
  shared file-to-state step (PR #84), with the computed gains since PR
  #188 ([status and log](../../develop/plugin-contract.md#status-and-log)).
  The meters go into the report with PLUG-10.
- [x] **LV2-5 — Release bundles.** Done for Linux, 12 November 2026
  (DOC-14): the Linux package holds `LV2/convolver.lv2` since 0.13.12, with
  `VERSION` mapped to LV2's minor and micro versions as jrp decided on
  10 November 2026 (PR #179; [LV2 versions](../../develop/versioning.md#lv2-versions)).
  Distribution packages and macOS or Windows LV2 bundles are separate
  possibilities, taken up only on request.
- [x] **AU-5 — Release.** Done for the previews, 12 November 2026 (DOC-14):
  the unsigned Audio Unit component, with its installer package, symbols
  and notices, ships in the macOS previews since 0.12.12 and `auval -strict`
  checks the installed component (PR #115;
  [what a package holds](../../develop/releasing.md#what-a-package-holds)).
  Signing and notarization continue in REL-1.
- [x] **AU-7 — Settings as Audio Unit parameters.** 12 November 2026
  (DOC-14): Gain (0), Mix (1003), Latency (1004) and the Gain mode (1002)
  as global parameters, Latency with its change notification (PRs #80, #95,
  #188; [settings](../../develop/plugin-contract.md#settings)). Their
  GarageBand checks are AU-4's; the meters come with PLUG-10.
- [x] **PERF-1 — Reproducible current benchmarks.** Joined PERF-3,
  12 November 2026 (DOC-14): reruns on named machines are part of PERF-3's
  one measurement exercise. The records so far are in
  [FFT measurements](../../develop/fft-measurements.md).
- [x] **PLUG-38 — Restoring the playing filter keeps its history.**
  13 November 2026, PR #193: restored state or a preset holding the filter
  already playing keeps it and its history, and the settings ramp; checked by
  the plug-in harness in every format and `vst3.adapter`
  ([scope of each change](../../develop/plugin-contract.md#scope-of-each-change)).
- [x] **DOC-14 — One backlog consolidation pass.** 12 November 2026, PR #191: open
  items say what remains, what would justify it and when it is done; their
  earlier text is in the
  [archive](../../archive/todo/2026-11-12-open-item-history.md)
  ([recording progress](../../develop/workflow.md#recording-progress-in-todo)).
- [x] **PLUG-37 — The scope of each change.** 12 November 2026, PR #191:
  jrp's rule
  that each change does only the work it needs, with the history check in
  `native.host_settings`
  ([scope of each change](../../develop/plugin-contract.md#scope-of-each-change)).
- [x] **PLUG-35 — Saved state records the settings at their defaults.**
  12 November 2026, PR #189: the VST3 plug-in's component state records
  Gain, Mix and Latency whenever it saves them, defaults included (host
  state version 3), so a
  `.vstpreset` a host saves at 0 dB, 100% and 0 frames resets them when
  chosen in the window, as the host's preset menu does
  ([decision](../../project/decisions.md#plug-in-behaviour)).
  Presets `convolver-preset` makes from a filter, and the Audio Unit's and
  LV2's states, still write version 1, and older presets and projects load
  as before; one saved at the defaults by an earlier version still leaves
  the settings. Cost: 0.12.12, the first preview, refuses a project saved
  at Latency 0 (now every one at the defaults) as damaged. Checked by `native.state`
  and `vst3.adapter`; not yet tried in a host.
- [x] **PLUG-26 — Choose a `.vstpreset` in the window.** 12 November
  2026, PR #185: Choose… and dropping a file in the VST3 and Audio Unit
  windows also open a Convolver `.vstpreset`, read as `convolver-preset`
  reads it, with the Gain, Mix and Latency it holds reported to the host
  ([decision](../../project/decisions.md#plug-in-behaviour)); another
  plug-in's preset or a damaged one is refused with a fix. Checked by
  `vst3.adapter` and, on macOS, `au.adapter`; not yet tried in a host
  ([a preset in the window](../../use/plugins.md#a-preset-in-the-window)).
  A preset a host saved at the default settings left them, until
  PLUG-35.
- [x] **PLUG-33 — A chosen file follows the same rules as a preset.**
  12 November 2026, PR #184: a file chosen in a window or through LV2's
  file parameter needs the host's outputs exactly and at least its inputs,
  as a preset does; from a list, the exact entry first, then the one with
  the most inputs below the host's, named in the log and the window
  ([decision](../../project/decisions.md#plug-in-behaviour)); with no
  filter, Status names choosing a filter where there is a window
  ([channels](../../use/plugins.md#channels),
  [channel layouts](../../develop/plugin-contract.md#channel-layouts)).
- [x] **LV2-7 — Settings as control ports.** 10 November 2026, PR #182:
  Gain, Mix and Latency as the control inputs `gain`, `mix` and
  `latency_frames` after the atom ports; a new Latency while active is built
  on the worker thread. Checked by `lv2.adapter`; older sessions in named
  hosts were not opened, as jrp's decision for LV2 in the previews allows
  ([LV2 ports](../../use/plugins.md#lv2-ports),
  [adapter contract](../../develop/plugin-contract.md#format-notes)).
- [x] **PLUG-34 — The window's behaviour in shared code.** 10 November
  2026, PR #181: what the window shows and what a user's action means, in
  `portable/window_model.hpp`, used by the macOS window with no visible
  change and by the Windows one
  ([implementation map](../../develop/architecture.md#the-shared-macos-window)).

- [x] **IO-11 — WAV files with unfilled sizes are read.** 10 November
  2026, PR #176. jrp chose to read them everywhere: a WAV whose sizes were
  left at `0xFFFFFFFF` (FFmpeg writing to a pipe) is read to its end,
  knowing that FFmpeg stopped half way leaves the same header and that its
  file is read as if complete. One of 4 GiB or more is still refused.
  macOS's reader is shown such a file with its sizes filled in, because
  left to open one it read 15 bytes of mono 24-bit audio as four frames
  and 18 as five
  ([input files](../../use/command-line.md#input-files),
  [decision](../../project/decisions.md#identities-and-file-formats)).
- [x] **IO-10 — WAV files that other programs read are refused.** 9
  November 2026, PR #176, from that day's project review: a WAV whose last
  odd chunk lacks its padding byte (Python's `wave` module) is read whole
  ([input files](../../use/command-line.md#input-files),
  [decision](../../project/decisions.md#identities-and-file-formats)).
- [x] **CI-36 — VST3 tests under sanitizers in CI.** 9 November 2026,
  from the whole-project review: ASan and UBSan for the `vst3.*` tests,
  ThreadSanitizer for `vst3.adapter`
  ([VST3 tests](../../develop/testing.md#vst3-tests)).
- [x] **TIDY-1 — Remove small duplicated helpers.** 9 November 2026, from
  the whole-project review of that day (M2; its `utf8_path` part is
  DIAG-7): one copy each of `splitmix64`, `ports()` and the little-endian
  helpers, the window's status numbers named, and the unused `fft_size()`
  removed ([implementation map](../../develop/architecture.md#components-and-source-of-truth)).
- [x] **CI-37 — Test programs report every failing case.** 9 November
  2026, from the whole-project review of that day: ten programs run their
  cases through one shared runner
  ([native production tests](../../develop/testing.md#native-production-tests)).
- [x] **DIAG-6 — Show the reader's reason when a sound file is refused as
  a filter.** 9 November 2026, from the whole-project review of that day
  ([filter files](../../use/configuration.md#filter-files)).
- [x] **DIAG-7 — File names as UTF-8 in messages and paths on Windows.** 9
  November 2026, from the whole-project review of that day: one shared
  helper, and a non-ANSI name loaded and refused on every system
  ([invariants](../../develop/architecture.md#invariants-to-preserve)).
- [x] **CI-35 — Prose edits to the first-use pages run Lint only.** 8
  November 2026, from jrp's question why a README link ran every workflow;
  [CI triggers](../../develop/testing.md#documentation-only-pull-requests).
- [x] **REL-9 — Windows and Linux in previews.** 8 November 2026,
  PR #163: from the second preview the draft release attaches the Windows
  and Linux packages, after they run on clean machines
  ([publishing](../../develop/releasing.md#publishing-an-unsigned-preview),
  [decision](../../project/decisions.md#releases)). jrp published the first
  such preview, [0.13.2](https://github.com/jrp2014/convolver/releases/tag/v0.13.2), on 9 November 2026, and the
  next, [0.13.12](https://github.com/jrp2014/convolver/releases/tag/v0.13.12), on 11 November 2026, then
  [0.13.20](https://github.com/jrp2014/convolver/releases/tag/v0.13.20) and [0.13.24](https://github.com/jrp2014/convolver/releases/tag/v0.13.24) on 16 November 2026,
  and [0.14.0](https://github.com/jrp2014/convolver/releases/tag/v0.14.0) on 19 November 2026.
- [x] **REL-7 — Apply bounded preview acceptance.** 8 November 2026,
  PRs #156 and #159; jrp published [0.12.12](https://github.com/jrp2014/convolver/releases/tag/v0.12.12) without the remaining
  host checks and REL-3, which stay open under HOST-1, AU-4 and REL-3
  ([checks left](../../project/status.md#checks-left-before-publication)).
- [x] **AU-17 — Audio Unit listeners told on the main thread.** 8
  November 2026, PR #158, from a ThreadSanitizer report on `main`
  ([run 42512164144](https://github.com/jrp2014/convolver/actions/runs/42512164144));
  [status and log](../../develop/plugin-contract.md#status-and-log).
- [x] **REL-8 — Rehearse publishing before the preview.** 8 November
  2026, PR #152; jrp's rehearsal from `main` on the v0.12.5 candidate
  ([run 42505146594](https://github.com/jrp2014/convolver/actions/runs/42505146594))
  made the draft, which he read ("That looked fine to me") and deleted;
  [publishing](../../develop/releasing.md#publishing-an-unsigned-preview).
- [x] **DOC-13 — Shorter backlog, superseded plans and evidence archived.** 8
  November 2026; [archived TODO entries](../../archive/todo/README.md),
  [archived plans](../../archive/plans/README.md).
- [x] **DOC-10 — One short route to load a filter.** 8 November 2026,
  PR #157, from jrp's file-loading review; [your first sound in a
  host](../../use/first-sound-in-a-host.md), [inputs](../../use/plugins.md#loading-a-filter),
  [preset folders](../../use/presets.md#preset-folders).
- [x] **PLUG-31 — Generated VST3 presets carry the filter's name.** 8
  November 2026, PR #148, checked in REAPER;
  [presets](../../use/presets.md),
  [record](../../archive/reviews/2026-11-08-plug31-reaper.md).
