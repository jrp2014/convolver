# SPDX-License-Identifier: GPL-2.0-or-later
"""The candidate's package phase takes only what its own build phase staged
(REL-1): the same platform, the same source archive, packaged once."""

import json
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import candidate  # noqa: E402
from contract import sha256  # noqa: E402


class PhaseTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.temp = Path(temp.name)
        self.archive = self.temp / 'Convolver-source.tar.gz'
        self.archive.write_bytes(b'source')
        self.work = self.temp / 'work'
        (self.work / 'stage').mkdir(parents=True)
        self.output = self.temp / 'assets'

    def built(self, **changes):
        record = {'platform': 'linux-x86_64', 'archive_sha256': sha256(self.archive)}
        record.update(changes)
        (self.work / candidate.BUILT).write_text(json.dumps(record))

    def package(self):
        candidate.package(self.archive, 'linux-x86_64', self.work, self.output)

    def test_needs_the_build_phase(self):
        with self.assertRaisesRegex(ValueError, 'run --phase build first'):
            self.package()

    def test_refuses_another_platform_or_archive(self):
        for changes in ({'platform': 'windows-x86_64'}, {'archive_sha256': '0' * 64}):
            with self.subTest(changes=changes):
                self.built(**changes)
                with self.assertRaisesRegex(ValueError, 'not linux-x86_64 from this archive'):
                    self.package()

    def test_packages_once(self):
        self.built()
        for packaged in (self.work / 'stage/artifact-manifest.json', self.work / 'package-source'):
            with self.subTest(packaged=packaged.name):
                packaged.write_text('{}')
                with self.assertRaisesRegex(ValueError, 'already been packaged'):
                    self.package()
                packaged.unlink()

    def test_output_must_be_new(self):
        self.built()
        self.output.mkdir()
        with self.assertRaisesRegex(ValueError, 'output directory must be new'):
            self.package()
        self.assertFalse((self.work / 'stage/artifact-manifest.json').exists())

    def bundle_archive(self, changed=None):
        """A source archive whose bundle holds one script and its manifest."""
        bundle = self.temp / 'bundle/Convolver-source'
        script = bundle / 'convolver/scripts/release/verify_artifact.py'
        script.parent.mkdir(parents=True)
        script.write_text('checked')
        manifest = {'schema': 2, 'files': {'convolver/scripts/release/verify_artifact.py': sha256(script)}}
        (bundle / 'source-manifest.json').write_text(json.dumps(manifest))
        if changed:
            script.write_text(changed)
        with tarfile.open(self.archive, 'w:gz') as tar:
            tar.add(bundle, arcname='Convolver-source')
        return manifest

    def test_packages_from_a_fresh_copy_of_the_archive(self):
        # The build and its tests ran as the same user and write into their
        # own copy: the package phase runs only the archive's scripts.
        manifest = self.bundle_archive()
        self.built()
        built_copy = self.work / 'Convolver-source/convolver/scripts/release'
        built_copy.mkdir(parents=True)
        (built_copy / 'verify_artifact.py').write_text('changed by a test')
        with self.assertRaises(Exception) as raised:
            self.package()
        # It got past the source check, to the release policy the fixture lacks.
        self.assertNotIn('inventory', str(raised.exception))
        self.assertEqual(candidate.checked_manifest(self.work / 'package-source/Convolver-source'), manifest)

    def test_refuses_an_archive_that_does_not_match_its_manifest(self):
        self.bundle_archive(changed='not what the manifest lists')
        self.built()
        with self.assertRaisesRegex(
            ValueError, 'inventory/hash mismatch: convolver/scripts/release/verify_artifact.py'
        ):
            self.package()

    def test_build_needs_a_new_work_directory(self):
        with self.assertRaisesRegex(ValueError, 'work directory must be new'):
            candidate.build(self.archive, 'linux-x86_64', self.work, 'cmake')


if __name__ == '__main__':
    unittest.main()
