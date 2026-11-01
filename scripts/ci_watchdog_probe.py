#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run `convolver_request_worker_tests --check-watchdog` repeatedly, supervised.

The check attaches the platform's debugger to itself (CI-22). On the macos-15
arm64 candidate of #104 it ran far past its CTest timeout of 120 s, and CTest
could not end it (CI-25). This script is the outside supervisor: each run is
in a new session with a hard deadline. On a run that overruns, it lists the
processes involved (with process group and state), samples the test if
`sample` is there, kills them and says whether the test process went away.

`--early-kills` adds runs whose debugger is ended while it attaches
(CONVOLVER_DEBUGGER_LIMIT_MS of 50 ms to 1 s): the test must then exit by
itself, leaving no debugger behind. Nothing is retried; every run is reported
(docs/develop/testing.md#watchdog-stacks-check).
"""

from __future__ import annotations

import argparse
import contextlib
import os
import platform
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

DEBUGGERS = ('lldb', 'debugserver', 'gdb')
EARLY_LIMITS_MS = (50, 200, 500, 1000)
ENDED_TRACED = 'ended while traced (no exit status)'
STATUS_GRACE = 5.0  # Seconds to wait for a status that a tracer hands back.
vanished: dict[int, float] = {}  # pid -> when it was first gone from ps.


def is_debugger(command: str) -> bool:
    return Path(command.split(maxsplit=1)[0]).name in DEBUGGERS if command.split() else False


def processes() -> dict[int, tuple[int, int, str, str]]:
    """Map pid -> (ppid, pgid, state, command) from ps."""
    output = subprocess.run(
        ['ps', '-axo', 'pid=,ppid=,pgid=,stat=,command='], capture_output=True, text=True, check=False
    ).stdout
    table = {}
    for line in output.splitlines():
        fields = line.split(None, 4)
        if len(fields) == 5 and fields[0].isdigit():
            table[int(fields[0])] = (int(fields[1]), int(fields[2]), fields[3], fields[4])
    return table


def descendants(root: int, table: dict[int, tuple[int, int, str, str]]) -> set[int]:
    found = {root}
    grew = True
    while grew:
        grew = False
        for pid, (ppid, _pgid, _state, _command) in table.items():
            if ppid in found and pid not in found:
                found.add(pid)
                grew = True
    return found


def ancestors(pid: int, table: dict[int, tuple[int, int, str, str]]) -> set[int]:
    """This process and those above it, which are never killed."""
    found = set()
    while pid > 1 and pid not in found:
        found.add(pid)
        pid = table[pid][0] if pid in table else 1
    return found


def show(pids: set[int], table: dict[int, tuple[int, int, str, str]]) -> None:
    print('  pid   ppid  pgid  state  command')
    for pid in sorted(pids):
        if pid in table:
            ppid, pgid, state, command = table[pid]
            print(f'  {pid:<5} {ppid:<5} {pgid:<5} {state:<6} {command[:150]}')
        else:
            print(f'  {pid:<5} (gone)')


def exit_status(test: subprocess.Popen) -> object:
    """The test's exit code (negative for a signal), or None while it runs.

    While a debugger traces it, macOS makes the debugger its parent, so
    waitpid fails with ECHILD, and Popen.poll() would then report exit 0.
    A test that exits while traced is reaped by its tracer, and macOS then
    hands it back to this process, so a status may come a little late.
    """
    try:
        pid, status = os.waitpid(test.pid, os.WNOHANG)
    except ChildProcessError:
        row = processes().get(test.pid)
        if row is not None and not row[2].startswith('Z'):
            vanished.pop(test.pid, None)
            return None
        since = vanished.setdefault(test.pid, time.monotonic())
        if time.monotonic() - since < STATUS_GRACE:
            return None
        test.returncode = -1000  # Gone, and its status went to the tracer.
        return ENDED_TRACED
    if pid == 0:
        vanished.pop(test.pid, None)
        return None
    if (since := vanished.pop(test.pid, None)) is not None:
        print(f'  The exit status came {time.monotonic() - since:.1f} s after the test was gone from ps.')
    code = -os.WTERMSIG(status) if os.WIFSIGNALED(status) else os.WEXITSTATUS(status)
    test.returncode = code
    return code


def overrun(test: subprocess.Popen, seen: set[int]) -> bool:
    """Report and end a run that overran; returns whether the test went away."""
    table = processes()
    related = seen | descendants(test.pid, table)
    # On a CI runner, also any debugger outside the run's tree (debugserver
    # can outlive lldb); never a developer's own debugging sessions.
    if os.environ.get('GITHUB_ACTIONS') == 'true':
        related |= {pid for pid, row in table.items() if is_debugger(row[3])}
    related -= ancestors(os.getpid(), table)
    print('Processes of the run (and, on CI, any debugger still running):')
    show(related, table)
    if platform.system() == 'Darwin':
        print('sample of the test, 3 s (30 s limit):', flush=True)
        try:
            subprocess.run(['sample', str(test.pid), '3'], timeout=30, check=False)
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f'  sample did not finish: {error}')
    for pid in sorted(related):
        with contextlib.suppress(OSError):
            os.kill(pid, signal.SIGKILL)
    give_up = time.monotonic() + 10
    while exit_status(test) is None:
        if time.monotonic() > give_up:
            print('After SIGKILL to all of them: the test is still there 10 s later.')
            show(related, processes())
            return False
        time.sleep(0.1)
    print('After SIGKILL to all of them: the test exited.')
    return True


def leftovers(seen: set[int]) -> set[int]:
    """Debuggers the run started that are still running 2 s after it ended."""
    give_up = time.monotonic() + 2
    while True:
        table = processes()
        left = {pid for pid in seen if pid in table and is_debugger(table[pid][3])}
        if not left or time.monotonic() > give_up:
            return left
        time.sleep(0.1)


def run_once(binary: Path, deadline: float, limit_ms: int | None, label: str) -> tuple[object, float]:
    env = {**os.environ, 'CI': 'true'}
    if limit_ms is not None:
        env['CONVOLVER_DEBUGGER_LIMIT_MS'] = str(limit_ms)
    with tempfile.TemporaryFile(mode='w+') as log:
        began = time.monotonic()
        test = subprocess.Popen(
            [str(binary.resolve()), '--check-watchdog'],
            stdout=log,
            stderr=subprocess.STDOUT,
            env=env,
            start_new_session=True,
        )
        seen: set[int] = set()
        outcome = None
        while outcome is None:
            if (status := exit_status(test)) is not None:
                outcome = status
            elif time.monotonic() - began > deadline and test.pid not in vanished:
                print(f'::group::{label}: no exit after {deadline:.0f} s')
                log.seek(0)
                print('Output so far:\n' + log.read())
                outcome = 'overran, ended' if overrun(test, seen) else 'overran, would not end'
                print('::endgroup::')
            else:
                seen |= descendants(test.pid, processes())
                time.sleep(0.1)
        elapsed = time.monotonic() - began
        if left := leftovers(seen):
            print(f'::group::{label}: debugger still running after the test ended')
            show(left, processes())
            for pid in left:
                with contextlib.suppress(OSError):
                    os.kill(pid, signal.SIGKILL)
            print('::endgroup::')
            outcome = f'{outcome}, debugger left running'
        if outcome != 0:
            log.seek(0)
            print(f'::group::{label}: exit {outcome}\n{log.read()}\n::endgroup::')
        print(f'{label}: {outcome} in {elapsed:.1f} s', flush=True)
        return outcome, elapsed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('binary', type=Path)
    parser.add_argument('--runs', type=int, default=20)
    parser.add_argument('--early-kills', type=int, default=0, help='runs that end the debugger while it attaches')
    parser.add_argument('--deadline', type=float, default=90.0, help='seconds for each run')
    args = parser.parse_args()
    plan = [(f'Run {n}', None) for n in range(1, args.runs + 1)]
    limits = [EARLY_LIMITS_MS[n % len(EARLY_LIMITS_MS)] for n in range(args.early_kills)]
    plan += [(f'Early kill {n} ({limit} ms)', limit) for n, limit in enumerate(limits, 1)]
    results = []
    for label, limit_ms in plan:
        outcome, elapsed = run_once(args.binary, args.deadline, limit_ms, label)
        # An early kill may leave no stacks (exit 1); stacks in time give 0.
        # A tracer that dies while attached takes the test with it on macOS
        # (SIGKILL, -9, or no status when the tracer had reaped it): ended,
        # not held.
        passed = outcome in ((0, 1, -signal.SIGKILL, ENDED_TRACED) if limit_ms is not None else (0,))
        results.append((label, outcome, elapsed, passed))
    lines = [f'### Watchdog check on {platform.platform()}', '', '| Run | Result | Seconds | Passed |']
    lines += ['| --- | --- | --- | --- |'] + [
        f'| {label} | {outcome} | {elapsed:.1f} | {"yes" if passed else "no"} |'
        for label, outcome, elapsed, passed in results
    ]
    print('\n'.join(lines))
    if summary := os.environ.get('GITHUB_STEP_SUMMARY'):
        with Path(summary).open('a', encoding='utf-8') as out:
            out.write('\n'.join(lines) + '\n')
    return 0 if all(passed for *_, passed in results) else 1


if __name__ == '__main__':
    sys.exit(main())
