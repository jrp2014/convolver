# SPDX-License-Identifier: GPL-2.0-or-later
"""Untrusted or incomplete sources cannot enter a corresponding-source build."""
import hashlib
import importlib.util
from pathlib import Path
import sys
import tarfile
import tempfile
import io
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))


class SourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / 'scripts/release/sources.py'
        assert path.is_file(), 'verified source acquisition is missing'
        spec = importlib.util.spec_from_file_location('sources', path)
        cls.sources = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.sources)

    def test_wrong_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            file = Path(temporary) / 'dep.tar.gz'
            file.write_bytes(b'not the pinned source')
            with self.assertRaisesRegex(ValueError, 'hash'):
                self.sources.verify(file, {'sha256': '0' * 64})

    def test_missing_archive_no_implicit_fetch(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, 'missing'):
                self.sources.acquire({'archive': 'absent.tar.gz', 'sha256': '0'*64}, Path(temporary), False)

    def test_path_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / 'bad.tar'
            with tarfile.open(archive, 'w') as tar:
                info = tarfile.TarInfo('../escape')
                info.size = 1
                tar.addfile(info, io.BytesIO(b'x'))
            with self.assertRaisesRegex(ValueError, 'unsafe'):
                self.sources.extract(archive, Path(temporary) / 'out')
            self.assertFalse((Path(temporary) / 'escape').exists())

    def test_archive_extract(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / 'good.tar'
            with tarfile.open(archive, 'w') as tar:
                info = tarfile.TarInfo('root/LICENSE')
                info.size = 5
                tar.addfile(info, io.BytesIO(b'legal'))
            self.sources.verify(archive, {'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
            self.sources.extract(archive, root / 'out')
            self.assertEqual((root / 'out/root/LICENSE').read_bytes(), b'legal')


if __name__ == '__main__':
    unittest.main()
