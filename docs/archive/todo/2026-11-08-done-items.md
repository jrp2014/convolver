<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Finished TODO items to 8 November 2026

> **Archived 8 November 2026.** The one-line entries of the
> [done](../../../TODO.md#done) list in `TODO.md` as they stood at `21711c5`
> (0.12.7), moved here so that TODO holds open work only. Each line keeps the
> item's ID, title, date, pull request and the page that describes the
> result; only relative links were changed, so they resolve from this folder.
> The [repository checks](../../develop/workflow.md#automated-checks) treat an
> ID listed here as defined and done. The longer entries written before
> 7 November 2026 are in the [earlier record](2026-11-06-done-entries.md).
> Use the [current documentation](../../README.md) for how Convolver behaves
> now.

- [x] **AU-1 — Adapter and test.** 23 October 2026, PR #44, with the bypass
  follow-up in PR #49; [plug-in guide](../../use/plugins.md).
- [x] **AU-2 — Loading filters.** 23 October 2026, PR #44;
  [presets](../../use/presets.md).
- [x] **AU-3 — Validation.** 8 November 2026: `auval -strict` since PR #44,
  pluginval on 6 November, and `au.adapter` under ThreadSanitizer in PR #150;
  [Audio Unit tests](../../develop/testing.md#audio-unit-tests).
- [x] **AU-8 — Split the adapter's source by responsibility.** 31 October 2026,
  PR #92; [implementation map](../../develop/architecture.md).
- [x] **AU-12 — Wake the Latency work with a dispatch source.** 5 November
  2026, PR #109; [Audio Unit settings](../../develop/plugin-contract.md).
- [x] **AU-14 — A completion witness for the Latency work.** 5 November 2026.
- [x] **AU-15 — Render through AVAudioEngine.** 6 November 2026; [AVAudioEngine
  render](../../develop/testing.md#avaudioengine-render).
- [x] **AU-13 — Safe partial construction of the Latency work.** 5 November
  2026, PR #111.
- [x] **AU-16 — Show a bypass switched while the control lock is held.**
  8 November 2026, PR #143; [Status and log](../../develop/plugin-contract.md#status-and-log).
- [x] **AU-11 — Separate file watching from the window.** 5 November 2026;
  [shared macOS window](../../develop/architecture.md#the-shared-macos-window).
- [x] **AU-10 — Separate the window's setting editing from its layout.** 1
  November 2026, PR #100; [implementation map](../../develop/architecture.md).
- [x] **AU-9 — Simplify the macOS plug-in code.** 31 October 2026, PR #93.
- [x] **CI-1 — Skip builds and tests for documentation-only pull requests.** 24
  October 2026, PR #63; [documentation-only pull
  requests](../../develop/testing.md#documentation-only-pull-requests).
- [x] **CI-2 — Shorten CI without losing coverage.** 24 October 2026, PR #65;
  [which configurations run which
  tests](../../develop/testing.md#which-configurations-run-which-tests).
- [x] **CI-3 — Latest stable Clang tools.** 24 October 2026, PR #69;
  [formatting and analysis](../../../CONTRIBUTING.md#formatting-and-analysis).
- [x] **CI-4 — Report the oracle error relative to the signal.** 24 October
  2026, PR #74; [native
  tests](../../develop/testing.md#native-production-tests).
- [x] **CI-5 — No actions on a deprecated Node.js.** 24 October 2026, PR #76;
  [lint checks](../../../CONTRIBUTING.md#lint-checks).
- [x] **CI-6 — Keep supporting libraries on their newest releases.** 24 October
  2026, PR #77; [supporting
  libraries](../../develop/build.md#supporting-libraries).
- [x] **CI-7 — Resolve shared SDKs once per workflow.** 5 November 2026;
  [supporting libraries](../../develop/build.md#supporting-libraries).
- [x] **CI-8 — Keep evidence from advisory failures.** 5 November 2026;
  [evidence from CI
  runs](../../develop/testing.md#evidence-from-failed-ci-runs).
- [x] **CI-9 — Shorter CI jobs, no test dropped.** 25 October 2026, PR #85;
  [build targets and test
  order](../../develop/testing.md#build-targets-and-test-order).
- [x] **CI-10 — One Linux source build for candidates.** 27 October 2026, PR
  #86; [candidate checks](../../develop/testing.md#plug-in-candidate-checks).
- [x] **CI-11 — FFT backend configurations as parallel jobs.** 27 October 2026,
  closed without the split, PR #86; [CI
  triggers](../../develop/testing.md#ci-triggers).
- [x] **CI-12 — Fuzz campaigns side by side.** 27 October 2026, PR #86; [fuzz
  smoke tests](../../develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).
- [x] **CI-13 — Run only the affected format's workflows.** 27 October 2026, PR
  #86; [pull requests for one plug-in
  format](../../develop/testing.md#pull-requests-for-one-plug-in-format).
- [x] **CI-14 — Use every runner CPU.** 27 October 2026, PR #86; [build targets
  and test order](../../develop/testing.md#build-targets-and-test-order).
- [x] **CI-15 — Test the fuzz scheduler's failure paths.** 28 October 2026, PR
  #87; [fuzz smoke
  tests](../../develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).
- [x] **CI-16 — A `--jobs` limit for release builds.** 28 October 2026, PR #87;
  [preview recipe](../../develop/build.md#preview-recipe).
- [x] **CI-17 — Remove unused Python imports.** 29 October 2026, PR #88;
  [Python formatting and lint](../../../CONTRIBUTING.md#python-formatting-and-lint).
- [x] **CI-18 — Format and lint the Python with ruff.** 29 October 2026, PR
  #88; [Python formatting and
  lint](../../../CONTRIBUTING.md#python-formatting-and-lint).
- [x] **CI-22 — Make intermittent CI failures fixable.** 1 November 2026;
  [intermittent failures](../../develop/testing.md#intermittent-failures).
- [x] **CI-30 — Complete the tests' replaced allocation functions.** 5 November
  2026, PR #111.
- [x] **CI-31 — Keep `native.host_settings` within its time limit under
  sanitizers.** 5 November 2026, PR #111.
- [x] **CI-33 — Compare audio, not file bytes, in the no-clip test.** 6
  November 2026, PR #124; [intermittent
  failures](../../develop/testing.md#intermittent-failures).
- [x] **CI-29 — Use LLVM's parallel clang-tidy runner.** 5 November 2026, PR
  #108; [formatting and analysis](../../../CONTRIBUTING.md#formatting-and-analysis).
- [x] **CI-24 — Find why `native.request_worker` timed out on Windows.** 5
  November 2026, closed without a cause, PR #109;
  [evidence](../../archive/reviews/2026-11-05-request-worker.md).
- [x] **CI-25 — Find why the watchdog check hung on the macOS arm64
  candidate.** 5 November 2026, closed without a cause, PR #109;
  [evidence](../../archive/reviews/2026-11-05-request-worker.md).
- [x] **CI-28 — Keep `vst3.adapter`'s Status and log case's log as evidence.**
  5 November 2026; [evidence from failed CI
  runs](../../develop/testing.md#evidence-from-failed-ci-runs).
- [x] **CI-27 — Keep the Audio Unit adapter test's log after a crash.** 2
  November 2026; [evidence from failed CI
  runs](../../develop/testing.md#evidence-from-failed-ci-runs).
- [x] **CI-26 — Run only VST3, Audio Unit and the candidates for window
  changes.** 2 November 2026; [one plug-in
  format](../../develop/testing.md#pull-requests-for-one-plug-in-format).
- [x] **CI-23 — Run `vst3.fft_symbols` on Windows.** 1 November 2026; [VST3
  tests](../../develop/testing.md#plug-in-format-tests).
- [x] **CI-19 — Build the Apple silicon candidate on `macos-15`.** 31 October
  2026; [candidate checks](../../develop/testing.md#plug-in-candidate-checks).
- [x] **CI-20 — Keep unpacked ZIP files inside their destination.** 1 November
  2026, PR #102; [candidates](../../develop/releasing.md#candidates).
- [x] **CI-21 — Pin third-party actions to a commit SHA.** 1 November 2026, PR
  #102; [lint checks](../../../CONTRIBUTING.md#lint-checks).
- [x] **DIAG-1 — Review how diagnostics reach the user.** 23 to 31 October
  2026, PRs #57, #68, #72, #73, #96; [status and
  log](../../develop/plugin-contract.md#status-and-log).
- [x] **DIAG-2 — Actionable diagnostics.** 5 November 2026, PR #110;
  [troubleshooting](../../use/troubleshooting.md).
- [x] **DIAG-3 — Report faults around a filter change together.** 5 November
  2026; [contract](../../develop/plugin-contract.md#status-and-log).
- [x] **DIAG-4 — Keep messages in their parts inside the library.** 5 November
  2026; [failure reasons](../../develop/plugin-contract.md#failure-reasons).
- [x] **DOC-0 — Refresh the entry points for 0.4.0.** 20 October 2026.
- [x] **DOC-1 — Runnable first-use examples.** 22 October 2026, PR #28;
  [first-use examples](../../../examples/first-use/README.md).
- [x] **DOC-3 — Resolve adapter-plan review findings.** 21 October 2026.
- [x] **DOC-4 — Organise the documentation by reader.** 23 October 2026, PRs
  #47, #51, #52, #53; [documentation guide](../../develop/documentation.md).
- [x] **DOC-5 — Refresh the status page's evidence.** 1 November 2026, PR #99;
  [status](../../project/status.md).
- [x] **DOC-6 — One home for progress.** 1 November 2026, PR #105; [recording
  progress](../../develop/workflow.md#recording-progress-in-todo).
- [x] **DOC-7 — Archive finished entries and remove cruft.** 6 November 2026,
  PR #123, on jrp's request; [recording
  progress](../../develop/workflow.md#recording-progress-in-todo).
- [x] **DOC-8 — Tune the agent guidance and workflow.** 6 November 2026,
  PR #129, from jrp's review; [workflow](../../develop/workflow.md#kinds-of-task).
- [x] **DOC-9 — Consistent words for filters, presets and projects.** 7
  November 2026, PR #136, from jrp's review, with clearer Waiting status wording;
  [words Convolver uses](../../use/concepts.md#words-convolver-uses).
- [x] **DOC-11 — Reconcile the release status page.** 8 November 2026,
  PR #142, from jrp's review of `bf6a207`; [what still blocks the first
  preview](../../project/status.md#what-still-blocks-the-first-preview).
- [x] **FFMPEG-1 — Test the pipe workflow.** 5 November 2026;
  [tests](../../develop/testing.md#native-production-tests).
- [x] **FFT-1 — Check that we have the latest Ooura version.** 21 October 2026;
  [upstream
  comparison](../../../portable/third_party/ooura/README.md#upstream-comparison-21-october-2026).
- [x] **FFT-3 — Accelerate by default on macOS.** 30 October 2026, PR #89;
  [which libraries each build
  uses](../../develop/build.md#which-libraries-each-build-uses).
- [x] **FFT-4 — Remove the finished vDSP experiment.** 5 November 2026, PR
  #108.
- [x] **IO-3 — More output containers.** 23 October 2026, PR #60; [output
  formats](../../use/command-line.md#output-formats).
- [x] **IO-4 — Raw filters and raw sample types.** 24 October 2026, PR #60;
  [RAW streams](../../use/command-line.md#raw-streams).
- [x] **IO-7 — Dither defaults by output resolution.** 25 October 2026, PR #79;
  [integer
  output](../../use/command-line.md#integer-formats-dither-and-noise-shaping).
- [x] **IO-9 — Shortest float text from the standard library.** 5 November
  2026, PR #109; [building for macOS before
  14](../../develop/build.md#building-for-macos-before-14).
- [x] **IO-6 — FLAC in the preview packages.** 24 October 2026, PR #60; [FLAC
  and Ogg](../../use/command-line.md#flac-and-ogg).
- [x] **HOST-5 — A headless Ardour session in CI.** 6 November 2026; [Ardour,
  headless](../../develop/testing.md#ardour-headless).
- [x] **LV2-1 — Adapter and headless test.** 22 October 2026, PR #43; [LV2
  tests](../../develop/testing.md#lv2-tests).
- [x] **LV2-2 — Loading filters.** 23 October 2026, PR #48; [LV2
  tests](../../develop/testing.md#lv2-tests).
- [x] **LV2-8 — Status after a request the host drops.** 6 November 2026,
  closed without a change, PR #122; [worker
  lifecycle](../plans/2026-10-21-plugin-formats.md#lv2-adapter-design).
- [x] **PERF-4 — Compiler flags and CPU-specific kernels.** 24 October 2026, PR
  #61; [compiler flags and AVX2
  kernels](../../develop/fft-backends.md#compiler-flags-and-avx2-kernels).
- [x] **PERF-5 — Track valid input history.** 24 October 2026, PR #70;
  [startup, reset and
  loading](../../develop/fft-backends.md#startup-reset-and-filter-loading).
- [x] **PERF-6 — Decode each filter file once per load.** 24 October 2026, PR
  #70; [startup, reset and
  loading](../../develop/fft-backends.md#startup-reset-and-filter-loading).
- [x] **PERF-9 — Load timing test.** 5 November 2026, PR #111; [load
  timing](../../develop/testing.md#load-timing-perf-9).
- [x] **PERF-10 — The load-timing gate cannot pass unqualified.** 5 November
  2026; [load timing](../../develop/testing.md#load-timing-perf-9).
- [x] **PERF-11 — Quiet noise as the load-timing input.** 5 November 2026.
- [x] **DOC-2 — Maintain documentation automatically.** 6 November 2026, PR
  #118; [automated checks](../../develop/documentation.md#automated-checks).
- [x] **GAIN-1 — Define no-clip headroom precisely.** 6 November 2026, PR #118;
  [gain](../../use/command-line.md#gain-and-attenuation).
- [x] **GAIN-3 — `no-clip` reserves the integer headroom.** 6 November 2026;
  [gain](../../use/command-line.md#gain-and-attenuation).
- [x] **PERF-12 — Load-timing verdicts from records, tested without timing.** 5
  November 2026; [load timing](../../develop/testing.md#load-timing-perf-9).
- [x] **PERF-14 — Load timing from a full history.** 5 November 2026.
- [x] **VST3-16 — A new Latency in REAPER.** 8 November 2026, PR #134;
  [settings](../../use/plugins.md#settings),
  [acceptance](../../archive/reviews/2026-11-08-vst3-16-reaper-acceptance.md).
- [x] **PLUG-28 — Name the packaged presets after their filter.** 8
  November 2026, PR #141 and, for the first-use presets, PR #144; [first sound in a host](../../use/first-sound-in-a-host.md#load-the-examples).
- [x] **PLUG-23 — A filter reads the first inputs of a wider input bus.** 6
  November 2026; [channels](../../use/plugins.md#channels).
- [x] **PLUG-22 — Name the filter in the window, and say each fact once.** 7
  November 2026, PR #128; [Convolver's
  window](../../use/plugins.md#convolvers-window).
- [x] **PLUG-21 — Teardown tested at chosen stages.** 5 November 2026.
- [x] **PERF-13 — Load-timing records as JSON.** 5 November 2026; [load
  timing](../../develop/testing.md#load-timing-perf-9).
- [x] **PERF-15 — Subnormal numbers flushed while processing.** 5 November
  2026, PR #117; [subnormal
  numbers](../../develop/architecture.md#subnormal-numbers).
- [x] **GAIN-2 — No noise simulation for `--gain-db no-clip`.** 5 November
  2026, PR #111.
- [x] **PLUG-1 — FFT backend for plug-ins.** 21 October 2026; [plug-in
  builds](../../develop/fft-backends.md#plug-in-builds).
- [x] **PLUG-2 — Shared adapter test harness.** 22 October 2026, PR #28;
  [plug-in comparisons](../../develop/testing.md#plug-in-comparisons-plug-2).
- [x] **PLUG-3 — One preset tool.** 22 October 2026, PR #32; [preset
  files](../../develop/plugin-contract.md#preset-files).
- [x] **PLUG-4 — Layouts and identities.** 22 October 2026, PR #27; [plug-in
  identities](../../develop/plugin-identities.md).
- [x] **PLUG-6 — Pin format dependencies.** 22 October 2026, PR #31;
  [supporting libraries](../../develop/build.md#supporting-libraries).
- [x] **PLUG-7 — Lifecycle sequence tests.** 23 October 2026, PR #58;
  [lifecycle sequences](../../develop/testing.md#lifecycle-sequences-plug-7).
- [x] **PLUG-8 — Keep a structured failure reason.** 23 October 2026, PR #46;
  [failure reasons](../../develop/plugin-contract.md#failure-reasons).
- [x] **PLUG-12 — Accept only filters that can be saved.** 31 October 2026;
  [host state](../../develop/plugin-contract.md#host-state).
- [x] **PLUG-13 — Keep room for the settings in saved state.** 1 November 2026,
  PR #105; [host state](../../develop/plugin-contract.md#host-state).
- [x] **PLUG-15 — One macOS deployment target.** 5 November 2026, PR #108;
  [build options](../../develop/build.md#build-options).
- [x] **PLUG-17 — Ramps measured in time rather than frames.** 5 November 2026,
  PR #109; [contract](../../develop/plugin-contract.md#settings).
- [x] **PLUG-19 — One frame's allowance at a ramp's end.** 5 November 2026;
  [settings](../../develop/plugin-contract.md#settings).
- [x] **PLUG-18 — Exact bypass and mute ramp endpoints.** 5 November 2026, PR
  #111; [settings](../../develop/plugin-contract.md#settings).
- [x] **PLUG-16 — Name the transition lengths.** 5 November 2026, PR #108;
  [contract](../../develop/plugin-contract.md).
- [x] **REL-2 — Define the first release by user journeys.** 6 November 2026,
  PR #118; [check a stereo filter](../../use/check-a-stereo-filter.md), [a
  crossover](../../use/check-a-crossover-in-a-host.md), [file
  rendering](../../use/check-files-with-the-command-line.md).
- [x] **REL-6 — Responsiveness qualification before a release.** 5 November
  2026; [procedure](../../develop/releasing.md#responsiveness-qualification).
- [x] **REL-5 — Early limited walkthrough on macOS.** 1 November 2026, PR #101;
  [check a stereo filter in a host](../../use/check-a-stereo-filter.md).
- [x] **ROBUST-3 — Silence when memory runs out after a recorded failure.** 31
  October 2026; [replacement and
  threads](../../develop/plugin-contract.md#replacement-and-threads).
- [x] **ROBUST-7 — A failed first load fails however early memory runs out.** 5
  November 2026, PR #111; [status and
  log](../../develop/plugin-contract.md#status-and-log).
- [x] **ROBUST-4 — Low-memory quantizer and non-finite gain estimates.** 24
  October 2026, PR #72; [gain](../../use/command-line.md#gain-and-attenuation).
- [x] **IO-5 — Speaker labels beyond WAV.** 6 November 2026, PR #118; [speaker
  labels](../../use/command-line.md#speaker-labels).
- [x] **ROBUST-2 — Bound report output.** 6 November 2026, PR #118; [resource
  limits](../../develop/library.md#resource-limits).
- [x] **ROBUST-6 — State the shared limits once.** 5 November 2026, PR #108;
  [resource limits](../../develop/library.md#resource-limits).
- [x] **VST3-15 — Test the messages through a host's proxy.** 5 November 2026;
  [messages](../../develop/plugin-contract.md#status-and-log).
- [x] **VST3-14 — Separate the controller from the processor's file.** 5
  November 2026; [implementation map](../../develop/architecture.md).
- [x] **VST3-1 — Speaker arrangements, not only channel counts.** 23 October
  2026, PR #42; [channel
  layouts](../../develop/plugin-identities.md#channel-layouts).
- [x] **VST3-3 — Editor messaging between controller and processor.** 1
  November 2026, PR #103;
  [messages](../../develop/plugin-contract.md#status-and-log).
- [x] **VST3-4 — Gain, mix and latency parameters.** 1 November 2026, PR #103;
  [settings](../../develop/plugin-contract.md#settings).
- [x] **VST3-7 — Follow the shared adapter contract.** 22 October 2026, PR #35;
  [adapter contract](../../develop/plugin-contract.md).
- [x] **VST3-8 — Follow the SDK documentation where the audit found gaps.** 23
  October 2026, PR #59; [adapter contract](../../develop/plugin-contract.md).
- [x] **VST3-9 — Non-integer host rates.** 5 November 2026; [when a filter does
  not fit](../../use/plugins.md#when-a-filter-does-not-fit).
- [x] **VST3-10 — Mark silent output.** 24 October 2026, PR #70; [format
  notes](../../develop/plugin-contract.md#format-notes).
