#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""List the Convolver plug-ins installed on this computer, before a host test.

Read-only (HOST-14): it never installs, removes, moves or changes anything.
For each installed Convolver plug-in it prints the format, version, scope
(your account or all accounts), path and the date of its files (the
package's build time, which copying keeps), and whether a running program
has it loaded. It then names what would spoil a host test: no plug-in with
Convolver's identity, a format installed more than once, formats at
different versions, an installed version older than the newest Convolver
download (or than --expect VERSION), or a program still running an earlier
copy that has since been replaced.

"Installed" means a bundle with Convolver's identifier is in a folder hosts
scan; one that is only named Convolver does not count. "Opened" means a
running process has some of its files open, as an indexer reading
Info.plist does. "Loaded" means a running process has the bundle's current
executable mapped (lsof on macOS, /proc on Linux): only that confirms which
copy a host is using. A plug-in that is installed but not loaded may still
be the one the host loads next time; the host's own plug-in list says which
one it scanned.

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
PLATFORM_WORDS = {'mac': 'macos', 'linux': 'linux'}
FORMAT_NAMES = {'.vst3': 'VST3', '.component': 'AU', '.lv2': 'LV2'}


def plugin_folders(platform, home, system_root, lv2_path=None):
    """(format suffix, folder, scope) for each folder that hosts scan: the
    install locations of docs/use/install.md and the macOS installer."""
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
    ]
    entries = [entry for entry in (lv2_path or '').split(os.pathsep) if entry]
    if not entries:
        folders += [
            ('.lv2', home / '.lv2', 'user'),
            ('.lv2', system / 'usr/lib/lv2', 'system'),
            ('.lv2', system / 'usr/local/lib/lv2', 'system'),
        ]
    # With LV2_PATH set, lilv scans only its folders.
    for entry in entries:
        folder = Path(entry).expanduser()
        try:
            folder.relative_to(home)
            scope = 'user'
        except ValueError:
            scope = 'system'
        folders.append(('.lv2', folder, scope))
    unique = []
    for folder in folders:
        if all(os.path.normpath(folder[1]) != os.path.normpath(seen[1]) for seen in unique):
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
    """('id' when the bundle carries Convolver's identity; 'name' when only its
    name says Convolver and it carries another identity; 'unknown' when it is
    named Convolver but its identity cannot be read; None when it is not
    Convolver's) and the version text or None."""
    named = 'name' if 'convolver' in bundle.name.lower() else None
    if suffix == '.lv2':
        text = '\n'.join(read_text(bundle / name) for name in ('manifest.ttl', 'convolver.ttl'))
        ours = 'id' if identities['lv2']['base_uri'] in text else named and ('name' if text else 'unknown')
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
        ours = 'id' if plist.get('CFBundleIdentifier') == identities[key]['macos_bundle_id'] else named
        return ours, plist.get('CFBundleShortVersionString')
    # A Linux or Windows VST3 bundle has no Info.plist; the SDK's moduleinfo.json
    # (JSON5, so read only the fields needed) names the factory and version.
    info = read_text(bundle / 'Contents' / 'Resources' / 'moduleinfo.json')
    version = re.search(r'"Version"\s*:\s*"([^"]*)"', info)
    ours = (
        'id'
        if identities['vst3']['processor_class_id'].upper() in info.upper()
        else named and ('name' if info else 'unknown')
    )
    return ours, version.group(1) if version else None


def installed_plugins(folders, identities):
    """One entry per installed bundle; a symbolic link to a bundle already
    listed is shown as a link to it, not as another copy."""
    found = {}
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
            real = bundle.resolve()
            if real in found:
                # Keep the bundle itself as the entry and the link as a link,
                # whichever folder was read first.
                entry = found[real]
                if entry['path'].is_symlink() and not bundle.is_symlink():
                    entry['links'].append(entry['path'])
                    entry['path'], entry['scope'] = bundle, scope
                else:
                    entry['links'].append(bundle)
                continue
            try:
                modified = dt.datetime.fromtimestamp(real.stat().st_mtime).astimezone()
            except OSError:
                modified = None
            found[real] = {
                'format': FORMAT_NAMES[suffix],
                'scope': scope,
                'path': bundle,
                'links': [],
                'identity': ours,
                'version': version_tuple(version),
                'version_text': version or 'unknown',
                'modified': modified,
            }
    return list(found.values())


