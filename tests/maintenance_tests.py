#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Behaviour tests for scripts/maintenance/ (CI-56, CI-57), on throwaway repositories.

The contract under test: the maintenance scripts remove automatically only
what a merged pull request accounts for (that branch at that exact commit),
remove nothing that changed since the plan was shown, list for review what
is merely recoverable, change nothing without --apply, keep everything else,
and let a retired folder expire only once it holds what its backup recorded.
"""

import contextlib
import importlib.util
import io
import json
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

# A commit hook exports GIT_INDEX_FILE (and, in a linked worktree, GIT_DIR);
# left in place, the throwaway repositories' commands would rewrite the real
# index. Nothing here needs them, so drop them for the whole test process.
for name in [name for name in os.environ if name.startswith('GIT_')]:
    del os.environ[name]

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

    def test_the_base_of_an_open_pull_request_is_kept(self):
        stacked = pull(2, 'top', 'b2', state='open', merged=False)
        stacked['base'] = {'ref': 'bottom'}
        verdicts = self.verdict({'bottom': 'a1', 'top': 'b2'}, [pull(1, 'bottom', 'a1'), stacked])
        self.assertEqual(verdicts['bottom'][0], 'keep', 'deleting the base of an open pull request would close it')

    def test_every_page_of_pull_requests_is_read(self):
        pages = [[{'number': n} for n in range(100)], [{'number': n} for n in range(100, 200)], [{'number': 200}]]
        asked = []

        def api(path):
            asked.append(path)
            return pages[len(asked) - 1]

        saved = branches_script.api
        branches_script.api = api
        try:
            pulls = branches_script.fetch_pulls(SLUG)
        finally:
            branches_script.api = saved
        self.assertEqual(len(pulls), 201, 'a pull request on a later page must not be missed')
        self.assertEqual(len(asked), 3, 'reading stops at the first short page')

    def test_merged_heads_lists_only_this_repositorys_merged_pull_requests(self):
        heads = branches_script.merged_heads(
            [
                pull(1, 'done', 'a1'),
                pull(2, 'open', 'b2', state='open', merged=False),
                pull(3, 'fork', 'c3', repo='x/y'),
            ],
            SLUG,
        )
        self.assertEqual(heads, [('done', 'a1', 1)], 'tidy must see only merged pull requests from this repository')

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
        self.pulls = []
        self.pulls_file = self.root / 'pulls.json'
        self.save_pulls()
        self.env = {
            **os.environ,
            **GIT_ENV,
            'HOME': str(self.root / 'home'),
            'CONVOLVER_PULLS_JSON': str(self.pulls_file),
        }
        self.env.pop('RETIRE', None)
        (self.root / 'home').mkdir()
        self.origin = self.root / 'origin.git'
        self.git(self.root, 'init', '--bare', '-b', 'main', str(self.origin))
        self.clone = self.root / 'home' / 'convolver'
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(self.clone))
        (self.clone / '.gitignore').write_text('build/\nfuzz-out/\n*.log\n', encoding='utf-8')
        self.commit('main', 'start')
        self.git(self.clone, 'push', '--quiet', 'origin', 'main')

    def save_pulls(self):
        self.pulls_file.write_text(json.dumps({'repository': SLUG, 'pulls': self.pulls}), encoding='utf-8')

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

    def push(self, branch, pull_number=None, *, merged=True):
        """Push BRANCH as a branch, or as pull request PULL_NUMBER's head (merged unless told otherwise)."""
        if pull_number is not None:
            self.git(self.clone, 'push', '--quiet', 'origin', f'{branch}:refs/pull/{pull_number}/head')
            sha = self.git(self.clone, 'rev-parse', branch).stdout.strip()
            self.pulls.append(pull(pull_number, branch, sha, state='closed' if merged else 'open', merged=merged))
            self.save_pulls()
        else:
            self.git(self.clone, 'push', '--quiet', '-u', 'origin', branch)

    def remote_heads(self):
        out = self.git(self.origin, 'for-each-ref', '--format=%(refname:strip=2)', 'refs/heads').stdout
        return set(out.split())

    def local_branches(self):
        return set(self.git(self.clone, 'for-each-ref', '--format=%(refname:short)', 'refs/heads').stdout.split())


