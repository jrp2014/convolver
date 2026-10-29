#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise automatic versioning in disposable Git repositories."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/version_source.py'


class GitFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='convolver-version-')
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name) / 'repo'
        self.repo.mkdir()
        self.git('init', '-b', 'main')
        self.git('config', 'user.name', 'Version test')
        self.git('config', 'user.email', 'version@example.invalid')
        self.git('config', 'commit.gpgsign', 'false')
        self.git('config', 'tag.gpgsign', 'false')
        self.write('VERSION', '0.2.0\n')
        self.write('portable/example.cpp', '// initial source\n')
        self.commit()
        self.git('tag', '-a', 'v0.2.0', '-m', 'Existing portable baseline')

    def git(self, *args, check=True):
        return subprocess.run(['git', *args], cwd=self.repo, text=True, capture_output=True, check=check).stdout.strip()

    def write(self, name, text):
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def commit(self):
        self.git('add', '.')
        self.git('commit', '-m', 'Change fixture')

    def run_version(self, *args, ok=True):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), *args], cwd=self.repo, text=True, capture_output=True, check=False
        )
        self.assertEqual(result.returncode == 0, ok, result.stdout + result.stderr)
        if not ok:
            self.assertIn('Versioning failed:', result.stderr)
        return result


class VersioningTests(GitFixture):
    def test_source_change_commits_version_and_annotated_tag_once(self):
        self.write('portable/example.cpp', '// changed source\n')
        self.commit()
        source = self.git('rev-parse', 'HEAD')
        self.run_version()
        self.assertEqual((self.repo / 'VERSION').read_text(), '0.2.1\n')
        self.assertEqual(self.git('cat-file', '-t', 'v0.2.1'), 'tag')
        self.assertEqual(self.git('rev-parse', 'v0.2.1^{}'), self.git('rev-parse', 'HEAD'))
        self.assertEqual(self.git('rev-parse', 'HEAD^'), source)
        self.assertEqual(self.git('diff', '--name-only', 'HEAD^', 'HEAD'), 'VERSION')
        self.assertIn(source, self.git('for-each-ref', '--format=%(contents)', 'refs/tags/v0.2.1'))
        head = self.git('rev-parse', 'HEAD')
        self.run_version()
        self.assertEqual(self.git('rev-parse', 'HEAD'), head)

    def test_documentation_only_change_does_not_bump(self):
        self.write('docs/guide.md', 'Documentation\n')
        self.commit()
        head = self.git('rev-parse', 'HEAD')
        self.run_version()
        self.assertEqual(self.git('rev-parse', 'HEAD'), head)
        self.assertEqual(self.git('tag', '--list', 'v0.2.1'), '')

    def test_explicit_minor_version_is_respected(self):
        self.write('VERSION', '0.3.0\n')
        self.commit()
        head = self.git('rev-parse', 'HEAD')
        self.run_version()
        self.assertEqual(self.git('rev-parse', 'v0.3.0^{}'), head)
        self.assertEqual((self.repo / 'VERSION').read_text(), '0.3.0\n')

    def test_reverted_source_changes_still_receive_a_version(self):
        self.write('portable/example.cpp', '// intermediate\n')
        self.commit()
        self.write('portable/example.cpp', '// initial source\n')
        self.commit()
        self.run_version()
        self.assertEqual((self.repo / 'VERSION').read_text(), '0.2.1\n')

    def test_dirty_or_wrong_branch_checkout_is_rejected(self):
        self.write('portable/example.cpp', '// unsaved change\n')
        self.run_version(ok=False)
        self.assertEqual((self.repo / 'VERSION').read_text(), '0.2.0\n')
        self.commit()
        self.git('checkout', '-b', 'feature')
        self.run_version(ok=False)
        self.assertEqual((self.repo / 'VERSION').read_text(), '0.2.0\n')

    def test_malformed_or_regressed_version_is_rejected(self):
        for value in ('0.01.0', '0.1.0', '0.3.0-beta.1'):
            self.write('VERSION', value + '\n')
            self.commit()
            self.run_version(ok=False)
            self.assertEqual((self.repo / 'VERSION').read_text(), value + '\n')

    def test_conflicting_tag_is_never_moved(self):
        self.git('checkout', '-b', 'other')
        self.write('portable/example.cpp', '// other branch\n')
        self.commit()
        self.git('tag', '-a', 'v0.2.1', '-m', 'Do not overwrite')
        existing = self.git('rev-parse', 'v0.2.1')
        self.git('checkout', 'main')
        self.write('portable/example.cpp', '// main branch\n')
        self.commit()
        self.run_version(ok=False)
        self.assertEqual(self.git('rev-parse', 'v0.2.1'), existing)
        self.assertEqual((self.repo / 'VERSION').read_text(), '0.2.0\n')

    def test_atomic_push_and_rejected_concurrent_push(self):
        remote = Path(self.temp.name) / 'remote.git'
        self.git('init', '--bare', str(remote))
        self.git('remote', 'add', 'origin', str(remote))
        self.git('push', 'origin', 'main', '--tags')
        self.write('cmake/example.cmake', '# source build change\n')
        self.commit()
        self.git('push', 'origin', 'main')
        self.run_version('--push')
        self.assertIn(self.git('rev-parse', 'HEAD'), self.git('ls-remote', 'origin', 'refs/heads/main'))
        self.assertIn(self.git('rev-parse', 'v0.2.1'), self.git('ls-remote', 'origin', 'refs/tags/v0.2.1'))
        # An upstream change must reject both refs, never leave a detached tag.
        clone = Path(self.temp.name) / 'other'
        self.git('clone', '--branch', 'main', str(remote), str(clone))
        subprocess.run(
            [
                'git',
                '-C',
                str(clone),
                '-c',
                'user.name=Other',
                '-c',
                'user.email=other@example.invalid',
                '-c',
                'commit.gpgsign=false',
                'commit',
                '--allow-empty',
                '-m',
                'Concurrent push',
            ],
            check=True,
            capture_output=True,
        )
        subprocess.run(['git', '-C', str(clone), 'push', 'origin', 'main'], check=True, capture_output=True)
        self.write('portable/example.cpp', '// local next source\n')
        self.commit()
        self.run_version('--push', ok=False)
        self.assertEqual(self.git('ls-remote', 'origin', 'refs/tags/v0.2.2'), '')

    def test_server_rejection_cannot_publish_only_the_tag(self):
        remote = Path(self.temp.name) / 'remote.git'
        self.git('init', '--bare', str(remote))
        self.git('remote', 'add', 'origin', str(remote))
        self.git('push', 'origin', 'main', '--tags')
        self.write('portable/example.cpp', '// next source\n')
        self.commit()
        self.git('push', 'origin', 'main')
        source = self.git('rev-parse', 'HEAD')
        hook = remote / 'hooks/update'
        # Git for Windows runs hooks with sh, which also needs an LF shebang.
        with hook.open('w', encoding='utf-8', newline='\n') as stream:
            stream.write('#!/bin/sh\n[ "$1" != "refs/heads/main" ]\n')
        hook.chmod(0o755)
        self.run_version('--push', ok=False)
        self.assertIn(source, self.git('ls-remote', 'origin', 'refs/heads/main'))
        self.assertEqual(self.git('ls-remote', 'origin', 'refs/tags/v0.2.1'), '')

    def test_shallow_checkout_is_rejected(self):
        clone = Path(self.temp.name) / 'shallow'
        self.git('clone', '--depth=1', self.repo.as_uri(), str(clone))
        self.repo = clone
        self.run_version(ok=False)


