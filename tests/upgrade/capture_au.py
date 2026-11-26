# SPDX-License-Identifier: GPL-2.0-or-later
"""Capture the Audio Unit state COMPAT-1 keeps from one published release.

Run once per published preview, on an Apple silicon Mac, after capture.py has
made the release's folder, with the release's own macOS arm64 package
unpacked and its Convolver.component installed for the user
(~/Library/Audio/Plug-Ins/Components). For each case capture.py recorded,
and for the settings cases capture_lv2.py added, the release's
convolver-preset makes an .aupreset from the case's filter; the release's
unit, hosted by AVAudioEngine (avaudioengine_render.swift, the Audio Unit
tests' host), restores it through ClassInfo, takes the case's settings
through its parameters, and gives its ClassInfo as a host saves a project; a
second instance of the release's unit restores that ClassInfo and renders
the case's input.

    python3 tests/upgrade/capture_au.py --package DIR --renderer RENDERER \
        --version 0.14.0 --zip Convolver-0.14.0-preview-macos-arm64.zip

DIR is the unpacked `Convolver-X.Y.Z-macos-arm64.zip` (`-preview-` after the
version in earlier previews), given with
--zip so its SHA-256 is checked against the folder's record; RENDERER is
avaudioengine_render built from any checkout (`xcrun swiftc -O`): it is only
a host. Each ClassInfo is kept as an XML property list written by Python's
plistlib (`au-state/NAME.plist`), so the file is the same whichever macOS
wrote it. The release's render is not kept: it must match the audio the
folder already holds (the CLI's, or capture_lv2.py's for the settings
cases) within upgrade.cli's bound, and the largest difference is recorded.
The script writes `tests/upgrade/X.Y.Z/au-state/` and `au.json`, with each
file's SHA-256, and refuses to write if either exists: a retained file is
never regenerated (tests/upgrade/README.md).
"""

import argparse
import datetime as dt
import json
import platform
import plistlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPOSITORY = HERE.parents[1]
sys.path.insert(0, str(HERE.parent / 'plugins'))
import fixtures  # noqa: E402  (tests/plugins/fixtures.py: WAV helpers)
from capture import run, sha256  # noqa: E402
from run_upgrade import bound  # noqa: E402

REGISTRY = json.loads((REPOSITORY / 'plugins' / 'identities.json').read_text(encoding='utf-8'))['au']
CODES = ','.join(REGISTRY[key] for key in ('type', 'subtype', 'manufacturer'))
# Frames per render call: not a multiple of the engine's 64-frame first block.
BLOCK = 509


def parameters(settings):
    """The unit's parameters for a case's settings (Latency goes by --latency)."""
    return [
        f'{REGISTRY["gain_parameter_id"]}={settings["gain"]:g}',
        f'{REGISTRY["mix_parameter_id"]}={settings["mix"]:g}',
        f'{REGISTRY["gain_mode_parameter_id"]}={settings["gain_mode"]:g}',
    ]


