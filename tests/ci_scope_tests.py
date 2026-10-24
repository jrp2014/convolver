#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for scripts/ci_scope.py and for the workflows that rely on it."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts' / 'ci_scope.py'
SPEC = importlib.util.spec_from_file_location('ci_scope', SCRIPT)
ci_scope = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ci_scope)
WORKFLOWS = ROOT / '.github' / 'workflows'
UNGATED = {'lint.yml'}  # Runs for every change, documentation included.
# A Git hook's GIT_DIR or GIT_INDEX_FILE would point the scratch repository's
# commands at the real one.
CLEAN = {key: value for key, value in os.environ.items() if not key.startswith('GIT_')}
OVERRIDE = re.compile(r'^    if:.*(always|cancelled)\(\)', re.M)
GATE = re.compile(r"^    if: needs\.scope\.outputs\.(build|package) == 'true'$", re.M)


def jobs(text):
    """Top-level job names and their text, from a workflow file."""
    body = text.split('\njobs:\n', 1)[1]
    parts = re.split(r'^  ([A-Za-z0-9_-]+):\n', body, flags=re.M)
    return dict(zip(parts[1::2], parts[2::2]))


class Classification(unittest.TestCase):
    def test_kinds(self):
        cases = {
            'docs/develop/testing.md': 'documentation',
            'docs/use/images/host.png': 'packaged',
            'docs/use/presets.md': 'packaged',
            'docs/benchmarks/2026-10-18-m5-max.csv': 'documentation',
            'TODO.md': 'documentation',
            'CHANGELOG.md': 'documentation',
            'tests/fuzz/README.md': 'documentation',
            '.github/pull_request_template.md': 'documentation',
            'README.md': 'code',
            'docs/use/first-filter.md': 'code',
            'docs/historical/convolver-4.4/index.html': 'code',
            'docs/review-2026-10/archive-documentation.json': 'code',
            'portable/third_party/ooura/README.md': 'packaged',
            'portable/engine.cpp': 'code',
            'COPYING': 'code',
            'VERSION': 'code',
            '.github/workflows/lint.yml': 'code',
            'scripts/ci_scope.py': 'code',
            'docsearch.txt': 'code',
        }
        for path, kind in cases.items():
            with self.subTest(path=path):
                self.assertEqual(ci_scope.classify(path), kind)

    def test_decisions(self):
        self.assertEqual(ci_scope.decide(['TODO.md', 'docs/develop/build.md']),
                         {'build': False, 'package': False})
        self.assertEqual(ci_scope.decide(['TODO.md', 'docs/use/presets.md']),
                         {'build': False, 'package': True})
        self.assertEqual(ci_scope.decide(['TODO.md', 'portable/engine.cpp']),
                         {'build': True, 'package': True})
        # An empty list is unusual (a merge that changes nothing); test everything.
        self.assertEqual(ci_scope.decide([]), {'build': True, 'package': True})

    def test_documentation_the_build_tests_reads(self):
        """Pages that CTest entries read must never count as documentation only."""
        fixtures = (ROOT / 'tests/plugins/fixtures.py').read_text(encoding='utf-8')
        block = re.search(r'^DOCUMENTED = \((.*?)\)$', fixtures, re.M | re.S).group(1)
        documented = re.findall(r"REPOSITORY / '([^']+)'", block)
        self.assertTrue(documented)
        regressions = (ROOT / 'tests/regression/documentation_regressions.py').read_text(encoding='utf-8')
        records = re.findall(r"^[A-Z]+ = Path\('([^']+)'\)", regressions, re.M)
        self.assertEqual(len(records), 3)
        # The archive is a folder; any file in it counts.
        records = [r if r.endswith('.json') else r + '/index.html' for r in records]
        for path in documented + records:
            with self.subTest(path=path):
                self.assertEqual(ci_scope.classify(path), 'code')

    def test_packaged_documentation(self):
        packaging = (ROOT / 'cmake/ConvolverPluginPackaging.cmake').read_text(encoding='utf-8')
        installed = re.findall(r'\$\{PROJECT_SOURCE_DIR\}/([^"\s]+\.md)', packaging)
        self.assertIn('docs/use/*.md', installed)
        for path in installed:
            path = path.replace('*', 'guide')
            with self.subTest(path=path):
                self.assertNotEqual(ci_scope.classify(path), 'documentation')


