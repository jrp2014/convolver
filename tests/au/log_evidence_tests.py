#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""CI-27: the Audio Unit adapter test's Convolver log reaches the CI evidence
after the test ends abruptly.

jrp's review of f2dc691 (2 November 2026): the test kept its log in a private
temporary file and copied it to the build folder only when it returned or
caught an exception, so a sanitizer abort, a crash or a forced end lost it.
The log now lives in the build folder from the start. For each way of ending
("abort" as a sanitizer report or failed assertion does, "kill" as a forced
end does), this runs the real test binary with a log already past host_log's
1 MiB limit, so its first line rotates the earlier text to ".old"; then it
collects evidence with scripts/ci_evidence.py, as the workflows do, and checks
that both files arrived: the new one with the line the process wrote before
it ended.

Usage: log_evidence_tests.py CONVOLVER_AU_TESTS
"""

import os
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import ci_evidence  # noqa: E402

LARGEST_LOG = 1024 * 1024  # host_log moves the log to ".old" past this.
LOG = Path('plugins/au/convolver-adapter-tests.log')
EARLIER = 'An earlier run of the adapter test.\n'


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def end_abruptly(test, how, folder):
    build = folder / 'build'
    log = build / LOG
    log.parent.mkdir(parents=True)
    log.write_text(EARLIER * (LARGEST_LOG // len(EARLIER) + 1))
    env = {**os.environ, 'CONVOLVER_LOG': str(log)}
    child = subprocess.Popen(
        [test, '--end-abruptly', how], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    out, err = child.communicate(timeout=60)
    expected = {'abort': -signal.SIGABRT, 'kill': -signal.SIGKILL}[how]
    check(
        'logged' in out and child.returncode != 0,
        f'{how}: the test did not log and then end abruptly (exit status {child.returncode})\n{out}{err}',
    )
    if child.returncode != expected:
        print(f'{how}: exit status {child.returncode} rather than {expected} (a sanitizer may handle the signal)')

    evidence = folder / 'evidence'
    copied = ci_evidence.collect(build, evidence)
    current = evidence / LOG
    old = evidence / LOG.with_name(LOG.name + '.old')
    check(str(LOG) in copied and current.is_file(), f'{how}: the log was not collected: {copied}')
    text = current.read_text(errors='replace')
    check(f'(process {child.pid})' in text, f'{how}: the collected log lacks the line process {child.pid} wrote')
    check(old.is_file(), f'{how}: the rotated log was not collected: {copied}')
    check(EARLIER not in text, f'{how}: the log was not rotated, so ".old" was not exercised')
    check(old.read_text(errors='replace').startswith(EARLIER), f'{how}: ".old" lacks the earlier text')
    print(f'{how}: exit status {child.returncode}; collected {", ".join(copied)}')


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    for how in ('abort', 'kill'):
        with tempfile.TemporaryDirectory() as folder:
            end_abruptly(argv[1], how, Path(folder))
    print('The adapter test log survives an abort and a forced end')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