def parse_lsof(text):
    """[(path, pid, command, inode or None, mapped)] from `lsof -F pcfin`
    output. A file is mapped when its descriptor is txt or mem (code or
    memory-mapped), and only opened when it is a numbered descriptor."""
    opened = []
    pid = command = inode = None
    mapped = False
    for line in text.splitlines():
        if not line:
            continue
        tag, value = line[0], line[1:]
        if tag == 'p':
            pid, command, inode = value, None, None
        elif tag == 'c':
            command = value
        elif tag == 'f':
            inode, mapped = None, value in ('txt', 'mem')
        elif tag == 'i':
            inode = value
        elif tag == 'n' and pid is not None:
            opened.append((value, pid, command or '?', inode, mapped))
            inode, mapped = None, False
    return opened


def open_files_linux(proc):
    opened = []
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
                opened.append((fields[5], entry.name, command, fields[4], True))
    return opened


def open_files(platform, proc=Path('/proc')):
    """([(path, pid, command, inode, mapped)] for the files running
    processes have open or mapped, how they were found or None, and why not)."""
    if platform == 'linux' and proc.is_dir():
        return open_files_linux(proc), '/proc, the processes you can read', None
    lsof = shutil.which('lsof') or ('/usr/sbin/lsof' if Path('/usr/sbin/lsof').exists() else None)
    if not lsof:
        return [], None, 'no lsof or /proc'
    command = [lsof, '-n', '-P', '-w', '-u', str(os.getuid()), '-F', 'pcfin']
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=60, check=False)
    except subprocess.TimeoutExpired:
        return [], None, 'lsof took over a minute'
    except OSError as error:
        return [], None, f'lsof failed: {error}'
    # lsof exits 1 when it cannot read some processes; what it did read stands.
    opened = parse_lsof(result.stdout)
    if not opened:
        return [], None, 'lsof listed nothing'
    return opened, 'lsof, your processes', None


def is_executable(relative):
    """Whether a path inside a bundle is its executable: Contents/MacOS/ on
    macOS, Contents/<architecture>/ for VST3 elsewhere, a shared library in
    an LV2 bundle."""
    parts = relative.split('/')
    if parts[:2] == ['Contents', 'MacOS']:
        return True
    if len(parts) == 3 and parts[0] == 'Contents' and parts[1] != 'Resources':
        return True
    return relative.endswith(('.so', '.dylib', '.dll'))


def loaded_by(plugin, opened):
    """{'loaded': processes that have the bundle's current executable mapped,
    'opened': processes that read its other files but have not mapped the
    executable, 'stale': processes that still map an earlier executable that
    has since been replaced or deleted}, each a sorted list of (pid, command)."""
    roots = {str(path).rstrip('/') + '/' for path in [plugin['path'], *plugin.get('links', [])]}
    roots.add(str(plugin['path'].resolve()) + '/')
    seen = {'loaded': set(), 'opened': set(), 'stale': set()}
    for path, pid, command, inode, mapped in opened:
        deleted = path.endswith(' (deleted)')
        name = path[: -len(' (deleted)')] if deleted else path
        root = next((root for root in roots if name.startswith(root)), None)
        if root is None:
            continue
        if not (mapped and is_executable(name[len(root) :])):
            seen['opened'].add((pid, command))
            continue
        replaced = deleted
        if not replaced and inode and inode.isdigit() and inode != '0':
            try:
                replaced = Path(name).stat().st_ino != int(inode)
            except OSError:
                replaced = True
        seen['stale' if replaced else 'loaded'].add((pid, command))

    def order(user):
        return (user[1].lower(), int(user[0]) if user[0].isdigit() else 0)

    return {
        'loaded': sorted(seen['loaded'], key=order),
        'opened': sorted(seen['opened'] - seen['loaded'] - seen['stale'], key=order),
        'stale': sorted(seen['stale'] - seen['loaded'], key=order),
    }


def newest_download(folder, platform):
    """((version, name) of the newest Convolver package in folder for this
    platform, or None; and why there is none)."""
    try:
        names = [entry.name for entry in folder.iterdir()]
    except OSError as error:
        return None, f'{folder} could not be read ({error.strerror or error})'
    others = set(PLATFORM_WORDS.values()) - {PLATFORM_WORDS[platform]} | {'windows'}
    newest = None
    for name in names:
        match = VERSION_NAME.search(name)
        if not match or any(word in name.lower() for word in others):
            continue
        version = tuple(int(part) for part in match.groups())
        if newest is None or version > newest[0]:
            newest = (version, name)
    return newest, None if newest else f'no Convolver package in {folder}'