class Workflows(unittest.TestCase):
    def test_pull_request_workflows_are_gated(self):
        """A workflow that builds on pull requests must start with the scope job,
        or limit itself with a paths filter, so documentation-only pull requests
        skip it. Every other job needs scope or a job that does."""
        for workflow in sorted(WORKFLOWS.glob('*.yml')):
            text = workflow.read_text(encoding='utf-8')
            trigger = re.search(r'^  pull_request:\n((?:    .*\n)*)', text, re.M)
            if workflow.name in UNGATED or not trigger or 'paths' in trigger.group(1):
                continue
            with self.subTest(workflow=workflow.name):
                found = jobs(text)
                self.assertIn('scope', found)
                self.assertIn('python3 scripts/ci_scope.py --github-output "$GITHUB_OUTPUT"',
                              found['scope'])
                self.assertIn('fetch-depth: 2', found['scope'])
                for name, body in found.items():
                    if name == 'scope':
                        continue
                    needs = re.search(r'^    needs: (\S+)$', body, re.M)
                    self.assertIsNotNone(needs, f'{name} must need scope or another job')
                    if needs.group(1) == 'scope':
                        self.assertRegex(body, GATE, name)
                    # A job that runs despite a skipped dependency would undo the gate.
                    self.assertNotRegex(body, OVERRIDE, name)


class Command(unittest.TestCase):
    def run_script(self, *args, cwd=ROOT, event='pull_request'):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'output'
            environment = dict(CLEAN, GITHUB_EVENT_NAME=event)
            result = subprocess.run([sys.executable, str(SCRIPT), '--github-output', str(output), *args],
                                    cwd=cwd, env=environment, capture_output=True, text=True, check=True)
            return output.read_text(), result.stdout

    def test_explicit_paths(self):
        written, printed = self.run_script('TODO.md', 'docs/develop/build.md')
        self.assertEqual(written, 'build=false\npackage=false\n')
        self.assertIn('Only documentation changed', printed)

    def test_push_and_manual_runs_test_everything(self):
        for event in ('push', 'workflow_dispatch'):
            with self.subTest(event=event):
                self.assertEqual(self.run_script(event=event)[0], 'build=true\npackage=true\n')

    def test_pull_request_merge_commit(self):
        def git(*args):
            subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@example.com',
                            '-c', 'commit.gpgsign=false', '-c', 'core.hooksPath=/dev/null', *args],
                           cwd=repository, env=CLEAN, check=True, capture_output=True)

        with tempfile.TemporaryDirectory() as temporary:
            repository = Path(temporary)
            git('init', '-q', '-b', 'main')
            (repository / 'engine.cpp').write_text('base\n')
            git('add', '.')
            git('commit', '-q', '-m', 'base')
            git('checkout', '-q', '-b', 'topic')
            (repository / 'docs').mkdir()
            (repository / 'docs/guide.md').write_text('guide\n')
            (repository / 'docs/guidé.md').write_text('non-ASCII name\n')
            git('add', '.')
            git('commit', '-q', '-m', 'docs')
            # Code that reached main after the branch started is not the pull request's change.
            git('checkout', '-q', 'main')
            (repository / 'engine.cpp').write_text('newer\n')
            git('commit', '-q', '-am', 'main moves on')
            git('merge', '-q', '--no-ff', '-m', 'merge', 'topic')
            written, printed = self.run_script(cwd=repository)
            self.assertEqual(written, 'build=false\npackage=false\n')
            self.assertIn('documentation docs/guide.md', printed)
            self.assertIn('documentation docs/guidé.md', printed)
            self.assertNotIn('engine.cpp', printed)
            # Without a merge commit the changes are unknown, so everything runs.
            git('checkout', '-q', 'topic')
            written, printed = self.run_script(cwd=repository)
            self.assertEqual(written, 'build=true\npackage=true\n')
            self.assertIn('cannot list the changed files', printed)


if __name__ == '__main__':
    unittest.main()
