#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks the built Convolver.component without loading it.

Its AudioComponents entry must carry the permanent codes from
plugins/identities.json (PLUG-4), the bundle's version must follow VERSION,
the executable must export only the factory and link only macOS's own
libraries (no libsndfile: the window reads filter files with AudioToolbox),
the licence notices must be inside (the component is GPL-3.0-or-later with AudioUnitSDK's Apache-2.0
notice), and the ad-hoc signature must cover the whole bundle.
"""

import json
import plistlib
import subprocess
import sys
from pathlib import Path


def fail(message):
    print(f'FAIL: {message}')
    sys.exit(1)


def main():
    if len(sys.argv) != 5 or sys.argv[4] not in {'system-only', 'any-libraries'}:
        sys.exit('usage: bundle_tests.py IDENTITIES.json Convolver.component VERSION system-only|any-libraries')
    registry = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
    bundle = Path(sys.argv[2])
    version = sys.argv[3]
    contents = bundle / 'Contents'
    with (contents / 'Info.plist').open('rb') as f:
        info = plistlib.load(f)

    au = registry['au']
    if info.get('CFBundleIdentifier') != au['macos_bundle_id']:
        fail(f'bundle identifier {info.get("CFBundleIdentifier")!r}, expected {au["macos_bundle_id"]!r}')
    if info.get('CFBundlePackageType') != 'BNDL':
        fail('the bundle is not of type BNDL')
    if info.get('CFBundleShortVersionString') != version:
        fail(f'bundle version {info.get("CFBundleShortVersionString")!r}, expected {version!r}')
    components = info.get('AudioComponents')
    if not isinstance(components, list) or len(components) != 1:
        fail('Info.plist must list exactly one AudioComponents entry')
    entry = components[0]
    for key in ('type', 'subtype', 'manufacturer'):
        if entry.get(key) != au[key]:
            fail(f'AudioComponents {key} {entry.get(key)!r}, expected {au[key]!r}')
    major, minor, patch = (int(part) for part in version.split('.'))
    expected = (major << 16) | (min(minor, 255) << 8) | min(patch, 255)
    if entry.get('version') != expected:
        fail(f'AudioComponents version {entry.get("version")!r}, expected {expected:#x}')
    if entry.get('factoryFunction') != 'ConvolverAUFactory':
        fail('factoryFunction is not ConvolverAUFactory')
    if entry.get('sandboxSafe') is not True:
        fail('the component must be sandbox-safe (it reads only the files chosen in its window)')

    executable = contents / 'MacOS' / info['CFBundleExecutable']
    symbols = subprocess.run(
        ['nm', '-gU', '-j', str(executable)], check=True, capture_output=True, text=True
    ).stdout.split()
    # The linker's own Mach-O header symbol aside, only the factory is visible.
    exported = sorted({s for s in symbols if not s.startswith('__mh_')})
    if exported != ['_ConvolverAUFactory']:
        fail(f'the executable must export only _ConvolverAUFactory, not {exported}')

    # Only macOS's own libraries, plus a sanitizer build's runtime: anything
    # else would have to be installed on every Mac that loads the component.
    # A build with a fallback (CONVOLVER_AU_FILE_READER=LIBSNDFILE or
    # CONVOLVER_FFT_BACKEND=FFTW) passes any-libraries and only lists them.
    linked = subprocess.run(
        ['otool', '-L', str(executable)], check=True, capture_output=True, text=True
    ).stdout.splitlines()
    # Lines ending in a colon name the file (and, in a universal binary, each architecture).
    linked = [line for line in linked if line.strip() and not line.rstrip().endswith(':')]
    for line in linked:
        library = line.strip().split(' (')[0]
        if not library.startswith(('/System/Library/', '/usr/lib/')) and 'libclang_rt.' not in library:
            if sys.argv[4] == 'system-only':
                fail(f'the executable links {library}, which is not part of macOS')
            print(f'note: links {library} (a fallback build: every Mac that loads it needs this library)')

    licenses = contents / 'Resources' / 'licenses'
    for name in ('NOTICE.txt', 'GPL-3.0.txt', 'AudioUnitSDK-LICENSE.txt'):
        if not (licenses / name).is_file():
            fail(f'missing licence file {name}')
    if 'Version 3' not in (licenses / 'GPL-3.0.txt').read_text(encoding='utf-8')[:200]:
        fail('GPL-3.0.txt is not the GPL version 3')
    if 'Apache License' not in (licenses / 'AudioUnitSDK-LICENSE.txt').read_text(encoding='utf-8')[:400]:
        fail('AudioUnitSDK-LICENSE.txt is not the Apache License')

    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
    print(
        f'Convolver.component: {au["type"]}/{au["subtype"]}/{au["manufacturer"]}, '
        f'version {version} ({expected:#x}), {len(linked)} macOS libraries, notices present, signature valid'
    )


if __name__ == '__main__':
    main()
