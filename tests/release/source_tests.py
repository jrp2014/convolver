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

    def _archive_with_link(self, root, name, link):
        archive = root / 'linked.tar'
        with tarfile.open(archive, 'w') as tar:
            info = tarfile.TarInfo('root/COPYING')
            info.size = 5
            tar.addfile(info, io.BytesIO(b'legal'))
            directory = tarfile.TarInfo('root/LICENSES')
            directory.type = tarfile.DIRTYPE
            tar.addfile(directory)
            symlink = tarfile.TarInfo(name)
            symlink.type = tarfile.SYMTYPE
            symlink.linkname = link
            tar.addfile(symlink)
        return archive

    def test_internal_file_link_becomes_copy(self):
        # lv2 1.18.10 has LICENSES/ISC.txt -> ../COPYING.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.sources.extract(self._archive_with_link(root, 'root/LICENSES/ISC.txt', '../COPYING'), root / 'out')
            copy = root / 'out/root/LICENSES/ISC.txt'
            self.assertFalse(copy.is_symlink())
            self.assertEqual(copy.read_bytes(), b'legal')

    def _archive(self, root, entries):
        archive = root / 'crafted.tar'
        with tarfile.open(archive, 'w') as tar:
            for name, link in entries:
                info = tarfile.TarInfo(name)
                if link is None:
                    info.size = 5
                    tar.addfile(info, io.BytesIO(b'legal'))
                else:
                    info.type = tarfile.SYMTYPE
                    info.linkname = link
                    tar.addfile(info)
        return archive

    def test_repeated_or_self_referring_paths(self):
        cases = {
            # A later link shadows a regular file another link points at.
            'shadowed chain': [('root/B', None), ('root/C', None), ('root/B', 'C'), ('root/A', 'B')],
            'self link': [('root/COPYING', None), ('root/COPYING', 'COPYING')],
            'unnormalised duplicate': [('root/COPYING', None), ('root/./COPYING', None)],
            'link to link': [('root/COPYING', None), ('root/B', 'COPYING'), ('root/A', 'B')],
        }
        for label, entries in cases.items():
            with self.subTest(label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                with self.assertRaisesRegex(ValueError, 'unsafe'):
                    self.sources.extract(self._archive(root, entries), root / 'out')

    def test_unnormalised_link_target(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = self._archive(root, [('root//COPYING', None), ('root/A', './COPYING')])
            self.sources.extract(archive, root / 'out')
            self.assertEqual((root / 'out/root/A').read_bytes(), b'legal')

    def test_unsafe_links(self):
        cases = (('root/escape', '../../outside'), ('root/absolute', '/etc/passwd'),
                 ('root/directory', 'LICENSES'), ('root/missing', 'absent'), ('root/drive', 'C:/x'))
        for name, link in cases:
            with self.subTest(link=link), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                with self.assertRaisesRegex(ValueError, 'unsafe'):
                    self.sources.extract(self._archive_with_link(root, name, link), root / 'out')
                self.assertFalse((root / 'out').exists() and any((root / 'out').iterdir()))


if __name__ == '__main__':
    unittest.main()
