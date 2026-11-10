# SPDX-License-Identifier: GPL-2.0-or-later
"""Check a staged distribution after relocation; optionally run SDK tools."""

import argparse
import hashlib
import json
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import trademarks  # noqa: E402

COMPONENT = 'Components/Convolver.component'


def vstpreset_parts(path):
    """A .vstpreset's chunks by ID ('Comp' holds the audio state; 'Cont', the
    filter file's name and identity, PLUG-31)."""
    data = path.read_bytes()
    (list_offset,) = struct.unpack_from('<q', data, 40)
    assert data[list_offset : list_offset + 4] == b'List', f'{path.name}: no chunk list'
    (count,) = struct.unpack_from('<i', data, list_offset + 4)
    parts = {}
    for i in range(count):
        chunk, offset, size = struct.unpack_from('<4sqq', data, list_offset + 8 + 20 * i)
        parts[chunk] = data[offset : offset + size]
    return parts


def staged_tool(root, name):
    tool = root / 'bin' / name
    if not tool.exists():
        tool = tool.with_suffix('.exe')
    assert tool.is_file(), f'missing staged tool: bin/{name}'
    return tool


def check_trademarks(root):
    """PLUG-5: the VST trademark wording and the unmodified VST Compatible
    logo, from packaging/trademarks/vst.json, in the plug-in's credits and in
    every packaged guide that mentions VST, with each logo link resolving
    inside the package."""
    wording = trademarks.load(ROOT)
    logo = root / wording['logo']['file']
    assert logo.is_file(), f'missing packaged logo: {wording["logo"]["file"]}'
    assert hashlib.sha256(logo.read_bytes()).hexdigest() == wording['logo']['sha256'], 'packaged logo differs'
    notice = (root / 'VST3/Convolver.vst3/Contents/Resources/NOTICE.txt').read_text(encoding='utf-8')
    assert wording['support'] in notice and wording['notice'] in notice, 'plug-in credits lack the VST wording'
    marked = 0
    for page in sorted((root / 'docs/use').glob('*.md')):
        text = page.read_text(encoding='utf-8')
        first = trademarks.first_mention_marked(trademarks.prose(text))
        if first is None:
            continue
        name = page.relative_to(root).as_posix()
        assert first, f'{name}: first mention of VST without ®'
        assert trademarks.block(wording, name) in text, f'{name}: no VST trademark block'
        for source in re.findall(r'<img\s[^>]*?src="([^"]+)"', text):
            assert (page.parent / source).is_file(), f'{name}: image {source} is not in the package'
        marked += 1
    assert marked >= 2, 'expected the VST notice in several packaged guides'


def check_audio_unit(root):
    """AU-5: macOS packages hold the signed component, as au.bundle checks
    a build's (identities, version, one exported symbol, macOS libraries
    only, licence notices, signature); other packages hold none."""
    if platform.system() != 'Darwin':
        assert not (root / 'Components').exists(), 'Audio Unit outside a macOS package'
        return
    component = root / COMPONENT
    assert component.is_dir(), f'missing staged distribution content: {COMPONENT}'
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(component)], check=True)
    version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
    subprocess.run(
        [
            sys.executable,
            str(ROOT / 'tests/au/bundle_tests.py'),
            str(ROOT / 'plugins/identities.json'),
            str(component),
            version,
            'system-only',
        ],
        check=True,
    )


def check_lv2(root):
    """LV2-5: a package that holds LV2/ (Linux) holds the bundle, whose
    library loads and describes the plug-ins its manifest lists
    (lv2_bundle.py, in its own process, so nothing here keeps it loaded).
    Which packages must hold it is the release policy's "formats", checked by
    verify_artifact.py. A sanitizer build's library cannot be loaded into
    this uninstrumented Python; no CI build packages one."""
    folder = root / 'LV2'
    if not folder.exists():
        return
    bundles = sorted(folder.iterdir())
    assert [p.name for p in bundles] == ['convolver.lv2'], f'LV2/ holds {[p.name for p in bundles]}'
    subprocess.run([sys.executable, str(ROOT / 'tests/release/lv2_bundle.py'), str(bundles[0])], check=True)


