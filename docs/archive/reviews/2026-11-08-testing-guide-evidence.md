<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Evidence moved from the testing guide, 8 November 2026

> **Dated record** (DOC-13). Passages moved unchanged, apart from relative
> links, from the [testing guide](../../develop/testing.md) at `21711c5`
> (0.12.7), so that the guide keeps current commands, coverage and
> troubleshooting. They record a local setup, the causes of intermittent
> failures already fixed, how the candidates used to be built, and two
> measurements. Use the [testing guide](../../develop/testing.md) for how the tests
> run now.

## Local dependency versions, October 2026

From "Validation limits and release work".

The native tests link actual production translation units and actual
libraries. The earlier October 2026 local test setup used these sources; they
are not the versions current builds use (those take the newest releases; see
[build from source](../../develop/build.md#supporting-libraries)):

- FFTW 3.3.11, archive SHA256
  `5630c24cdeb33b131612f7eb4b1a9934234754f9f388ff8617458d0be6f239a1`.
- libsndfile 1.2.2, archive SHA256
  `3799ca9924d3125038880367bf1468e53a1b7e3686a934f098b7e1d286cdb80e`;
  optional external codecs disabled for that local installation.

FFTW's 3.3.11 CMake sources still set their reported version to 3.3.10. That
local verification used the official configure/make build of that source
archive, which reports 3.3.11. Convolver itself is built with CMake in both
cases. Current LLVM, SDK and library results must be tied to the revision
actually tested.

## Causes of intermittent failures found to 6 November 2026

From [intermittent failures](../../develop/testing.md#intermittent-failures).
Causes found so far, starting with CI-22's survey (1 November 2026, the 1,000
most recent workflow runs, 25 October to 1 November, with their re-run
attempts):

- Windows builds, "The process cannot access the file because it is being
  used by another process", exit 32, in the app-local DLL step
  (`vcpkg.exe z-applocal`) after a link, both in the main build (PR #88) and
  in `packaging.consumer.source-no-ctest` (PR #102). The message is the
  system's, not one of vcpkg's own errors, and 32 is Windows'
  sharing-violation code: the command could not start `vcpkg.exe`. In #88
  it came 8 ms after another target's `z-applocal` had copied its DLL and
  was exiting. An exiting vcpkg sends its metrics by first copying
  `vcpkg.exe` to a temporary folder, and vcpkg opened the source of that
  copy without sharing
  ([microsoft/vcpkg-tool#2163](https://github.com/microsoft/vcpkg-tool/issues/2163),
  reported with vcpkg-tool 2026-09-26; fixed by #2165, merged 28 October
  2026). The Windows jobs set
  `VCPKG_DISABLE_METRICS=1`, which removes that copy; do the same for local
  parallel builds with an older vcpkg. No handle trace was taken, so this
  is the cause the evidence points to; if the error comes back with metrics
  off, it is another one.
- `au.editor` (PR #103): the read after a held load could get the last
  report while the Latency worker held the lock a moment; the test now reads
  until the result shows, as the window's next timed read would.
- `au.adapter` under ASan (`au-sanitizers`, PR #104, d7a9795):
  heap-use-after-free in `host_log_path()`, which read `CONVOLVER_LOG`
  with `getenv` on a unit's Latency worker while the test's private-log
  helper changed the variable with `setenv` for the next case. The test
  now sets the variable once, before any unit exists, and each case reads
  what the log gained while it ran. The log is the file CTest names in the
  build folder, its own since CI-27 (it was a private file copied at exit,
  which an abort lost; `au.editor`'s was a private file deleted at exit).
- `native.cli` (PR #123, `ec631a8`, Ooura build): the no-clip case for
  float output compared two renders byte for byte, and libsndfile writes
  the second a float WAV file was made into its `PEAK` chunk, so renders
  either side of a second differed. The test now compares the sample data
  as stored and the format fields (CI-33).
- The Audio Unit's former Latency worker (`native.request_worker`, CI-24,
  and its watchdog check, CI-25) was removed by AU-12; its tests and the
  evidence from its failures are [archived](2026-11-05-request-worker.md).

## How the candidates were built before November 2026

From [plug-in candidate checks](../../develop/testing.md#plug-in-candidate-checks).

Until 27 October 2026 the
candidate was built with networking, and a second, offline build of the same
archive with `tests/release/source_build.py` checked that it builds offline;
the script remains for [local checks](../../develop/build.md#preview-recipe).

GitHub retires its macOS 14 image on 2 November 2026 and cancelled the arm64
candidate during its scheduled brownouts on 30 October
([runner-images#13518](https://github.com/actions/runner-images/issues/13518),
CI-19).

## Fuzz campaigns side by side, 27 October 2026

From [fuzz smoke tests and longer campaigns](../../develop/testing.md#fuzz-smoke-tests-and-longer-campaigns).

On 27 October
2026 four at once gave each campaign about half its executions on `main`
(`config_text` 194k against 447k); two at once gave 426k, and
`config_list` 86k against 56k, `host_state` 1751k against 1640k and
`ir_file` 121k against 161k.

## Test order timing, 25 October 2026

From [build targets and test order](../../develop/testing.md#build-targets-and-test-order).

On `main` at
b9381e4 (25 October 2026, VST3 run 40236000059), Windows `vst3.lifecycle`
took 100 s and began 21 s into its step, which then took 122 s.