CMAKE = os.environ.get('CONVOLVER_TEST_CMAKE') or shutil.which('cmake')


@unittest.skipUnless(CMAKE, 'CMake is not on PATH')
class BuildVersionTests(GitFixture):
    def setUp(self):
        super().setUp()
        source = SCRIPT.parent.parent / 'cmake'
        (self.repo / 'cmake').mkdir()
        for name in ('ConvolverBuildVersion.cmake', 'convolver_version.hpp.in'):
            shutil.copyfile(source / name, self.repo / 'cmake' / name)
        self.commit()
        self.build = Path(self.temp.name) / 'build'

    def generate(self, source=None, *options):
        source = source or self.repo
        subprocess.run(
            [
                CMAKE,
                '-DCONVOLVER_SOURCE_DIR=' + str(source),
                '-DCONVOLVER_BINARY_DIR=' + str(self.build),
                *options,
                '-P',
                str(source / 'cmake/ConvolverBuildVersion.cmake'),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        return (self.build / 'generated/convolver_version.hpp').read_text()

    def test_identity_tracks_commits_and_uncommitted_edits(self):
        revision = self.git('rev-parse', '--short=12', 'HEAD')
        self.assertIn('"0.2.0+g' + revision + '"', self.generate())
        self.write('portable/example.cpp', '// local edit\n')
        self.assertIn('"0.2.0+g' + revision + '.dirty"', self.generate())
        self.commit()
        revision = self.git('rev-parse', '--short=12', 'HEAD')
        self.assertIn('"0.2.0+g' + revision + '"', self.generate())

    def test_archive_ignores_parent_repository_identity(self):
        archive = self.repo / 'archive'
        shutil.copytree(self.repo / 'cmake', archive / 'cmake')
        shutil.copyfile(self.repo / 'VERSION', archive / 'VERSION')
        self.assertIn('"0.2.0"', self.generate(archive))

    def test_git_is_optional(self):
        self.assertIn('"0.2.0"', self.generate(None, '-DCMAKE_DISABLE_FIND_PACKAGE_Git=ON'))


if __name__ == '__main__':
    unittest.main()
