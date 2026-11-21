#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure the supported plug-in workloads (PERF-3).

Runs convolver_callback_benchmark, which drives the engine through
HostProcessor as the plug-ins do, over a fixed set of long and multichannel
filters, and records for each run the callback times, the preparation time and
the process's peak resident memory. One process per run, so each peak belongs
to one workload:

  warm     callbacks once the history is full, at Latency 0 (the plug-ins'
           default) and 256, in 64- and 512-frame callbacks
  startup  preparing a new processor (the plug-in's first load) and the
           callbacks that follow, in 512-frame callbacks at Latency 0
  replace  preparing a replacement while the first filter plays, and the
           crossfade's callbacks: two engines are alive, so this is the peak
           memory of a live replacement

The benchmark builds its filters at 48 kHz; the engine's work does not depend
on the rate, so a 96 kHz workload is the same filter with twice the taps, and
its share of a core and its callback period are given at 96 kHz. Each
workload also gives the plug-in state its samples need (four bytes a sample,
plus a few hundred bytes of routes and settings) against the 64 MiB limit:
workloads over it are measured as command-line work only.

  python3 tests/native/benchmarks/workloads.py BUILD/convolver_callback_benchmark \\
      --rounds 3 --csv raw.csv > summary.md

The summary (Markdown, on standard output) gives the median over rounds of
each measurement; the CSV keeps every run with the machine's description.
--quick runs one round of three workloads, to check a build and the script.
Results are indicative on shared machines (cloud VMs, CI runners): read the
medians and spread, not single maxima.
"""

import argparse
import csv
import os
import platform
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

MAX_STATE_BYTES = 64 * 1024 * 1024

# name, layout, inputs, outputs, seconds of filter or None for taps, taps at 48 kHz
# Room correction and crossovers use 65,536-tap filters (1.4 s at 48 kHz,
# typical of DRC and Audiolense output); reverbs 10, 30 and 60 s.
WORKLOADS = [
    ('baseline', 'diagonal', 1, 1, None, 64),
    ('stereo correction', 'diagonal', 2, 2, None, 65536),
    ('2-to-6 crossover', 'crossover', 2, 6, None, 65536),
    ('5.1 correction', 'diagonal', 6, 6, None, 65536),
    ('7.1 correction', 'diagonal', 8, 8, None, 65536),
    ('7.1 matrix', 'matrix', 8, 8, None, 65536),
    ('stereo reverb 10 s', 'diagonal', 2, 2, 10, None),
    ('stereo reverb 30 s', 'diagonal', 2, 2, 30, None),
    ('stereo reverb 60 s', 'diagonal', 2, 2, 60, None),
    ('true stereo reverb 10 s', 'matrix', 2, 2, 10, None),
    ('true stereo reverb 30 s', 'matrix', 2, 2, 30, None),
    ('true stereo reverb 60 s', 'matrix', 2, 2, 60, None),
    ('7.1 reverb 10 s', 'diagonal', 8, 8, 10, None),
]
QUICK = ('baseline', 'stereo correction', 'true stereo reverb 10 s')
RATES = (48000, 96000)
WARM = [(0, 64), (0, 512), (256, 64), (256, 512)]
ONCE = [('startup', 0, 512), ('replace', 0, 512)]


def paths(layout, inputs, outputs):
    if layout == 'diagonal':
        return inputs
    if layout == 'crossover':
        return outputs
    return inputs * outputs


def cases(quick):
    for name, layout, inputs, outputs, seconds, taps48 in WORKLOADS:
        if quick and name not in QUICK:
            continue
        for rate in RATES:
            if name == 'baseline' and rate != 48000:
                continue
            taps = seconds * rate if seconds else taps48 * rate // 48000
            n = paths(layout, inputs, outputs)
            yield {
                'workload': name,
                'rate': rate,
                'layout': layout,
                'inputs': inputs,
                'outputs': outputs,
                'paths': n,
                'taps': taps,
                'filter_seconds': round(taps / rate, 3),
                'state_bytes': 4 * taps * n,
                'fits_plugin_state': 4 * taps * n + 4096 <= MAX_STATE_BYTES,
            }


def peak_rss_windows(process):
    import ctypes
    from ctypes import wintypes

    class Counters(ctypes.Structure):
        _fields_ = [
            ('cb', wintypes.DWORD),
            ('PageFaultCount', wintypes.DWORD),
            ('PeakWorkingSetSize', ctypes.c_size_t),
            ('WorkingSetSize', ctypes.c_size_t),
            ('QuotaPeakPagedPoolUsage', ctypes.c_size_t),
            ('QuotaPagedPoolUsage', ctypes.c_size_t),
            ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t),
            ('QuotaNonPagedPoolUsage', ctypes.c_size_t),
            ('PagefileUsage', ctypes.c_size_t),
            ('PeakPagefileUsage', ctypes.c_size_t),
        ]

    counters = Counters()
    counters.cb = ctypes.sizeof(counters)
    psapi = ctypes.WinDLL('psapi')
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
    # The process has exited, but Popen keeps its handle open until the object goes.
    handle = int(process._handle)  # noqa: SLF001
    if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
        return 0
    return counters.PeakWorkingSetSize


def run(command):
    """Run the benchmark; return its result row and peak resident bytes."""
    # Errors go to a file, so a child that writes much to both streams
    # cannot block on a full pipe while this reads the other.
    with tempfile.TemporaryFile(mode='w+') as errors:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors, text=True)
        if os.name == 'nt':
            out, _ = process.communicate()
            peak = peak_rss_windows(process)
            status = process.returncode
        else:
            # wait4 gives this child's own peak, not the largest of all children.
            out = process.stdout.read()
            _, wait_status, usage = os.wait4(process.pid, 0)
            process.returncode = os.waitstatus_to_exitcode(wait_status)
            status = process.returncode
            peak = usage.ru_maxrss * (1 if sys.platform == 'darwin' else 1024)
        errors.seek(0)
        err = errors.read()
    if status != 0:
        raise SystemExit(f'{" ".join(command)} failed ({status}): {err.strip()}')
    header, row = out.strip().splitlines()[-2:]
    return dict(zip(header.split(','), row.split(','))), peak


def cpu_name():
    try:
        if sys.platform == 'darwin':
            return subprocess.run(
                ['sysctl', '-n', 'machdep.cpu.brand_string'], capture_output=True, text=True, check=False
            ).stdout.strip()
        if sys.platform.startswith('linux'):
            with Path('/proc/cpuinfo').open(encoding='utf-8') as f:
                for line in f:
                    if line.startswith('model name'):
                        return line.split(':', 1)[1].strip()
        if os.name == 'nt':
            import winreg

            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DESCRIPTION\System\CentralProcessor\0')
            return winreg.QueryValueEx(key, 'ProcessorNameString')[0].strip()
    except OSError:
        pass
    return platform.processor()


def machine():
    return f'{cpu_name()}; {os.cpu_count()} logical CPUs; {platform.platform()}; Python {platform.python_version()}'


def median(rows, key):
    values = [float(r[key]) for r in rows if r.get(key) not in (None, '')]
    return statistics.median(values) if values else float('nan')


def spread(rows, key):
    values = [float(r[key]) for r in rows]
    return min(values), max(values)


def summary(rows, label):
    out = [f'# Workload measurements: {label}', '', f'Machine: {rows[0]["machine"]}', '']
    out.append(
        f'Backend: {rows[0]["backend"]}; kernels: {rows[0]["kernels"]}; rounds: {max(int(r["round"]) for r in rows)}'
    )
    out.append('')
    out.append('## Callbacks once the history is full')
    out.append('')
    out.append(
        'Median over rounds. Core: mean processing time as a share of one core at the '
        "workload's rate. p99 and max: callback times in µs; period: the callback's "
        'real-time budget in µs. RSS: peak resident memory of the benchmark process in MiB.'
    )
    out.append('')
    out.append(
        '| Workload | Rate | Taps | Paths | State MiB | Latency | Callback | Core % '
        '| p99 µs | max µs [range] | Period µs | RSS MiB |'
    )
    out.append('| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |')
    key = lambda r: (r['workload'], int(r['rate']), int(r['latency']), int(r['callback']))  # noqa: E731
    order = []
    groups = {}
    for r in rows:
        if r['scenario'] != 'warm':
            continue
        k = key(r)
        if k not in groups:
            order.append(k)
            groups[k] = []
        groups[k].append(r)
    for k in order:
        g = groups[k]
        rate = int(g[0]['rate'])
        core = median(g, 'ns_per_frame') * rate / 1e7
        lo, hi = spread(g, 'max_us')
        period = int(g[0]['callback']) * 1e6 / rate
        out.append(
            f'| {k[0]} | {rate // 1000} kHz | {int(g[0]["taps"]):,} | {g[0]["paths"]} | '
            f'{int(g[0]["state_bytes"]) / 2**20:.1f}{"" if g[0]["fits_plugin_state"] == "True" else " (over)"} | '
            f'{k[2]} | {k[3]} | {core:.2f} | {median(g, "p99_us"):.0f} | '
            f'{median(g, "max_us"):.0f} [{lo:.0f} to {hi:.0f}] | {period:.0f} | '
            f'{median(g, "peak_rss_bytes") / 2**20:.0f} |'
        )
    out.append('')
    out.append('## Preparation and replacement')
    out.append('')
    out.append(
        'Median over rounds, Latency 0, 512-frame callbacks. Prepare: building the engine '
        '(transforming the filter) off the audio thread, in ms. Startup and replacement '
        'p99 and max: the callbacks covering the filter after the event, in µs. '
        'RSS: peak resident MiB (replacement holds two filters and two engines).'
    )
    out.append('')
    out.append(
        '| Workload | Rate | Prepare ms | Startup RSS MiB | Startup p99 µs | Startup max µs '
        '| Replace prepare ms | Replace max µs | Replace RSS MiB |'
    )
    out.append('| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |')
    seen = []
    for r in rows:
        k = (r['workload'], int(r['rate']))
        if r['scenario'] in ('startup', 'replace') and k not in seen:
            seen.append(k)
    for k in seen:
        s = [r for r in rows if (r['workload'], int(r['rate'])) == k and r['scenario'] == 'startup']
        p = [r for r in rows if (r['workload'], int(r['rate'])) == k and r['scenario'] == 'replace']
        out.append(
            f'| {k[0]} | {k[1] // 1000} kHz | {median(s, "prepare_ms"):.0f} | '
            f'{median(s, "peak_rss_bytes") / 2**20:.0f} | {median(s, "p99_us"):.0f} | {median(s, "max_us"):.0f} | '
            f'{median(p, "prepare_ms"):.0f} | {median(p, "max_us"):.0f} | {median(p, "peak_rss_bytes") / 2**20:.0f} |'
        )
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('benchmark', help='path to convolver_callback_benchmark (a Release build)')
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--seconds', type=float, default=1.0, help='timed seconds per warm run')
    parser.add_argument('--csv', help='write every run here')
    parser.add_argument('--label', default=platform.node() or 'this machine')
    parser.add_argument('--quick', action='store_true', help='one round of three workloads')
    args = parser.parse_args()
    rounds = 1 if args.quick else args.rounds
    host = machine()
    rows = []
    started = time.monotonic()
    all_cases = list(cases(args.quick))
    for number in range(1, rounds + 1):
        # Every other round runs the workloads in reverse, so drift on the
        # machine does not fall on the same workloads each time.
        ordered = all_cases if number % 2 else list(reversed(all_cases))
        for case in ordered:
            runs = [('warm', latency, callback) for latency, callback in WARM]
            runs += ONCE if case['workload'] != 'baseline' else []
            for scenario, latency, callback in runs:
                command = [
                    args.benchmark,
                    '--scenario',
                    scenario,
                    '--taps',
                    str(case['taps']),
                    '--inputs',
                    str(case['inputs']),
                    '--outputs',
                    str(case['outputs']),
                    '--layout',
                    case['layout'],
                    '--block',
                    str(latency),
                    '--callback',
                    str(callback),
                    '--seconds',
                    str(args.seconds if scenario == 'warm' else 0.001),
                ]
                result, peak = run(command)
                row = {'round': number, **case, 'latency': latency, **result, 'peak_rss_bytes': peak, 'machine': host}
                row['scenario'] = scenario
                row['callback'] = callback
                rows.append({k: str(v) for k, v in row.items()})
                print(
                    f'[{time.monotonic() - started:7.0f} s] round {number} {case["workload"]} '
                    f'{case["rate"]} {scenario} L{latency} C{callback}: {float(result["ns_per_frame"]):.0f} ns/frame, '
                    f'max {float(result["max_us"]):.0f} µs, {peak / 2**20:.0f} MiB',
                    file=sys.stderr,
                    flush=True,
                )
    if args.csv:
        with Path(args.csv).open('w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    sys.stdout.write(summary(rows, args.label))


if __name__ == '__main__':
    main()