def problems_with(plugins, opened, expected, download):
    problems = []
    checked = [plugin for plugin in plugins if plugin['identity'] == 'id']
    if not checked:
        problems.append(
            "No plug-in with Convolver's identity is installed in the folders hosts scan; install the package first."
        )
    problems += [
        f'{plugin["path"]}: named Convolver, but its identity cannot be read; reinstall it from a package.'
        for plugin in plugins
        if plugin['identity'] == 'unknown'
    ]
    by_format = {}
    for plugin in checked:
        by_format.setdefault(plugin['format'], []).append(plugin)
    for name, copies in sorted(by_format.items()):
        if len(copies) > 1:
            paths = ''.join(f'\n    {copy["version_text"]:>9}  {copy["scope"]:<6}  {copy["path"]}' for copy in copies)
            problems.append(
                f'{name} is installed {len(copies)} times; a host may load any of them:{paths}\n'
                '  Keep one, and remove the others yourself (this tool removes nothing).'
            )
    readable = [plugin for plugin in checked if plugin['version'] is not None]
    if len({plugin['version'] for plugin in readable}) > 1:
        listed = ', '.join(f'{plugin["format"]} {plugin["version_text"]}' for plugin in readable)
        problems.append(f'The installed plug-ins are not all the same version: {listed}.')
    problems += [
        f'{plugin["path"]}: its version cannot be read; reinstall it from a package.'
        for plugin in checked
        if plugin['version'] is None
    ]
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
    for plugin in plugins:
        for pid, command in loaded_by(plugin, opened)['stale']:
            problems.append(
                f'{command} (pid {pid}) still has an earlier copy of {plugin["path"]} loaded, '
                'replaced since it started: quit and reopen it before testing.'
            )
    return problems


def report(plugins, files, platform, expected, download, out):
    opened, how, why = files
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
        for link in plugin['links']:
            print(f'        also reached through the link {link}', file=out)
        if plugin['identity'] == 'name':
            print(
                '        named Convolver, but carries another identity: not counted, and left out of the checks',
                file=out,
            )
        elif plugin['identity'] == 'unknown':
            print('        named Convolver, but its identity cannot be read: not counted as installed', file=out)
        print(f'        files dated {when}', file=out)
        use = loaded_by(plugin, opened)

        def names(users):
            return ', '.join(f'{command} (pid {pid})' for pid, command in users)

        if use['loaded']:
            print(f'        loaded by {names(use["loaded"])}: its executable is mapped', file=out)
        if use['stale']:
            print(f'        an earlier copy, since replaced, is loaded by {names(use["stale"])}', file=out)
        if use['opened']:
            print(
                f'        opened by {names(use["opened"])}: files read, executable not mapped, so not loaded',
                file=out,
            )
        if not (use['loaded'] or use['stale']):
            if how:
                print(f'        not seen loaded in the processes checked ({how})', file=out)
            else:
                print(f'        whether a program has it loaded is unknown ({why})', file=out)
    print(file=out)
    if expected:
        print(f'Expected version: {version_text(expected)} (--expect).', file=out)
    elif download[0]:
        print(f'Newest Convolver download: {download[0][1]} ({version_text(download[0][0])}).', file=out)
    else:
        print(
            f'Versions were not compared with a download: {download[1]}. Give --expect VERSION to compare.',
            file=out,
        )
    print(
        '"Installed" is not "loaded": only a running host that has mapped the plug-in\'s '
        'executable confirms which copy it uses; reading its files is not loading it. '
        'Start the host, add the plug-in, and run this again.',
        file=out,
    )
    problems = problems_with(plugins, opened, expected, None if expected else download[0])
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
        expected = version_tuple(args.expect[1:] if args.expect.startswith('v') else args.expect)
        if expected is None:
            parser.error(f'--expect {args.expect}: give a version as MAJOR.MINOR.PATCH, such as 0.19.21')
    platform = args.platform or ('mac' if sys.platform == 'darwin' else 'linux')
    identities = json.loads(IDENTITIES.read_text(encoding='utf-8'))
    folders = plugin_folders(platform, args.home, args.system_root, os.environ.get('LV2_PATH'))
    plugins = installed_plugins(folders, identities)
    files = open_files(platform, args.proc)
    download = (None, None) if expected else newest_download(args.downloads, platform)
    problems = report(plugins, files, platform, expected, download, sys.stdout)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
