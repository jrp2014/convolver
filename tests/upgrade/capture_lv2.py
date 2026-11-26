# SPDX-License-Identifier: GPL-2.0-or-later
"""Capture the LV2 state COMPAT-1 keeps from one published release.

Run once per published preview that ships the LV2 plug-in, on Linux x86-64,
after capture.py has made the release's folder, with the release's own Linux
package unpacked. For each case capture.py recorded, and for a few with
settings away from their defaults, the release's LV2 plug-in chooses the
case's filter as a host's file chooser does and saves its state with lilv,
as Jalv and other lilv hosts save a session; the release's plug-in then
restores that state in a new instance and renders the case's input.

    python3 tests/upgrade/capture_lv2.py --package DIR \
        --render build/.../convolver_lv2_render --version 0.14.0 \
        --zip Convolver-0.14.0-preview-linux-x86_64.zip

DIR is the unpacked `Convolver-X.Y.Z-preview-linux-x86_64.zip`, given with
--zip so its SHA-256 is checked against the folder's record;
convolver_lv2_render comes from any LV2 build with tests (it is only a host:
the release's bundle does the saving and the rendering). The script writes
`tests/upgrade/X.Y.Z/lv2-state/` and `lv2.json`, with each file's SHA-256, and
refuses to write if either exists: a retained file is never regenerated
(tests/upgrade/README.md).
"""

import argparse
import datetime as dt
import json
import shutil
import struct
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'plugins'))
import fixtures  # noqa: E402  (tests/plugins/fixtures.py: WAV helpers)
from capture import machine, run, sha256  # noqa: E402

DEFAULTS = {'gain': 0.0, 'mix': 100.0, 'latency_frames': 0.0, 'gain_mode': 0.0}
# Sessions saved with the settings away from their defaults, so that a
# renamed port, a changed range or scale, or a setting no longer restored
# shows. Gain mode 1 is Estimated.
SETTINGS = {
    'first-use-stereo': {'gain': -6.0, 'mix': 50.0, 'latency_frames': 256.0, 'gain_mode': 0.0},
    'first-use-crossover': {'gain': 0.0, 'mix': 100.0, 'latency_frames': 1024.0, 'gain_mode': 1.0},
}
HOST_BLOCKS = '1, 73, 137, 3, 129, 512, 64, 137, repeated (tests/upgrade/lv2_render.cpp)'


def filter_files(folder, filter_name):
    """The filter and the audio files beside it, without the case's input and expected output."""
    names = {filter_name}
    names |= {p.name for p in folder.glob('*.wav') if p.name not in ('input.wav', 'expected.wav')}
    return sorted(names)


def render(tool, bundle, state, case, frames, work):
    planar = fixtures.read_float_wav(case['input_path'])
    interleaved = [planar[c][i] for i in range(len(planar[0])) for c in range(len(planar))]
    raw_in, raw_out = work / 'in.f32', work / 'out.f32'
    raw_in.write_bytes(struct.pack(f'={len(interleaved)}f', *interleaved))
    printed = run(
        [tool, 'render', bundle, state, case['rate'], case['inputs'], case['outputs'], raw_in, raw_out, frames]
    )
    latency = int(printed.split()[1])
    data = raw_out.read_bytes()
    samples = struct.unpack(f'={len(data) // 4}f', data)
    return [list(samples[c :: case['outputs']]) for c in range(case['outputs'])], latency


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--render', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--zip', type=Path, required=True)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    package = args.package.resolve()
    out = (args.out or HERE / args.version).resolve()
    record = json.loads((out / 'manifest.json').read_text(encoding='utf-8'))
    if (out / 'lv2-state').exists() or (out / 'lv2.json').exists():
        sys.exit(f'{out} already holds LV2 state: retained files are never regenerated')
    manifest = json.loads((package / 'artifact-manifest.json').read_text(encoding='utf-8'))
    if manifest['version'] != args.version or manifest['source_commit'] != record['source_commit']:
        sys.exit('the package is not the version and commit the folder was captured from')
    bundle = package / 'LV2/convolver.lv2'
    linux_zip = record['captured']['from']
    if args.zip.name != linux_zip or sha256(args.zip) != record['packages'][linux_zip]:
        sys.exit(f'{args.zip} is not the {linux_zip} the folder was captured from')

    cases = []
    for case in record['cases']:
        cases.append((case, f'{case["name"]}', DEFAULTS))
        if case['name'] in SETTINGS:
            cases.append((case, f'{case["name"]}-settings', SETTINGS[case['name']]))
    found = []
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        for case, name, settings in cases:
            source = out / case['filter']
            state = out / 'lv2-state' / name
            state.mkdir(parents=True)
            for file in filter_files(source.parent, source.name):
                shutil.copyfile(source.parent / file, state / file)
            run(
                [
                    args.render,
                    'capture',
                    bundle,
                    state,
                    state / source.name,
                    case['rate'],
                    case['inputs'],
                    case['outputs'],
                    settings['gain'],
                    settings['mix'],
                    settings['latency_frames'],
                    settings['gain_mode'],
                ]
            )
            reference = next(c for c in case['checks'] if c['renderer'] == 'cli' and c['block_size'] == 0)
            cli_audio = fixtures.read_float_wav(out / reference['expected'])
            audio, latency = render(
                args.render, bundle, state, {**case, 'input_path': out / case['input']}, len(cli_audio[0]), work
            )
            expected = reference['expected']
            if audio != cli_audio:
                expected = f'expected/{name}.lv2.wav'
                fixtures.write_float_wav(out / expected, case['rate'], audio)
            found.append(
                {
                    'name': name,
                    'state': f'lv2-state/{name}',
                    'rate': case['rate'],
                    'inputs': case['inputs'],
                    'outputs': case['outputs'],
                    'input': case['input'],
                    'settings': settings,
                    'latency': latency,
                    'expected': expected,
                }
            )
    new_files = [p for p in sorted((out / 'lv2-state').rglob('*')) if p.is_file()]
    new_files += [out / c['expected'] for c in found if c['expected'].endswith('.lv2.wav')]
    lv2_record = {
        'schema': 1,
        'version': args.version,
        'package': {linux_zip: record['packages'][linux_zip]},
        'captured': {
            'date': dt.datetime.now(tz=dt.timezone.utc).date().isoformat(),
            'by': 'tests/upgrade/capture_lv2.py',
            'from': linux_zip,
            'machine': machine(),
            'lilv': run(['pkg-config', '--modversion', 'lilv-0']).strip(),
            # The kernels this machine's CPU gives the release, which an
            # identical-samples check of the .lv2.wav files must match.
            'cli_version': run([package / 'bin' / 'convolver', '--version']).splitlines(),
            'build': record['captured']['build'],
            'host_blocks': HOST_BLOCKS,
            'how': 'the filter chosen through the filter parameter, then lilv_state_new_from_instance and '
            'lilv_state_save with the state folder as the save folder; rendered from lilv_state_new_from_file '
            'and lilv_state_restore, ports included, in a new instance of the release plug-in',
        },
        'files': {str(p.relative_to(out)): sha256(p) for p in sorted(set(new_files))},
        'cases': found,
    }
    (out / 'lv2.json').write_text(json.dumps(lv2_record, indent=2) + '\n')
    print(f'captured {len(found)} LV2 states into {out / "lv2-state"}')


if __name__ == '__main__':
    main()
