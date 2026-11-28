# SPDX-License-Identifier: GPL-2.0-or-later
"""Untrusted or incomplete sources cannot enter a corresponding-source build."""

import contextlib
import hashlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

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
        with tempfile.TemporaryDirectory() as temporary, self.assertRaisesRegex(ValueError, 'missing'):
            self.sources.acquire({'archive': 'absent.tar.gz', 'sha256': '0' * 64}, Path(temporary), fetch=False)

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
        cases = (
            ('root/escape', '../../outside'),
            ('root/absolute', '/etc/passwd'),
            ('root/directory', 'LICENSES'),
            ('root/missing', 'absent'),
            ('root/drive', 'C:/x'),
        )
        for name, link in cases:
            with self.subTest(link=link), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                with self.assertRaisesRegex(ValueError, 'unsafe'):
                    self.sources.extract(self._archive_with_link(root, name, link), root / 'out')
                self.assertFalse((root / 'out').exists() and any((root / 'out').iterdir()))


class AssembleTests(unittest.TestCase):
    """build_sources.py (REL-18). CI always assembles from a clean checkout of
    the tag with --latest, and the candidates then check the archive against
    its own manifest and the dependency archives they build against their
    records. Nothing there would notice an archive that carries a different
    commit's code, policy or VERSION than its manifest names, or other
    dependency releases than the lock file asked for, so these cases are
    checked here."""

    def setUp(self):
        import build_sources
        import candidate
        import sources

        self.build_sources, self.candidate, self.sources = build_sources, candidate, sources
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        # A stand-in repository holding the real policy and VERSION, so the
        # policy is valid, and one ordinary text file, with the repository's
        # own line-ending attributes. Neither the developer's Git settings
        # nor a hook's GIT_* variables reach it.
        self.environment = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
        self.environment.pop('GITHUB_STEP_SUMMARY', None)
        self.environment.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1')
        self.repo = self.root / 'repo'
        (self.repo / 'packaging').mkdir(parents=True)
        self.policy_path = self.repo / 'packaging/release-policy.json'
        self.policy_path.write_bytes((ROOT / 'packaging/release-policy.json').read_bytes())
        (self.repo / 'VERSION').write_bytes((ROOT / 'VERSION').read_bytes())
        (self.repo / 'README.md').write_bytes(b'committed\n')
        (self.repo / '.gitattributes').write_bytes(b'* text=auto\n')
        self.git('init', '-q')
        self.git('add', '.')
        self.git(
            '-c',
            'user.name=Test',
            '-c',
            'user.email=test@example.invalid',
            '-c',
            'commit.gpgsign=false',
            'commit',
            '-q',
            '--no-verify',
            '-m',
            'fixture',
        )
        self.commit = self.git('rev-parse', 'HEAD')
        # A lock file naming one recorded archive per dependency, each in the cache.
        self.cache = self.root / 'cache'
        self.cache.mkdir()
        policy = json.loads(self.policy_path.read_text(encoding='utf-8'))
        self.records = {}
        for name, rule in policy['dependencies'].items():
            archive = f'{name}-1.0.tar.gz'
            (self.cache / archive).write_bytes(f'{name} 1.0 source'.encode())
            self.records[name] = {
                'version': '1.0',
                'tag': 'v1.0',
                'revision': hashlib.sha256(name.encode()).hexdigest()[:40],
                'url': f'https://example.invalid/{archive}',
                'license': rule['license'],
                'archive': archive,
                'directory': rule['directory'],
                'sha256': hashlib.sha256((self.cache / archive).read_bytes()).hexdigest(),
                **({'formats': rule['formats']} if 'formats' in rule else {}),
            }
        self.lock = self.root / 'lock.json'
        self.lock.write_text(
            json.dumps({'schema': 1, 'resolved_at': '2026-11-28T00:00:00Z', 'dependencies': self.records})
        )
        self.output = self.root / 'out/Convolver-source.tar.gz'

    def git(self, *arguments):
        return subprocess.run(
            ['git', '-C', str(self.repo), *arguments], check=True, capture_output=True, text=True, env=self.environment
        ).stdout.strip()

    def assemble(self, **options):
        # The summary goes to stdout and, in CI, to the job summary: keep both quiet.
        with mock.patch.dict(os.environ, self.environment, clear=True), contextlib.redirect_stdout(io.StringIO()):
            self.build_sources.assemble(self.policy_path, self.cache, self.output, lock=self.lock, **options)

    def assert_refused(self, message, **options):
        with self.assertRaisesRegex(ValueError, message):
            self.assemble(**options)
        self.assertFalse(self.output.exists(), 'a refused assembly must leave no source archive')

    def test_assembles_the_commit_and_exactly_the_locked_releases(self):
        # A dirty checkout: neither an edit nor an untracked file belongs in
        # the source of the commit the manifest names.
        (self.repo / 'README.md').write_bytes(b'uncommitted edit\n')
        (self.repo / 'untracked.txt').write_bytes(b'not in the commit\n')
        # Git for Windows' default: the archive keeps the endings
        # .gitattributes sets, not the CRLF a checkout there would get.
        self.git('config', 'core.autocrlf', 'true')
        self.assemble()
        unpacked = self.root / 'unpacked'
        self.sources.extract(self.output, unpacked)
        bundle = unpacked / 'Convolver-source'
        # The candidates' own check of the archive accepts it.
        manifest = self.candidate.checked_manifest(bundle)
        self.assertEqual(manifest['source_commit'], self.commit)
        self.assertEqual((bundle / 'convolver/SOURCE_COMMIT').read_bytes(), self.commit.encode() + b'\n')
        self.assertEqual(manifest['version'], (ROOT / 'VERSION').read_text(encoding='utf-8').strip())
        self.assertEqual((bundle / 'convolver/README.md').read_bytes(), b'committed\n')
        self.assertFalse((bundle / 'convolver/untracked.txt').exists())
        self.assertEqual(manifest['resolved_at'], '2026-11-28T00:00:00Z')
        self.assertEqual(manifest['dependencies'], self.records)
        self.assertEqual(
            {p.name: p.read_bytes() for p in (bundle / 'dependencies').iterdir()},
            {r['archive']: (self.cache / r['archive']).read_bytes() for r in self.records.values()},
        )

    def test_policy_or_version_not_in_the_commit(self):
        # The archive would carry the committed policy and VERSION while its
        # manifest named the working tree's.
        policy = json.loads(self.policy_path.read_text(encoding='utf-8'))
        policy['dependencies']['lv2']['license'] = 'an uncommitted edit'
        for path, text in (
            (self.policy_path, json.dumps(policy)),
            (self.repo / 'VERSION', '99.0.0\n'),
        ):
            with self.subTest(changed=path.name):
                original = path.read_bytes()
                path.write_text(text, encoding='utf-8')
                try:
                    self.assert_refused(f'release policy or VERSION differs from source commit {self.commit}')
                finally:
                    path.write_bytes(original)

    def test_lock_with_requested_versions(self):
        # Otherwise the requested release would be silently ignored.
        self.assert_refused('with --lock the records decide', wanted={'lv2': '1.18.10'})

    def test_lock_missing_a_dependency(self):
        del self.records['lv2']
        self.lock.write_text(json.dumps({'schema': 1, 'dependencies': self.records}))
        self.assert_refused(r"no record for dependencies: \['lv2'\]")

    def test_cached_archive_not_the_locked_release(self):
        (self.cache / 'lv2-1.0.tar.gz').write_bytes(b'another release')
        self.assert_refused('source hash mismatch: lv2-1.0.tar.gz')


