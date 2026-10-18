# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject dependency leaks, incorrect architecture and changed package bytes."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
from contract import load_policy


class ArtifactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / 'scripts/release/verify_artifact.py'
        assert path.is_file(), 'runtime artifact verifier is missing'
        spec = importlib.util.spec_from_file_location('verify_artifact', path)
        cls.verifier = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.verifier)
        cls.policy = load_policy(ROOT / 'packaging/release-policy.json')

    def test_macos_build_tree_dependency(self):
        with self.assertRaisesRegex(ValueError, 'dependency'):
            self.verifier.check_dependencies('Darwin', ['/tmp/build/libsndfile.dylib'], self.policy)

    def test_macos_unresolved_dependency(self):
        with self.assertRaisesRegex(ValueError, 'dependency'):
            self.verifier.check_dependencies('Darwin', ['@rpath/libmissing.dylib'], self.policy)

    def test_linux_missing_dependency(self):
        with self.assertRaisesRegex(ValueError, 'dependency'):
            self.verifier.check_dependencies('Linux', ['libfftw3f.so.3'], self.policy)

    def test_system_dependencies(self):
        self.verifier.check_dependencies('Linux', ['libc.so.6', 'libstdc++.so.6'], self.policy)
        self.verifier.check_dependencies('Darwin', ['/usr/lib/libSystem.B.dylib', '/System/Library/Frameworks/Accelerate.framework/Versions/A/Accelerate'], self.policy)

    def test_changed_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'tool').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'hash'):
                self.verifier.verify_files(root, {'tool': '0' * 64})

    def test_unrecorded_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'extra').write_text('unexpected')
            with self.assertRaisesRegex(ValueError, 'inventory'):
                self.verifier.verify_files(root, {})

    def test_source_archive_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, 'source archive'):
                self.verifier.source_record(Path(temporary) / 'missing.tar.gz')

    def test_old_libc_floor(self):
        with self.assertRaisesRegex(ValueError, 'GLIBC'):
            self.verifier.check_symbol_versions('GLIBC_2.36 GLIBC_2.2.5', '2.35')
        self.verifier.check_symbol_versions('GLIBC_2.2.5 GLIBC_2.35', '2.35')


if __name__ == '__main__':
    unittest.main()
