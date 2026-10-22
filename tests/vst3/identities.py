#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the built VST3 bundle and the SDK against plugins/identities.json (PLUG-4).

- moduleinfo.json (what hosts scan) names the registry's vendor and class IDs;
- on macOS, Info.plist carries the registry's bundle identifier;
- each speaker's VST3 constant in the SDK is the same bit as its WAV mask bit,
  so a WAV channel mask and a VST3 speaker arrangement mean the same layout.

Usage: identities.py REGISTRY SDK_DIR BUNDLE [--require-moduleinfo]
"""
import argparse
import json
from pathlib import Path
import plistlib
import re
import sys


def speaker_bits(header):
    """Map each `const Speaker kSpeakerX = 1 << n;` (or alias) to its bit."""
    bits = {}
    for name, value in re.findall(r'const\s+Speaker\s+(kSpeaker\w+)\s*=\s*([^;]+);', header):
        value = value.strip()
        shift = re.fullmatch(r'1\s*<<\s*(\d+)', value)
        if shift:
            bits[name] = int(shift.group(1))
        elif value in bits:
            bits[name] = bits[value]
    return bits


def module_info(path):
    # The SDK writes JSON5-style trailing commas.
    text = re.sub(r',(\s*[}\]])', r'\1', path.read_text(encoding='utf-8'))
    return json.loads(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('registry', type=Path)
    parser.add_argument('sdk', type=Path)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--require-moduleinfo', action='store_true')
    args = parser.parse_args()
    registry = json.loads(args.registry.read_text(encoding='utf-8'))
    errors = []

    header = (args.sdk / 'pluginterfaces/vst/vstspeaker.h').read_text(encoding='utf-8')
    bits = speaker_bits(header)
    for speaker in registry['speakers']:
        if bits.get(speaker['vst3']) != speaker['bit']:
            errors.append(f"{speaker['vst3']} is bit {bits.get(speaker['vst3'])} in the SDK, "
                          f"but WAV {speaker['wav']} is bit {speaker['bit']}")

    vst3 = registry['vst3']
    info_path = args.bundle / 'Contents/Resources/moduleinfo.json'
    if info_path.is_file():
        info = module_info(info_path)
        factory = info['Factory Info']
        if (factory['Vendor'], factory['URL']) != (registry['vendor']['name'],
                                                   registry['vendor']['url']):
            errors.append('moduleinfo.json: factory vendor or URL differs from the registry')
        classes = {c['Category']: c['CID'].upper() for c in info['Classes']}
        wanted = {'Audio Module Class': vst3['processor_class_id'],
                  'Component Controller Class': vst3['controller_class_id']}
        if classes != wanted:
            errors.append(f'moduleinfo.json: classes {classes}, registry {wanted}')
    elif args.require_moduleinfo:
        errors.append(f'{info_path} is missing')
    else:
        print('moduleinfo.json not generated (runtime validation off); class IDs not checked')

    plist_path = args.bundle / 'Contents/Info.plist'
    if sys.platform == 'darwin':
        with plist_path.open('rb') as handle:
            identifier = plistlib.load(handle).get('CFBundleIdentifier')
        if identifier != vst3['macos_bundle_id']:
            errors.append(f'Info.plist: CFBundleIdentifier {identifier!r}, '
                          f"registry {vst3['macos_bundle_id']!r}")

    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print('VST3 identities match plugins/identities.json')
    return 0


if __name__ == '__main__':
    sys.exit(main())
