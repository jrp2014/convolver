# SPDX-License-Identifier: GPL-2.0-or-later
"""Capture the files COMPAT-1 keeps from one published release.

Run once per published preview, on Linux x86-64, with the release's own Linux
package unpacked. Every expected file is rendered by that package's binaries:
its `convolver` command-line tool and its VST3 plug-in, which
`convolver_vst3_render` loads from the package as a host does. The package's
example presets and their filters are copied byte for byte.

    python3 tests/upgrade/capture.py --package DIR --render build/convolver_vst3_render \
        --version 0.13.2 --source-commit SHA --run-id ID --zip Convolver-0.13.2-...zip ...

DIR is the unpacked `Convolver-X.Y.Z-preview-linux-x86_64.zip`; give every
published ZIP with --zip so the record holds each one's SHA-256. The script
refuses to write into an existing folder: a retained file is never
regenerated (tests/upgrade/README.md).
"""

import argparse
import datetime as dt
import hashlib
import json
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'plugins'))
import fixtures  # noqa: E402  (tests/plugins/fixtures.py: WAV helpers and noise)

INPUT_FRAMES = 1024  # Per preset input; the first-use examples bring their own.
BLOCK_SIZES = (0, 256)  # The CLI's default (no latency), and the old default 256.


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def machine():
    """The capture's system: Ooura's twiddle factors come from this libm."""
    try:
        system = platform.freedesktop_os_release().get('PRETTY_NAME', platform.system())
    except OSError:
        system = platform.system()
    libc = ' '.join(platform.libc_ver())
    return f'{system}, {libc}, {platform.machine()}'


def run(args):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.exit(f'{args[0]} failed ({result.returncode}):\n{result.stdout}{result.stderr}')
    return result.stdout


def cases(package):
    """Every preset the package ships, with the filter a user would pick for the CLI."""
    found = []
    for entry in json.loads((package / 'presets/examples.json').read_text()):
        name = entry['name']
        found.append(
            {
                'name': name,
                'preset': f'package/presets/{name}/{name}.vstpreset',
                'filter': f'package/presets/{name}/filter.cfg',
                'rate': entry['rate'],
                'inputs': entry['channels'],
                'outputs': entry['channels'],
                'input': f'inputs/{entry["rate"]}-{entry["channels"]}ch.wav',
            }
        )
    for name, source in (('mono', 'impulse.wav'), ('stereo', 'filter.cfg'), ('crossover', 'filter.cfg')):
        folder = package / 'examples/first-use' / name
        planar, rate = fixtures.read_float_wav(folder / 'input.wav', with_rate=True)
        outputs = len(fixtures.read_float_wav(folder / 'expected.wav'))
        found.append(
            {
                'name': f'first-use-{name}',
                'preset': f'package/examples/first-use/{name}/first-use-{name}.vstpreset',
                'filter': f'package/examples/first-use/{name}/{source}',
                'rate': rate,
                'inputs': len(planar),
                'outputs': outputs,
                'input': f'package/examples/first-use/{name}/input.wav',
            }
        )
    return found


def write_inputs(out, found):
    for case in found:
        path = out / case['input']
        if case['input'].startswith('inputs/') and not path.exists():
            channels = [fixtures.noise(INPUT_FRAMES, seed=17 + c) for c in range(case['inputs'])]
            channels = [[fixtures.to_float32(v) for v in c] for c in channels]
            path.parent.mkdir(parents=True, exist_ok=True)
            fixtures.write_float_wav(path, case['rate'], channels)


def render_vst3(render, bundle, out, case, frames, work):
    planar = fixtures.read_float_wav(out / case['input'])
    raw_in, raw_out = work / 'in.f32', work / 'out.f32'
    interleaved = [planar[c][i] for i in range(len(planar[0])) for c in range(len(planar))]
    raw_in.write_bytes(struct.pack(f'={len(interleaved)}f', *interleaved))
    printed = run(
        [render, bundle, out / case['preset'], case['rate'], case['inputs'], case['outputs'], raw_in, raw_out, frames]
    )
    latency = int(printed.split()[1])
    data = raw_out.read_bytes()
    samples = struct.unpack(f'={len(data) // 4}f', data)
    channels = [list(samples[c :: case['outputs']]) for c in range(case['outputs'])]
    return channels, latency


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--render', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--zip', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    package = args.package.resolve()
    out = (args.out or HERE / args.version).resolve()
    if out.exists():
        sys.exit(f'{out} exists: retained files are never regenerated')
    cli = package / 'bin/convolver'
    bundle = package / 'VST3/Convolver.vst3'
    manifest = json.loads((package / 'artifact-manifest.json').read_text())
    if manifest['version'] != args.version or manifest['source_commit'] != args.source_commit:
        sys.exit('the package is not the named version and commit')

    out.mkdir(parents=True)
    for folder in ('presets', 'examples/first-use'):
        shutil.copytree(package / folder, out / 'package' / folder)
    found = cases(package)
    write_inputs(out, found)
    (out / 'expected').mkdir()
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        for case in found:
            case['checks'] = []
            for block in BLOCK_SIZES:
                if block and not case['name'].startswith('first-use-'):
                    continue  # The trivial preset filters only at the default.
                rendered = work / 'cli.wav'
                run(
                    [
                        cli,
                        '--filter',
                        out / case['filter'],
                        '--input',
                        out / case['input'],
                        '--output',
                        rendered,
                        '--block-size',
                        block,
                    ]
                )
                cli_audio = fixtures.read_float_wav(rendered)
                suffix = '' if block == 0 else f'.block{block}'
                expected = f'expected/{case["name"]}{suffix}.wav'
                fixtures.write_float_wav(out / expected, case['rate'], cli_audio)
                case['checks'].append({'renderer': 'cli', 'block_size': block, 'expected': expected})
                if block == 0:
                    vst3_audio, latency = render_vst3(args.render, bundle, out, case, len(cli_audio[0]), work)
                    if vst3_audio != cli_audio:
                        expected = f'expected/{case["name"]}.vst3.wav'
                        fixtures.write_float_wav(out / expected, case['rate'], vst3_audio)
                    case['checks'].append({'renderer': 'vst3', 'latency': latency, 'expected': expected})
    version_text = run([cli, '--version']).splitlines()
    record = {
        'schema': 1,
        'version': args.version,
        'tag': f'v{args.version}',
        'source_commit': args.source_commit,
        'candidate_run': f'https://github.com/jrp2014/convolver/actions/runs/{args.run_id}',
        'packages': {z.name: sha256(z) for z in args.zip},
        'captured': {
            'date': dt.datetime.now(tz=dt.timezone.utc).date().isoformat(),
            'by': 'tests/upgrade/capture.py',
            'from': next((z.name for z in args.zip if 'linux' in z.name), 'unknown'),
            'machine': machine(),
            'cli_version': version_text,
            'build': {k: manifest['build'][k] for k in ('compiler', 'configuration', 'flags', 'backend')},
            'vst3_host_blocks': '1, 73, 137, 3, 129, 512, 64, 137, repeated (tests/upgrade/vst3_render.cpp)',
            'preset_inputs': f'{INPUT_FRAMES} frames of tests/plugins/fixtures.py noise, seed 17 + channel',
        },
        'files': {
            str(p.relative_to(out)): sha256(p)
            for p in sorted(out.rglob('*'))
            if p.is_file() and p.name != 'manifest.json'
        },
        'cases': found,
    }
    (out / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n')
    print(f'captured {len(found)} cases into {out}')


if __name__ == '__main__':
    main()
