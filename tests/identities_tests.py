#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check plugins/identities.json, the registry of permanent plug-in identities (PLUG-4).

Three kinds of check:

- the registry follows each format's rules (AU codes, VST3 class IDs, URIs);
- its permanent values equal the frozen copy below, so changing one takes two
  deliberate edits and an owner's decision (docs/develop/plugin-identities.md);
- files that must agree with it (installer scripts, state codec) do, and no
  source file keeps its own copy of an identity.

Run from anywhere: python3 tests/identities_tests.py [--root DIR].
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

# Released or reserved identities. Hosts store them in projects, presets and
# installer receipts. Change one only by an owner's decision, recorded in
# docs/develop/plugin-identities.md, and only before the first release that uses it.
FROZEN = {
    ('vendor', 'name'): 'Convolver',
    ('vendor', 'url'): 'https://github.com/jrp2014/convolver',
    ('vst3', 'processor_class_id'): '73A81329E6484ADF9DA7D731A8C94201',
    ('vst3', 'controller_class_id'): '622AD87C23664CCFB157473FBCD4A6B9',
    ('vst3', 'macos_bundle_id'): 'io.github.jrp2014.convolver.vst3',
    ('au', 'type'): 'aufx',
    ('au', 'subtype'): 'Conv',
    ('au', 'manufacturer'): 'CONV',
    ('au', 'macos_bundle_id'): 'io.github.jrp2014.convolver.au',
    ('lv2', 'base_uri'): 'https://github.com/jrp2014/convolver/lv2#',
    ('lv2', 'bundle'): 'convolver.lv2',
    ('macos_installer', 'vst3_package_id'): 'io.github.jrp2014.convolver.pkg.vst3',
    ('macos_installer', 'tools_package_id'): 'io.github.jrp2014.convolver.pkg.tools',
    ('windows_installer', 'registry_key'): 'Software\\Convolver',
    ('windows_installer', 'uninstall_key'): 'Convolver',
    ('host_state', 'magic'): 'CNVS',
    ('historical', 'vst2_unique_id'): 'Conv',
}
# A published fixed layout's id is part of its LV2 URI: entries may be added, never
# changed or removed.
FROZEN_LAYOUTS = {
    'discrete-1-1': (1, 1), 'discrete-2-2': (2, 2), 'discrete-1-2': (1, 2),
    'discrete-2-4': (2, 4), 'discrete-2-6': (2, 6), 'discrete-2-8': (2, 8),
    'discrete-6-6': (6, 6), 'discrete-8-8': (8, 8),
}
# WAVE_FORMAT_EXTENSIBLE speaker bits 0-17 (ksmedia.h), the meaning of a channel mask.
WAV_SPEAKERS = ['FL', 'FR', 'FC', 'LFE', 'BL', 'BR', 'FLC', 'FRC', 'BC', 'SL', 'SR',
                'TC', 'TFL', 'TFC', 'TFR', 'TBL', 'TBC', 'TBR']
# Apple's AudioChannelBitmap names for the same bits (CoreAudioBaseTypes.h).
AU_SPEAKERS = ['Left', 'Right', 'Center', 'LFEScreen', 'LeftSurround', 'RightSurround',
               'LeftCenter', 'RightCenter', 'CenterSurround', 'LeftSurroundDirect',
               'RightSurroundDirect', 'TopCenterSurround', 'VerticalHeightLeft',
               'VerticalHeightCenter', 'VerticalHeightRight', 'TopBackLeft',
               'TopBackCenter', 'TopBackRight']
# Channel designations in LV2 port-groups (lv2/port-groups.lv2/port-groups.ttl).
LV2_CHANNELS = {'left', 'right', 'center', 'side', 'centerLeft', 'centerRight', 'sideLeft',
                'sideRight', 'rearLeft', 'rearRight', 'rearCenter', 'lowFrequencyEffects'}
REVERSE_DNS = re.compile(r'^[a-z][a-z0-9-]*(\.[a-z0-9][a-z0-9-]*){2,}$')
# Files allowed to spell identities out: the registry, this test and dated records.
COPIES_ALLOWED = ('plugins/identities.json', 'tests/identities_tests.py', 'CHANGELOG.md',
                  'packaging/macos/Distribution.xml', 'packaging/windows/installer.nsi')