def check(root, validator=None, probe=None):
    root = Path(root).resolve()
    for name in (
        'VST3/Convolver.vst3',
        'presets/examples.json',
        'examples/first-use/stereo/first-use-stereo.vstpreset',
        'docs/use/README.md',
        'docs/use/first-sound-in-a-host.md',
        'docs/use/presets.md',
        'docs/use/equalizer-apo.md',
        'docs/use/images/VST_Compatible_Logo_Steinberg.png',
        'VST3/Convolver.vst3/Contents/Resources/NOTICE.txt',
        'licenses/Convolver.txt',
        'VERSION.txt',
    ):
        assert (root / name).exists(), f'missing staged distribution content: {name}'
    if platform.system() == 'Darwin':
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(root / 'VST3/Convolver.vst3')], check=True)
    check_audio_unit(root)
    check_lv2(root)
    check_trademarks(root)
    cli, tool, exporter = (
        staged_tool(root, name) for name in ('convolver', 'convolver-preset', 'convolver-equalizer-apo')
    )
    assert not list((root / 'bin').glob('convolver-vst3-preset*')), 'the old converter name is for source builds only'
    from documentation_examples import check as check_documentation

    check_documentation(root)
    examples = json.loads((root / 'presets/examples.json').read_text())
    assert len(examples) == 18, 'expected mono/stereo x three rates x identity/echo/repeat'
    with tempfile.TemporaryDirectory(prefix='convolver-relocation-') as temporary:
        relocated = Path(temporary) / 'Convolver ü with spaces'
        shutil.copytree(root, relocated)
        cli = relocated / 'bin' / cli.name
        tool = relocated / 'bin' / tool.name
        exporter = relocated / 'bin' / exporter.name
        # The first-use examples (REL-2): the CLI renders each to its
        # expected.wav, as --compare confirms, and each shipped preset is the
        # converter's output for its filter.
        for name, source in (('mono', 'impulse.wav'), ('stereo', 'filter.cfg'), ('crossover', 'filter.cfg')):
            folder = relocated / 'examples/first-use' / name
            assert not (folder / 'README.md').exists() and not (folder.parent / 'README.md').exists()
            render = folder / 'render ü.wav'
            subprocess.run(
                [
                    str(cli),
                    '--filter',
                    str(folder / source),
                    '--input',
                    str(folder / 'input.wav'),
                    '--output',
                    str(render),
                ],
                check=True,
                capture_output=True,
            )
            compared = subprocess.run(
                [str(cli), '--compare', str(render), str(folder / 'expected.wav')],
                capture_output=True,
                text=True,
                check=False,
            )
            assert compared.returncode == 0 and 'Match' in compared.stdout, compared.stdout + compared.stderr
            converted = folder / 'converted ü.vstpreset'
            subprocess.run([str(tool), str(folder / source), str(converted)], check=True)
            shipped = folder / f'first-use-{name}.vstpreset'
            assert converted.read_bytes() == shipped.read_bytes(), f'{name} preset differs'
            assert not (folder / 'filter.vstpreset').exists(), f'{name} preset still has the old name'
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
            # The same filter from either file; each preset names its own file.
            from_wav, from_config = vstpreset_parts(output), vstpreset_parts(config_output)
            assert from_wav[b'Comp'] == from_config[b'Comp'], 'WAV and config conversion disagree'
            assert b'impulse.wav' in from_wav[b'Cont'] and b'filter.cfg' in from_config[b'Cont'], (
                'a converted preset does not name its file'
            )
            shipped = folder / f'{example["name"]}.vstpreset'
            assert original == shipped.read_bytes(), 'shipped example differs from converter'
            assert not (folder / 'filter.vstpreset').exists(), 'example preset still has the old name'
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
        # DIAG-2: the problem names the file and line, and a Fix line follows.
        assert result.returncode and b'malformed.cfg:1' in result.stderr, result.stderr
        assert b'\n  Fix: ' in result.stderr, result.stderr
        assert not (relocated / 'bad.vstpreset').exists(), 'malformed input left an output'
        result = subprocess.run(
            [str(exporter), str(bad), str(relocated / 'bad export')], capture_output=True, check=False
        )
        assert result.returncode and not (relocated / 'bad export').exists(), 'malformed input left an export'
        if platform.system() == 'Darwin':
            subprocess.run(
                ['codesign', '--verify', '--deep', '--strict', str(relocated / 'VST3/Convolver.vst3')], check=True
            )
            subprocess.run(['codesign', '--verify', '--deep', '--strict', str(relocated / COMPONENT)], check=True)
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