class DeleteMergedBranches(Repositories):
    def run_script(self, pulls, *args):
        names = ('repo_slug', 'fetch_pulls', 'default_branch')
        saved = {name: getattr(branches_script, name) for name in names}
        cwd = Path.cwd()
        branches_script.repo_slug = lambda _url: SLUG
        branches_script.fetch_pulls = lambda _slug: pulls
        branches_script.default_branch = lambda _slug: 'main'
        os.environ.update(GIT_ENV)
        try:
            os.chdir(self.clone)
            with contextlib.redirect_stdout(io.StringIO()) as out:
                branches_script.main(list(args))
        finally:
            for name, value in saved.items():
                setattr(branches_script, name, value)
            os.chdir(cwd)
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

    def test_a_branch_that_moved_after_the_check_is_kept(self):
        checked = self.commit('moved', 'a')
        self.push('moved')
        self.git(self.clone, 'switch', '--quiet', 'moved')
        self.commit('main', 'pushed later')  # commits on the checked-out branch
        self.git(self.clone, 'push', '--quiet', 'origin', 'moved')
        self.git(self.clone, 'switch', '--quiet', 'main')
        self.git(self.clone, 'fetch', '--quiet', 'origin')
        os.environ.update(GIT_ENV)
        cwd = Path.cwd()
        try:
            os.chdir(self.clone)
            with contextlib.redirect_stdout(io.StringIO()):
                deleted = branches_script.delete('moved', checked, 1, self.root / 'log')
        finally:
            os.chdir(cwd)
        self.assertFalse(deleted, 'a branch pushed to after the check must not be deleted')
        self.assertIn('moved', self.remote_heads(), 'a branch pushed to after the check must not be deleted')


class PushRepository(Repositories):
    def push_repository(self):
        cwd = Path.cwd()
        os.environ.update(GIT_ENV)
        try:
            os.chdir(self.clone)
            return branches_script.push_repository(SLUG)
        finally:
            os.chdir(cwd)

    def test_a_push_url_for_another_repository_stops_the_deletes(self):
        self.git(self.clone, 'config', 'remote.origin.pushurl', 'https://github.com/someone/fork.git')
        with self.assertRaises(SystemExit, msg='branches must be deleted only where their pull requests were read'):
            self.push_repository()
        self.git(self.clone, 'config', 'remote.origin.pushurl', 'https://github.com/owner/convolver.git')
        self.assertEqual(self.push_repository(), 'https://github.com/owner/convolver.git')
        self.git(self.clone, 'config', '--add', 'remote.origin.pushurl', 'https://github.com/someone/fork.git')
        with self.assertRaises(SystemExit, msg='git push origin pushes to every push URL, so each must be checked'):
            self.push_repository()


