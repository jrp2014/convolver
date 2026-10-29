# SPDX-License-Identifier: GPL-2.0-or-later
"""The fuzz scheduler's failure paths (CI-15), with stand-in fuzzers.

scripts/fuzz_campaigns.sh runs the fuzz workflow's campaigns side by side
(CI-12). A successful CI run shows only that passing campaigns pass; these
tests check that a failing campaign, a campaign that never records its exit
status (even with a stale status file from an earlier run) and a missing
fuzzer fail the step, that the other campaigns still run, and that no more
campaigns run at once than the limit. Each stand-in is a small shell script
named like a fuzz target; nothing is compiled.
"""

import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/fuzz_campaigns.sh'
TARGETS = ('alpha', 'beta', 'gamma', 'delta')

# Each stand-in notes its arguments, marks itself running for SLEEP seconds,
# notes how many campaigns were running then, and ends as BEHAVIOUR says.
STAND_IN = r"""#!/usr/bin/env bash
name=$(basename "$0")
printf '%s\n' "$@" > "$RECORD/$name.args"
mkdir "$RECORD/running/$name"
ls "$RECORD/running" | wc -l >> "$RECORD/concurrency"
sleep "${SLEEP:-0}"
rmdir "$RECORD/running/$name"
echo "#2 DONE   cov: 1 ft: 1 corp: 1/1b"
echo "Done 2 runs in 0 second(s)"
case "$BEHAVIOUR" in
  fail) echo "==1==ERROR: AddressSanitizer: stand-in finding"; exit 1 ;;
  # The subshell that would record the exit status dies first, as when the
  # runner kills it.
  kill-recorder) kill -9 "$PPID"; exit 0 ;;
esac
exit 0
"""


def bash_supports_wait_n():
    bash = shutil.which('bash')
    if os.name != 'posix' or not bash:
        return False
    version = subprocess.run(
        [bash, '-c', 'echo "${BASH_VERSINFO[0]} ${BASH_VERSINFO[1]}"'], capture_output=True, text=True
    ).stdout.split()
    return len(version) == 2 and (int(version[0]), int(version[1])) >= (4, 3)


