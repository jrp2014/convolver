#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Install a macOS candidate's .pkg on a disposable CI runner and check the result.

PLUG-5 and AU-5: the installer puts exactly the ZIP's files in their places
(the VST3 plug-in in /Library/Audio/Plug-Ins/VST3, the Audio Unit in
/Library/Audio/Plug-Ins/Components, the rest in
/Library/Application Support/Convolver), with every byte as in the ZIP; the
installed bundles' ad-hoc signatures hold; the installed component passes
au.bundle's checks and `auval -strict`, which lists and loads its window; the
installed CLI runs; and the shipped uninstall.sh removes every installed file.

This is an automated package check, not clean-install qualification: the
runner is not a fresh Mac, nothing is downloaded with quarantine, and no
named host loads the plug-ins (docs/develop/releasing.md).
"""

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
from installers import destinations  # noqa: E402

COMPONENTS = Path('/Library/Audio/Plug-Ins/Components')
VST3 = Path('/Library/Audio/Plug-Ins/VST3')
SUPPORT = Path('/Library/Application Support/Convolver')


def run(command, **kwargs):
    print('+', ' '.join(str(part) for part in command), flush=True)
    return subprocess.run([str(part) for part in command], check=True, **kwargs)


def zip_files(archive):
    """{member name: SHA-256} of the ZIP's regular files."""
    with zipfile.ZipFile(archive) as package:
        return {
            info.filename: hashlib.sha256(package.read(info)).hexdigest()
            for info in package.infolist()
            if not info.is_dir()
        }


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def check(installer, archive):
    identities = json.loads((ROOT / 'plugins/identities.json').read_text(encoding='utf-8'))['macos_installer']
    receipts = set(run(['pkgutil', '--pkgs'], capture_output=True, text=True).stdout.split())
    # Never touch an existing installation: CI runners are disposable.
    for path in (COMPONENTS / 'Convolver.component', VST3 / 'Convolver.vst3', SUPPORT):
        if path.exists():
            raise RuntimeError(f'refusing to install over an existing {path}')
    if receipts & set(identities.values()):
        raise RuntimeError(f'refusing to install over existing receipts {sorted(receipts & set(identities.values()))}')
    expected = zip_files(archive)
    owned = destinations(sorted(expected), 'Darwin')
    run(['sudo', 'installer', '-pkg', installer, '-target', '/'])
    try:
        receipts = set(run(['pkgutil', '--pkgs'], capture_output=True, text=True).stdout.split())
        missing = set(identities.values()) - receipts
        assert not missing, f'installer left no receipt for {sorted(missing)}'
        for name, target in owned.items():
            assert Path(target).is_file(), f'{name} was not installed at {target}'
            assert digest(target) == expected[name], f'{target} differs from {name} in the ZIP'
        # Only what the ZIP holds, plus the uninstaller, in Convolver's own folders.
        installed = {
            str(path)
            for folder in (COMPONENTS / 'Convolver.component', VST3 / 'Convolver.vst3', SUPPORT)
            for path in folder.rglob('*')
            if path.is_file()
        }
        extra = installed - set(owned.values()) - {str(SUPPORT / 'uninstall.sh')}
        assert not extra, f'installed files that the ZIP does not hold: {sorted(extra)}'
        for bundle in (VST3 / 'Convolver.vst3', COMPONENTS / 'Convolver.component'):
            run(['codesign', '--verify', '--deep', '--strict', bundle])
        version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
        run(
            [
                sys.executable,
                ROOT / 'tests/au/bundle_tests.py',
                ROOT / 'plugins/identities.json',
                COMPONENTS / 'Convolver.component',
                version,
                'system-only',
            ]
        )
        run([SUPPORT / 'bin/convolver', '--version'], capture_output=True)
        validate_audio_unit()
    finally:
        run(['sudo', 'sh', SUPPORT / 'uninstall.sh'])
        for package in identities.values():
            subprocess.run(['sudo', 'pkgutil', '--forget', package], check=False, capture_output=True)
    left = [target for target in owned.values() if Path(target).exists()]
    assert not left, f'uninstall.sh left {left}'
    for folder in (COMPONENTS / 'Convolver.component', VST3 / 'Convolver.vst3'):
        assert not folder.exists(), f'uninstall.sh left {folder}'
    print('Installed paths, signatures, Audio Unit validation and uninstall passed')


def validate_audio_unit():
    """auval against the installed component, as the Audio Unit workflow does
    for a build: strict, with the window listed."""
    # Make the component registrar rescan, as after an installer.
    subprocess.run(['killall', '-9', 'AudioComponentRegistrar'], check=False, capture_output=True)
    with tempfile.TemporaryDirectory() as temporary:
        log = Path(temporary) / 'auval.txt'
        with log.open('w', encoding='utf-8') as stream:
            result = subprocess.run(
                ['auval', '-strict', '-v', 'aufx', 'Conv', 'CONV'], stdout=stream, stderr=subprocess.STDOUT, check=False
            )
        text = log.read_text(encoding='utf-8', errors='replace')
    print(text)
    assert result.returncode == 0 and 'AU VALIDATION SUCCEEDED' in text, 'auval failed on the installed component'
    assert 'Cocoa Views Available: 1' in text, "auval did not find the component's window"


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--installer', type=Path, required=True, help='the candidate .pkg')
    parser.add_argument('--archive', type=Path, required=True, help="the same candidate's ZIP")
    args = parser.parse_args()
    check(args.installer.resolve(), args.archive.resolve())
