<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Finished TODO items: full entries to 6 November 2026

> **Archived 6 November 2026** (DOC-7). These are the entries of the
> [done](../../../TODO.md#done) list in `TODO.md` as they stood at `82cdc90`
> (0.10.10), before each was cut to one line as the
> [workflow guide](../../develop/workflow.md#recording-progress-in-todo) asks.
> Only relative links were changed, so they resolve from this folder. Use the
> [current documentation](../../README.md) for how Convolver behaves now.

- [x] **AU-1 — Adapter and test.** 23 October 2026, PR #44, with the bypass
  follow-up in PR #49; [plug-in guide](../../use/plugins.md),
  [Audio Unit licence](../../use/plugins.md#audio-unit-licence).
- [x] **AU-2 — Loading filters.** 23 October 2026, PR #44: host state in
  `ClassInfo` under `convolver-state`, and `.aupreset` files from the preset
  tool; loading one from a named host's preset menu is checked under AU-4;
  [presets](../../use/presets.md).
- [x] **AU-8 — Split the adapter's source by responsibility.** 31 October
  2026, PR #92, from jrp's review of 30 October: `plugins/au/effect.cpp`
  (1,166 lines) moved unchanged into `effect.cpp`, `effect_state.cpp` and
  `effect_properties.cpp`, then the repeated silence and refusal steps were
  made one function each and Core Foundation values owned by `cf_owned`; no
  behaviour changes (CHANGELOG has the one out-of-memory exception);
  [implementation map](../../develop/architecture.md).
- [x] **AU-12 — Wake the Latency work with a dispatch source.** 5 November
  2026, PR #109, from the project review of that day, on jrp's go-ahead:
  the Audio Unit applies a new Latency on a serial dispatch queue woken by a
  `DISPATCH_SOURCE_TYPE_DATA_OR` source, in place of
  `portable/request_worker.hpp`, a thread of our own. Apple does not
  document `dispatch_source_merge_data` as real-time safe; it is an atomic
  OR, then a wake-up that may make a system call to ask for a thread, and
  the open-source libdispatch neither allocates nor takes a user-space lock
  there (`au.adapter` counts only C++ allocations, so it cannot see one
  inside libdispatch). The
  worker's tests, the watchdog workflow and `scripts/ci_watchdog_probe.py`
  went with it ([archived](../reviews/2026-11-05-request-worker.md));
  [Audio Unit settings](../../develop/plugin-contract.md).
- [x] **AU-14 — A completion witness for the Latency work.** 5 November
  2026, from jrp's review of `db54160`: `au.adapter` slept 100 ms to show
  that an unchanged Latency choice sent no second notification, and 20 ms
  "for its log line" after an allocation failure, although the work logs
  after releasing the lock the test had waited for. Each unit now numbers
  its requests, and each finished run (logged, the host told) reports the
  number it had seen when it began, with the unit; the test waits until a
  run of the unit it changed that saw its change has finished, with a 10 s
  deadline that only catches a deadlock. The first version counted for the
  whole process, so one unit's finished work satisfied another's check
  (jrp's review of `5f47ecb`, reproduced with two units); a two-unit case
  now checks this. The component's only addition is a lock-free increment
  per request and a null test hook. It runs on macOS CI only.
- [x] **AU-15 — Render through AVAudioEngine.** 6 November 2026, on john's
  request for unattended host checks: the Audio Unit workflow plays the
  three first-use fixtures through the installed unit with Apple's
  AVAudioEngine and checks them against the oracle
  ([AVAudioEngine render](../../develop/testing.md#avaudioengine-render)).
- [x] **AU-13 — Safe partial construction of the Latency work.** 5 November
  2026, PR #111, from the external review of `91fcf1f` (0.8.13), a source
  review: AU-12 created the dispatch source before the semaphore, so if the
  semaphore could not be created an inactive source was released, which
  `dispatch_activate`'s documentation calls undefined. The semaphore and
  queue are now created first and the source last, and an activated source
  is released only after it is cancelled. `au.adapter` makes each creation
  fail in turn (`latency_queue_failure_for_tests`) and closes units while
  Latency work is queued or running; it runs on macOS CI only.
- [x] **AU-11 — Separate file watching from the window.** 5 November 2026,
  from jrp's review of `276c7dc`: following the playing filter's files and
  deciding when a change is due to reload moved from
  `plugins/editor/mac/diagnostic_view.mm` (now 811 lines) to
  `plugins/editor/mac/file_watch.mm`, a C++ class, so it needs no versioned
  Objective-C name; behaviour unchanged, checked by `vst3.editor` and
  `au.editor` on macOS; [shared macOS window](../../develop/architecture.md#the-shared-macos-window).
- [x] **AU-10 — Separate the window's setting editing from its layout.**
  1 November 2026, PR #100, from jrp's review of PR #98: the Gain, Mix and
  Latency rows and everything the user does with them (dragging, reset,
  typing, choosing) moved from `plugins/au/diagnostic_view.mm` (959 lines,
  now about 630) to `plugins/au/setting_rows.mm`, with the shared layout
  helpers in `plugins/au/view_layout.h`. Behaviour unchanged; `au.editor`
  unchanged. See the [implementation map](../../develop/architecture.md).
- [x] **AU-9 — Simplify the macOS plug-in code.** 31 October 2026, PR #93,
  jrp's request: behaviour-preserving simplifications of the AudioToolbox
  reader, the window and the adapter's property calls, one commit each,
  undoing no reviewed fix, plus the out-of-memory fix AU-8's review found;
  what was left alone, and why, is listed in the pull request.
- [x] **CI-1 — Skip builds and tests for documentation-only pull requests.**
  24 October 2026, PR #63; [documentation-only pull requests](../../develop/testing.md#documentation-only-pull-requests).
- [x] **CI-2 — Shorten CI without losing coverage.** 24 October 2026, PR #65:
  LV2 sanitizer stages as parallel jobs building only their targets,
  one CTest entry per package consumer, a parallel offline rebuild, ccache on
  Linux and macOS, and each kind of test run only where it can change; [which configurations run which tests](../../develop/testing.md#which-configurations-run-which-tests).
- [x] **CI-3 — Latest stable Clang tools.** 24 October 2026, PR #69:
  formatting and analysis use the latest stable packaged Clang in CI and cloud
  sessions; local installation on jrp's Mac is deferred;
  [formatting and analysis](../../../CONTRIBUTING.md#formatting-and-analysis).
- [x] **CI-4 — Report the oracle error relative to the signal.** 24 October
  2026, PR #74: `native.engine` prints the worst error relative to the output
  peak for each group of cases and kernel set, with B, and where the largest
  absolute error came from, so a new, louder test (PERF-5's
  `history_validity`, peak 7.6) no longer looks like a tenfold loss of
  accuracy; [native tests](../../develop/testing.md#native-production-tests).
- [x] **CI-5 — No actions on a deprecated Node.js.** 24 October 2026, PR #76:
  an inline MSVC developer-environment step replaces `ilammy/msvc-dev-cmd`, and
  `ccache-action` uses its newest exact release; [GitHub Actions versions](../../../CONTRIBUTING.md#lint-checks).
- [x] **CI-6 — Keep supporting libraries on their newest releases.** 24
  October 2026, PR #77, by jrp's decision: builds resolve the newest release of
  each SDK, library and tool instead of a pin, and record version, commit and
  checksum in the job summary, source and artifact manifests and, for VST3 and
  AU, the Convolver log; `sources.py --lock` repeats a recorded build. Formal
  tests use full releases; newer pre-releases (pluginval 2.0 beta) and
  upstream lilv run as advisory checks;
  [supporting libraries](../../develop/build.md#supporting-libraries).
- [x] **CI-7 — Resolve shared SDKs once per workflow.** 5 November 2026,
  from jrp's review of 0.5.2: the VST3 and Audio Unit workflows resolve
  their SDK in a first `sdk` job, whose lock file every build job of the run
  fetches with `sources.py --lock`; each job's summary still lists the
  release. LV2's formal jobs need no shared SDK (one job fetches the lv2
  headers; the rest use Ubuntu's packages), and the candidates already share
  their `sources` job's corresponding source. Per-OS tools (pluginval,
  lv2lint) are still resolved per job
  ([supporting libraries](../../develop/build.md#supporting-libraries)).
- [x] **CI-8 — Keep evidence from advisory failures.** 5 November 2026,
  from jrp's review of 0.5.2: when the newest pluginval pre-release reports
  problems, or the LV2 tests fail against the newest upstream lilv, the job
  keeps CTest's output as an `advisory-evidence-…` artifact, although it
  stays green ([evidence from CI runs](../../develop/testing.md#evidence-from-failed-ci-runs)).
- [x] **CI-9 — Shorter CI jobs, no test dropped.** 25 October 2026, PR #85,
  from jrp's CI analysis: the Linux VST3 jobs and the Audio Unit sanitizer job
  build only what their tests run, long tests carry a CTest `COST` so they
  start first, and the portable clang-tidy files are analysed side by side;
  [build targets and test order](../../develop/testing.md#build-targets-and-test-order).
- [x] **CI-10 — One Linux source build for candidates.** 27 October 2026,
  PR #86, from jrp's CI analysis: the Linux candidate is built from the
  verified archive with networking disabled and CMake's default generator, so
  the second, offline build is gone; `source_build.py` stays for local checks
  (CI no longer runs it);
  [candidate checks](../../develop/testing.md#plug-in-candidate-checks).
- [x] **CI-11 — FFT backend configurations as parallel jobs.** Closed
  without the split, 27 October 2026, PR #86, by jrp's choice: once builds used
  every CPU (CI-14) the macOS job's Accelerate and Ooura builds took about
  65–82 s instead of 120 s, and one commit already starts 6 macOS jobs against
  the account's limit of 5, so more macOS jobs would make the candidates queue
  longer; [CI triggers](../../develop/testing.md#ci-triggers).
- [x] **CI-12 — Fuzz campaigns side by side.** 27 October 2026, PR #86, from
  jrp's CI analysis: two campaigns at a time, one per core of the Linux
  runner's two, each keeping its 60 seconds and about its usual executions;
  the step went from 244 s to 122 s;
  [fuzz smoke tests](../../develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).
- [x] **CI-13 — Run only the affected format's workflows.** 27 October 2026,
  PR #86, from jrp's CI analysis: a pull request that changes only one plug-in
  format's own files (or one workflow file) runs Lint and that format's
  workflows; a test keeps every other file from depending on a format's files;
  [pull requests for one plug-in format](../../develop/testing.md#pull-requests-for-one-plug-in-format).
- [x] **CI-14 — Use every runner CPU.** 27 October 2026, PR #86: builds and
  tests used 2 of the 4 CPUs on the Linux and Windows runners; they now use
  all of them; the macOS Accelerate and Ooura builds went from about 120 s
  to 65–82 s (whole workflows vary too much between runs to show more);
  [build targets and test order](../../develop/testing.md#build-targets-and-test-order).
- [x] **CI-15 — Test the fuzz scheduler's failure paths.** 28 October 2026,
  PR #87, from the external review of PR #86: the scheduler is
  `scripts/fuzz_campaigns.sh`, and `tests/fuzz/scheduler_tests.py` checks a
  failing campaign, a missing status file and the concurrency limit with
  stand-in fuzzers;
  [fuzz smoke tests](../../develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).
- [x] **CI-16 — A `--jobs` limit for release builds.** 28 October 2026,
  PR #87, from the external review of PR #86: `candidate.py`,
  `build_dependencies.py` and `source_build.py` take `--jobs N`; the default
  stays every CPU; [preview recipe](../../develop/build.md#preview-recipe).
- [x] **CI-17 — Remove unused Python imports.** 29 October 2026, PR #88,
  from a Skylos scan: 11 unused imports in the release scripts and four test
  scripts; [Python formatting and lint](../../../CONTRIBUTING.md#python-formatting-and-lint).
- [x] **CI-18 — Format and lint the Python with ruff.** 29 October 2026,
  PR #88: the latest stable ruff in pre-commit and lint CI, with every stable
  rule but a few listed in `ruff.toml`;
  [Python formatting and lint](../../../CONTRIBUTING.md#python-formatting-and-lint).
- [x] **CI-22 — Make intermittent CI failures fixable.** 1 November 2026,
  after jrp lost time re-running jobs (his five points): Windows jobs turn
  off vcpkg's metrics, whose copy of `vcpkg.exe` the evidence shows blocked
  parallel app-local DLL steps ([vcpkg-tool#2163](https://github.com/microsoft/vcpkg-tool/issues/2163));
  `native.request_worker`'s watchdog reports a timeout with where each
  thread was and their stacks; waits are by the clock; the package consumer
  builds count two CPUs in CTest; failed tests stay in each attempt's job
  summary and evidence; `au.editor` reads the report after a load until it
  shows; `au.adapter` no longer changes `CONVOLVER_LOG` while a worker
  reads it; the build and test jobs have time limits. No automatic re-runs;
  CI-24 and CI-25 continue;
  [intermittent failures](../../develop/testing.md#intermittent-failures).
- [x] **CI-30 — Complete the tests' replaced allocation functions.**
  5 November 2026, PR #111, from the external review of `91fcf1f` (0.8.13):
  `native.engine` replaced `operator new` without the sized `operator
  delete`, so with GCC's library AddressSanitizer stopped it with
  alloc-dealloc-mismatch before the oracle tests ran. The array and sized
  forms are now one shared file, `tests/native/replaced_allocation.hpp`,
  used by every native test that watches allocation.
- [x] **CI-31 — Keep `native.host_settings` within its time limit under
  sanitizers.** 5 November 2026, PR #111: the external review of `91fcf1f`
  (0.8.13) saw it time out in a GCC AddressSanitizer build. The shared
  double-precision oracle (`tests/native/oracle.hpp`) spent 90 of its 91
  seconds there; it now works out each route's invariant parts once, with
  the same arithmetic in the same order (worst errors unchanged), and the
  test takes about 4 seconds.
- [x] **CI-29 — Use LLVM's parallel clang-tidy runner.** 5 November 2026,
  PR #108, from the project review of that day: CI runs `run-clang-tidy.py`,
  which the clang-tidy package installs, in place of our
  `scripts/clang_tidy.py` and its tests;
  [lint checks](../../../CONTRIBUTING.md#formatting-and-analysis).
- [x] **CI-24 — Find why `native.request_worker` timed out on Windows.**
  Closed 5 November 2026 without a cause, PR #109: AU-12 removed the worker
  and its test; the evidence is
  [archived](../reviews/2026-11-05-request-worker.md).
- [x] **CI-25 — Find why the watchdog check hung on the macOS arm64
  candidate.** Closed 5 November 2026 without a cause, PR #109: AU-12
  removed the check, its workflow and its supervisor; the evidence is
  [archived](../reviews/2026-11-05-request-worker.md).
- [x] **CI-28 — Keep `vst3.adapter`'s Status and log case's log as
  evidence.** 5 November 2026, from CI-27's review: the case pointed
  `CONVOLVER_LOG` at a temporary file with `setenv` while the processor
  existed, and deleted it. `vst3.adapter` and `vst3.preset` (which runs the
  same tests) now each have a log of their own in the build folder, set once
  in `main` (wide on Windows), and the case reads it from
  an offset with the helper the Audio Unit tests use, moved to
  `tests/plugins/test_log.hpp`;
  [evidence from failed CI runs](../../develop/testing.md#evidence-from-failed-ci-runs).
- [x] **CI-27 — Keep the Audio Unit adapter test's log after a crash.** 2
  November 2026, from jrp's review of `f2dc691`: the test kept its Convolver
  log in a private temporary file and copied it to the build folder only on
  a normal exit, so a sanitizer abort, crash or forced end lost it, and the
  rotated `.old` file was deleted (`au.editor` deleted its private log).
  Both now log to their own file in the build folder from the start, never
  copied or deleted; the evidence collector takes every test log and its
  `.old` file; `au.log_evidence` checks both after an abort and a kill;
  [evidence from failed CI runs](../../develop/testing.md#evidence-from-failed-ci-runs).
- [x] **CI-26 — Run only VST3, Audio Unit and the candidates for window
  changes.** 2 November 2026, from jrp's review of `276c7dc`: a pull
  request that changes only the shared macOS window (`plugins/editor/mac/`)
  no longer runs the native, LV2 and fuzz workflows; any shared code in the
  same pull request still runs everything;
  [one plug-in format](../../develop/testing.md#pull-requests-for-one-plug-in-format).
- [x] **CI-23 — Run `vst3.fft_symbols` on Windows.** 1 November 2026: CTest
  runs outside a developer prompt, so `dumpbin` was not on PATH and the test
  always skipped there; it now finds `dumpbin` through `vswhere`;
  [VST3 tests](../../develop/testing.md#plug-in-format-tests).
- [x] **CI-19 — Build the Apple silicon candidate on `macos-15`.** 31 October
  2026: GitHub retires the `macos-14` image on 2 November 2026, and its
  brownouts on 30 October cancelled the candidate job before it ran (PRs #84
  and #89, [runner-images#13518](https://github.com/actions/runner-images/issues/13518));
  the package still targets macOS 14; [candidate checks](../../develop/testing.md#plug-in-candidate-checks).
- [x] **CI-20 — Keep unpacked ZIP files inside their destination.** 1
  November 2026, PR #102, from a Skylos scan: `candidate.extract_zip` reads member
  names as POSIX paths (so `/name` is refused on Windows too) and checks each
  resolved path stays under the destination, as `sources.extract` does for
  source archives; tested in `release.source`;
  [candidates](../../develop/releasing.md#candidates).
- [x] **CI-21 — Pin third-party actions to a commit SHA.** 1 November 2026,
  PR #102, jrp's choice after a Skylos scan: `hendrikmuhs/ccache-action` is pinned to
  v1.2.24's commit with the release in a comment, and Dependabot still moves
  it to each new release; GitHub's own actions keep major tags;
  [GitHub Actions versions](../../../CONTRIBUTING.md#lint-checks).
- [x] **DIAG-1 — Review how diagnostics reach the user.** Status parameter
  and Convolver log (23 October 2026, PR #57), processing faults and run
  identity (PR #68), symbol checks (PR #72), fault state after reloads (PR
  #73); the [diagnostics by surface](../plans/2026-10-24-diagnostics-by-surface.md)
  table, and jrp's choices among its proposals implemented (31 October 2026,
  PR #96): CLI sample faults and incomplete outputs, next steps for damaged
  files, `convolver-preset` wording and option order, logged plug-in refusals
  and failed saves, with a test per message. What named hosts show is
  HOST-1 and AU-4; [output safety](../../use/command-line.md#output-safety),
  [status and log](../../develop/plugin-contract.md#status-and-log).
- [x] **DIAG-2 — Actionable diagnostics.** 5 November 2026, PR #110, from
  jrp's request of that day: every message a user can meet states the
  problem, its cause and the fix, with a note for an environment, host or
  design limit. The command-line tools print them as labelled lines, the
  Convolver log as one line, and the macOS window shows the problem in bold,
  Cause and Fix rows, a button that carries out the fix (Choose Filter… or
  Copy Diagnostics) and a Note row, with Bypass and the diagnostics buttons
  moved off the bottom edge (jrp's choices); a test per message. The window's
  look in a host is part of HOST-1;
  [troubleshooting](../../use/troubleshooting.md),
  [plan](../plans/2026-11-05-actionable-diagnostics.md).
- [x] **DIAG-3 — Report faults around a filter change together.** 5
  November 2026, from jrp's simplification review: faults counted while a
  replacement was prepared are logged once, as met around a filter change,
  rather than split from the new filter's own; `HostFaultReport` holds
  `faults`, `changed` and `held`, and `HostFilter` keeps no separate tally
  of them. Kept, because each protects something users see: one snapshot as
  the baseline of both the status and the audio thread's signal (new faults
  show, old ones do not), Fault signalled once per baseline, and the hold
  that stops Fault replacing Load failed;
  [contract](../../develop/plugin-contract.md#status-and-log).
- [x] **DIAG-4 — Keep messages in their parts inside the library.** 5
  November 2026, from jrp's simplification review: `HostFailure::message`,
  `HostFilter::notice()`, `speaker_notice()`, `host_state_size_error()` and
  the readers' `FilterFileFailure` hold an `Advice` (problem, cause, fix,
  note; new public header `convolver/advice.hpp`), and `reject()` takes one.
  The log, the window and `HostFilter::error()` format it where it leaves
  the library, so the report no longer parses text it had just written.
  Only exception messages are still parsed, as they must be. The wording is
  unchanged;
  [failure reasons](../../develop/plugin-contract.md#failure-reasons).
- [x] **DOC-0 — Refresh the entry points for 0.4.0.** 20 October 2026;
  [documentation review](../reviews/2026-10-20-documentation-review.md).
- [x] **DOC-1 — Runnable first-use examples.** 22 October 2026, PR #28;
  [first-use examples](../../../examples/first-use/README.md).
- [x] **DOC-3 — Resolve adapter-plan review findings.** 21 October 2026;
  [review](../../review-2026-10/ooura-and-roadmap-2026-10-21.md#documentation-findings).
- [x] **DOC-4 — Organise the documentation by reader.** 23 October 2026,
  PRs #47, #51, #52 and #53; [documentation guide](../../develop/documentation.md),
  [plan](../plans/2026-10-23-documentation-by-reader.md).
- [x] **DOC-5 — Refresh the status page's evidence.** 1 November 2026,
  PR #99; [status](../../project/status.md), checked against `main` at 0.8.2.
- [x] **DOC-6 — One home for progress.** 1 November 2026, PR #105, from
  jrp's review of `276c7dc`: the status page had drifted (the CLI and
  journey presets "missing", journeys "not started", the VST3 half
  "unclaimed"). Checked against 0.8.6; progress detail now lives only in
  TODO's `Progress` lines, and the status page names what remains and links
  the items ([recording progress](../../develop/workflow.md#recording-progress-in-todo)).
- [x] **FFMPEG-1 — Test the pipe workflow.** 5 November 2026:
  `native.ffmpeg_pipe` runs the guide's FFmpeg pipe on a film FFmpeg makes,
  checking track selection, 2-in/6-out routing, the frame count with the
  tail, and the start time of late audio, wherever FFmpeg is installed (the
  native g++ Release job on Linux CI);
  [tests](../../develop/testing.md#native-production-tests).
- [x] **FFT-1 — Check that we have the latest Ooura version.** 21 October 2026;
  [provenance](../../../portable/third_party/ooura/README.md#upstream-comparison-21-october-2026).
- [x] **FFT-3 — Accelerate by default on macOS.** 30 October 2026, PR #89: every
  macOS build, the command-line tools included, uses Accelerate unless built
  with `-DCONVOLVER_FFT_BACKEND=FFTW`;
  [which libraries each build uses](../../develop/build.md#which-libraries-each-build-uses).
- [x] **FFT-4 — Remove the finished vDSP experiment.** 5 November 2026, PR #108,
  from the project review of that day: `portable/kernels_vdsp.cpp` and the
  `CONVOLVER_EXPERIMENT_VDSP_MAC` option are gone with the experiment's two
  scripts; its timing program stays as `convolver_kernel_benchmark`; the
  [24 October results](../reviews/2026-10-24-apple-vdsp.md) stay
  archived; the code can be rebuilt from `v0.8.10` (`f5ecb12`) or earlier.
- [x] **IO-3 — More output containers.** 23 October 2026, PR #60;
  [output formats](../../use/command-line.md#output-formats).
- [x] **IO-4 — Raw filters and raw sample types.** 24 October 2026, PR #60;
  `f32le` by default and reported, `--raw-filter-format` for others;
  [filter files](../../use/configuration.md#filter-files),
  [RAW streams](../../use/command-line.md#raw-streams).
- [x] **IO-7 — Dither defaults by output resolution.** 25 October 2026, PR #79;
  [integer output](../../use/command-line.md#integer-formats-dither-and-noise-shaping).
- [x] **IO-9 — Shortest float text from the standard library.** 5 November
  2026, PR #109, from the project review of that day, on jrp's "Do io-9":
  the Equalizer APO exporter formats gains with `std::to_chars` instead of
  trying 1 to 160 decimal places with `snprintf` and `strtof`. Compared for
  every positive finite float with libstdc++ and libc++, the text is the
  same except for ±2^-96 (about −578 dB), which `to_chars` writes with one
  digit fewer; both texts read back as the same float. The inspection report keeps its
  9-step `%g` search: `std::to_chars`' general format switches to exponents
  at another point than `%g`, so using it would change that report. The
  exporter now needs macOS 13.3 or later to build;
  [building for macOS before 14](../../develop/build.md#building-for-macos-before-14)
  says what older targets need.
- [x] **IO-6 — FLAC in the preview packages.** 24 October 2026, PR #60:
  decided not to ship the codec libraries; limitation and workarounds in
  [FLAC and Ogg](../../use/command-line.md#flac-and-ogg).
- [x] **HOST-5 — A headless Ardour session in CI.** 6 November 2026, on
  john's request for unattended host checks: the LV2 workflow records,
  saves, restores and exports the three first-use fixtures through the
  plug-in in Ardour 8 and checks the exports against the oracle
  ([Ardour, headless](../../develop/testing.md#ardour-headless)). Ardour
  8.4's export aborts in about 1 export in 10 with any plug-in, Ardour's
  own included, and now and then hangs, so an export killed by a signal or
  hung for 2 minutes is tried again with a warning; a newer Ardour on the
  runners can drop the retry. One unexplained early export is HOST-6.
- [x] **LV2-1 — Adapter and headless test.** 22 October 2026, PR #43;
  [LV2 tests](../../develop/testing.md#lv2-tests).
- [x] **LV2-2 — Loading filters.** 23 October 2026, PR #48;
  [loading a filter](../../use/plugins.md#loading-a-filter), [LV2 tests](../../develop/testing.md#lv2-tests).
- [x] **LV2-8 — Status after a request the host drops.** 6 November 2026,
  PR #122: closed without a change. In their current source (Jalv `main`,
  Ardour `master`, Carla `main`, PipeWire `master`; released versions and
  other hosts not checked), none drops a queued request and then
  reactivates: Ardour and Carla keep the queue across deactivation,
  PipeWire waits for queued work before freeing the instance in its normal
  configuration, and Jalv drops requests only when it quits. The tests'
  deferred worker stays as a stress case; reopen if a host is found that
  drops requests and reactivates
  ([worker lifecycle](../../project/plans/2026-10-21-plugin-formats.md#lv2-adapter-design)).
- [x] **PERF-4 — Compiler flags and CPU-specific kernels.** 24 October 2026,
  PR #61: run-time AVX2+FMA kernels, with FMA by jrp's decision and a divergence test; [compiler flags and AVX2 kernels](../../develop/fft-backends.md#compiler-flags-and-avx2-kernels).
- [x] **PERF-5 — Track valid input history.** 24 October 2026, PR #70: paths
  multiply only the history written since start or reset, and reset
  invalidates instead of clearing; output bit-identical;
  [startup, reset and loading](../../develop/fft-backends.md#startup-reset-and-filter-loading).
- [x] **PERF-6 — Decode each filter file once per load.** 24 October 2026,
  PR #70: same errors and limits;
  [startup, reset and loading](../../develop/fft-backends.md#startup-reset-and-filter-loading).
- [x] **PERF-9 — Load timing test.** 5 November 2026, PR #111, from the
  external review of `91fcf1f` (0.8.13), on jrp's go-ahead: `vst3.load_timing`,
  `au.load_timing` and `lv2.load_timing` time each format's load entry points
  while another thread renders at the host's pace, and report how long the
  caller was held and how late callbacks were, failing above jrp's limits
  in optimised builds ([loading responsiveness](../../../TODO.md#loading-responsiveness));
  [load timing](../../develop/testing.md#load-timing-perf-9).
- [x] **PERF-10 — The load-timing gate cannot pass unqualified.** 5 November
  2026, from jrp's review of `59ac8e0` (0.10.0): each run of an entry is
  passed, failed or not qualified; the host thread's hold is judged whatever
  the audio thread saw; a run is not qualified only when callbacks broke the
  limits with a late baseline (half a second before the phases and another
  after them), or the build does not enforce them; the test exits 77
  (CTest: skipped) rather than 0 when an entry did not qualify; an entry no
  limit applies to is reported only, not passed; the summary keeps every
  run that did not pass and counts passes after a run that did not; an
  entry passes only if one run met both limits (jrp's review of `cc2ebde`:
  limits met in different runs are not qualified, or failed when every run
  broke one), and the summary names the run that met each limit for an
  entry that did not pass; a test where nothing was judged (every entry
  reported only) is skipped, not passed. `native.load_timing_harness` checks the verdict
  rules on made-up runs, and drives the harness with a mock adapter: slow
  loads fail, with late callbacks too (the review's reproduction); a load
  slow on the first run only meets the hold limit later, with that run kept;
  late callbacks with a late baseline do not qualify; a load no limit
  applies to is reported only; a refused load fails;
  [load timing](../../develop/testing.md#load-timing-perf-9).
- [x] **PERF-11 — Quiet noise as the load-timing input.** 5 November 2026,
  from jrp's review of `59ac8e0` (0.10.0): the input generator subtracted
  in unsigned arithmetic, so the negative half became about 1.8e15 (measured
  range 0.0002 to 1.84e15). It now subtracts in signed arithmetic, and
  `native.load_timing_harness` checks the range (-0.1 to 0.1) and that both
  signs occur; it failed with the old generator. The loading timings were
  measured again with it ([loading responsiveness](../../../TODO.md#loading-responsiveness)).
- [x] **DOC-2 — Maintain documentation automatically.** 6 November 2026, PR #118:
  `check_repository.py` (pre-commit and lint CI) checks every CMake `-D`
  option in the guides, workflows and scripts against the build, with its
  allowed values, and that the build guide describes every option; and
  every option shown with a Convolver tool against the options its source
  parses, and that each tool's guide names them all. The packaged guide's
  marked examples already ran in the candidates workflow (PR #101). Links
  and anchors were already checked;
  [automated checks](../../develop/documentation.md#automated-checks).
- [x] **GAIN-1 — Define no-clip headroom precisely.** 6 November 2026, PR #118: with
  the no-clip gain, the input reaching the bound stays within full scale up
  to float rounding (largest sample 0.99999994, `native.engine`); integer
  output never clips within ±(1 − H / 2^(B−1)), H = (1 + S)(½ + D) + ½
  steps for dither D and shaper coefficient sum S (a safe bound with half a
  step of margin), checked for 8, 16 and
  24 bits, every dither and shaper (`native.io`, which detects clipping by
  comparing with a quantizer one bit wider), with the exact undithered
  endpoints. No change to the sound: `no-clip` still reserves no quantizer
  headroom; [gain](../../use/command-line.md#gain-and-attenuation).
- [x] **GAIN-3 — `no-clip` reserves the integer headroom.** 6 November 2026,
  as [decided](../../project/decisions.md#configuration-and-sound): for integer output `--gain-db no-clip` lowers its
  gain by the headroom H that rounding, dither and noise shaping need at the
  output's significant bits (`quantization_headroom` in `io.hpp`), and by
  2^−16 for the convolution's float rounding (about 100 times the largest
  overshoot measured at the bound, 1.2e-7 at 65,536 and 262,144 taps). The
  CLI prints the gain `no-clip` and `auto` apply with every digit needed to
  give it back to `--gain-db` and get the same file. Numeric gains and float
  output are unchanged. `native.cli` renders the input that reaches the bound
  for 8, 16, 18 (valid bits), 24 and 32 bits, with each dither and with each
  shaper the command line offers, and checks that the level, also rendered
  as float, stays within ±(1 − H / 2^(B−1)), where `native.io` shows the
  quantizer cannot clip (`native.io` also checks `quantization_headroom` for
  every dither and shaper), and that the printed figure reproduces each file
  byte for byte; [gain](../../use/command-line.md#gain-and-attenuation).
- [x] **PERF-12 — Load-timing verdicts from records, tested without
  timing.** 5 November 2026, from jrp's review of `db54160`: the renderer
  records each callback (due, wake, entry into and return from the plug-in,
  and the thread's CPU time where the platform gives it), and a pure
  `judge()` gives a run's verdict from those records. A callback that missed
  its deadline only because the thread woke late is not the plug-in's own
  time: not judged, so the run is not qualified rather than failed. A slow
  call with little CPU time was at first excused as descheduled too; after
  jrp's review of `5f47ecb` (a call blocked on a lock in the plug-in looks
  the same) it is judged as the plug-in's, and its note says the records
  cannot tell blocked from descheduled. `native.load_timing_harness` checks the
  rules on made-up records (deadline missed with a fast callback, slow
  callback, loader over budget, unsuitable baseline, functional errors,
  mixed results across runs) and keeps only a few short real-thread runs:
  about 9 s instead of 40 to 60;
  [load timing](../../develop/testing.md#load-timing-perf-9).
- [x] **PERF-14 — Load timing from a full history.** 5 November 2026,
  from jrp's review of `db54160`: a newly loaded long filter is cheap
  because the engine processes only the parts that have heard input, so the
  timing tests measured startup, not a long filter's sustained cost. With
  `--warm`, the renderer plays the filter's length of audio unpaced before
  the replacement and before the reset, and shows half a second of each
  filter at full history ("warm first", "warm second", reported only: no
  limit for steady playing is decided). Opt-in because filling takes
  minutes with the 30 s filter; CI keeps the cold runs, and REL-6 runs it.
  `native.load_timing_harness` checks the phases with a mock. It found
  PERF-15 at once; [loading responsiveness](../../../TODO.md#loading-responsiveness).
- [x] **PLUG-21 — Teardown tested at chosen stages.** 5 November 2026
  (first committed as PLUG-20, which main then gave to the VST logo),
  from jrp's review of `db54160` ("test shutdown at explicit stages rather
  than hoping repeated runs encounter the troublesome interleaving"):
  `lv2.adapter` holds `work()` at each of its pause points (read, prepared,
  checked) while another thread deactivates the instance, and releases it
  only once `deactivate()` has signalled from inside that it is waiting for
  `work()` (jrp's review of `5f47ecb`: otherwise the worker could finish
  before deactivation began): reactivated, the new filter plays and is
  reported and saved; closed with a newer request queued, nothing is used
  after it is freed. `au.adapter`
  holds a run of the Latency work through a new test-only hook
  (`latency_started_for_tests`) while the unit closes, alone and with a
  change queued, releasing it only once closing has cancelled the work and
  is about to wait for the run (a second hook, `latency_closing_for_tests`);
  the queued run must then never start. A 60 s watchdog stops either test,
  naming the stage. With the lock in LV2's `deactivate()` removed, the
  staged case fails (before the handshake, under ASan, with a heap use
  after free) while the earlier repeated loop passes. Loading and replacement ordering
  was already tested this way (the preparation-window cases). Found: LV2-8.
- [x] **PERF-13 — Load-timing records as JSON.** 5 November 2026, from
  jrp's review of `db54160`: each timing test writes every run's records
  (each callback's due, wake, entry, return, CPU time and success; each
  phase's hold and summary; verdicts and reasons) with the Convolver
  version, build, compiler, FFT backend, operating system and processor
  count, to `FORMAT-load-timing.json` in its work folder. The VST3 and Audio
  Unit Release jobs keep them as an artifact, and a failed job keeps them
  with its test evidence. Timing the loading stages inside the plug-in
  remains open; [load timing](../../develop/testing.md#load-timing-perf-9).
- [x] **PERF-15 — Subnormal numbers flushed while processing.** 5 November
  2026, PR #117, on jrp's decision that day (option A, "protect the complete
  processing call, including mixing and gain"; preparation too, 6
  November): found by PERF-14, where the 30 s filter, decaying below the
  smallest normal float, took 25 to 43 ms a callback on the Linux cloud VM (clang Release, Ooura,
  stereo 48 kHz, 512-frame callbacks 10.7 ms apart) and 6 to 7 ms with
  flush-to-zero on the audio thread. `ScopedFlushDenormals`
  (`portable/denormals.hpp`, as JUCE's `ScopedNoDenormals`) sets MXCSR's
  FTZ and DAZ on x86-64 and FPCR.FZ on AArch64 for the whole of
  `Engine::process`, `HostProcessor::process` (conversion, gain, mixing and
  crossfades too), the CLI's render and engine preparation, and restores
  the caller's mode on every exit. Measured with the LV2 plug-in at full
  history (`--warm`, same VM and build) and a renderer thread that does not
  flush: the longest callbacks 37 to 43 ms before, 8 to 12 ms after (6 to
  12 ms with a flushing renderer); a 30 s load held the worker for 170 to
  200 ms before, 63 to 93 ms after. Subnormal arithmetic is flushed, not bit-identical to IEEE
  gradual underflow; `native.denormals` checks restoration, subnormal input
  and a decaying tail, and `native.kernel_divergence`'s 1e-35 case has an
  absolute limit (1000 times `FLT_MIN`) where flushing applies. The callback limits are unchanged:
  the 30 s filter's replacement still fails them on that VM (longest
  callbacks 7 to 10 ms against the 5.3 ms half-period budget);
  [subnormal numbers](../../develop/architecture.md#subnormal-numbers).
- [x] **GAIN-2 — No noise simulation for `--gain-db no-clip`.** 5 November
  2026, PR #111, from the external review of `91fcf1f` (0.8.13): no-clip
  ran the whole noise estimate to use only the bound it also returns. It
  now validates the configuration and takes `worst_case_peak()` directly
  (0.43 ms rather than 69.5 ms for a 65,536-tap stereo filter in the
  review's Release measurement); `auto` still filters noise. `native.cli`
  checks the gain and the rendered output for silent, cancelling, delayed
  and multichannel configurations.
- [x] **PLUG-1 — FFT backend for plug-ins.** 21 October 2026;
  [plug-in builds](../../develop/fft-backends.md#plug-in-builds).
- [x] **PLUG-2 — Shared adapter test harness.** 22 October 2026, PR #28;
  [plug-in comparisons](../../develop/testing.md#plug-in-comparisons-plug-2).
- [x] **PLUG-3 — One preset tool.** 22 October 2026, PR #32;
  [plug-in presets](../../use/presets.md), [preset files](../../develop/plugin-contract.md#preset-files).
- [x] **PLUG-4 — Layouts and identities.** 22 October 2026, PR #27;
  [plug-in identities](../../develop/plugin-identities.md).
- [x] **PLUG-6 — Pin format dependencies.** 22 October 2026, PR #31;
  [supporting libraries](../../develop/build.md#supporting-libraries); replaced by CI-6.
- [x] **PLUG-7 — Lifecycle sequence tests.** 23 October 2026, PR #58;
  [lifecycle sequences](../../develop/testing.md#lifecycle-sequences-plug-7).
- [x] **PLUG-8 — Keep a structured failure reason.** 23 October 2026, PR #46;
  [failure reasons](../../develop/plugin-contract.md#failure-reasons).
- [x] **PLUG-12 — Accept only filters that can be saved.** 31 October 2026,
  from jrp's review of `6920a45`: a `.cfg` may hold 64 Mi samples but saved
  state only 64 MiB, so a 16,777,216-sample filter loaded and then could not
  be saved. `HostFilter::load` refuses a filter whose state would exceed the
  limit before installing it (`too_large`), and `convolver-preset` refuses
  to write one; [host state](../../develop/plugin-contract.md#host-state).
- [x] **PLUG-13 — Keep room for the settings in saved state.** 1 November
  2026, PR #105, from jrp's review of `276c7dc`: a VST3 filter that filled
  the limit at default settings could not be saved once Gain changed. The
  size check keeps 256 bytes free for the settings whether or not they are
  in use;
  [host state](../../develop/plugin-contract.md#host-state).
- [x] **PLUG-15 — One macOS deployment target.** 5 November 2026, PR #108,
  from the project review of that day: the top-level `CMakeLists.txt` sets
  `CMAKE_OSX_DEPLOYMENT_TARGET` to 14.0 before `project()`, as CMake asks,
  in place of the VST3 and Audio Unit folders forcing 11.0 after it, so
  every Mac build targets what the packages do;
  [build options](../../develop/build.md#build-options).
- [x] **PLUG-17 — Ramps measured in time rather than frames.** 5 November
  2026, PR #109: jrp chose "whichever is easier to use, for the final
  user". `HostProcessor`'s gain, mix, bypass and mute ramps last 1.33 ms and
  a replacement's crossfade 5.33 ms at every sample rate (64 and 256 frames
  at 48 kHz, to the nearest frame elsewhere), so they are as smooth at
  96 kHz and above as at 48 kHz, which plays exactly as before;
  `native.host_settings` checks the lengths at 44.1, 48 and 96 kHz;
  [contract](../../develop/plugin-contract.md#settings).
- [x] **PLUG-19 — One frame's allowance at a ramp's end.** 5 November
  2026, from jrp's simplification review: a setting transition must not
  click, must finish promptly and must end exactly at its target, but may
  end one frame after its last (inaudible: about 5.7 µs at 176.4 kHz).
  `native.host_settings` tests that allowance; PLUG-18's frame counting is
  kept because it is no larger than the float step it replaced. Reported
  latency and channel alignment have no allowance;
  [settings](../../develop/plugin-contract.md#settings).
- [x] **PLUG-18 — Exact bypass and mute ramp endpoints.** 5 November 2026,
  PR #111, from the external review of `91fcf1f` (0.8.13): PLUG-17's bypass
  and mute ramps summed a float step, so at 176.4 kHz they left about
  2.4e-6 of unit input for one frame after their 235 frames. They now count
  frames and reach exactly 0 or 1 on the last frame, also when reversed;
  `native.host_settings` checks 8 kHz to 768 kHz;
  [settings](../../develop/plugin-contract.md#settings).
- [x] **PLUG-16 — Name the transition lengths.** 5 November 2026, PR #108,
  from the project review of that day: `HostProcessor`'s 64-frame setting,
  bypass and mute ramps and its 256-frame crossfade are named constants in
  `portable/host.cpp`, with the same values, and the plug-in contract says a
  replacement plays for one Latency block before the crossfade, as it has
  since PLUG-10; [contract](../../develop/plugin-contract.md).
- [x] **REL-2 — Define the first release by user journeys.** 6 November
  2026, PR #118: the three journeys are written in `docs/use/` as numbered steps with
  the expected result of each, from download and checksum:
  [check a stereo filter in a host](../../use/check-a-stereo-filter.md)
  (PR #101), [check a crossover in a host](../../use/check-a-crossover-in-a-host.md)
  (2→6 in REAPER, VST3 and the Audio Unit) and
  [check file rendering with the command line](../../use/check-files-with-the-command-line.md)
  (the three examples, each checked with `--compare`, and filters refused
  for another rate or channel count, whose messages the guide quotes). The
  package holds what they use, now also required by `verify_artifact.py` for
  the crossover; the release tests run the guides' commands, and since this
  change also check each quoted refusal (`<!-- refused-example -->`). No
  journey has been followed in a host yet: the host steps, and REAPER's
  routing of an effect with 2 inputs and 6 outputs in particular, are for
  HOST-1 and REL-3 to demonstrate.
- [x] **REL-6 — Responsiveness qualification before a release.** 5
  November 2026, from jrp's review of `db54160`: CI's timing tests run on
  shared runners and report "not qualified" when they cannot judge, which
  is not qualification. The
  [procedure](../../develop/releasing.md#responsiveness-qualification): on a
  named machine (jrp's Mac first, through the command line), build the
  candidate's tag with the SDKs its source manifest records, optimised and
  without sanitizers; run the timing tests cold and `--warm`; the release
  qualifies only if every entry passed (or no limit applied), with the
  existing limits; every run's records are kept and the release notes state
  the result either way. Not yet carried out: the first preview's run is
  part of its publication.
- [x] **REL-5 — Early limited walkthrough on macOS.** 1 November 2026,
  PR #101. An assisted check for jrp, not by an independent tester, of the
  stereo journey's package and command-line steps on the pull request's
  macOS arm64 candidate
  ([run 41347884655](https://github.com/jrp2014/convolver/actions/runs/41347884655),
  version 0.8.3 built from merge commit `773605c`, macOS 27.2, package
  SHA-256
  `936cc921e39c046d5bd5aa8073ff15532e3bf5513c46127b08b8b433861821a1`):
  the hashes matched, the package held every file the guide names, and the
  command-line render matched `expected.wav` (largest difference -131 dB).
  No defect found. jrp closed it without the host steps (install, render,
  save and reopen, presets that do not fit), which moved to HOST-1 and
  REL-3; nor did it cover the browser's quarantine, the installer, the
  Audio Unit or other platforms;
  [check a stereo filter in a host](../../use/check-a-stereo-filter.md).
- [x] **ROBUST-3 — Silence when memory runs out after a recorded failure.**
  31 October 2026, from jrp's review of `6920a45`: a failed first load
  silences the processor with `HostProcessor::silence()`, which cannot
  allocate or fail, in VST3, the Audio Unit and LV2; lifecycle sequence
  `state-malformed-starved`;
  [replacement and threads](../../develop/plugin-contract.md#replacement-and-threads).
- [x] **ROBUST-7 — A failed first load fails however early memory runs
  out.** 5 November 2026, PR #111, from the external review of `91fcf1f`
  (0.8.13), which reproduced it: `HostFilter::record` copied the request
  before marking the instance failed, so with allocations failing a refused
  first load left `failed()` false and the adapters let the input through.
  The failure is now marked first without allocating
  (`HostFilter::fail_request()`), with a record of its kind and request
  number; `reject()` never throws; VST3, the Audio Unit and LV2 also mark it
  when memory runs out building the message; an accepted filter is kept.
  Lifecycle sequences now require it after every starved attempt
  (`native.host` `unrecorded_failures` too);
  [status and log](../../develop/plugin-contract.md#status-and-log).
- [x] **ROBUST-4 — Low-memory quantizer and non-finite gain estimates.**
  24 October 2026, PR #72, from jrp's review of 0.4.43: `Quantizer` allocates
  in its constructor, so out of memory reaches the caller, and
  `estimate_attenuation` refuses a non-finite noise response; tests in
  `native.io` and `native.engine`; [gain](../../use/command-line.md#gain-and-attenuation).
- [x] **IO-5 — Speaker labels beyond WAV.** 6 November 2026, PR #118: checked with
  libsndfile 1.2.2 and FFmpeg's reader. RF64 now carries `--speaker-map`
  labels in its WAVE_FORMAT_EXTENSIBLE header, any mask, read back exactly
  by both. libsndfile writes no labels in W64 or FLAC; in CAF and AIFF it
  writes only Apple layouts, refusing side-speaker 5.1 and 7.1 and turning
  WAVE's back speakers into surround ones that FFmpeg reads as side, so
  those containers, like W64 and FLAC, refuse a map before the output is
  created. Tests in `native.io` and `native.cli`;
  [speaker labels](../../use/command-line.md#speaker-labels).
- [x] **ROBUST-2 — Bound report output.** 6 November 2026, PR #118: traversal and
  diagnostics were already bounded (4096 visits, 256 diagnostics), but
  `--inspect --all-configurations` gives each entry a render's decoding
  budget, so a 20 KB list naming a 64 MB filter 4095 times kept it busy for
  about 18 minutes, and a 16 MiB list of 4096-path configurations made a
  533 MB JSON report using 2.9 GB. One check now stops, with one error,
  before the next entry once it has decoded 1 Gi samples, or before an entry
  that would take it past 16,384 reported paths (4.2 s and 4 MB on the same
  lists) or 131,072 reported routes (jrp's review of `7b4cb7a`: 128 entries
  of one path with 8192 routes made a 37 MB report; now 4.6 MB, about 1 s
  and 130 MiB under ASan); a no-match message
  names at most ten sample rates, as it already did entries. Tests in
  `native.limits`;
  [resource limits](../../develop/library.md#resource-limits).
- [x] **ROBUST-6 — State the shared limits once.** 5 November 2026, PR #108,
  from the project review of that day: the sample-rate, channel, block and
  impulse limits are `constexpr` values in `convolver/engine.hpp`, used by
  the engine, readers, saved state, tools and adapters in place of literals
  in fifteen files, with the same messages;
  [resource limits](../../develop/library.md#resource-limits).
- [x] **VST3-15 — Test the messages through a host's proxy.** 5 November
  2026, from jrp's review of `276c7dc`: `vst3.adapter` connects the
  components through a forwarding proxy and a deferring one
  (`IConnectionPoint`). The deferring one showed two assumptions of direct
  delivery: the host was asked to restart for a new Latency before the
  processor had it, and a refused file marked the project changed. The
  processor's `ConvolverStatus` now answers each load and Latency by its
  request number, and the controller acts on those answers (a restart only
  for the last Latency chosen) rather than on
  `sendMessage()`'s result; a deferred load returns before its answer and
  the report shows it;
  [messages](../../develop/plugin-contract.md#status-and-log).
- [x] **VST3-14 — Separate the controller from the processor's file.**
  5 November 2026, from jrp's review of `276c7dc`: `Controller` moved from
  `plugins/vst3/processor.cpp` (then 1,041 lines, about 780 after) to
  `plugins/vst3/controller.cpp`, unchanged; the Status values' names, which both use, moved to
  `processor.hpp`; [implementation map](../../develop/architecture.md).
- [x] **VST3-1 — Speaker arrangements, not only channel counts.** 23 October
  2026, PR #42; [channel layouts](../../develop/plugin-identities.md#channel-layouts).
- [x] **VST3-3 — Editor messaging between controller and processor.**
  1 November 2026, PR #103: the processor sends the PLUG-9 report with each
  Status update and when the controller asks (`ConvolverReport`); a
  `ConvolverLoad` message carries a chosen path, which the processor reads
  as `convolver-preset` does (AudioToolbox on macOS) and installs as
  `setState` does, and the controller then calls `setDirty`; headless tests
  in `vst3.adapter` (the lifecycle sequences have no messages to add).
  Bypass stays the host's parameter, edited through the controller as
  before; a Bypass control in the window moved to VST3-11. Not done:
  `IDataExchangeHandler` for meters (with PLUG-10's meters) and plug-in
  snapshots for host browsers (`smtg_target_add_plugin_snapshots`), which
  need the window;
  [messages](../../develop/plugin-contract.md#status-and-log).
- [x] **VST3-4 — Gain, mix and latency parameters.** 1 November 2026, PR
  #103: Gain (0), Mix (1003, `kDryWetMix`) and Latency (1004, not
  automatable, `restartComponent(kLatencyChanged)`), saved in host state
  version 3 only when they differ from the defaults; tests in
  `vst3.adapter` and `native.state`. Gain mode (1002) waits with PLUG-10's
  deferred parts; whether REAPER and Audacity compensate a new Latency
  moved to their host steps under HOST-1; [settings](../../develop/plugin-contract.md#settings).
- [x] **VST3-7 — Follow the shared adapter contract.** 22 October 2026, PR #35,
  with the reset follow-up in PR #40; [adapter contract](../../develop/plugin-contract.md).
- [x] **VST3-8 — Follow the SDK documentation where the audit found gaps.**
  23 October 2026, PR #59, from the
  [VST3 API audit](../../review-2026-10/vst3-api-audit-2026-10-23.md): tail
  includes the latency, input silence flags not trusted, Bypass text converts
  back, controller state calls succeed; [adapter contract](../../develop/plugin-contract.md).
- [x] **VST3-9 — Non-integer host rates.** 5 November 2026, on jrp's
  decision of that day: `setupProcessing` accepts any finite rate from 1 to
  768000 Hz, such as 48000 ÷ 1.001 ≈ 47952.04795 Hz (47952 Hz itself was
  always accepted). The rate is kept exactly (`HostLayout::fractional_rate`)
  and matched strictly: no filter suits a fractional rate, so the plug-in
  plays silence (bypass still plays the input), keeps its filter, names both
  rates, and plays the filter again at its rate; only floating-point
  rounding of a whole rate counts as that rate. Checked by `vst3.adapter`
  (`fractional_rates`) and `native.filter_file`;
  [when a filter does not fit](../../use/plugins.md#when-a-filter-does-not-fit).
- [x] **VST3-10 — Mark silent output.** 24 October 2026, PR #70: an output
  channel is flagged silent exactly when a call left it all zero, so a tail
  that is still sounding is never flagged; [format notes](../../develop/plugin-contract.md#format-notes).