RECORDS = ('historical/', 'docs/', 'TODO.md')


def lookup(registry, path):
    value = registry
    for key in path:
        value = value[key]
    return value


def strings(value, path=()):
    if isinstance(value, str):
        yield path, value
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from strings(item, path + (key,))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from strings(item, path + (str(index),))


def check_rules(registry, errors):
    # The build pastes these values into C++ string literals and Info.plist.
    for path, value in strings(registry):
        if path != ('about',) and not all(' ' <= c <= '~' and c not in '"<>&' for c in value):
            errors.append(f"{'.'.join(path)}: printable ASCII without quotes, <, > or &")
    if registry.get('schema') != 1:
        errors.append('schema must be 1; a new schema needs this test updated')
    au = registry['au']
    for name in ('type', 'subtype', 'manufacturer'):
        code = au[name]
        if len(code) != 4 or not all(' ' <= c <= '~' for c in code):
            errors.append(f'au.{name} {code!r}: an AU code is four printable ASCII characters')
    if au['type'] != 'aufx':
        errors.append('au.type must be aufx (an effect)')
    # Apple reserves all-lowercase manufacturer codes, and DEMO is a placeholder.
    if not any(c.isupper() for c in au['manufacturer']) or au['manufacturer'] == 'DEMO':
        errors.append('au.manufacturer needs an uppercase letter and must not be DEMO')
    ids = [registry['vst3'][k] for k in ('processor_class_id', 'controller_class_id')]
    for cid in ids:
        if not re.fullmatch(r'[0-9A-F]{32}', cid) or set(cid) == {'0'}:
            errors.append(f'VST3 class ID {cid}: 32 uppercase hex digits, not all zero')
    if len(set(ids)) != len(ids):
        errors.append('VST3 processor and controller class IDs must differ')
    bundles = [registry['vst3']['macos_bundle_id'], registry['au']['macos_bundle_id'],
               *registry['macos_installer'].values()]
    for bundle in bundles:
        if not REVERSE_DNS.match(bundle):
            errors.append(f'{bundle}: macOS identifiers are lowercase reverse-DNS names')
    if len(set(bundles)) != len(bundles):
        errors.append('every macOS bundle and package identifier must be distinct')
    base = registry['lv2']['base_uri']
    if not re.fullmatch(r'https://[^\s#]+[#/]', base):
        errors.append('lv2.base_uri: an https URI ending in # or /')
    channels = registry['channels']
    low, high = channels['minimum'], channels['maximum']
    if not 1 <= low <= high <= 8:
        errors.append('channels: 1 <= minimum <= maximum <= 8 (the engine and VST3 limit)')
    seen = set()
    for layout in registry['fixed_layouts']:
        lid = layout['id']
        if lid in seen:
            errors.append(f'fixed layout {lid} listed twice')
        seen.add(lid)
        if not re.fullmatch(r'[a-z][a-z0-9-]*', lid):
            errors.append(f'fixed layout {lid}: lowercase letters, digits and hyphens only')
        for side in ('inputs', 'outputs'):
            if not low <= layout[side] <= high:
                errors.append(f'fixed layout {lid}: {side} outside the channel bounds')
        for side in ('input', 'output'):
            if layout[side] not in ('discrete', 'named'):
                errors.append(f'fixed layout {lid}: {side} must be discrete or named')
        if layout['inputs'] != layout['outputs'] and not channels['unequal_counts']:
            errors.append(f'fixed layout {lid}: unequal counts are not supported')
        if lid.startswith('discrete-') and lid != f"discrete-{layout['inputs']}-{layout['outputs']}":
            errors.append(f'fixed layout {lid}: a discrete id names its counts')
    speakers = registry['speakers']
    for bit, speaker in enumerate(speakers):
        if speaker['bit'] != bit:
            errors.append('speakers: list bits 0, 1, 2, ... in order')
            break
        if bit < len(WAV_SPEAKERS) and speaker['wav'] != WAV_SPEAKERS[bit]:
            errors.append(f'speaker bit {bit}: WAV name is {WAV_SPEAKERS[bit]}')
        if bit < len(AU_SPEAKERS) and speaker['au'] != 'kAudioChannelBit_' + AU_SPEAKERS[bit]:
            errors.append(f'speaker bit {bit}: AU bit is kAudioChannelBit_{AU_SPEAKERS[bit]}')
        if speaker['lv2'] and speaker['lv2'] not in LV2_CHANNELS:
            errors.append(f"speaker bit {bit}: {speaker['lv2']} is not an LV2 port-groups channel")
    if len(speakers) != len(WAV_SPEAKERS):
        errors.append(f'speakers: list the {len(WAV_SPEAKERS)} WAV bits the CLI understands')
    lv2 = [s['lv2'] for s in speakers if s['lv2']]
    if len(set(lv2)) != len(lv2):
        errors.append('speakers: an LV2 designation may name only one bit')


