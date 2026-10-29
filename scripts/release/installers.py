# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate exact installer ownership lists from a private stage."""

import argparse
import json
from pathlib import Path, PurePosixPath
import shlex
import shutil
from contract import load_policy


def destinations(files, system, prefix=Path('/')):
    result = {}
    for name in files:
        path = PurePosixPath(name)
        if (
            path.is_absolute()
            or '..' in path.parts
            or any(c in name for c in '\\$"\r\n:')
            or not path.parts
            or path.parts[0]
            not in ('VST3', 'bin', 'docs', 'presets', 'licenses', 'VERSION.txt', 'artifact-manifest.json')
        ):
            raise ValueError('path outside installer ownership: ' + name)
        if path.parts[0] == 'VST3' and (len(path.parts) < 3 or path.parts[1] != 'Convolver.vst3'):
            raise ValueError('unowned plug-in: ' + name)
        if system == 'Windows':
            base = '$COMMONFILES64' if path.parts[0] == 'VST3' else '$INSTDIR'
            target = base + '\\' + name.replace('/', '\\')
        elif system == 'Darwin':
            base = (
                prefix / 'Library/Audio/Plug-Ins'
                if path.parts[0] == 'VST3'
                else prefix / 'Library/Application Support/Convolver'
            )
            target = str(base / name)
        else:
            raise ValueError('installer platform must be Windows or Darwin')
        result[name] = target
    return result


def write_uninstaller(mapping, output):
    lines = [
        '#!/bin/sh',
        '# SPDX-License-Identifier: GPL-2.0-or-later',
        'set -eu',
        '# Remove only shipped files. Empty directories only; custom files survive.',
    ]
    lines += ['rm -f -- ' + shlex.quote(path) for path in mapping.values()]
    # Do not remove shared system parent directories.
    directories = set()
    for path in mapping.values():
        parent = Path(path).parent
        while parent.name and (
            parent.name == 'Convolver' or 'Convolver' in parent.parts or 'Convolver.vst3' in parent.parts
        ):
            directories.add(str(parent))
            parent = parent.parent
    lines += [
        'rmdir -- ' + shlex.quote(path) + ' 2>/dev/null || :'
        for path in sorted(directories, key=lambda p: (-len(Path(p).parts), p))
    ]
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    output.chmod(0o755)


def prepare(stage, output, system, policy):
    if output.exists():
        raise ValueError('installer output directory must be new')
    files = sorted(p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file())
    if any(p.is_symlink() for p in stage.rglob('*')):
        raise ValueError('installer stage must not contain symlinks')
    mapping = destinations(files, system)
    output.mkdir(parents=True)
    (output / 'owned-files.json').write_text(json.dumps(mapping, indent=2) + '\n')
    if system == 'Windows':
        install, uninstall, directories = [], [], set()
        for name, target in mapping.items():
            parent, basename = target.rsplit('\\', 1)
            source = str((stage / name).resolve()).replace('$', '$$').replace('"', '$\\"')
            install += [f'SetOutPath "{parent}"', f'File "/oname={basename}" "{source}"']
            uninstall += [
                'ClearErrors',
                f'Delete "{target}"',
                '${If} ${Errors}',
                'SetErrorLevel 1',
                'Abort "Close plug-in hosts and retry uninstall."',
                '${EndIf}',
            ]
            while parent not in ('$INSTDIR', '$COMMONFILES64\\VST3', '$COMMONFILES64'):
                directories.add(parent)
                parent = parent.rsplit('\\', 1)[0]
        uninstall += [f'RMDir "{p}"' for p in sorted(directories, key=lambda p: (-p.count('\\'), p))]
        (output / 'install-files.nsh').write_text('\n'.join(install) + '\n', encoding='utf-8')
        (output / 'uninstall-files.nsh').write_text('\n'.join(uninstall) + '\n', encoding='utf-8')
        (output / 'version.nsh').write_text(f'!define CONVOLVER_VERSION "{policy["version"]}"\n')
    else:
        for name in files:
            if name.startswith('VST3/'):
                target = output / 'plugin-payload' / name[len('VST3/') :]
            else:
                target = output / 'tools-payload' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(stage / name, target)
        write_uninstaller(mapping, output / 'uninstall.sh')
        shutil.copy2(output / 'uninstall.sh', output / 'tools-payload/uninstall.sh')
        # Prevent macOS Installer relocating the bundle to an old test build.
        import plistlib

        with (output / 'component.plist').open('wb') as stream:
            plistlib.dump(
                [
                    dict(
                        BundleHasStrictIdentifier=True,
                        BundleIsRelocatable=False,
                        BundleIsVersionChecked=True,
                        BundleOverwriteAction='upgrade',
                        RootRelativeBundlePath='Convolver.vst3',
                    )
                ],
                stream,
            )
    return mapping


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--system', choices=['Windows', 'Darwin'], required=True)
    parser.add_argument(
        '--policy', type=Path, default=Path(__file__).resolve().parents[2] / 'packaging/release-policy.json'
    )
    args = parser.parse_args()
    prepare(args.stage.resolve(), args.output.resolve(), args.system, load_policy(args.policy))
