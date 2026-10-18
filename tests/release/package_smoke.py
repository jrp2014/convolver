# SPDX-License-Identifier: GPL-2.0-or-later
"""Check a staged distribution after relocation; optionally run SDK tools."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def check(root, validator=None, probe=None):
    root = Path(root).resolve()
    for name in ('VST3/Convolver.vst3', 'presets/examples.json', 'docs/plugins/quickstart.md', 'licenses/Convolver.txt', 'VERSION.txt'):
        assert (root / name).exists(), f'missing staged distribution content: {name}'
    tool = root / 'bin/convolver-vst3-preset'
    if not tool.exists():
        tool = tool.with_suffix('.exe')
    assert tool.is_file(), 'missing staged preset converter'
    examples = json.loads((root / 'presets/examples.json').read_text())
    assert len(examples) == 12, 'expected mono/stereo x three rates x identity/echo'
    with tempfile.TemporaryDirectory(prefix='convolver-relocation-') as temporary:
        relocated = Path(temporary) / 'Convolver ü with spaces'
        shutil.copytree(root, relocated)
        tool = relocated / 'bin' / tool.name
        for example in examples:
            folder = relocated / 'presets' / example['name']
            output = folder / 'converted ü.vstpreset'
            subprocess.run([str(tool), str(folder / 'impulse.wav'), str(output)], check=True)
            original = output.read_bytes()
            result = subprocess.run([str(tool), str(folder / 'impulse.wav'), str(output)], capture_output=True)
            assert result.returncode and output.read_bytes() == original, 'converter overwrote existing preset'
            config_output = folder / 'from config.vstpreset'
            subprocess.run([str(tool), str(folder / 'filter.cfg'), str(config_output)], check=True)
            assert original == config_output.read_bytes(), 'WAV and config conversion disagree'
            assert original == (folder / 'filter.vstpreset').read_bytes(), 'shipped example differs from converter'
            for source in folder.glob('*.wav'):
                source.unlink()
            (folder / 'filter.cfg').unlink()
            if probe:
                subprocess.run([str(probe), str(output), str(example['rate']), str(example['channels']), example['kind']], check=True)
        bad = relocated / 'malformed.cfg'
        bad.write_text('not a configuration\n')
        result = subprocess.run([str(tool), str(bad), str(relocated / 'bad.vstpreset')], capture_output=True)
        assert result.returncode and b'invalid configuration' in result.stderr, result.stderr
        assert not (relocated / 'bad.vstpreset').exists(), 'malformed input left an output'
        if validator:
            subprocess.run([str(validator), str(relocated / 'VST3/Convolver.vst3')], check=True)
    print('Relocated converter, embedded examples and requested SDK checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--validator', type=Path)
    parser.add_argument('--probe', type=Path)
    args = parser.parse_args()
    check(args.root, args.validator, args.probe)