def check_frozen(registry, errors):
    for path, value in FROZEN.items():
        actual = lookup(registry, path)
        if actual != value:
            errors.append(f"{'.'.join(path)} is {actual!r}, frozen as {value!r}. Released "
                          'identities never change; see docs/develop/plugin-identities.md')
    layouts = {layout['id']: (layout['inputs'], layout['outputs'])
               for layout in registry['fixed_layouts']}
    for lid, counts in FROZEN_LAYOUTS.items():
        if layouts.get(lid) != counts:
            errors.append(f'fixed layout {lid} {counts} changed or removed; add new entries '
                          'instead (its id is part of a published LV2 URI)')
    for lid in sorted(set(layouts) - set(FROZEN_LAYOUTS)):
        errors.append(f'fixed layout {lid} is new: add it to FROZEN_LAYOUTS in this test too')
    if 1 not in registry['host_state']['versions']:
        errors.append('host_state.versions: version 1 must stay readable')


def check_consumers(root, registry, errors):
    installer = registry['macos_installer']
    distribution = (root / 'packaging/macos/Distribution.xml').read_text(encoding='utf-8')
    refs = set(re.findall(r'<pkg-ref id="([^"]+)"', distribution))
    if refs != set(installer.values()):
        errors.append('packaging/macos/Distribution.xml: pkg-ref ids must be exactly '
                      f'{sorted(installer.values())}, found {sorted(refs)}')
    nsis = (root / 'packaging/windows/installer.nsi').read_text(encoding='utf-8')
    windows = registry['windows_installer']
    own = {key for key in re.findall(r'HKLM "(Software\\[^"]+)"', nsis, re.I)
           if not key.lower().startswith('software\\microsoft\\')}
    if own != {windows['registry_key']}:
        errors.append('packaging/windows/installer.nsi: every HKLM key outside Microsoft\\ must be '
                      f"{windows['registry_key']!r}, found {sorted(own)}")
    uninstall = set(re.findall(r'CurrentVersion\\Uninstall\\([^"]+)"', nsis))
    if uninstall != {windows['uninstall_key']}:
        errors.append(f"packaging/windows/installer.nsi: uninstall keys {sorted(uninstall)}, "
                      f"registry {windows['uninstall_key']!r}")
    plist = (root / 'plugins/vst3/Info.plist.in').read_text(encoding='utf-8')
    if '<string>${CONVOLVER_ID_VST3_BUNDLE}</string>' not in plist:
        errors.append('plugins/vst3/Info.plist.in: CFBundleIdentifier must be ${CONVOLVER_ID_VST3_BUNDLE}')
    state = (root / 'portable/state.cpp').read_text(encoding='utf-8')
    magic = int.from_bytes(registry['host_state']['magic'].encode('ascii'), 'little')
    newest = max(registry['host_state']['versions'])
    if not re.search(rf'magic = 0x{magic:08x}, version = {newest}\b', state):
        errors.append(f'portable/state.cpp: expected magic 0x{magic:08x} and version {newest}')


