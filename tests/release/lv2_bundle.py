# SPDX-License-Identifier: GPL-2.0-or-later
"""Load a packaged LV2 bundle's library as a host does and check its plug-ins (LV2-5).

Usage: lv2_bundle.py BUNDLE

The bundle must hold exactly the library, manifest.ttl and convolver.ttl.
The library is opened with ctypes, not lilv, so that a clean machine needs
nothing beyond Python: `lv2_descriptor(i)` must return one plug-in per fixed
layout in plugins/identities.json, under the URIs that manifest.ttl lists,
and then NULL. This finds a library that the machine cannot load (a missing
or too new system library) and a manifest that names plug-ins the library
does not have. lilv's own reading of the Turtle is checked by lv2.adapter,
and lv2lint and pluginval check the bundle in the LV2 workflow.
"""

import ctypes
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Descriptor(ctypes.Structure):
    # Only the first member is read; the rest of LV2_Descriptor is function pointers.
    _fields_ = [('URI', ctypes.c_char_p)]


def check(bundle):
    bundle = Path(bundle).resolve()
    identities = json.loads((ROOT / 'plugins/identities.json').read_text(encoding='utf-8'))
    base = identities['lv2']['base_uri']
    expected = [base + layout['id'] for layout in identities['fixed_layouts']]
    assert bundle.name == identities['lv2']['bundle'], (
        f'the LV2 bundle is {bundle.name}, not {identities["lv2"]["bundle"]}'
    )
    libraries = [p for p in bundle.iterdir() if p.suffix in ('.so', '.dylib', '.dll')]
    names = sorted(p.name for p in bundle.iterdir())
    assert len(libraries) == 1, f'{bundle}: expected one plug-in library, found {names}'
    assert names == sorted([libraries[0].name, 'convolver.ttl', 'manifest.ttl']), f'{bundle}: unexpected files {names}'
    manifest = (bundle / 'manifest.ttl').read_text(encoding='utf-8')
    listed = re.findall(r'^<(https:[^>]*)>$', manifest, re.MULTILINE)
    assert listed == expected, f'manifest.ttl lists {listed}, not the fixed layouts {expected}'
    assert f'lv2:binary <{libraries[0].name}>' in manifest, f'manifest.ttl does not name {libraries[0].name}'
    library = ctypes.CDLL(str(libraries[0]))
    lv2_descriptor = library.lv2_descriptor
    lv2_descriptor.argtypes = [ctypes.c_uint32]
    lv2_descriptor.restype = ctypes.POINTER(Descriptor)
    found = []
    while len(found) <= len(expected):
        descriptor = lv2_descriptor(len(found))
        if not descriptor:
            break
        found.append(descriptor.contents.URI.decode('utf-8'))
    assert found == expected, f'{libraries[0].name} describes {found}, not {expected}'
    print(f'Loaded {libraries[0]}: its {len(found)} plug-ins match manifest.ttl and plugins/identities.json')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    check(sys.argv[1])
