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
            'tests/native/benchmarks/vdsp_mac.cpp': 'code',
            'tests/native/benchmarks/callback_scenarios.sh': 'code',
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
            '.github/workflows/lint.yml': 'workflow:lint',
            '.github/workflows/regression.yml': 'workflow:regression',
            '.github/actions/use-every-cpu/action.yml': 'code',
            'scripts/ci_scope.py': 'code',
            'docsearch.txt': 'code',
            'plugins/au/effect.hpp': 'format:au',
            'plugins/au/README.md': 'documentation',
            'tests/au/harness_test.cpp': 'format:au',
            'plugins/vst3/CMakeLists.txt': 'format:vst3',
            'tests/vst3/preset_format_tests.cpp': 'format:vst3',
            'tests/vst3/fft_symbols.py': 'code',
            'plugins/lv2/plugin.cpp': 'format:lv2',
            'plugins/identities.json': 'code',
            'plugins/audio_guard.hpp': 'code',
            'tests/plugins/fixtures.py': 'code',
            'tests/auxiliary.py': 'code',
        }
        for path, kind in cases.items():
            with self.subTest(path=path):
                self.assertEqual(ci_scope.classify(path), kind)

    def test_decisions(self):
        self.assertEqual(ci_scope.decide(['TODO.md', 'docs/develop/build.md']), {'build': False, 'package': False})
        self.assertEqual(ci_scope.decide(['TODO.md', 'docs/use/presets.md']), {'build': False, 'package': True})
        self.assertEqual(ci_scope.decide(['TODO.md', 'portable/engine.cpp']), {'build': True, 'package': True})
        # An empty list is unusual (a merge that changes nothing); test everything.
        self.assertEqual(ci_scope.decide([]), {'build': True, 'package': True})

    def test_format_decisions(self):
        def runs(paths):
            # The candidate workflow reads package; the others read build.
            return {
                workflow
                for workflow in ci_scope.WORKFLOWS
                if ci_scope.decide(paths, workflow)['package' if workflow == 'release' else 'build']
            }

        everything = set(ci_scope.WORKFLOWS)
        self.assertEqual(runs(['plugins/au/effect.hpp', 'tests/au/harness_test.cpp', 'TODO.md']), {'au'})
        self.assertEqual(runs(['plugins/lv2/plugin.cpp']), {'lv2'})
        # The candidates build and package the VST3 plug-in.
        self.assertEqual(runs(['plugins/vst3/processor.cpp']), {'vst3', 'release'})
        self.assertEqual(runs(['plugins/au/effect.hpp', 'plugins/lv2/plugin.cpp']), {'au', 'lv2'})
        self.assertEqual(runs(['plugins/au/effect.hpp', 'docs/use/plugins.md']), {'au', 'release'})
        self.assertEqual(runs(['.github/workflows/fuzz.yml']), {'fuzz'})
        self.assertEqual(runs(['.github/workflows/lint.yml']), set())
        self.assertEqual(runs(['plugins/au/effect.hpp', 'portable/engine.cpp']), everything)
        self.assertEqual(runs(['plugins/au/effect.hpp', 'tests/plugins/harness.hpp']), everything)
        self.assertEqual(runs(['tests/vst3/fft_symbols.py']), everything)
        self.assertEqual(ci_scope.decide(['plugins/au/effect.hpp'], 'release'), {'build': False, 'package': False})
        self.assertEqual(ci_scope.decide(['plugins/vst3/processor.cpp'], 'release'), {'build': True, 'package': True})
        # Without a workflow name the answer is whether any workflow builds.
        self.assertEqual(ci_scope.decide(['plugins/au/effect.hpp']), {'build': True, 'package': True})
        self.assertEqual(ci_scope.decide(['.github/workflows/lint.yml']), {'build': False, 'package': False})

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
                self.assertIn(workflow.stem, ci_scope.WORKFLOWS)
                self.assertIn(
                    f'python3 scripts/ci_scope.py --workflow {workflow.stem} --github-output "$GITHUB_OUTPUT"',
                    found['scope'],
                )
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

    def test_every_named_workflow_exists(self):
        for name in ci_scope.WORKFLOWS:
            with self.subTest(workflow=name):
                self.assertTrue((WORKFLOWS / f'{name}.yml').is_file())
        for workflows in ci_scope.FORMAT_WORKFLOWS.values():
            self.assertLessEqual(workflows, set(ci_scope.WORKFLOWS))


