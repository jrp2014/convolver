# SPDX-License-Identifier: GPL-2.0-or-later
"""Run Tracktion's pluginval on every plug-in in Convolver's LV2 bundle (LV2-3).

  pluginval_tests.py <pluginval> <convolver.lv2> [extra pluginval options...]

pluginval hosts LV2 through JUCE, which is independent of the lilv host the
other LV2 tests use. Given a bundle, pluginval tests only the first plug-in
it lists, so each of the bundle's plug-ins (one per channel layout) is
copied into a bundle of its own whose manifest names only that plug-in, and
validated there at strictness level 10. LV2_PATH is cleared for the runs, so
pluginval cannot also find the original bundle.
"""

import os
import pathlib
import re
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'plugins'))
from pluginval import validate  # noqa: E402

SUBJECT = re.compile(r'^<([^>]+)>$', re.MULTILINE)


def split_manifest(text):
    """The manifest's prefix lines and its statements, keyed by plug-in URI."""
    starts = [match.start() for match in SUBJECT.finditer(text)]
    if not starts:
        sys.exit('the manifest names no plug-ins')
    head = text[: starts[0]]
    entries = {}
    for start, end in zip(starts, [*starts[1:], len(text)]):
        statement = text[start:end].rstrip() + '\n'
        entries[SUBJECT.match(statement).group(1)] = statement
    return head, entries


def single_bundle(source, directory, head, statement):
    """A copy of the bundle whose manifest lists one plug-in."""
    bundle = directory / source.name
    shutil.copytree(source, bundle)
    (bundle / 'manifest.ttl').write_text(head + statement)
    return bundle


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    pluginval, source, *extra = sys.argv[1:]
    source = pathlib.Path(source).resolve()
    head, entries = split_manifest((source / 'manifest.ttl').read_text())
    os.environ.pop('LV2_PATH', None)
    failed = []
    with tempfile.TemporaryDirectory() as temporary:
        for index, (uri, statement) in enumerate(entries.items()):
            directory = pathlib.Path(temporary) / str(index)
            directory.mkdir()
            bundle = single_bundle(source, directory, head, statement)
            print(f'=== {uri}', flush=True)
            if validate(pluginval, bundle, extra) != 0:
                failed.append(uri)
    passed = len(entries) - len(failed)
    print(f'pluginval: {passed} of {len(entries)} LV2 plug-ins pass at strictness 10')
    for uri in failed:
        print(f'  failed: {uri}')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