class ZipTests(unittest.TestCase):
    """candidate.extract_zip unpacks downloaded tools (NSIS) and candidate ZIPs."""

    @classmethod
    def setUpClass(cls):
        from candidate import extract_zip

        cls.extract_zip = staticmethod(extract_zip)

    def make_zip(self, path, *names):
        with zipfile.ZipFile(path, 'w') as package:
            for name in names:
                package.writestr(name, b'x')
        return path

    def test_extract(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.extract_zip(self.make_zip(root / 'good.zip', 'tool/bin/run.exe', 'tool/README'), root / 'out')
            self.assertEqual((root / 'out/tool/bin/run.exe').read_bytes(), b'x')
            self.assertTrue((root / 'out/tool/README').is_file())

    def test_unsafe_names(self):
        # A name starting with / is rooted on Windows too, where Path() alone
        # would not call it absolute and the join would leave the destination.
        names = ['/escape', '../escape', 'a/../../escape', 'C:escape']
        if sys.platform != 'win32':
            # zipfile on Windows turns a backslash into / when it writes or
            # reads a name, so there the name is a/escape, which is safe.
            names.append('a\\escape')
        for name in names:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                with self.assertRaisesRegex(ValueError, 'unsafe ZIP member'):
                    self.extract_zip(self.make_zip(root / 'bad.zip', name), root / 'out')
                self.assertFalse((root / 'escape').exists())

    @unittest.skipIf(sys.platform == 'win32', 'creating a symlink needs extra rights on Windows')
    def test_existing_link_out_of_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'out').mkdir()
            (root / 'elsewhere').mkdir()
            (root / 'out/link').symlink_to(root / 'elsewhere', target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'unsafe ZIP member outside'):
                self.extract_zip(self.make_zip(root / 'bad.zip', 'link/escape'), root / 'out')
            self.assertFalse((root / 'elsewhere/escape').exists())


if __name__ == '__main__':
    unittest.main()