# Files outside a format's folders that name its files (as plugins/NAME/,
# tests/NAME/ or a relative NAME/ path) or its CMake option, and why that does
# not make them depend on the format (CI-13). Documentation, historical code
# and the format's own workflow are not checked.
FORMAT_REFERENCES = {
    'vst3': {
        'CMakeLists.txt': 'adds plugins/vst3 only with CONVOLVER_BUILD_VST3',
        'CMakePresets.json': 'local presets; CI uses only core-only (checked below)',
        'cmake/ConvolverDebugSymbols.cmake': 'a comment',
        'cmake/ConvolverPluginPackaging.cmake': 'packages the VST3 build, in the candidates',
        'scripts/release/candidate.py': 'the candidates, which VST3 changes run',
        'tests/release/source_build.py': 'a local check of the source archive; CI does not run it',
        'tests/identities_tests.py': 'Lint runs it for every pull request',
        'tests/native/preset_tests.cpp': 'a comment',
        'tests/release/package_smoke.py': 'runs the VST3 validator in the candidates, which VST3 changes run',
    },
    'au': {
        'CMakeLists.txt': 'adds plugins/au only with CONVOLVER_BUILD_AU',
        'tests/plugins/lifecycle.hpp': 'a comment',
    },
    'lv2': {
        'CMakeLists.txt': 'adds plugins/lv2 only with CONVOLVER_BUILD_LV2',
        'tests/repository_checks_tests.py': 'made-up paths in its own test repository',
        'tests/identities_tests.py': 'a comment naming an LV2 specification file',
    },
}


class FormatIsolation(unittest.TestCase):
    """A pull request that changes only one format's files runs only that
    format's workflows, so nothing else may build or read those files."""

    def tracked(self):
        listed = (
            subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, env=CLEAN, check=True, capture_output=True)
            .stdout.decode()
            .split('\0')
        )
        for path in filter(None, listed):
            if path.startswith(('docs/', 'historical/')) or path.endswith('.md') or not (ROOT / path).is_file():
                continue
            try:
                yield path, (ROOT / path).read_text(encoding='utf-8')
            except UnicodeDecodeError:
                continue

    def test_only_listed_files_name_a_format(self):
        files = list(self.tracked())
        for name, folders in ci_scope.FORMATS.items():
            # A relative path such as parents[1] / 'vst3/x.py' counts too; a
            # URL path such as github.com/lv2/lv2 does not.
            pattern = re.compile(
                r'(?:plugins|tests)/' + name + r'\b|(?<![\w./-])' + name + r'/'
                r'|CONVOLVER_BUILD_' + name.upper() + r'\b'
            )
            own = folders + (f'.github/workflows/{name}.yml',)
            found = set()
            for path, text in files:
                if path.startswith(own) or path in ('scripts/ci_scope.py', 'tests/ci_scope_tests.py'):
                    continue
                for shared in ci_scope.SHARED:
                    text = text.replace(shared, '')
                if pattern.search(text):
                    found.add(path)
            with self.subTest(format=name):
                self.assertEqual(found, set(FORMAT_REFERENCES[name]))

    def test_ci_uses_only_the_core_preset(self):
        for workflow in sorted(WORKFLOWS.glob('*.yml')):
            presets = set(re.findall(r'--preset[ =](\S+)', workflow.read_text(encoding='utf-8')))
            with self.subTest(workflow=workflow.name):
                self.assertLessEqual(presets, {'core-only'})


class Command(unittest.TestCase):
    def run_script(self, *args, cwd=ROOT, event='pull_request'):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'output'
            environment = dict(CLEAN, GITHUB_EVENT_NAME=event)
            result = subprocess.run(
                [sys.executable, str(SCRIPT), '--github-output', str(output), *args],
                cwd=cwd,
                env=environment,
                capture_output=True,
                text=True,
                check=True,
            )
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
            subprocess.run(
                [
                    'git',
                    '-c',
                    'user.name=t',
                    '-c',
                    'user.email=t@example.com',
                    '-c',
                    'commit.gpgsign=false',
                    '-c',
                    'core.hooksPath=/dev/null',
                    *args,
                ],
                cwd=repository,
                env=CLEAN,
                check=True,
                capture_output=True,
            )

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
