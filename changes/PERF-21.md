<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PERF-21

## For contributors

- Measurements (PERF-21): `workloads.py` opens its summary with two
  verdicts: stereo room correction and the 2-to-6 crossover against the
  everyday budget (3% of one core at 48 kHz, Latency 0, 64-frame
  callbacks), and every workload measured at all four warm settings against
  the supported ceiling. It exits with status 1 when a budgeted workload is
  over the budget; the ceiling is reported only. `--budget` measures just
  the budget's two workloads, five rounds, in about ten seconds; the
  Workload measurements workflow has a matching "Only the everyday budget's
  workloads" option, and its job fails when a workload is over. The release
  steps run it on each preview's tag, and a result over the budget is
  investigated before publishing; the draft workflow does not require the
  run. `tests/native/benchmarks/workloads_tests.py` (pre-commit) checks the
  verdicts against a stand-in benchmark. On the cloud VM on 25 November
  2026: 0.90% for stereo correction and 1.95% for the crossover. See
  [the everyday budget](../docs/develop/fft-measurements.md#the-everyday-budget).

## Done

- [x] **PERF-21 — An everyday load budget.** 25 November 2026, PR #284
  ([the everyday budget](../docs/develop/fft-measurements.md#the-everyday-budget)).
