# SPDX-License-Identifier: GPL-2.0-or-later
"""COMPAT-1: check this build against the files kept from published releases.

Each folder beside this script (tests/upgrade/VERSION/) holds presets, filters
and inputs from one published release, with the audio that release rendered
(see README.md). This build must load every retained file and reproduce that
audio after the latency it reports: within 1e-5 absolute (the PLUG-2 oracle
tolerance), and sample for sample where it builds and runs as the capture did.

    python3 tests/upgrade/run_upgrade.py --cli build/convolver
    python3 tests/upgrade/run_upgrade.py --vst3-render build/.../convolver_vst3_render \
        --vst3-bundle build/VST3/Release/Convolver.vst3 --vst3-backend OOURA

Only the standard library is used.
"""

import argparse
import hashlib
import json
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'plugins'))
import fixtures  # noqa: E402  (tests/plugins/fixtures.py: WAV helpers)

TOLERANCE = 1e-5


def run(args):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, check=False)
    return result.returncode, result.stdout + result.stderr


def compare(label, actual, expected, exact):
    """None when actual reproduces expected, otherwise what differs."""
    if len(actual) != len(expected):
        return f'{label}: {len(actual)} channels, expected {len(expected)}'
    worst, first, where = 0.0, None, None
    for c, (a, e) in enumerate(zip(actual, expected)):
        if len(a) != len(e):
            return f'{label}: {len(a)} frames in channel {c}, expected {len(e)}'
        for i, (x, y) in enumerate(zip(a, e)):
            if not math.isfinite(x):
                return f'{label}: sample {i} of channel {c} is {x}'
            if x != y:
                if first is None:
                    first = (c, i, x, y)
                if abs(x - y) > worst:
                    worst, where = abs(x - y), (c, i)
    if first is None:
        print(f'{label}: identical')
        return None
    c, i, x, y = first
    detail = (
        f'first difference at channel {c}, frame {i}: {x!r} for {y!r}; '
        f'largest {worst:.3g} at channel {where[0]}, frame {where[1]}'
    )
    if exact:
        return f'{label}: not identical, though built and run as the capture was; {detail}'
    if worst > TOLERANCE:
        return f'{label}: {detail}, above {TOLERANCE}'
    print(f'{label}: within {TOLERANCE} ({detail})')
    return None


def check_files(folder, record):
    """Retained files are never regenerated or edited."""
    failures = []
    for name, digest in record['files'].items():
        path = folder / name
        if not path.is_file():
            failures.append(f'{folder.name}: {name} is missing')
        elif hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            failures.append(f'{folder.name}: {name} changed; retained files are never regenerated')
    return failures


def version_lines(cli):
    code, text = run([cli, '--version'])
    return text.splitlines() if code == 0 else []


def captured_line(record, prefix):
    return next((line for line in record['captured']['cli_version'] if line.startswith(prefix)), None)


def same_kernels(record, lines):
    """True when this CPU uses the multiply-accumulate kernels the capture used."""
    wanted = captured_line(record, 'CPU kernels')
    return wanted is not None and wanted in lines


def same_backend(record, lines):
    wanted = captured_line(record, 'FFT backend')
    return wanted is not None and wanted in lines


def check_cli(folder, record, case, check, cli, exact, work):
    output = work / 'cli.wav'
    code, text = run(
        [
            cli,
            '--filter',
            folder / case['filter'],
            '--input',
            folder / case['input'],
            '--output',
            output,
            '--block-size',
            check['block_size'],
        ]
    )
    label = f'{folder.name} {case["name"]} cli block {check["block_size"]}'
    if code != 0:
        return f'{label}: exit {code}\n{text}'
    actual, rate = fixtures.read_float_wav(output, with_rate=True)
    if rate != case['rate']:
        return f'{label}: rate {rate}, expected {case["rate"]}'
    return compare(label, actual, fixtures.read_float_wav(folder / check['expected']), exact)


def check_vst3(folder, case, check, render, bundle, exact, work):
    label = f'{folder.name} {case["name"]} vst3'
    expected = fixtures.read_float_wav(folder / check['expected'])
    planar = fixtures.read_float_wav(folder / case['input'])
    interleaved = [planar[c][i] for i in range(len(planar[0])) for c in range(len(planar))]
    raw_in, raw_out = work / 'in.f32', work / 'out.f32'
    raw_in.write_bytes(struct.pack(f'={len(interleaved)}f', *interleaved))
    code, text = run(
        [
            render,
            bundle,
            folder / case['preset'],
            case['rate'],
            case['inputs'],
            case['outputs'],
            raw_in,
            raw_out,
            len(expected[0]),
        ]
    )
    if code != 0:
        return f'{label}: exit {code}\n{text}'
    print(f'{label}: {text.strip()} (captured {check["latency"]})')
    data = raw_out.read_bytes()
    samples = struct.unpack(f'={len(data) // 4}f', data)
    actual = [list(samples[c :: case['outputs']]) for c in range(case['outputs'])]
    return compare(label, actual, expected, exact)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--cli', type=Path, help='the convolver command-line tool to check')
    parser.add_argument('--vst3-render', type=Path, help='convolver_vst3_render')
    parser.add_argument('--vst3-bundle', type=Path, help='the Convolver.vst3 bundle to check')
    parser.add_argument('--vst3-backend', help="the bundle's FFT backend, as CMake names it (OOURA)")
    parser.add_argument(
        '--kernels-from', type=Path, help='a convolver CLI from this build, asked which CPU kernels run here'
    )
    args = parser.parse_args(argv)
    if not args.cli and not (args.vst3_render and args.vst3_bundle):
        parser.error('give --cli, or --vst3-render with --vst3-bundle')

    folders = sorted(p.parent for p in HERE.glob('*/manifest.json'))
    if not folders:
        sys.exit('no retained releases found')
    failures = []
    cli_lines = version_lines(args.cli) if args.cli else []
    kernel_lines = version_lines(args.kernels_from) if args.kernels_from else cli_lines
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        for folder in folders:
            record = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
            failures += check_files(folder, record)
            # Sample for sample when the FFT backend and the CPU kernels are the
            # capture's (the block sizes always are). The bundle's kernels are
            # chosen on this CPU as the CLI's are; without a CLI to report them,
            # the bundle is held to the tolerance only.
            cli_exact = same_backend(record, cli_lines) and same_kernels(record, cli_lines)
            vst3_exact = same_kernels(record, kernel_lines) and (
                args.vst3_backend == record['captured']['build']['backend']
            )
            for case in record['cases']:
                for check in case['checks']:
                    if check['renderer'] == 'cli' and args.cli:
                        failure = check_cli(folder, record, case, check, args.cli, cli_exact, work)
                    elif check['renderer'] == 'vst3' and args.vst3_render and args.vst3_bundle:
                        failure = check_vst3(folder, case, check, args.vst3_render, args.vst3_bundle, vst3_exact, work)
                    else:
                        continue
                    if failure:
                        failures.append(failure)
    for failure in failures:
        print(f'FAIL {failure}', file=sys.stderr)
    if failures:
        return 1
    print(f'all retained files from {", ".join(f.name for f in folders)} reproduced')
    return 0


if __name__ == '__main__':
    sys.exit(main())
