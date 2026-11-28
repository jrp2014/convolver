#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""List the Convolver plug-ins installed on this computer, before a host test.

Read-only (HOST-14): it never installs, removes, moves or changes anything.
For each installed Convolver plug-in it prints the format, version, scope
(your account or all accounts), path and when it was installed, and whether
a running program has it loaded now. It then names what would spoil a host
test: a format installed more than once, formats at different versions, or
an installed version older than the newest Convolver download (or than
--expect VERSION).

"Installed" means the bundle is in a folder hosts scan. "Loaded now" means a
running process has the bundle's files open or mapped (lsof on macOS,
/proc on Linux): only that confirms which copy a host is using. A plug-in
that is installed but not loaded may still be the one the host loads next
time; the host's own plug-in list says which one it scanned.

Exit status: 0 when nothing needs attention, 1 when something does, 2 on a
usage error.
"""

import argparse
import datetime as dt
import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
IDENTITIES = REPOSITORY / 'plugins' / 'identities.json'
VERSION_NAME = re.compile(r'(?i)\bconvolver[-_ ](\d+)\.(\d+)\.(\d+)\b')
FORMAT_NAMES = {'.vst3': 'VST3', '.component': 'AU', '.lv2': 'LV2'}


def plugin_folders(platform, home, system_root, lv2_path=None):
    """(format suffix, folder, scope) for each folder that hosts scan."""
    system = Path(system_root)
    if platform == 'mac':
        return [
            ('.vst3', home / 'Library/Audio/Plug-Ins/VST3', 'user'),
            ('.vst3', system / 'Library/Audio/Plug-Ins/VST3', 'system'),
            ('.component', home / 'Library/Audio/Plug-Ins/Components', 'user'),
            ('.component', system / 'Library/Audio/Plug-Ins/Components', 'system'),
        ]
    folders = [
        ('.vst3', home / '.vst3', 'user'),
        ('.vst3', system / 'usr/lib/vst3', 'system'),
        ('.vst3', system / 'usr/local/lib/vst3', 'system'),
        ('.lv2', home / '.lv2', 'user'),
        ('.lv2', system / 'usr/lib/lv2', 'system'),
        ('.lv2', system / 'usr/local/lib/lv2', 'system'),
    ]
    for entry in (lv2_path or '').split(os.pathsep):
        if entry:
            folder = Path(entry).expanduser()
            scope = 'user' if folder.is_relative_to(home) else 'system'
            folders.append(('.lv2', folder, scope))
    unique = []
    for folder in folders:
        if all(Path(os.path.normpath(folder[1])) != Path(os.path.normpath(seen[1])) for seen in unique):
            unique.append(folder)
    return unique


def version_tuple(text):
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)', text or '')
    return tuple(int(part) for part in match.groups()) if match else None


def version_text(version):
    return '.'.join(map(str, version)) if version else 'unknown'


def read_plist(path):
    try:
        with path.open('rb') as handle:
            return plistlib.load(handle)
    except (OSError, plistlib.InvalidFileException, ValueError):
        return None


def read_text(path):
    try:
        return path.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return ''


def bundle_identity(bundle, suffix, identities):
    """(is Convolver, version text or None) for one bundle."""
    if suffix == '.lv2':
        text = '\n'.join(read_text(bundle / name) for name in ('manifest.ttl', 'convolver.ttl'))
        ours = identities['lv2']['base_uri'] in text or bundle.name.lower() == identities['lv2']['bundle']
        minor = re.search(r'lv2:minorVersion\s+(\d+)', text)
        micro = re.search(r'lv2:microVersion\s+(\d+)', text)
        if not (minor and micro):
            return ours, None
        # The LV2 Turtle generator (LV2-5) writes minor = 2 x (100 x MAJOR + MINOR), micro = 2 x PATCH.
        minor, micro = int(minor.group(1)), int(micro.group(1))
        return ours, f'{minor // 200}.{(minor // 2) % 100}.{micro // 2}'
    plist = read_plist(bundle / 'Contents' / 'Info.plist')
    key = 'vst3' if suffix == '.vst3' else 'au'
    if plist is not None:
        ours = plist.get('CFBundleIdentifier') == identities[key]['macos_bundle_id']
        return ours or 'convolver' in bundle.name.lower(), plist.get('CFBundleShortVersionString')
    # A Linux or Windows VST3 bundle has no Info.plist; the SDK's moduleinfo.json
    # (JSON5, so read only the fields needed) names the factory and version.
    info = read_text(bundle / 'Contents' / 'Resources' / 'moduleinfo.json')
    version = re.search(r'"Version"\s*:\s*"([^"]*)"', info)
    ours = identities['vst3']['processor_class_id'].upper() in info.upper() or 'convolver' in bundle.name.lower()
    return ours, version.group(1) if version else None


def installed_plugins(folders, identities):
    found = []
    for suffix, folder, scope in folders:
        try:
            entries = sorted(folder.iterdir())
        except OSError:
            continue
        for bundle in entries:
            if bundle.suffix.lower() != suffix or not bundle.is_dir():
                continue
            ours, version = bundle_identity(bundle, suffix, identities)
            if not ours:
                continue
            try:
                modified = dt.datetime.fromtimestamp(bundle.stat().st_mtime).astimezone()
            except OSError:
                modified = None
            found.append(
                {
                    'format': FORMAT_NAMES[suffix],
                    'scope': scope,
                    'path': bundle,
                    'version': version_tuple(version),
                    'version_text': version or 'unknown',
                    'modified': modified,
                }
            )
    return found


def parse_lsof(text):
    """{path: {(pid, command)}} from `lsof -F pcn` output."""
    opened = {}
    pid = command = None
    for line in text.splitlines():
        if not line:
            continue
        tag, value = line[0], line[1:]
        if tag == 'p':
            pid, command = value, None
        elif tag == 'c':
            command = value
        elif tag == 'n' and pid is not None:
            opened.setdefault(value, set()).add((pid, command or '?'))
    return opened


def open_files_linux(proc):
    opened = {}
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        try:
            command = (entry / 'comm').read_text(encoding='utf-8', errors='replace').strip()
            maps = (entry / 'maps').read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue  # Gone, or another user's process.
        for line in maps.splitlines():
            fields = line.split(None, 5)
            if len(fields) == 6 and fields[5].startswith('/'):
                opened.setdefault(fields[5], set()).add((entry.name, command))
    return opened


def open_files(platform, proc=Path('/proc')):
    """{path: {(pid, command)}} for the files running processes have open or
    mapped, and how that was found (None when it could not be)."""
    if platform == 'linux' and proc.is_dir():
        return open_files_linux(proc), '/proc'
    lsof = shutil.which('lsof') or ('/usr/sbin/lsof' if Path('/usr/sbin/lsof').exists() else None)
    if not lsof:
        return {}, None
    try:
        result = subprocess.run(
            [lsof, '-n', '-P', '-w', '-F', 'pcn'], capture_output=True, text=True, timeout=60, check=False
        )
    except (OSError, subprocess.TimeoutExpired):
        return {}, None
    # lsof exits 1 when it cannot read some processes; what it did read stands.
    return parse_lsof(result.stdout), 'lsof'


def loaded_by(bundle, opened):
    inside = tuple({str(bundle.resolve()).rstrip('/') + '/', str(bundle).rstrip('/') + '/'})
    users = set()
    for path, processes in opened.items():
        if path.startswith(inside):
            users |= processes
    return sorted(users, key=lambda user: (user[1].lower(), int(user[0]) if user[0].isdigit() else 0))


def newest_download(folder):
    """(version, name) of the newest Convolver package in folder, or None."""
    newest = None
    try:
        names = [entry.name for entry in folder.iterdir()]
    except OSError:
        return None
    for name in names:
        match = VERSION_NAME.search(name)
        if match:
            version = tuple(int(part) for part in match.groups())
            if newest is None or version > newest[0]:
                newest = (version, name)
    return newest


def problems_with(plugins, expected, download):
    problems = []
    if not plugins:
        problems.append('No Convolver plug-in is installed in the folders hosts scan; install the package first.')
    by_format = {}
    for plugin in plugins:
        by_format.setdefault(plugin['format'], []).append(plugin)
    for name, copies in sorted(by_format.items()):
        if len(copies) > 1:
            paths = ''.join(f'\n    {copy["version_text"]:>9}  {copy["scope"]:<6}  {copy["path"]}' for copy in copies)
            problems.append(
                f'{name} is installed {len(copies)} times; a host may load any of them:{paths}\n'
                '  Keep one, and remove the others yourself (this tool removes nothing).'
            )
    versions = {plugin['version_text'] for plugin in plugins}
    if len(versions) > 1:
        listed = ', '.join(f'{plugin["format"]} {plugin["version_text"]}' for plugin in plugins)
        problems.append(f'The installed plug-ins are not all the same version: {listed}.')
    problems += [
        f'{plugin["path"]}: its version cannot be read; reinstall it from a package.'
        for plugin in plugins
        if plugin['version'] is None
    ]
    readable = [plugin for plugin in plugins if plugin['version'] is not None]
    if expected:
        problems += [
            f'{plugin["format"]} at {plugin["path"]} is {plugin["version_text"]}, '
            f'not the expected {version_text(expected)}.'
            for plugin in readable
            if plugin['version'] != expected
        ]
    elif download:
        version, name = download
        problems += [
            f'{plugin["format"]} at {plugin["path"]} is {plugin["version_text"]}, older than the '
            f'download {name} ({version_text(version)}): install that package first.'
            for plugin in readable
            if plugin['version'] < version
        ]
    return problems


def report(plugins, opened, how, platform, expected, download, out):
    place = 'this Mac' if platform == 'mac' else 'this computer'
    print(f'Convolver plug-ins installed on {place} (read-only: nothing is changed)', file=out)
    print(file=out)
    if not plugins:
        print('  None found in the folders hosts scan.', file=out)
    for plugin in plugins:
        when = plugin['modified'].strftime('%Y-%m-%d %H:%M') if plugin['modified'] else 'unknown'
        print(
            f'  {plugin["format"]:<4}  {plugin["version_text"]:>9}  {plugin["scope"]:<6}  {plugin["path"]}',
            file=out,
        )
        print(f'        installed {when}', end='', file=out)
        users = loaded_by(plugin['path'], opened)
        if users:
            names = ', '.join(f'{command} (pid {pid})' for pid, command in users)
            print(f'; loaded now by {names}', file=out)
        elif how:
            print('; not loaded by any running program', file=out)
        else:
            print('; whether a program has it loaded is unknown (no lsof or /proc)', file=out)
    print(file=out)
    if expected:
        print(f'Expected version: {version_text(expected)} (--expect).', file=out)
    elif download:
        print(f'Newest Convolver download: {download[1]} ({version_text(download[0])}).', file=out)
    print(
        '"Installed" is not "loaded": only a running host that has opened the plug-in '
        'confirms which copy it uses. Start the host, add the plug-in, and run this again.',
        file=out,
    )
    problems = problems_with(plugins, expected, download)
    if problems:
        print(file=out)
        print(f'{len(problems)} thing(s) to fix before a host test:', file=out)
        for problem in problems:
            print(f'- {problem}', file=out)
    else:
        print('Nothing found that would spoil a host test.', file=out)
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--expect', metavar='VERSION', help='the version every installed plug-in should be')
    parser.add_argument(
        '--downloads',
        type=Path,
        default=Path.home() / 'Downloads',
        help='where to look for the newest Convolver package (default ~/Downloads)',
    )
    # For tests: look at another layout or another folder tree.
    parser.add_argument('--platform', choices=('mac', 'linux'), help=argparse.SUPPRESS)
    parser.add_argument('--home', type=Path, default=Path.home(), help=argparse.SUPPRESS)
    parser.add_argument('--system-root', type=Path, default=Path('/'), help=argparse.SUPPRESS)
    parser.add_argument('--proc', type=Path, default=Path('/proc'), help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    expected = None
    if args.expect:
        expected = version_tuple(args.expect.removeprefix('v'))
        if expected is None:
            parser.error(f'--expect {args.expect}: give a version as MAJOR.MINOR.PATCH, such as 0.19.21')
    platform = args.platform or ('mac' if sys.platform == 'darwin' else 'linux')
    identities = json.loads(IDENTITIES.read_text(encoding='utf-8'))
    folders = plugin_folders(platform, args.home, args.system_root, os.environ.get('LV2_PATH'))
    plugins = installed_plugins(folders, identities)
    opened, how = open_files(platform, args.proc)
    download = None if expected else newest_download(args.downloads)
    problems = report(plugins, opened, how, platform, expected, download, sys.stdout)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
