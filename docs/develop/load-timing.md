<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Load timing (PERF-9)

[Documentation index](../README.md) · [Testing](testing.md#load-timing-perf-9)

**In short:**

- A thread plays audio in real time while the test loads, replaces and
  resets 1, 10 and 30 second filters through each format's own entry points.
- Limits (jrp, 5 November 2026): no callback late or over half its period
  while a load runs elsewhere; a load holds the host's thread at most 100 ms
  (filters up to 10 s) or 250 ms (30 s).
- Only optimised builds without sanitizers are judged. A run whose timing
  cannot be qualified is skipped in CTest, never passed.

How the `vst3.load_timing`, `au.load_timing` and `lv2.load_timing` tests
measure loading, the limits jrp set, how each run gets its verdict and what
the records hold. Read it when one of these tests fails, is skipped or needs
changing; for which tests a change needs, see [testing](testing.md).

The [loading-responsiveness gate](../../TODO.md#loading-responsiveness)
measures, rather than judges by hand in a host, how long each format's own
load entry points hold the thread that calls them, and whether audio
callbacks running meanwhile on another thread finish in time.
[`tests/plugins/load_timing.hpp`](../../tests/plugins/load_timing.hpp) drives
each format's lifecycle driver: a thread calls `process` every 512 frames of
real time at 48 kHz, as an audio device would, while the test thread loads a
stereo filter of 1, 10 and 30 seconds into an empty instance, replaces it with
another as long, and then resets processing from the audio thread. A callback
is late when it finishes after the next one was due. Half a second with
nothing loading before the phases and another after them (the baselines)
show scheduling noise; one would not do, since on Windows
CI on 5 November 2026 a quarter-second baseline was once on time by chance
on a runner where every other phase had two or three late callbacks.

| Test | Entry points timed |
| --- | --- |
| `vst3.load_timing` | `setState`, and the window's file (the controller's load message to `notify`) where the build reads files: macOS, and elsewhere with libsndfile lent as in `vst3.adapter` |
| `au.load_timing` | `RestoreState` (ClassInfo) and the window's load property, macOS only |
| `lv2.load_timing` | `restore()`, with `run()` held back meanwhile because the plug-in does not declare `state:threadSafeRestore`, and a file chosen on the filter parameter, timed as the worker's `work()` |

The renderer records each callback (PERF-12, from jrp's review of
`db54160`): when it was due, when the thread woke for it, when it entered and
left the plug-in, and the thread's CPU time in the call (not on Windows,
whose thread times move only at the scheduler's tick). Each line gives the
time the caller was held and, for the callbacks in that phase, how many there
were, how many were late, how many of those were late only because of the
thread waking late (`sched.`), by how much the worst was late, the latest
wake after its due time, and the longest call into the plug-in. A slow call
with little CPU time is still the plug-in's: the thread may have been
descheduled or blocked on a lock in the plug-in, which the records cannot
tell apart (jrp's review of `5f47ecb`), so the note says so and the
callback is judged. A phase
lasts a third of a second after its load, so callbacks
do not reach a long filter's steady cost, which grows as input history fills
(PERF-5). With `--warm` (PERF-14) the renderer first plays the filter's
length of audio as fast as it can, before the replacement and again before
the reset, so both start from a full history, and half a second of each
filter playing at that cost is shown ("warm first", "warm second") but not
judged, since no limit for steady playing has been decided. Filling takes
minutes with the 30 s filter, so CI does not use it.

The limits (jrp, 5 November 2026): while a load, a replacement or a reset
runs on another thread, no callback may be late or take more than half its
period (5.3 ms here); where the host stops audio (LV2's `restore()` and its
reset) the callbacks are reported only. A load may hold the host's thread at
most 100 ms with filters up to 10 s and 250 ms with 30 s (100 ms feels
instant; 250 ms is where Apple's tools count an application as hung); the
120 s filter and LV2's worker are reported only.

Each run of an entry (one entry point with one filter) gets a verdict
(PERF-10, from jrp's review of `59ac8e0`):

| Verdict | When |
| --- | --- |
| Failed | A load was refused, a callback failed or threw, or the instance did not then save the second filter (in every build, and not run again); or the host's thread was held over its limit, whatever the audio thread saw; or callbacks broke their limits by the plug-in's own time while both baselines were on time |
| Not qualified | Callbacks broke their limits, but a baseline was late too, so the run's timing could not be qualified (the trailing baseline plays a loaded filter, so a late callback there shows uncertainty, not that the machine alone was busy); or they broke them only because the audio thread woke late (late although, started on time, the callback would have finished on time), which these measurements cannot pin on the plug-in; and every run in a build that does not enforce the limits (anything but an optimised build without sanitizers, which only prints what would exceed them) |
| Reported only | No limit applies to any of its phases: LV2's `restore()`, for which the host stops audio, and its reset (the 120 s filter's hold is reported only, but its callbacks are judged, so its entries are not) |
| Passed | None of these |

An entry that did not pass runs again, up to three times, because a shared
CI runner can stall one run without a late baseline (on Windows CI the 30 s
filter once held the host's thread for about 265 ms, where the run before
took 55 ms). The entry passes only if one run passed: one complete
qualifying run that met both limits (jrp's review of `cc2ebde`). Otherwise
it fails if a run had an error, if a limit was broken with evidence in one
run and met in none, or if every run broke a limit (a stalled hold in one,
late callbacks after an on-time baseline in another); it is reported only
if no limit applied; and it is otherwise not qualified. So limits met in
different runs do not add up to a pass: a hold that stalled on the first
run and stayed within the limit on the next, whose callbacks could not be
judged because a baseline was late, leaves the entry not qualified (the macOS
VST3 job on 5 November 2026: 281.6 ms on the first run, 25 to 39 ms on the
next two). A summary at the end lists every entry with every run that did
not pass, so the failed attempts stay in the evidence; for an entry that
did not pass after more than one run it names the run that met each limit;
and its last line counts the entries that passed only after a run that did
not. The test passes only when an entry passed and every other
passed or was reported only; it fails if one failed, and exits 77 when one
was not qualified or none was judged, which CTest reports as skipped
(`SKIP_RETURN_CODE`), never as passed. So sanitizer and debug jobs show the
three tests as skipped.

`native.load_timing_harness` tests the harness without timing it (PERF-12):
`judge()` gives a run's verdict from its records alone, so the rules are
checked on made-up records, as jrp's review of `db54160` listed them: a
deadline missed by a fast callback that woke late (not qualified), a slow
callback (failed) and one as slow with little CPU time (failed, its note
saying the records cannot tell blocked from descheduled), a callback blocked
past the next deadline (failed), a
loader over its limit (failed, also with every callback and both baselines
late: the reproduction from the review of `59ac8e0`), a late baseline before
or after (not qualified), functional errors (failed in every build), no limit
applying (reported only), the 120 s filter's unlimited hold, and a build that
does not enforce the limits. The timing summaries (late, missed deadlines,
whose time) and the rules across runs (`verdict()`, exit codes) are checked
the same way. A few runs with real threads and a mock adapter then check
that the renderer's records reach the verdict: a quick adapter records
every phase, each callback's times in order (due, wake, entry, return) and
its CPU time within the call; a 60 ms load over a 20 ms limit fails on both
of two runs; a refused load fails at once; and with `--warm` the two warm
phases appear, filled faster than real time and reported only. It takes about nine seconds, where the first
version, which slept and spun, took 40 to 60. It also checks the input the
audio thread plays: noise from -0.1 to 0.1 of both signs (PERF-11; the first
version wrapped its negative half to about 1.8e15).

Each test also writes every run's records as JSON (PERF-13), as
`FORMAT-load-timing.json` in its work folder (`vst3-load-timing/` and the
like in the build folder): the Convolver version and commit, the build, the
compiler, the FFT backend, the operating system and processor count, the
time, and for each run its workload, limits, verdict and reasons, and for
each phase its hold, a summary and every callback (due, wake, entry, return,
CPU time and success, in seconds). The Release jobs of the VST3 and Audio
Unit workflows keep these files as a `load-timing-*` artifact, and a failed
job keeps them with its test evidence.

CTest runs the three alone (`RUN_SERIAL`); the first line says which build
ran, and the last gives the overall result. The plug-in candidates leave
them out (`-LE timing` in `candidate.py`), because they run the other tests
alongside: on 5 November 2026 the macOS x86-64 candidate, with the runner
that busy, held the host's thread for about 267 ms with the 10 s filter on
all three runs. The Release jobs of the VST3 and Audio Unit
workflows print the tables in a "Load timings" step; LV2's run only in its
sanitizer job. The
`timing` label selects the three tests. Run a lifecycle executable with
`--load-timing DIRECTORY --long` (LV2: `BUNDLE DIRECTORY --load-timing --long`)
to add the 120-second filter jrp loaded in GarageBand, and `--warm` (after
or instead of `--long`) for the full-history runs above.