@unittest.skipUnless(shutil.which('bash'), 'workspace.sh needs bash')
class Workspace(Repositories):
    def workspace(self, command, *args, answer='', repo=None, check=True):
        script = MAINTENANCE / 'workspace.sh'
        return subprocess.run(
            ['bash', str(script), command, '--repo', str(repo or self.clone), *args],
            input=answer,
            env=self.env,
            text=True,
            capture_output=True,
            check=check,
        )

    def tidy(self, *args, answer='', repo=None):
        return self.workspace('tidy', *args, answer=answer, repo=repo).stdout

    def worktree(self, name, branch, *options):
        path = self.root / 'home' / name
        self.git(self.clone, 'worktree', 'add', '--quiet', *options, str(path), branch)
        return path

    def retired(self):
        folders = list((self.root / 'home').glob('convolver-retired-*'))
        self.assertEqual(len(folders), 1)
        return folders[0]

    def test_tidy_removes_only_what_a_merged_pull_request_accounts_for(self):
        self.commit('merged', 'a')
        self.push('merged', 1)  # a pull request's head; its branch is gone from GitHub
        self.commit('recoverable', 'f')
        self.push('recoverable', 4, merged=False)  # GitHub has the commit, but nothing says it is finished
        self.commit('local-only', 'b')  # never pushed
        self.commit('open', 'c')
        self.push('open')  # still a branch on GitHub
        self.commit('finished', 'd')
        self.push('finished', 2)
        finished = self.worktree('wt-finished', 'finished')
        (finished / 'fuzz-out' / 'crashes').mkdir(parents=True)
        (finished / 'fuzz-out' / 'crashes' / 'id0').write_text('a crash input', encoding='utf-8')
        (finished / 'bench.log').write_text('ignored results', encoding='utf-8')
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
            {'main', 'local-only', 'open', 'changed', 'recoverable'},
            'only branches a merged pull request recorded at their tip, and not checked out in a kept worktree, may go',
        )
        self.assertFalse(finished.exists(), 'a clean worktree whose commit GitHub has must be removed')
        self.assertTrue((changed / 'notes.txt').exists(), 'a worktree with untracked files must be kept')
        kept = self.retired() / 'wt-finished-kept' / 'files'
        for name in ('fuzz-out/crashes/id0', 'bench.log'):
            self.assertTrue((kept / name).exists(), f'ignored {name} must be copied before its worktree goes')
        bundles = list(self.retired().glob('convolver-local-refs-*.bundle'))
        self.assertEqual(len(bundles), 1, 'tidy --apply must save every local branch before removing any')
        heads = self.git(self.clone, 'bundle', 'list-heads', str(bundles[0])).stdout
        self.assertIn('refs/heads/merged', heads, 'the backup must hold the branches tidy removed')

    def finished_worktree(self, path, branch, number, log):
        self.commit(branch, branch)
        self.push(branch, number)
        path.parent.mkdir(parents=True, exist_ok=True)
        self.git(self.clone, 'worktree', 'add', '--quiet', str(path), branch)
        (path / 'bench.log').write_text(log, encoding='utf-8')

    def test_worktrees_with_the_same_name_keep_their_own_copies(self):
        home = self.root / 'home'
        self.finished_worktree(home / 'group1' / 'done', 'one', 1, 'first log')
        self.finished_worktree(home / 'group2' / 'done', 'two', 2, 'second log')
        self.tidy('--apply', answer='yes\n')
        logs = sorted(p.read_text(encoding='utf-8') for p in self.retired().glob('done-kept*/files/bench.log'))
        self.assertEqual(logs, ['first log', 'second log'], "one worktree's copies must not overwrite another's")

    def test_a_failed_copy_removes_nothing(self):
        done = self.root / 'home' / 'done'
        self.finished_worktree(done, 'finished', 1, 'results')
        shim = self.root / 'shim'
        shim.mkdir()
        real_cp = shutil.which('cp')
        (shim / 'cp').write_text(
            f'#!/bin/sh\ncase "$*" in *bench.log*) exit 1 ;; esac\nexec {real_cp} "$@"\n', encoding='utf-8'
        )
        (shim / 'cp').chmod(0o755)
        self.env['PATH'] = f'{shim}{os.pathsep}{self.env["PATH"]}'
        self.tidy('--apply', answer='yes\n')
        self.assertTrue((done / 'bench.log').exists(), 'a worktree whose files could not be copied must be kept')
        self.assertIn('finished', self.local_branches(), 'the branch of a kept worktree must be kept')

    def test_tidy_from_a_worktree_leaves_the_main_checkout_alone(self):
        self.commit('finished', 'a')
        self.push('finished', 1)
        self.git(self.clone, 'switch', '--quiet', 'finished')  # the main checkout is on a finished branch
        elsewhere = self.worktree('wt-elsewhere', 'main')
        plan = self.tidy(repo=elsewhere)
        self.assertNotIn(f'REMOVE {self.clone}', plan, 'the main checkout must never be offered for removal')
        self.tidy('--apply', answer='yes\n', repo=elsewhere)
        self.assertIn('finished', self.local_branches(), "the main checkout's branch must be kept")
        self.assertTrue((self.clone / 'a').exists(), 'the main checkout must keep its files')
        self.assertTrue(elsewhere.exists(), 'the worktree tidy runs from must be kept')

    def test_refusing_the_prompt_changes_nothing(self):
        self.commit('merged', 'a')
        self.push('merged', 1)
        with self.assertRaises(subprocess.CalledProcessError):
            self.tidy('--apply', answer='no\n')
        self.assertIn('merged', self.local_branches(), 'answering anything but yes must change nothing')

    def test_backups_on_the_same_day_are_all_kept(self):
        self.workspace('backup')
        self.workspace('backup')
        bundles = list(self.retired().glob('convolver-local-refs-*.bundle'))
        self.assertEqual(len(bundles), 2, 'a second backup must not overwrite the first')

    def test_retire_dir_keeps_branches_and_stashes_that_github_lacks(self):
        old = self.root / 'home' / 'old-clone'
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(old))
        self.git(old, 'switch', '--quiet', '-c', 'secret')
        (old / 'work.txt').write_text('committed only here', encoding='utf-8')
        self.git(old, 'add', '.')
        self.git(old, 'commit', '--quiet', '-m', 'secret work')
        (old / 'work.txt').write_text('stashed only here', encoding='utf-8')
        self.git(old, 'stash', '--quiet')

        self.workspace('retire-dir', str(old))
        self.assertTrue(old.exists(), 'retire-dir without --apply must change nothing')
        self.workspace('retire-dir', str(old), '--apply', answer='yes\n')
        self.assertFalse(old.exists(), 'retire-dir --apply moves the clone')
        heads = self.git(self.root, 'bundle', 'list-heads', str(self.retired() / 'old-clone-kept' / 'refs.bundle'))
        self.assertIn('refs/heads/secret', heads.stdout, 'a branch only in the clone must be saved')
        self.assertIn('refs/retired-stash/0', heads.stdout, 'a stash only in the clone must be saved')

    def test_retire_dir_refuses_the_main_checkout(self):
        elsewhere = self.worktree('wt-elsewhere', 'main', '--detach')
        result = self.workspace('retire-dir', str(self.clone), '--apply', answer='yes\n', repo=elsewhere, check=False)
        self.assertNotEqual(result.returncode, 0, 'the folder holding the Git data must never be retired')
        self.assertTrue((self.clone / '.git').exists(), 'the folder holding the Git data must never be retired')

    def test_empty_retired_deletes_only_dated_folders_over_a_week_old(self):
        home = self.root / 'home'
        for name in ('convolver-retired-2000-01-01', 'convolver-retired-2999-01-01', 'convolver-retired-0-scratch'):
            (home / name).mkdir()
        self.workspace('empty-retired', '--apply', answer='yes\n')
        left = sorted(p.name for p in home.glob('convolver-retired-*'))
        self.assertEqual(
            left,
            ['convolver-retired-0-scratch', 'convolver-retired-2999-01-01'],
            'only folders named with a date more than a week ago may be deleted',
        )

    def test_a_new_worktree_is_listed_for_review_not_removed(self):
        self.git(self.clone, 'branch', 'starting', 'main')  # about to start work: no edits yet
        fresh = self.worktree('wt-starting', 'starting')
        plan = self.tidy()
        review = plan.split('Review', 1)[-1]
        self.assertIn(str(fresh), review, 'a recoverable but unfinished worktree must be listed for review')
        self.tidy('--apply', answer='yes\n')
        self.assertTrue(fresh.exists(), 'a worktree no merged pull request accounts for must not be removed')
        self.assertIn('starting', self.local_branches(), 'its branch must not be deleted either')

    def test_a_worktree_changed_after_the_plan_is_kept(self):
        done = self.root / 'home' / 'done'
        self.finished_worktree(done, 'finished', 1, 'results')
        script = MAINTENANCE / 'workspace.sh'
        with subprocess.Popen(
            ['bash', str(script), 'tidy', '--repo', str(self.clone), '--apply'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=self.env,
        ) as process:
            shown = b''
            while not shown.endswith(b'[type yes to go on] '):
                byte = process.stdout.read(1)
                self.assertTrue(byte, f'tidy ended before asking: {shown.decode(errors="replace")}')
                shown += byte
            self.assertIn(str(done).encode(), shown, 'the worktree must be in the plan')
            # While the prompt waits, the worktree moves to a commit nothing else holds.
            (done / 'new.txt').write_text('new work', encoding='utf-8')
            self.git(done, 'add', 'new.txt')
            self.git(done, 'commit', '--quiet', '-m', 'new work')
            self.git(done, 'switch', '--quiet', '--detach')
            rest, _ = process.communicate(b'yes\n', timeout=60)
        self.assertTrue((done / 'new.txt').exists(), 'a worktree that changed after the plan must be kept')
        self.assertIn(b'since the plan', rest, 'tidy must say why it kept the worktree')

    def test_an_in_source_build_is_listed_for_review(self):
        done = self.root / 'home' / 'insource'
        self.finished_worktree(done, 'finished', 1, 'results')
        (done / 'CMakeCache.txt').write_text('# in-source build', encoding='utf-8')
        (self.clone / '.git' / 'info').mkdir(exist_ok=True)
        (self.clone / '.git' / 'info' / 'exclude').write_text('CMakeCache.txt\nfuzz-crash\n', encoding='utf-8')
        (done / 'fuzz-crash').write_text('a crash input', encoding='utf-8')
        self.assertEqual(self.git(done, 'status', '--porcelain').stdout, '', 'the fixture must be clean')
        self.tidy('--apply', answer='yes\n')
        self.assertTrue((done / 'fuzz-crash').exists(), 'an in-source build must be kept for review')

    def test_an_unexpected_argument_is_refused(self):
        self.commit('merged', 'a')
        self.push('merged', 1)
        result = self.workspace('tidy', 'wrong-checkout-path', '--apply', answer='yes\n', check=False)
        self.assertNotEqual(result.returncode, 0, 'tidy must refuse an argument it does not take')
        self.assertIn('wrong-checkout-path', result.stderr)
        self.assertIn('merged', self.local_branches(), 'a refused command must change nothing')

    def test_a_deleted_branch_leaves_no_configuration(self):
        self.commit('merged', 'a')
        self.push('merged', 1)
        self.git(self.clone, 'config', 'branch.merged.remote', 'origin')
        self.git(self.clone, 'config', 'branch.merged.merge', 'refs/heads/merged')
        self.tidy('--apply', answer='yes\n')
        self.assertNotIn('merged', self.local_branches())
        left = self.git(self.clone, 'config', '--get-regexp', r'^branch\.merged\.', check=False).stdout
        self.assertEqual(left, '', "a deleted branch's configuration must go with it")

    def test_a_backup_that_cannot_hold_the_stashes_stops(self):
        (self.clone / 'start').write_text('stashed only here', encoding='utf-8')
        self.git(self.clone, 'stash', '--quiet')
        shim = self.root / 'shim'
        shim.mkdir()
        real_git = shutil.which('git')
        (shim / 'git').write_text(
            f'#!/bin/sh\ncase "$*" in *retired-stash/\\**) exit 1 ;; esac\nexec {real_git} "$@"\n', encoding='utf-8'
        )
        (shim / 'git').chmod(0o755)
        self.env['PATH'] = f'{shim}{os.pathsep}{self.env["PATH"]}'
        result = self.workspace('backup', check=False)
        self.assertNotEqual(result.returncode, 0, 'a backup must not report stashes it could not save')
        self.assertNotIn('Saved', result.stdout)

    def test_a_git_folder_as_the_checkout_is_refused(self):
        other = self.root / 'home' / 'other'
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(other))
        result = subprocess.run(
            [
                'bash',
                str(MAINTENANCE / 'workspace.sh'),
                'retire-dir',
                str(self.clone),
                '--repo',
                str(self.clone / '.git'),
            ],
            cwd=other,
            env=self.env,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(
            result.returncode, 0, '--repo naming a .git folder must not fall back to the current folder'
        )
        self.assertNotIn(f'Checkout: {other}', result.stdout)
        self.assertTrue(self.clone.exists())

    def test_retire_dir_refuses_a_clone_with_linked_worktrees(self):
        old = self.root / 'home' / 'old-clone'
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(old))
        linked = self.root / 'home' / 'old-linked'
        self.git(old, 'worktree', 'add', '--quiet', '-b', 'side', str(linked), 'main')
        result = self.workspace('retire-dir', str(old), '--apply', answer='yes\n', check=False)
        self.assertNotEqual(result.returncode, 0, 'moving a clone would break the worktrees whose Git data it holds')
        self.assertIn(str(linked), result.stderr)
        self.assertTrue((old / '.git').exists())

    def test_retire_dir_refuses_the_git_data(self):
        objects = self.clone / '.git' / 'objects'
        result = self.workspace('retire-dir', str(objects), '--apply', answer='yes\n', check=False)
        self.assertNotEqual(result.returncode, 0, "the active checkout's Git data must never be retired")
        self.assertTrue(objects.exists(), "the active checkout's Git data must never be retired")

    def verify_fails(self):
        result = self.workspace('verify-backup', check=False)
        self.assertNotEqual(result.returncode, 0, 'a missing or damaged backup must fail verify-backup')
        return result.stdout

    def retire_to(self, date):
        self.env['RETIRE'] = str(self.root / 'home' / f'convolver-retired-{date}')
        return Path(self.env['RETIRE'])

    def test_empty_retired_deletes_a_folder_that_verifies(self):
        self.commit('merged', 'a')
        self.push('merged', 1)
        retired = self.retire_to('2000-01-01')
        self.tidy('--apply', answer='yes\n')
        self.assertIn('OK', self.workspace('verify-backup').stdout)
        self.workspace('empty-retired', '--apply', answer='yes\n')
        self.assertFalse(retired.exists(), 'a week-old folder holding everything it recorded may be deleted')

    def test_empty_retired_keeps_a_folder_missing_what_it_recorded(self):
        self.commit('merged', 'a')
        self.push('merged', 1)
        retired = self.retire_to('2000-01-01')
        self.tidy('--apply', answer='yes\n')
        bundle = next(retired.glob('convolver-local-refs-*.bundle'))
        self.git(self.clone, 'bundle', 'create', str(bundle), 'main')  # the saved branch is gone from it
        report = self.verify_fails()
        self.assertIn('MISSING', report)
        self.assertIn('refs/heads/merged', report, 'verify-backup must name what is missing')
        self.workspace('empty-retired', '--apply', answer='yes\n')
        self.assertTrue(retired.exists(), 'a folder missing what its backup recorded must be kept')

    def test_a_damaged_bundle_is_not_trusted(self):
        self.commit('merged', 'a')
        self.push('merged', 1)
        retired = self.retire_to('2000-01-01')
        self.tidy('--apply', answer='yes\n')
        bundle = next(retired.glob('convolver-local-refs-*.bundle'))
        data = bundle.read_bytes()
        bundle.write_bytes(data[: len(data) - 30])  # the header, and so bundle verify, still pass
        report = self.verify_fails()
        self.assertIn(f'{bundle} has changed since it was made', report)
        self.workspace('empty-retired', '--apply', '--delete-local-work', answer='yes\n')
        self.assertTrue(retired.exists(), 'a bundle that changed after it was made must keep its folder')

        Path(f'{bundle}.hash').unlink()  # as a bundle made before the hash was recorded
        report = self.verify_fails()
        self.assertIn(f'{bundle} does not restore refs/heads/merged', report)
        self.workspace('empty-retired', '--apply', '--delete-local-work', answer='yes\n')
        self.assertTrue(retired.exists(), 'a bundle that does not restore must keep its folder')

    def test_a_backup_that_does_not_restore_stops(self):
        self.commit('merged', 'a')
        self.push('merged', 1)
        shim = self.root / 'shim'
        shim.mkdir()
        real_git = shutil.which('git')
        (shim / 'git').write_text(
            '#!/bin/sh\n'
            f'{real_git} "$@"; s=$?\n'
            'case "$*" in *"bundle create "*) ;; *) exit $s ;; esac\n'
            'f=; next=; for a; do if [ -n "$next" ]; then f=$a; break; fi; [ "$a" != create ] || next=1; done\n'
            'n=$(wc -c <"$f"); head -c $((n - 30)) "$f" >"$f.cut" && mv "$f.cut" "$f"\n'
            'exit $s\n',
            encoding='utf-8',
        )
        (shim / 'git').chmod(0o755)
        self.env['PATH'] = f'{shim}{os.pathsep}{self.env["PATH"]}'
        result = self.workspace('tidy', '--apply', answer='yes\n', check=False)
        self.assertNotEqual(result.returncode, 0, 'a backup that does not restore must stop tidy')
        self.assertIn('does not restore', result.stderr)
        self.assertIn('merged', self.local_branches(), 'nothing may be removed after a failed backup')

    def test_a_retired_clones_detached_commit_is_saved_as_local_work(self):
        old = self.root / 'home' / 'old-clone'
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(old))
        self.git(old, 'switch', '--quiet', '--detach')
        self.git(old, 'rm', '--quiet', 'start')
        self.git(old, 'commit', '--quiet', '-m', 'detached work')
        retired = self.retire_to('2000-01-01')
        self.workspace('retire-dir', str(old), '--apply', answer='yes\n')
        heads = self.git(self.root, 'bundle', 'list-heads', str(retired / 'old-clone-kept' / 'refs.bundle')).stdout
        self.assertIn('refs/retired-head/HEAD', heads, 'a commit only a detached HEAD names must be saved')
        report = self.workspace('verify-backup').stdout
        self.assertIn('only known copy', report)
        self.assertIn('retired-head/HEAD', report, 'verify-backup must name the detached commit')
        self.workspace('empty-retired', '--apply', answer='yes\n')
        self.assertTrue(retired.exists(), 'the only copy of a detached commit must not expire unasked')
        self.workspace('empty-retired', '--apply', '--delete-local-work', answer='yes\n')
        self.assertFalse(retired.exists(), '--delete-local-work lets it go')

    def test_a_backup_keeps_and_records_refs_it_did_not_make(self):
        self.commit('merged', 'a')
        self.push('merged', 1)
        restored = self.commit('elsewhere', 'only-here')
        self.git(self.clone, 'branch', '--quiet', '-D', 'elsewhere')
        self.git(self.clone, 'update-ref', 'refs/retired-head/HEAD', restored)
        self.git(self.clone, 'update-ref', 'refs/retired-stash/0', restored)
        (self.clone / 'start').write_text('stashed here', encoding='utf-8')
        self.git(self.clone, 'stash', '--quiet')
        self.git(self.clone, 'switch', '--quiet', '--detach')
        detached = self.git(self.clone, 'rev-parse', 'HEAD').stdout.strip()
        retired = self.retire_to('2000-01-01')
        self.tidy('--apply', answer='yes\n')
        left = self.git(self.clone, 'for-each-ref', '--format=%(refname)', 'refs/retired-head', 'refs/retired-stash')
        self.assertEqual(
            left.stdout.split(),
            ['refs/retired-head/HEAD', 'refs/retired-stash/0'],
            'refs the backup did not make must stay',
        )
        tips = next(retired.glob('convolver-local-refs-*.bundle.tips')).read_text(encoding='utf-8')
        self.assertIn(f'{restored} refs/retired-head/HEAD', tips)
        self.assertIn(f'{restored} refs/retired-stash/0', tips)
        self.assertIn('refs/retired-stash/1', tips, 'the stash must be saved under a name not yet taken')
        self.assertIn(f'{detached} refs/retired-head/HEAD-2', tips, 'so must the detached HEAD')
        report = self.workspace('verify-backup').stdout
        self.assertIn('retired-head/HEAD', report, 'a saved ref that exists nowhere else is local work')
        self.workspace('empty-retired', '--apply', answer='yes\n')
        self.assertTrue(retired.exists())

    def test_retire_dir_refuses_a_clone_whose_linked_worktree_is_not_there(self):
        old = self.root / 'home' / 'old-clone'
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(old))
        usb = self.root / 'home' / 'usb-worktree'
        self.git(old, 'worktree', 'add', '--quiet', '--detach', str(usb), 'main')
        self.git(usb, 'rm', '--quiet', 'start')
        self.git(usb, 'commit', '--quiet', '-m', 'only on the stick')
        self.git(old, 'worktree', 'lock', str(usb))
        usb.rename(self.root / 'unplugged')
        result = self.workspace('retire-dir', str(old), '--apply', answer='yes\n', check=False)
        self.assertNotEqual(result.returncode, 0, "a linked worktree's HEAD lives in the clone, folder or not")
        self.assertIn('not there now', result.stderr)
        self.assertTrue((old / '.git').exists())

    def test_every_retired_clone_bundle_is_checked(self):
        retired = self.retire_to('2000-01-01')
        for group in ('one', 'two'):
            old = self.root / 'home' / group / 'old-clone'
            self.git(self.root, 'clone', '--quiet', str(self.origin), str(old))
            self.git(old, 'switch', '--quiet', '-c', f'work-{group}')
            self.workspace('retire-dir', str(old), '--apply', answer='yes\n')
        second = retired / 'old-clone-kept-2' / 'refs.bundle'
        data = second.read_bytes()
        second.write_bytes(data[: len(data) - 30])
        Path(f'{second}.hash').unlink()
        first = retired / 'old-clone-kept' / 'refs.bundle'
        first.unlink()
        report = self.verify_fails()
        self.assertIn(f'{second} does not restore', report, 'a -kept-2 bundle must be checked like any other')
        self.assertIn(f'{first} is missing', report, 'a kept bundle whose record survives it is missing')

    def test_empty_retired_checks_every_kept_folder_and_missing_bundle(self):
        home = self.root / 'home'
        retired = self.retire_to('2000-01-01')
        self.finished_worktree(home / 'group1' / 'done', 'one', 1, 'first log')
        self.finished_worktree(home / 'group2' / 'done', 'two', 2, 'second log')
        self.tidy('--apply', answer='yes\n')
        self.assertTrue((retired / 'done-kept-2' / 'manifest.txt').exists())
        report = self.workspace('verify-backup').stdout
        self.assertIn('done-kept-2/files/bench.log', report, 'a -kept-2 folder must be checked like any other')
        (retired / 'done-kept-2' / 'files' / 'bench.log').unlink()
        self.workspace('empty-retired', '--apply', '--delete-local-work', answer='yes\n')
        self.assertTrue(retired.exists(), 'a file missing from a -kept-2 folder must keep the folder')

        (retired / 'done-kept-2' / 'manifest.txt').unlink()
        (retired / 'done-kept-2' / 'files').rename(retired / 'removed-files')
        bundle = next(retired.glob('convolver-local-refs-*.bundle'))
        bundle.unlink()
        report = self.verify_fails()
        self.assertIn(f'{bundle} is missing', report, 'a bundle whose record survives it is missing')
        self.workspace('empty-retired', '--apply', '--delete-local-work', answer='yes\n')
        self.assertTrue(retired.exists(), 'a folder whose bundle is gone must be kept')

    def test_empty_retired_keeps_the_only_copy_of_local_work(self):
        old = self.root / 'home' / 'old-clone'
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(old))
        self.git(old, 'switch', '--quiet', '-c', 'secret')
        (old / 'work.txt').write_text('committed only here', encoding='utf-8')
        self.git(old, 'add', '.')
        self.git(old, 'commit', '--quiet', '-m', 'secret work')
        retired = self.retire_to('2000-01-01')
        self.workspace('retire-dir', str(old), '--apply', answer='yes\n')
        report = self.workspace('verify-backup').stdout
        self.assertIn('only known copy', report)
        self.assertIn('heads/secret', report, 'verify-backup must name the local work it holds')
        self.workspace('empty-retired', '--apply', answer='yes\n')
        self.assertTrue(retired.exists(), 'the only copy of local work must not expire unasked')
        self.workspace('empty-retired', '--apply', '--delete-local-work', answer='yes\n')
        self.assertFalse(retired.exists(), '--delete-local-work lets it go')

    def test_empty_retired_keeps_a_kept_build_result(self):
        old = self.root / 'home' / 'old-clone'
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(old))
        (old / 'build').mkdir()
        (old / 'build' / 'CMakeCache.txt').write_text('cache', encoding='utf-8')
        (old / 'build' / 'results.csv').write_text('host,load ms\nREAPER,412\n', encoding='utf-8')
        retired = self.retire_to('2000-01-01')
        self.workspace('retire-dir', str(old), '--apply', answer='yes\n')
        report = self.workspace('verify-backup').stdout
        self.assertIn('OK', report)
        self.assertIn('only known copy', report)
        self.assertIn('old-clone-kept/files/build/results.csv', report, 'a measurement cannot be rebuilt')
        self.workspace('empty-retired', '--apply', answer='yes\n')
        self.assertTrue(retired.exists(), 'a kept build result must not expire unasked')
        self.workspace('empty-retired', '--apply', '--delete-local-work', answer='yes\n')
        self.assertFalse(retired.exists(), '--delete-local-work lets it go')

    def test_new_work_started_from_a_merged_branch_is_only_for_review(self):
        self.commit('claude/foo', 'a')
        self.push('claude/foo')  # merged, but still on GitHub
        self.push('claude/foo', 1)
        self.git(self.clone, 'fetch', '--quiet', 'origin')
        followup = self.root / 'home' / 'followup'
        self.git(self.clone, 'worktree', 'add', '--quiet', '-b', 'followup', str(followup), 'origin/claude/foo')
        self.git(self.clone, 'branch', '--quiet', '--track', 'followup2', 'origin/claude/foo')
        self.tidy('--apply', answer='yes\n')
        self.assertTrue(followup.exists(), "a merged branch's upstream says nothing about new work started from it")
        self.assertLessEqual({'followup', 'followup2'}, self.local_branches())

    def test_a_detached_worktree_is_only_for_review(self):
        self.commit('finished', 'a')
        self.push('finished', 1)
        detached = self.worktree('wt-detached', 'finished', '--detach')
        self.tidy('--apply', answer='yes\n')
        self.assertTrue(detached.exists(), 'a detached HEAD names no branch for a merged pull request to account for')

    def test_retire_dir_keeps_build_logs_and_refuses_nested_checkouts(self):
        old = self.root / 'home' / 'old-clone'
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(old))
        for build in ('aa', 'build', 'zz-build'):  # whichever find lists last must not stop the script
            (old / build).mkdir()
            (old / build / 'CMakeCache.txt').write_text('cache', encoding='utf-8')
            (old / build / 'test.log').write_text(f'{build} log', encoding='utf-8')
        self.workspace('retire-dir', str(old), '--apply', answer='yes\n')
        kept = self.retired() / 'old-clone-kept' / 'files'
        self.assertEqual((kept / 'zz-build' / 'test.log').read_text(encoding='utf-8'), 'zz-build log')

        outer = self.root / 'home' / 'experiments'
        outer.mkdir()
        self.git(self.root, 'clone', '--quiet', str(self.origin), str(outer / 'convolver-old'))
        result = self.workspace('retire-dir', str(outer), '--apply', answer='yes\n', check=False)
        self.assertNotEqual(result.returncode, 0, 'a folder holding a checkout would move without its branches saved')
        self.assertTrue((outer / 'convolver-old' / '.git').exists())


if __name__ == '__main__':
    unittest.main()
