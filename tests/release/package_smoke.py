# SPDX-License-Identifier: GPL-2.0-or-later
"""Check a staged distribution after relocation; optionally run SDK tools."""

import argparse
import json
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def staged_tool(root, name):
    tool = root / 'bin' / name
    if not tool.exists():
        tool = tool.with_suffix('.exe')
    assert tool.is_file(), f'missing staged tool: bin/{name}'
    return tool


def check(root, validator=None, probe=None):
    root = Path(root).resolve()
    for name in (
        'VST3/Convolver.vst3',
        'presets/examples.json',
        'docs/use/README.md',
        'docs/use/first-sound-in-a-host.md',
        'docs/use/presets.md',
        'docs/use/equalizer-apo.md',
        'licenses/Convolver.txt',
        'VERSION.txt',
    ):
        assert (root / name).exists(), f'missing staged distribution content: {name}'
    if platform.system() == 'Darwin':
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(root / 'VST3/Convolver.vst3')], check=True)
    tool, exporter = (staged_tool(root, name) for name in ('convolver-preset', 'convolver-equalizer-apo'))
    assert not list((root / 'bin').glob('convolver-vst3-preset*')), 'the old converter name is for source builds only'
    from documentation_examples import check as check_documentation

    check_documentation(root)
    examples = json.loads((root / 'presets/examples.json').read_text())
    assert len(examples) == 18, 'expected mono/stereo x three rates x identity/echo/repeat'
    with tempfile.TemporaryDirectory(prefix='convolver-relocation-') as temporary:
        relocated = Path(temporary) / 'Convolver ü with spaces'
        shutil.copytree(root, relocated)
        tool = relocated / 'bin' / tool.name
        exporter = relocated / 'bin' / exporter.name
        for example in examples:
            folder = relocated / 'presets' / example['name']
            output = folder / 'converted ü.vstpreset'
            subprocess.run([str(tool), str(folder / 'impulse.wav'), str(output)], check=True)
            original = output.read_bytes()
            result = subprocess.run(
                [str(tool), str(folder / 'impulse.wav'), str(output)], capture_output=True, check=False
            )
            assert result.returncode and output.read_bytes() == original, 'converter overwrote existing preset'
            config_output = folder / 'from config.vstpreset'
            subprocess.run([str(tool), str(folder / 'filter.cfg'), str(config_output)], check=True)
            assert original == config_output.read_bytes(), 'WAV and config conversion disagree'
            assert original == (folder / 'filter.vstpreset').read_bytes(), 'shipped example differs from converter'
            other_formats = [folder / 'other ü.aupreset', folder / 'other ü.preset.lv2']
            subprocess.run([str(tool), str(folder / 'filter.cfg'), *map(str, other_formats)], check=True)
            assert all(path.exists() for path in other_formats), 'AU/LV2 presets not written'
            export = folder / 'Equalizer APO ü'
            subprocess.run([str(exporter), str(folder / 'filter.cfg'), str(export)], check=True, capture_output=True)
            commands = (export / 'convolver.txt').read_text(encoding='utf-8')
            assert 'Convolution:' in commands and list(export.glob('ir-*.wav')), 'incomplete Equalizer APO export'
            result = subprocess.run(
                [str(exporter), str(folder / 'filter.cfg'), str(export)], capture_output=True, check=False
            )
            assert result.returncode and (export / 'convolver.txt').read_text(encoding='utf-8') == commands, (
                'exporter replaced an existing folder'
            )
            for source in folder.glob('*.wav'):
                source.unlink()
            (folder / 'filter.cfg').unlink()
            if probe:
                subprocess.run(
                    [str(probe), str(output), str(example['rate']), str(example['channels']), example['kind']],
                    check=True,
                )
        bad = relocated / 'malformed.cfg'
        bad.write_text('not a configuration\n')
        result = subprocess.run(
            [str(tool), str(bad), str(relocated / 'bad.vstpreset')], capture_output=True, check=False
        )
        assert result.returncode and b'invalid configuration' in result.stderr, result.stderr
        assert not (relocated / 'bad.vstpreset').exists(), 'malformed input left an output'
        result = subprocess.run(
            [str(exporter), str(bad), str(relocated / 'bad export')], capture_output=True, check=False
        )
        assert result.returncode and not (relocated / 'bad export').exists(), 'malformed input left an export'
        if platform.system() == 'Darwin':
            subprocess.run(
                ['codesign', '--verify', '--deep', '--strict', str(relocated / 'VST3/Convolver.vst3')], check=True
            )
        if validator:
            subprocess.run(
                [
                    sys.executable,
                    str(Path(__file__).resolve().parents[1] / 'vst3/validate_bundle.py'),
                    str(validator),
                    str(relocated / 'VST3/Convolver.vst3'),
                ],
                check=True,
            )
    print('Relocated tools, embedded examples and requested SDK checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--validator', type=Path)
    parser.add_argument('--probe', type=Path)
    args = parser.parse_args()
    check(args.root, args.validator, args.probe)
