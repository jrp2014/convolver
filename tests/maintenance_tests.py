#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Behaviour tests for scripts/maintenance/ (CI-56), on throwaway repositories.

The contract under test: the maintenance scripts remove only what GitHub
still has (a merged pull request's head, or a commit in a GitHub branch or
pull request), change nothing without --apply, and keep everything else.
"""

import contextlib
import importlib.util
import io
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

MAINTENANCE = Path(__file__).resolve().parents[1] / 'scripts' / 'maintenance'
SLUG = 'owner/convolver'

spec = importlib.util.spec_from_file_location('delete_merged_branches', MAINTENANCE / 'delete_merged_branches.py')
branches_script = importlib.util.module_from_spec(spec)
spec.loader.exec_module(branches_script)

GIT_ENV = {
    'GIT_AUTHOR_NAME': 'Maintenance test',
    'GIT_AUTHOR_EMAIL': 'maintenance@example.invalid',
    'GIT_COMMITTER_NAME': 'Maintenance test',
    'GIT_COMMITTER_EMAIL': 'maintenance@example.invalid',
    'GIT_CONFIG_GLOBAL': os.devnull,
    'GIT_CONFIG_NOSYSTEM': '1',
}


def pull(number, ref, sha, *, state='closed', merged=True, repo=SLUG):
    return {
        'number': number,
        'state': state,
        'merged_at': '2026-11-27T00:00:00Z' if merged else None,
        'head': {'ref': ref, 'sha': sha, 'repo': {'full_name': repo} if repo else None},
    }


class Classify(unittest.TestCase):
    def verdict(self, branches, pulls):
        return branches_script.classify(branches, pulls, SLUG, {'main'})

    def test_only_a_merged_head_is_deleted(self):
        verdicts = self.verdict(
            {'done': 'a1', 'later': 'b2', 'open': 'c3', 'closed': 'd4', 'none': 'e5', 'main': 'f6'},
            [
                pull(1, 'done', 'a1'),
                pull(2, 'later', 'b0'),
                pull(3, 'open', 'c0'),
                pull(4, 'open', 'c3', state='open', merged=False),
                pull(5, 'closed', 'd4', merged=False),
                pull(6, 'main', 'f6'),
            ],
        )
        self.assertEqual(verdicts['done'][:2], ('delete', 1), 'a branch at its merged head must be deleted')
        self.assertEqual(verdicts['later'][0], 'keep', 'commits pushed after the merged head would be lost')
        self.assertEqual(verdicts['open'][0], 'keep', 'a branch with an open pull request must be kept')
        self.assertEqual(verdicts['closed'][:2], ('closed', 5), 'a closed, unmerged head waits for --closed')
        self.assertEqual(verdicts['none'][0], 'keep', 'a branch with no pull request must be kept')
        self.assertEqual(verdicts['main'][0], 'keep', 'the default branch must never be deleted')

    def test_a_reused_branch_follows_its_newest_pull_request(self):
        verdicts = self.verdict({'reused': 'b2'}, [pull(1, 'reused', 'a1'), pull(7, 'reused', 'b2')])
        self.assertEqual(
            verdicts['reused'][:2], ('delete', 7), 'the restore command must name the pull request at the tip'
        )

    def test_a_fork_with_the_same_branch_name_does_not_count(self):
        verdicts = self.verdict(
            {'patch': 'a1'}, [pull(1, 'patch', 'a1', repo='someone/convolver'), pull(2, 'patch', 'a1', repo=None)]
        )
        self.assertEqual(
            verdicts['patch'][0], 'keep', "a fork's pull request says nothing about this repository's branch"
        )

    def test_github_remote_urls(self):
        for url in (
            'https://github.com/owner/convolver.git',
            'git@github.com:owner/convolver.git',
            'https://github.com/owner/convolver',
            'ssh://git@github.com/owner/convolver.git/',
        ):
            self.assertEqual(branches_script.repo_slug(url), SLUG, url)
        self.assertIsNone(branches_script.repo_slug('/srv/git/convolver.git'))


class Repositories(unittest.TestCase):
    """A bare 'GitHub' with branches and pull request heads, and a clone of it."""

    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='convolver-maintenance-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.env = {**os.environ, **GIT_ENV, 'HOME': str(self.root / 'home')}
        (self.root / 'home').mkdir()
        self.origin = self.root / 'origin.git'
        self.git(self.root, 'init', '--bare', '-b', 'main', str(self.origin))
        self.clone = self.root / 'home' / 'convolver'
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(self.clone))
        (self.clone / '.gitignore').write_text('build/\n', encoding='utf-8')
        self.commit('main', 'start')
        self.git(self.clone, 'push', '--quiet', 'origin', 'main')

    def git(self, cwd, *args, check=True):
        return subprocess.run(['git', *args], cwd=cwd, env=self.env, text=True, capture_output=True, check=check)

    def commit(self, branch, name):
        if branch != 'main':
            self.git(self.clone, 'switch', '--quiet', '-c', branch, 'main')
        (self.clone / name).write_text(name, encoding='utf-8')
        self.git(self.clone, 'add', '.')
        self.git(self.clone, 'commit', '--quiet', '-m', name)
        sha = self.git(self.clone, 'rev-parse', 'HEAD').stdout.strip()
        if branch != 'main':
            self.git(self.clone, 'switch', '--quiet', 'main')
        return sha

    def push(self, branch, pull_number=None):
        if pull_number is not None:
            self.git(self.clone, 'push', '--quiet', 'origin', f'{branch}:refs/pull/{pull_number}/head')
        else:
            self.git(self.clone, 'push', '--quiet', '-u', 'origin', branch)

    def remote_heads(self):
        out = self.git(self.origin, 'for-each-ref', '--format=%(refname:strip=2)', 'refs/heads').stdout
        return set(out.split())

    def local_branches(self):
        return set(self.git(self.clone, 'for-each-ref', '--format=%(refname:short)', 'refs/heads').stdout.split())


class DeleteMergedBranches(Repositories):
    def run_script(self, pulls, *args):
        saved = (branches_script.repo_slug, branches_script.fetch_pulls, Path.cwd())
        branches_script.repo_slug = lambda _url: SLUG
        branches_script.fetch_pulls = lambda _slug: pulls
        os.environ.update(GIT_ENV)
        try:
            os.chdir(self.clone)
            with contextlib.redirect_stdout(io.StringIO()) as out:
                branches_script.main(list(args))
        finally:
            branches_script.repo_slug, branches_script.fetch_pulls = saved[0], saved[1]
            os.chdir(saved[2])
        return out.getvalue()

    def test_dry_run_then_apply(self):
        merged = self.commit('merged', 'a')
        self.push('merged')
        self.commit('open', 'b')
        self.push('open')
        closed = self.commit('closed', 'c')
        self.push('closed')
        pulls = [
            pull(1, 'merged', merged),
            pull(2, 'open', 'x', state='open', merged=False),
            pull(3, 'closed', closed, merged=False),
        ]
        everything = {'main', 'merged', 'open', 'closed'}

        self.run_script(pulls)
        self.assertEqual(self.remote_heads(), everything, 'a dry run must delete nothing')

        out = self.run_script(pulls, '--apply')
        self.assertEqual(self.remote_heads(), everything - {'merged'}, 'only the merged branch may be deleted')
        self.assertIn(
            f'git push origin {merged}:refs/heads/merged', out, 'each deletion must print its restore command'
        )
        log = (self.clone / '.git' / 'deleted-branches.log').read_text(encoding='utf-8')
        self.assertIn(f'merged {merged} #1', log, 'each deletion must be logged with its commit and pull request')

        self.run_script(pulls, '--apply', '--closed')
        self.assertEqual(
            self.remote_heads(), {'main', 'open'}, '--closed adds the closed, unmerged branch, nothing else'
        )


@unittest.skipUnless(shutil.which('bash'), 'workspace.sh needs bash')
class WorkspaceTidy(Repositories):
    def tidy(self, *args, answer=''):
        script = MAINTENANCE / 'workspace.sh'
        return subprocess.run(
            ['bash', str(script), 'tidy', '--repo', str(self.clone), *args],
            input=answer,
            env=self.env,
            text=True,
            capture_output=True,
            check=True,
        ).stdout

    def worktree(self, name, branch):
        path = self.root / 'home' / name
        self.git(self.clone, 'worktree', 'add', '--quiet', str(path), branch)
        return path

    def test_removes_only_what_github_has(self):
        self.commit('merged', 'a')
        self.push('merged', 1)  # a pull request's head; its branch is gone from GitHub
        self.commit('local-only', 'b')  # never pushed
        self.commit('open', 'c')
        self.push('open')  # still a branch on GitHub
        self.commit('finished', 'd')
        self.push('finished', 2)
        finished = self.worktree('wt-finished', 'finished')
        (finished / 'build').mkdir()
        (finished / 'build' / 'out.o').write_text('ignored build output', encoding='utf-8')
        self.commit('changed', 'e')
        self.push('changed', 3)
        changed = self.worktree('wt-changed', 'changed')
        (changed / 'notes.txt').write_text('untracked work', encoding='utf-8')
        branches = self.local_branches()

        self.tidy()
        self.assertEqual(self.local_branches(), branches, 'tidy without --apply must change nothing')
        self.assertTrue(finished.exists(), 'tidy without --apply must change nothing')

        self.tidy('--apply', answer='yes\n')
        self.assertEqual(
            self.local_branches(),
            {'main', 'local-only', 'open', 'changed'},
            'only branches GitHub has, and not checked out in a kept worktree, may be deleted',
        )
        self.assertFalse(finished.exists(), 'a clean worktree whose commit GitHub has must be removed')
        self.assertTrue((changed / 'notes.txt').exists(), 'a worktree with untracked files must be kept')
        bundles = list((self.root / 'home').glob('convolver-retired-*/convolver-local-refs.bundle'))
        self.assertEqual(len(bundles), 1, 'tidy --apply must save every local branch before removing any')
        heads = self.git(self.clone, 'bundle', 'list-heads', str(bundles[0])).stdout
        self.assertIn('refs/heads/merged', heads, 'the backup must hold the branches tidy removed')

    def test_refusing_the_prompt_changes_nothing(self):
        self.commit('merged', 'a')
        self.push('merged', 1)
        with self.assertRaises(subprocess.CalledProcessError):
            self.tidy('--apply', answer='no\n')
        self.assertIn('merged', self.local_branches(), 'answering anything but yes must change nothing')


if __name__ == '__main__':
    unittest.main()