def render(renderer, preset, case, settings, expected, work, save=None):
    """The unit's output for the case, less the latency it reports, and that latency."""
    planar = fixtures.read_float_wav(case['input_path'])
    interleaved = [planar[c][i] for i in range(len(planar[0])) for c in range(len(planar))]
    raw_in, raw_out = work / 'in.f32', work / 'out.f32'
    raw_in.write_bytes(struct.pack(f'<{len(interleaved)}f', *interleaved))
    latency = int(settings['latency_frames'])
    frames = latency + len(expected[0])
    command = [renderer, CODES, preset, raw_in, raw_out, case['rate'], case['inputs'], case['outputs']]
    command += [BLOCK, frames]
    if save:
        command += ['--round-trip', '--save', save, '--latency', latency]
        for setting in parameters(settings):
            command += ['--parameter', setting]
    printed = run(command)
    reported = [int(line.split()[1]) for line in printed.splitlines() if line.startswith('latency ')]
    data = raw_out.read_bytes()
    samples = struct.unpack(f'<{len(data) // 4}f', data)
    outputs = case['outputs']
    rendered = [list(samples[c::outputs]) for c in range(outputs)]
    early = max((abs(v) for channel in rendered for v in channel[:latency]), default=0.0)
    return [channel[latency:] for channel in rendered], reported, early


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--zip', type=Path, required=True)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    package = args.package.resolve()
    out = (args.out or HERE / args.version).resolve()
    record = json.loads((out / 'manifest.json').read_text(encoding='utf-8'))
    lv2_record = json.loads((out / 'lv2.json').read_text(encoding='utf-8'))
    if (out / 'au-state').exists() or (out / 'au.json').exists():
        sys.exit(f'{out} already holds Audio Unit state: retained files are never regenerated')
    manifest = json.loads((package / 'artifact-manifest.json').read_text(encoding='utf-8'))
    if manifest['version'] != args.version or manifest['source_commit'] != record['source_commit']:
        sys.exit('the package is not the version and commit the folder was captured from')
    mac_zip = args.zip.name
    if record['packages'].get(mac_zip) != sha256(args.zip) or manifest['platform'] != 'macos-arm64':
        sys.exit(f'{args.zip} is not the release macOS arm64 package the folder records')
    preset_tool = package / 'bin' / 'convolver-preset'

    # The settings come from lv2.json, so the two formats keep the same cases.
    by_name = {c['name']: c for c in record['cases']}
    cases = []
    for lv2_case in lv2_record['cases']:
        name = lv2_case['name']
        # Not str.removesuffix, which needs Python 3.9; the minimum is 3.8.
        base = by_name[name[: -len('-settings')] if name.endswith('-settings') else name]
        cases.append((base, lv2_case['name'], lv2_case['settings'], lv2_case['expected']))

    found = []
    (out / 'au-state').mkdir()
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        for case, name, settings, expected_name in cases:
            preset = work / f'{name}.aupreset'
            run([preset_tool, out / case['filter'], preset])
            expected = fixtures.read_float_wav(out / expected_name)
            saved = work / f'{name}.plist'
            audio, reported, early = render(
                args.renderer, preset, {**case, 'input_path': out / case['input']}, settings, expected, work, saved
            )
            latency = int(settings['latency_frames'])
            if reported != [latency] or early != 0.0:
                sys.exit(f'{name}: the release reported latency {reported} with {early} before it; expected {latency}')
            if [len(c) for c in audio] != [len(c) for c in expected]:
                sys.exit(
                    f'{name}: the release rendered {len(audio)} channels of {len(audio[0])} frames; '
                    f'{expected_name} has {len(expected)} of {len(expected[0])}'
                )
            worst = max(abs(a - e) for ca, ce in zip(audio, expected) for a, e in zip(ca, ce))
            limit = bound(expected)
            if worst > limit:
                sys.exit(f"{name}: the release's unit differs from {expected_name} by {worst:.3g}, above {limit:.3g}")
            state = f'au-state/{name}.plist'
            classinfo = plistlib.loads(saved.read_bytes())
            (out / state).write_bytes(plistlib.dumps(classinfo, fmt=plistlib.FMT_XML, sort_keys=True))
            print(f'{name}: latency {latency}, largest difference {worst:.3g} ({worst / limit:.1%} of the bound)')
            found.append(
                {
                    'name': name,
                    'state': state,
                    'rate': case['rate'],
                    'inputs': case['inputs'],
                    'outputs': case['outputs'],
                    'input': case['input'],
                    'settings': settings,
                    'latency': latency,
                    'expected': expected_name,
                    'release_largest_difference': float(f'{worst:.3g}'),
                }
            )
    new_files = sorted(p for p in (out / 'au-state').rglob('*') if p.is_file())
    swift = subprocess.run(['xcrun', 'swiftc', '--version'], capture_output=True, text=True, check=False)
    au_record = {
        'schema': 1,
        'version': args.version,
        'package': {mac_zip: record['packages'][mac_zip]},
        'captured': {
            'date': dt.datetime.now(tz=dt.timezone.utc).date().isoformat(),
            'by': 'tests/upgrade/capture_au.py',
            'from': mac_zip,
            'machine': f'macOS {platform.mac_ver()[0]}, {platform.machine()}',
            'host': 'AVAudioEngine offline manual rendering (avaudioengine_render.swift), '
            + (swift.stdout.strip().splitlines() or ['swiftc version unknown'])[0],
            'build': manifest['build'],
            'block_size': BLOCK,
            'how': "the release's convolver-preset made an .aupreset from the filter; the release's unit restored "
            'it through ClassInfo, took the settings through its parameters (Latency as its menu index) and gave '
            'its ClassInfo, written here by plistlib; a second instance restored that ClassInfo and rendered, '
            'within the bound of the expected audio',
        },
        'files': {str(p.relative_to(out)): sha256(p) for p in new_files},
        'cases': found,
    }
    (out / 'au.json').write_text(json.dumps(au_record, indent=2) + '\n')
    print(f'captured {len(found)} Audio Unit states into {out / "au-state"}')


if __name__ == '__main__':
    main()