@unittest.skipUnless(bash_supports_wait_n(), 'needs bash 4.3 or later (macOS: install a newer bash)')
class SchedulerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='convolver-fuzz-scheduler-')
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.build = self.root / 'build'
        self.output = self.root / 'out'
        self.record = self.root / 'record'
        (self.record / 'running').mkdir(parents=True)
        self.build.mkdir()
        for target in TARGETS:
            for kind in ('seeds', 'regressions'):
                folder = self.source / 'tests/fuzz' / kind / target
                folder.mkdir(parents=True)
                (folder / 'input').write_text(target)
            self.stand_in(target)

    def tearDown(self):
        self.temporary.cleanup()

    def stand_in(self, target, behaviour='pass'):
        program = self.build / ('fuzz_' + target)
        text = STAND_IN.replace('"$BEHAVIOUR"', '"' + behaviour + '"')
        program.write_text(text)
        program.chmod(program.stat().st_mode | stat.S_IXUSR)

    def run_scheduler(self, *arguments, jobs='2', sleep='0', targets=TARGETS, path=None):
        command = [
            'bash',
            str(SCRIPT),
            '--build',
            str(self.build),
            '--output',
            str(self.output),
            '--source',
            str(self.source),
            '--seconds',
            '7',
        ]
        if jobs is not None:
            command += ['--jobs', jobs]
        command += list(arguments) + list(targets)
        environment = dict(os.environ, RECORD=str(self.record), SLEEP=sleep)
        if path:
            environment['PATH'] = path
        return subprocess.run(command, capture_output=True, text=True, env=environment, timeout=120)

    def concurrency(self):
        return [int(line) for line in (self.record / 'concurrency').read_text().split()]

    def test_all_pass(self):
        result = self.run_scheduler()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for target in TARGETS:
            self.assertIn(f'== fuzz_{target} exited with 0', result.stdout)
            self.assertIn('Done 2 runs', (self.output / f'{target}.log').read_text())
            self.assertEqual((self.output / 'corpus' / target / 'input').read_text(), target)
        # The arguments the workflow has always passed, with the time budget.
        self.assertEqual(
            (self.record / 'fuzz_alpha.args').read_text().splitlines(),
            [
                str(self.output / 'corpus/alpha'),
                str(self.source / 'tests/fuzz/regressions/alpha'),
                '-max_total_time=7',
                '-max_len=65536',
                '-timeout=2',
                '-rss_limit_mb=512',
                f'-artifact_prefix={self.output}/alpha-',
            ],
        )

    def test_failing_campaign_fails_the_step(self):
        self.stand_in('beta', 'fail')
        result = self.run_scheduler()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('== fuzz_beta exited with 1', result.stdout)
        self.assertIn('stand-in finding', result.stdout)
        # The others still ran to the end and are reported.
        for target in ('alpha', 'gamma', 'delta'):
            self.assertIn(f'== fuzz_{target} exited with 0', result.stdout)

    def test_last_campaign_failing_fails_the_step(self):
        self.stand_in('delta', 'fail')
        result = self.run_scheduler(jobs='1')
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('== fuzz_delta exited with 1', result.stdout)

    def test_missing_status_file_fails_the_step(self):
        self.stand_in('gamma', 'kill-recorder')
        result = self.run_scheduler()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('== fuzz_gamma left no exit status', result.stdout)
        self.assertFalse((self.output / 'gamma.status').exists())
        for target in ('alpha', 'beta', 'delta'):
            self.assertIn(f'== fuzz_{target} exited with 0', result.stdout)

    def test_stale_status_file_is_not_reused(self):
        (self.output).mkdir()
        (self.output / 'gamma.status').write_text('0\n')
        self.stand_in('gamma', 'kill-recorder')
        result = self.run_scheduler()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('== fuzz_gamma left no exit status', result.stdout)

    def test_missing_fuzzer_fails_the_step(self):
        (self.build / 'fuzz_alpha').unlink()
        result = self.run_scheduler()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('== fuzz_alpha exited with 127', result.stdout)

    def test_missing_seed_folder_stops_before_any_campaign(self):
        shutil.rmtree(self.source / 'tests/fuzz/seeds/delta')
        result = self.run_scheduler()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('no seed folder', result.stderr)
        self.assertFalse((self.record / 'concurrency').exists())

    def test_concurrency_limit(self):
        # Each campaign runs for a second, long enough for the limit to be
        # reached; more than the limit at once would show in the record.
        for jobs in ('1', '2', '3'):
            with self.subTest(jobs=jobs):
                (self.record / 'concurrency').unlink(missing_ok=True)
                result = self.run_scheduler(jobs=jobs, sleep='1')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f'Running {jobs} campaigns at a time', result.stdout)
                counts = self.concurrency()
                self.assertEqual(len(counts), len(TARGETS))
                self.assertLessEqual(max(counts), int(jobs))
                self.assertEqual(max(counts), int(jobs), 'the limit was never reached')

    def test_default_limit_is_at_least_one(self):
        result = self.run_scheduler(jobs=None)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertRegex(result.stdout, r'Running [1-9][0-9]* campaigns at a time')

    def test_default_limit_sources(self):
        # Stand-ins for lscpu, sysctl and getconf, first on PATH. lscpu's
        # parsable output lists one line per hardware thread: 4 threads on
        # 2 cores give 2 campaigns at a time, as on GitHub's Linux runner.
        lscpu_two_cores = 'printf "# CORE,SOCKET\\n0,0\\n0,0\\n1,0\\n1,0\\n"'
        cases = (
            ({'lscpu': lscpu_two_cores}, 'Running 2 campaigns', '2 physical cores (lscpu)'),
            ({'sysctl': 'echo 3'}, 'Running 3 campaigns', '3 physical cores (sysctl)'),
            ({'getconf': 'echo 5'}, 'Running 5 campaigns', '5 CPUs (getconf'),
            ({}, 'Running 1 campaigns', 'no CPU count found'),
        )
        for working, running, described in cases:
            with self.subTest(described=described):
                tools = self.root / 'tools'
                shutil.rmtree(tools, ignore_errors=True)
                tools.mkdir()
                for tool in ('lscpu', 'sysctl', 'getconf'):
                    program = tools / tool
                    program.write_text('#!/bin/sh\n' + working.get(tool, 'exit 1') + '\n')
                    program.chmod(0o755)
                result = self.run_scheduler(jobs=None, path=f'{tools}{os.pathsep}{os.environ["PATH"]}')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(running + ' at a time', result.stdout)
                self.assertIn(described, result.stdout)

    def test_rejects_bad_numbers(self):
        for option, value in (('--jobs', '0'), ('--jobs', 'two'), ('--seconds', '-5'), ('--seconds', '')):
            with self.subTest(option=option, value=value):
                command = ['bash', str(SCRIPT), '--source', str(self.source), option, value, 'alpha']
                result = subprocess.run(command, capture_output=True, text=True, timeout=30, cwd=self.root)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertFalse((self.record / 'concurrency').exists())


if __name__ == '__main__':
    unittest.main()
