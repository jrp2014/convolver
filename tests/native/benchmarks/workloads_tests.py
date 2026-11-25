#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for workloads.py's verdicts (PERF-21) without a benchmark build.

The everyday budget is checked before each preview by running workloads.py
on the release tag; these check, against a stand-in benchmark whose costs
they choose, that a workload over the budget is named and fails the run,
that one within it passes, and that the supported ceiling is reported
without failing the run.
"""

import csv
import importlib.util
import io
import json
import os
import pathlib
import stat
import sys
import tempfile
import textwrap
import unittest
from contextlib import ExitStack, redirect_stderr, redirect_stdout
from unittest import mock

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('workloads', HERE / 'workloads.py')
workloads = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(workloads)

# A stand-in convolver_callback_benchmark: prints the benchmark's two CSV
# lines, with ns_per_frame and p99_us taken from FAKE_COSTS, a JSON object
# keyed by "outputs,taps" (default 100 ns a frame and 10 µs).
FAKE_BENCHMARK = textwrap.dedent(
    r"""
    #!/usr/bin/env python3
    import json, os, sys
    args = dict(zip(sys.argv[1::2], sys.argv[2::2]))
    costs = json.loads(os.environ.get('FAKE_COSTS', '{}'))
    ns, p99 = costs.get(f"{args['--outputs']},{args['--taps']}", (100, 10))
    print('backend,kernels,scenario,block,callback,taps,channels,paths,repetitions,ns_per_frame,'
          'p50_us,p99_us,p999_us,max_us,event_us,prepare_ms,filter,thread_denormals,callbacks')
    print(f"Fake,baseline,{args['--scenario']},{args['--block']},{args['--callback']},{args['--taps']},"
          f"{args['--inputs']}->{args['--outputs']},1,1,{ns},1,{p99},{p99},{p99},0,1,noise,gradual,10")
    """
).lstrip()

# ns_per_frame giving a share of one core at 48 kHz: 625 ns is exactly 3%.
WITHIN = 600
OVER = 650


@unittest.skipIf(os.name == 'nt', 'the stand-in benchmark is a script run directly')
class BudgetRun(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        root = pathlib.Path(self.directory.name)
        self.benchmark = root / 'convolver_callback_benchmark'
        self.benchmark.write_text(f'#!{sys.executable}\n' + FAKE_BENCHMARK.split('\n', 1)[1])
        self.benchmark.chmod(self.benchmark.stat().st_mode | stat.S_IXUSR)
        self.csv = root / 'runs.csv'

    def tearDown(self):
        self.directory.cleanup()

    def run_script(self, costs, *options):
        out, err = io.StringIO(), io.StringIO()
        argv = ['workloads.py', str(self.benchmark), '--csv', str(self.csv), *options]
        with ExitStack() as stack:
            stack.enter_context(mock.patch.dict(os.environ, {'FAKE_COSTS': json.dumps(costs)}))
            stack.enter_context(mock.patch.object(sys, 'argv', argv))
            stack.enter_context(redirect_stdout(out))
            stack.enter_context(redirect_stderr(err))
            status = workloads.main()
        with self.csv.open(encoding='utf-8') as f:
            runs = list(csv.DictReader(f))
        return status, out.getvalue(), err.getvalue(), runs

    def test_budget_within_passes_at_its_one_setting(self):
        status, out, _, runs = self.run_script({'2,65536': (WITHIN, 10), '6,65536': (WITHIN, 10)}, '--budget')
        self.assertEqual(status, 0, out)
        self.assertIn('| stereo correction | 2.88 [2.88 to 2.88] | 3 | within |', out)
        self.assertIn('| 2-to-6 crossover | 2.88 [2.88 to 2.88] | 3 | within |', out)
        # Only the budget's two workloads, five rounds, at 48 kHz, Latency 0, 64 frames.
        self.assertEqual(len(runs), 10)
        self.assertEqual({r['workload'] for r in runs}, {'stereo correction', '2-to-6 crossover'})
        self.assertEqual(
            {(r['rate'], r['latency'], r['callback'], r['scenario']) for r in runs}, {('48000', '0', '64', 'warm')}
        )
        self.assertNotIn('ceiling', out)

    def test_workload_over_budget_is_named_and_fails_the_run(self):
        status, out, err, _ = self.run_script({'2,65536': (WITHIN, 10), '6,65536': (OVER, 10)}, '--budget')
        self.assertEqual(status, 1)
        self.assertIn('| 2-to-6 crossover | 3.12 [3.12 to 3.12] | 3 | OVER: investigate before publishing |', out)
        self.assertIn('| stereo correction | 2.88 [2.88 to 2.88] | 3 | within |', out)
        self.assertIn('Over the everyday budget of 3% of one core: 2-to-6 crossover.', err)

    def test_over_ceiling_is_reported_without_failing(self):
        # --quick: the true-stereo 10 s reverb at 48 kHz over 25% of a core.
        status, out, _, _ = self.run_script({'2,480000': (6000, 10)}, '--quick')
        self.assertEqual(status, 0, out)
        self.assertIn('| true stereo reverb 10 s | 48 kHz | 28.80 | 1 | over |', out)
        self.assertIn('| stereo correction | 48 kHz | 0.48 | 1 | within |', out)
        self.assertIn('| 2-to-6 crossover | not measured | 3 | not judged |', out)


def warm(name, rate, latency, callback, ns, p99):
    return {
        'workload': name,
        'rate': str(rate),
        'latency': str(latency),
        'callback': str(callback),
        'scenario': 'warm',
        'ns_per_frame': str(ns),
        'p99_us': str(p99),
    }


class CeilingVerdicts(unittest.TestCase):
    def test_p99_over_half_its_period_is_over(self):
        # 64 frames at 48 kHz: a period of 1,333 µs; 700 µs is 52.5% of it.
        rows = [warm('w', 48000, latency, callback, 100, 10) for latency, callback in workloads.WARM]
        rows[2]['p99_us'] = '700'
        ((name, rate, _core, p99, ok),) = workloads.ceiling_verdicts(rows)
        self.assertEqual((name, rate, ok), ('w', 48000, False))
        self.assertAlmostEqual(p99, 52.5)

    def test_workload_missing_a_setting_is_not_judged(self):
        rows = [warm('w', 48000, latency, callback, 100, 10) for latency, callback in workloads.WARM[:3]]
        self.assertEqual(workloads.ceiling_verdicts(rows), [])


if __name__ == '__main__':
    unittest.main()
