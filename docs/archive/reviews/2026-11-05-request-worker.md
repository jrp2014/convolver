# RequestWorker and its watchdog, 1 to 5 November 2026

> **Archived 5 November 2026.** AU-12 replaced `portable/request_worker.hpp`
> with a dispatch source in the Audio Unit, and removed the worker's test
> (`tests/native/request_worker_tests.cpp`), the `native.request_worker` and
> `native.request_worker.watchdog` tests, `.github/workflows/watchdog.yml` and
> `scripts/ci_watchdog_probe.py`; CI-24 and CI-25 closed with them. The text
> below is what [testing](../../develop/testing.md) said about them at
> `1467571` (0.8.11), moved here unchanged apart from links. The code is in
> the history before PR #109.

## The tests

- `native.request_worker`: the thread that applies the Audio Unit's
  Latency (`portable/request_worker.hpp`, PLUG-10). A request runs the
  task; requests while it runs make it run once more; a stop while idle,
  while the task runs (which it waits for) and at each point the worker
  passes between waking and running the task, held there until the stop
  has been asked for, as a destructor asks; and 2000 workers requested and
  stopped at once. Each case has a watchdog: when a case makes no progress
  for 10 seconds (each of the 2000 workers counts as progress), the test
  fails instead of hanging and reports a timeout with the workers done, what
  the test's thread was doing (starting, requesting or stopping a worker),
  the last point the current worker reported from its own thread (half the
  workers have a probe for this; the other half run without one, as the
  Audio Unit's does), and every thread's stack from the platform's debugger
  (cdb, gdb or lldb). The debugger suspends the test while attached, so a
  separate process ends it after 30 seconds; the report says how long the
  debugger took. cdb reads symbols only from the test's own folder, so
  Windows frames show by their exports: with the runners' default path
  (`srv*`, Microsoft's symbol server over the network) it took 2 to over 30
  seconds, ran past 30 in the Ooura builds on 35221f0 and 6f20a44, and on
  fa5ce6a printed no listing in time, most likely while it looked symbols
  up. It reports a timeout, not a
  lost stop: the stacks tell a stall on a busy runner from a lost wake.
  Conditions are waited for by the clock, up to 5 seconds, so they fail as
  themselves before the watchdog fires. With the order jrp found (the stop
  checked before the request is taken), the case held as the worker wakes
  hangs and fails. `native.request_worker.watchdog` runs a case that never
  finishes, with a 1-second limit, and passes only when the report comes
  with a thread listing from the debugger. It is skipped where none is
  installed and, outside CI (no `CI` variable), where the debugger cannot
  attach; the native Linux CI jobs install gdb. A debugger that overruns
  is killed with its process group and, on macOS, with the children lldb
  started: lldb starts `debugserver` in a process group of its own, and
  `debugserver` holds the test suspended until it exits. However the
  debugger ends, the supervisor then sends the test SIGCONT: lldb's attach
  stops the test, and a stop can outlast the debugger. On macOS
  `debugserver` may finish detaching after lldb exits, so the supervisor
  resends SIGCONT until nothing traces the test, for up to 2 seconds. A tracer that dies
  while attached takes the test with it on macOS, so there the test may
  end with SIGKILL after the line "The debugger ran past its time limit
  and is ended". The time limit is 30 seconds, or
  `CONVOLVER_DEBUGGER_LIMIT_MS` (the
  [Watchdog stacks check](#watchdog-stacks-check) sets it short). The check has the CTest label `debugger`, which the
  package candidates leave out ([below](../../develop/testing.md#which-configurations-run-which-tests)).

## Evidence from CI

- `native.request_worker`, Windows (PR #102): "many workers did not finish
  within 10 s" in the Ooura build, its only run of that binary, while two
  package consumer builds ran beside it; the main and core-only builds' tests
  had passed in about 0.5 s earlier in that job. The cause is not
  established: a stall on an oversubscribed runner and a lost
  wake look alike from the old report. It happened again on PR #105 (1
  November, main's test without this report): again the Ooura build, the
  job's third run of the binary after passes in about 0.5 seconds, this
  time beside `native.kernel_divergence` and `native.host_lifecycle`
  rather than the consumer builds. The watchdog now records the evidence
  that tells them apart ([above](../../develop/testing.md#native-production-tests)).
- `native.request_worker.watchdog`, the macOS arm64 candidate on
  `macos-15` (PR #104, 1 November 2026): it started and never finished,
  though its CTest timeout is 120 seconds, and the job's build-and-test step
  ran for over 18 minutes until it was cancelled. The same check passed on
  that image in the Audio Unit job (7 seconds), on `macos-15-intel` and on
  `macos-26`. The [Watchdog stacks check](#watchdog-stacks-check) then
  caught a stall on `macos-26` (35221f0, 1 run in 20): lldb was gone and
  its supervisor had exited by the normal path (no overrun note), but the
  test was left stopped (state `T`) in its wait for the supervisor. That is
  consistent with lldb's attach stopping the test (SIGSTOP) and nothing
  resuming it; only an overrun sent SIGCONT. The supervisor then sent
  SIGCONT however the debugger ended, and on macOS it now resends SIGCONT
  until nothing traces the test, in case `debugserver` detaches after
  lldb exits (a SIGCONT sent while it still traces can be lost). Why CTest's 120-second timeout did not end the stopped test on the
  candidate is not established (CI-25). The candidates still leave the
  check out, but `native.request_worker` runs there, and a real stall
  would take the same lldb path, bounded by the step's time limit. While
  `debugserver` traces the test, macOS makes it the test's parent, so the
  test's own parent (CTest, or the probe) cannot wait for it until the
  debugger lets go; and a tracer that dies while attached takes the test
  with it (SIGKILL).

## Watchdog stacks check

`.github/workflows/watchdog.yml` builds only the request_worker test, with
the candidates' flags (`-O3 -DNDEBUG -g`), and runs `--check-watchdog` 50
times on `macos-15`, `macos-15-intel`, `macos-latest` and Linux with gdb,
each run in a session of its own under `scripts/ci_watchdog_probe.py` with
a 90-second deadline. For a run that overruns it shows the test's output,
every process of the run with its process group and state, and a `sample`
of the test on macOS; it then kills them and says whether the test went
away. Fifty more runs end the debugger while it attaches
(`CONVOLVER_DEBUGGER_LIMIT_MS` of 50 ms to 1 second): each must end, with
or without stacks or by the SIGKILL macOS sends when the tracer dies (then
sometimes with no exit status for the probe), and leave no debugger
running. A debugger
left behind fails the run. Only on CI does the script also kill debuggers
outside the run's own processes.

Each run is alone on an idle runner, with its output to a file. The
candidate ran it from a CMake build under `ctest --parallel`, beside other
tests, with CTest reading its output through pipes; passes here do not rule
out a cause that needs those. The workflow runs for pull requests that
change the worker, its test, the script or the workflow, and by hand ("Run
workflow", with the number of runs).

On 1 November 2026 (90b9a40) the 20 runs on Linux printed stacks within
1.5 seconds. The macOS runs of that commit and the next two (5e96a3f,
d7a9795) are not evidence: they took the probe's word for when the test
ended, and on macOS that was wrong: with the test reparented to `debugserver`, Python's `Popen.poll()`
gets ECHILD and reports exit 0. So they showed "debugger left running"
for runs that had not finished. The probe now waits with `waitpid` itself
and treats ECHILD as still running while the process exists. On 1b9350f
all 80 plain runs printed stacks (at most 5.2 seconds on `macos-15`, 4.3
on `macos-26`, 12.5 on `macos-15-intel` and 1.4 on Linux), and all 80
early kills ended: on macOS with exit 0, 1 or SIGKILL (three times with no
exit status), on Linux with exit 0 or 1. None hung and none left a
debugger running. On 35221f0 run 15 of 20 on `macos-26` overran: the
probe's listing showed the test stopped (`T`) with its supervisor a
zombie, and `sample` showed the test's main thread in `wait4` in
`print_stacks()`. That is the stall described
[above](#evidence-from-ci). On 6f20a44, with SIGCONT after every
debugger, all 200 plain runs printed stacks and all 200 early kills ended
(the slowest plain run 10.8 seconds, on `macos-15-intel`). On bcf9c65 (the
same code) plain run 29 of 50 on `macos-26`, and on df822c1 (which waits
for the detach) plain run 48 of 50 on `macos-15`, ended in normal time
with the stacks printed but no exit status for the probe: the test was
gone from `ps` and `waitpid` gave ECHILD. Whether it exited while still
traced, or the probe looked in the moment the test was handed back, is
not known. A test that exits while traced is reaped by its tracer, and
macOS then hands it back to its parent, so the probe now waits up to 5
seconds for a late status and says how late it came. On fa5ce6a all 400
runs passed and every status came: at most 0.5 seconds late, each from an
early kill that ended with SIGKILL. So those two runs were the probe's
misreading, not a test left behind.