def tracked_files(root):
    """Git's file list when root is a work tree's top; otherwise walk the tree.

    A source archive extracted inside another repository (as release candidates
    are) would get an empty list from Git, so Git is used only for its own top.
    """
    try:
        top = subprocess.run(['git', 'rev-parse', '--show-toplevel'], cwd=root,
                             capture_output=True, check=True).stdout.decode().strip()
        if Path(top).resolve() == root.resolve():
            listed = subprocess.run(['git', 'ls-files', '-z'], cwd=root, capture_output=True,
                                    check=True).stdout.decode('utf-8')
            return [name for name in listed.split('\0') if name and (root / name).is_file()]
    except (OSError, subprocess.CalledProcessError):
        pass
    files = []
    for directory, folders, names in os.walk(root):
        # Skip version control and any CMake build tree (generated copies live there).
        folders[:] = [f for f in folders if f != '.git'
                      and not (Path(directory) / f / 'CMakeCache.txt').exists()]
        files += [(Path(directory) / n).relative_to(root).as_posix() for n in names]
    return files


def check_copies(root, registry, errors):
    """Sources must read identities from the registry, never repeat them."""
    patterns = []
    for cid in (registry['vst3']['processor_class_id'], registry['vst3']['controller_class_id']):
        patterns.append(re.compile(cid[:8], re.I))  # First FUID word in any spelling.
    for bundle in (registry['vst3']['macos_bundle_id'], registry['au']['macos_bundle_id'],
                   *registry['macos_installer'].values(), 'org.convolver.'):
        patterns.append(re.compile(re.escape(bundle)))
    patterns.append(re.compile(re.escape(registry['lv2']['base_uri'])))
    # Every text file: sources in any language, Turtle, plists, workflows.
    # AU codes such as Conv are too common to search for; review catches those.
    for name in tracked_files(root):
        if name in COPIES_ALLOWED or name.startswith(RECORDS):
            continue
        data = (root / name).read_bytes()
        if b'\0' in data[:8192]:
            continue  # Binary (audio fixtures, archives).
        text = data.decode('utf-8', errors='replace')
        for pattern in patterns:
            if pattern.search(text):
                errors.append(f'{name}: contains {pattern.pattern!r}; read it from '
                              'plugins/identities.json (cmake/ConvolverIdentities.cmake) instead')


def check(root):
    registry = json.loads((root / 'plugins/identities.json').read_text(encoding='utf-8'))
    errors = []
    check_rules(registry, errors)
    check_frozen(registry, errors)
    check_consumers(root, registry, errors)
    check_copies(root, registry, errors)
    return errors


def self_test():
    """The checks must notice the mistakes they exist for."""
    root = Path(__file__).resolve().parents[1]
    registry = json.loads((root / 'plugins/identities.json').read_text(encoding='utf-8'))
    cases = [
        (('au', 'manufacturer'), 'conv'), (('au', 'subtype'), 'Conv1'),
        (('vst3', 'processor_class_id'), '73a81329e6484adf9da7d731a8c94201'),
        (('vst3', 'macos_bundle_id'), 'org.convolver.vst3'),
        (('lv2', 'base_uri'), 'http://example.org/convolver#'),
    ]
    for path, value in cases:
        mutated = json.loads(json.dumps(registry))
        lookup(mutated, path[:-1])[path[-1]] = value
        errors = []
        check_rules(mutated, errors)
        check_frozen(mutated, errors)
        assert errors, f'{path} = {value!r} was not caught'
    mutated = json.loads(json.dumps(registry))
    mutated['fixed_layouts'][4]['outputs'] = 5
    errors = []
    check_rules(mutated, errors)
    check_frozen(mutated, errors)
    assert errors, 'changed fixed layout was not caught'
    mutated = json.loads(json.dumps(registry))
    mutated['fixed_layouts'].append(dict(mutated['fixed_layouts'][0], id='discrete-4-4',
                                         inputs=4, outputs=4))
    errors = []
    check_frozen(mutated, errors)
    assert errors, 'unfrozen new layout was not caught'
    mutated = json.loads(json.dumps(registry))
    mutated['vendor']['name'] = 'Con"volver'
    errors = []
    check_rules(mutated, errors)
    assert errors, 'quote in a C++ string value was not caught'
    mutated = json.loads(json.dumps(registry))
    mutated['speakers'][4]['lv2'] = 'sideLeft'
    errors = []
    check_rules(mutated, errors)
    assert errors, 'wrong or duplicated LV2 designation was not caught'


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    self_test()
    errors = check(args.root.resolve())
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print('plug-in identities passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
