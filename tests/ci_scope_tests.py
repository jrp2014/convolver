#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for scripts/ci_scope.py and for the workflows that rely on it."""

import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

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
OVERRIDE = re.compile(r'^    if:.*(always|cancelled)\(\)', re.MULTILINE)
GATE = re.compile(r"^    if: needs\.scope\.outputs\.(build|package) == 'true'$", re.MULTILINE)


def jobs(text):
    """Top-level job names and their text, from a workflow file."""
    body = text.split('\njobs:\n', 1)[1]
    parts = re.split(r'^  ([A-Za-z0-9_-]+):\n', body, flags=re.MULTILINE)
    return dict(zip(parts[1::2], parts[2::2]))


class Classification(unittest.TestCase):
    def setUp(self):
        # What a changed guide holds, before and after the change; None
        # stands for a page that cannot be read.
        self.guides = {}
        patcher = mock.patch.object(ci_scope, 'versions', self.guides.get)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_kinds(self):
        cases = {
            'docs/develop/testing.md': 'documentation',
            'docs/use/images/host.png': 'packaged',
            'docs/use/presets.md': 'packaged',
            'docs/benchmarks/2026-10-18-m5-max.csv': 'documentation',
            'tests/native/benchmarks/kernels.cpp': 'code',
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
            'plugins/editor/mac/diagnostic_view.mm': 'format:editor',
            'plugins/editor/mac/view_layout.h': 'format:editor',
            # Only the macOS window is the VST3 and Audio Unit builds' alone.
            'plugins/editor/windows/view.cpp': 'code',
            'plugins/identities.json': 'code',
            'plugins/audio_guard.hpp': 'code',
            'tests/plugins/fixtures.py': 'code',
            'tests/auxiliary.py': 'code',
            # Release files only the candidates use (CI-34), and the few others use too.
            'scripts/release/candidate.py': 'release',
            'tests/release/package_smoke.py': 'release',
            'packaging/macos/Distribution.xml': 'release',
            'packaging/windows/installer.nsi': 'release',
            'packaging/trademarks/README.md': 'documentation',
            'scripts/release/resolve.py': 'code',
            'scripts/release/contract.py': 'code',
            'packaging/release-policy.json': 'code',
            'packaging/trademarks/vst.json': 'code',
            '.github/workflows/release.yml': 'workflow:release',
        }
        for path, kind in cases.items():
            with self.subTest(path=path):
                self.assertEqual(ci_scope.classify(path), kind)

    def test_decisions(self):
        self.assertEqual(ci_scope.decide(['TODO.md', 'docs/develop/build.md']), {'build': False, 'package': False})
        self.guides['docs/use/presets.md'] = ['<!-- executable-example -->\n']
        self.assertEqual(ci_scope.decide(['TODO.md', 'docs/use/presets.md']), {'build': False, 'package': True})
        # Ordinary code builds the candidates only on main or by hand (CI-34).
        self.assertEqual(ci_scope.decide(['TODO.md', 'portable/engine.cpp']), {'build': True, 'package': False})
        self.assertEqual(ci_scope.decide(['portable/engine.cpp', 'cmake/X.cmake']), {'build': True, 'package': True})
        self.assertEqual(ci_scope.decide(['scripts/release/candidate.py']), {'build': False, 'package': True})
        self.assertEqual(ci_scope.decide(['packaging/trademarks/README.md']), {'build': False, 'package': False})
        # An empty list is unusual (a merge that changes nothing); test everything.
        self.assertEqual(ci_scope.decide([]), {'build': True, 'package': True})
        self.assertEqual(ci_scope.decide([], 'release'), {'build': True, 'package': True})

    def test_format_decisions(self):
        def runs(paths):
            # The candidate workflow reads package; the others read build.
            return {
                workflow
                for workflow in ci_scope.WORKFLOWS
                if ci_scope.decide(paths, workflow)['package' if workflow == 'release' else 'build']
            }

        everything = set(ci_scope.WORKFLOWS)
        builds = everything - {'release'}
        # A format's own code runs its workflows; the candidates, which package
        # the VST3 plug-in and the Audio Unit, build for it only on main or by
        # hand (CI-34).
        self.assertEqual(runs(['plugins/au/effect.hpp', 'tests/au/harness_test.cpp', 'TODO.md']), {'au'})
        self.assertEqual(runs(['plugins/lv2/plugin.cpp']), {'lv2'})
        self.assertEqual(runs(['plugins/vst3/processor.cpp']), {'vst3'})
        self.assertEqual(runs(['plugins/au/effect.hpp', 'plugins/lv2/plugin.cpp']), {'au', 'lv2'})
        # What a format puts into its bundle builds the candidates too.
        self.assertEqual(runs(['plugins/au/Info.plist.in']), {'au', 'release'})
        self.assertEqual(runs(['plugins/vst3/CMakeLists.txt']), {'vst3', 'release'})
        self.assertEqual(runs(['plugins/lv2/CMakeLists.txt']), {'lv2'})
        # The macOS window, which both the VST3 plug-in and the Audio Unit show
        # (CI-26), and which the candidates package.
        self.assertEqual(runs(['plugins/editor/mac/setting_rows.mm']), {'vst3', 'au', 'release'})
        self.assertEqual(
            runs(['plugins/editor/mac/setting_rows.mm', 'tests/au/editor_tests.mm']), {'vst3', 'au', 'release'}
        )
        self.assertEqual(runs(['plugins/editor/mac/setting_rows.mm', 'plugins/identities.json']), everything)
        self.assertEqual(runs(['plugins/editor/mac/setting_rows.mm', 'portable/host.cpp']), everything)
        self.assertEqual(runs(['plugins/au/effect.hpp', 'docs/use/plugins.md']), {'au', 'release'})
        self.guides['docs/use/plugins.md'] = ['Prose only.\n', 'Changed prose.\n']
        self.assertEqual(runs(['plugins/au/effect.hpp', 'docs/use/plugins.md']), {'au'})
        self.assertEqual(runs(['.github/workflows/fuzz.yml']), {'fuzz'})
        self.assertEqual(runs(['.github/workflows/release.yml']), {'release'})
        self.assertEqual(runs(['.github/workflows/lint.yml']), set())
        self.assertEqual(runs(['plugins/au/effect.hpp', 'portable/engine.cpp']), builds)
        self.assertEqual(runs(['plugins/au/effect.hpp', 'tests/plugins/harness.hpp']), builds)
        self.assertEqual(runs(['tests/vst3/fft_symbols.py']), builds)
        # Release files: only the candidates, unless other workflows use them too.
        self.assertEqual(runs(['scripts/release/candidate.py', 'tests/release/package_smoke.py']), {'release'})
        self.assertEqual(runs(['packaging/macos/Distribution.xml', 'TODO.md']), {'release'})
        self.assertEqual(runs(['scripts/release/resolve.py']), everything)
        self.assertEqual(runs(['packaging/release-policy.json']), everything)
        self.assertEqual(runs(['scripts/release/candidate.py', 'plugins/au/effect.hpp']), {'au', 'release'})
        self.assertEqual(ci_scope.decide(['plugins/au/effect.hpp'], 'release'), {'build': False, 'package': False})
        self.assertEqual(ci_scope.decide(['plugins/lv2/plugin.cpp'], 'release'), {'build': False, 'package': False})
        self.assertEqual(ci_scope.decide(['cmake/X.cmake'], 'release'), {'build': True, 'package': True})
        # Without a workflow name, build says whether any workflow but the candidates builds.
        self.assertEqual(ci_scope.decide(['plugins/au/effect.hpp']), {'build': True, 'package': False})
        self.assertEqual(ci_scope.decide(['.github/workflows/lint.yml']), {'build': False, 'package': False})

    def test_candidates(self):
        """What the packages are built from or hold builds the candidates on a
        pull request; other code does not (CI-34)."""
        build = (
            'CMakeLists.txt',
            'cmake/ConvolverPluginPackaging.cmake',
            'cmake/FindConvolverFFTW.cmake',
            'VERSION',
            'COPYING',
            'examples/first-use/stereo/filter.wav',
            'examples/plugin-presets/stereo.vstpreset',
            'portable/third_party/ooura/fftsg.c',
            'plugins/identities.json',
            'plugins/editor/mac/view_layout.h',
            'plugins/vst3/NOTICE.txt.in',
            'plugins/au/GPL-3.0.txt',
            'tests/plugins/fixtures.py',
            'scripts/trademarks.py',
            'tests/au/bundle_tests.py',
            'tests/vst3/package_probe.cpp',
            '.gitattributes',
            'scripts/release/candidate.py',
            'scripts/release/resolve.py',
            'tests/release/macos_installer.py',
            'packaging/release-policy.json',
            'packaging/trademarks/vst.json',
            '.github/workflows/release.yml',
            'docs/use/images/host.png',
            'examples/plugin-presets/README.md',
        )
        skip = (
            'portable/engine.cpp',
            'portable/host.cpp',
            'include/convolver/engine.hpp',
            'plugins/vst3/processor.cpp',
            'plugins/vst3/editor.mm',
            'plugins/au/effect.cpp',
            'plugins/lv2/CMakeLists.txt',
            'tests/consumer/CMakeLists.txt',
            'tests/native/engine_tests.cpp',
            'tests/plugins/harness.hpp',
            '.github/workflows/vst3.yml',
            '.github/actions/use-every-cpu/action.yml',
            'CMakePresets.json',
            'TODO.md',
            'docs/develop/testing.md',
            # Markdown that nothing packages is documentation, wherever it is.
            'packaging/trademarks/README.md',
            'examples/host-audio/README.md',
        )
        for path in build:
            with self.subTest(path=path):
                self.assertTrue(ci_scope.candidates([path]))
        for path in skip:
            with self.subTest(path=path):
                self.assertFalse(ci_scope.candidates([path]))

    def test_guides(self):
        """A guide in docs/use/ builds the candidates only when it has a
        command they run, before or after the change (CI-34)."""
        page = 'docs/use/install.md'
        cases = [
            (['Prose.\n', 'Changed prose.\n'], False),
            (['Prose.\n'], False),  # Added or deleted.
            (['<!-- executable-example -->\n```sh\nconvolver\n```\n', 'Prose.\n'], True),  # Removed.
            (['Prose.\n', '<!-- refused-example -->\n'], True),  # Added.
            (['Prose with ![a logo](images/logo.png).\n'], True),  # Only packaging checks images.
            (['<img src="images/logo.png" alt="">\n', 'Prose.\n'], True),
            (None, True),  # Cannot be read: build them.
        ]
        for texts, build in cases:
            with self.subTest(texts=texts):
                self.guides[page] = texts
                self.assertEqual(ci_scope.candidates([page]), build)
                self.assertEqual(ci_scope.decide([page, 'TODO.md'], 'release')['package'], build)
        # Pages outside docs/use/ and images build them whatever they hold.
        self.guides['examples/plugin-presets/README.md'] = ['Prose.\n']
        self.assertTrue(ci_scope.candidates(['examples/plugin-presets/README.md']))
        self.assertTrue(ci_scope.candidates(['docs/use/images/host.png']))
        # Only docs/use/*.md are guides; Markdown in a subfolder is packaged
        # as it is (install(DIRECTORY docs/use/images/)), so it builds them.
        self.guides['docs/use/images/notes.md'] = ['Prose.\n']
        self.assertTrue(ci_scope.candidates(['docs/use/images/notes.md']))

    def test_example_markers_match_the_packaged_check(self):
        """Every guide whose commands the candidates' documentation check runs
        is one the scope counts as marked, and some guides are."""
        spec = importlib.util.spec_from_file_location(
            'documentation_examples', ROOT / 'tests/release/documentation_examples.py'
        )
        examples = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(examples)
        texts = {page: page.read_text(encoding='utf-8') for page in sorted((ROOT / 'docs/use').glob('*.md'))}
        run = [page for page, text in texts.items() if examples.EXAMPLE.search(text)]
        self.assertTrue(run)
        for page in run:
            with self.subTest(page=page.name):
                self.assertRegex(texts[page], ci_scope.EXAMPLE)

    def test_packaged_sources_build_the_candidates(self):
        """Every source file that only packaging builds or reads, and every
        file the release scripts read outside their folders, builds the
        candidates."""
        named = set()
        texts = [(ROOT / 'cmake/ConvolverPluginPackaging.cmake').read_text(encoding='utf-8')]
        # Blocks that only a CONVOLVER_PACKAGE_PLUGINS build configures.
        for cmake in sorted(ROOT.glob('plugins/*/CMakeLists.txt')):
            texts += re.findall(
                r'^if\([^\n]*CONVOLVER_PACKAGE_PLUGINS[^\n]*$(.*?)^endif\(\)',
                cmake.read_text(encoding='utf-8'),
                re.MULTILINE | re.DOTALL,
            )
        for text in texts:
            named |= set(re.findall(r'\$\{PROJECT_SOURCE_DIR\}/([^"\s)]+)', text))
        self.assertIn('COPYING', named)
        self.assertIn('tests/vst3/package_probe.cpp', named)
        for script in sorted([*ROOT.glob('scripts/release/*.py'), *ROOT.glob('tests/release/*.py')]):
            named |= set(re.findall(r"\bROOT / '([^']+)'", script.read_text(encoding='utf-8')))
        self.assertIn('tests/au/bundle_tests.py', named)
        # SOURCE_COMMIT is written into the source archive. The include
        # directory and the import path are not read whole; the files they
        # are used for are named one by one.
        for pattern in sorted(named - {'SOURCE_COMMIT', 'portable', 'scripts'}):
            path = pattern.replace('*', 'guide').replace('${release_test}', 'manifest')
            if (ROOT / path).is_dir():
                path = path.rstrip('/') + '/file'
            with self.subTest(path=path):
                self.assertTrue(ci_scope.candidates([path]))

    def test_documentation_the_build_tests_reads(self):
        """Pages that CTest entries read must never count as documentation only."""
        fixtures = (ROOT / 'tests/plugins/fixtures.py').read_text(encoding='utf-8')
        block = re.search(r'^DOCUMENTED = \((.*?)\)$', fixtures, re.MULTILINE | re.DOTALL).group(1)
        documented = re.findall(r"REPOSITORY / '([^']+)'", block)
        self.assertTrue(documented)
        regressions = (ROOT / 'tests/regression/documentation_regressions.py').read_text(encoding='utf-8')
        records = re.findall(r"^[A-Z]+ = Path\('([^']+)'\)", regressions, re.MULTILINE)
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
        for pattern in installed:
            path = pattern.replace('*', 'guide')
            with self.subTest(path=path):
                self.assertNotEqual(ci_scope.classify(path), 'documentation')


class Workflows(unittest.TestCase):
    def test_pull_request_workflows_are_gated(self):
        """A workflow that builds on pull requests must start with the scope job,
        or limit itself with a paths filter, so documentation-only pull requests
        skip it. Every other job needs scope or a job that does."""
        for workflow in sorted(WORKFLOWS.glob('*.yml')):
            text = workflow.read_text(encoding='utf-8')
            trigger = re.search(r'^  pull_request:\n((?:    .*\n)*)', text, re.MULTILINE)
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
                    needs = re.search(r'^    needs: (\S+)$', body, re.MULTILINE)
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
# and the format's own workflow are not checked. The candidates build and
# package the VST3 plug-in and the Audio Unit, but since CI-34 a pull request
# changing only their code builds them only when started by hand; main,
# the weekly run and manual runs still do.
FORMAT_REFERENCES = {
    'vst3': {
        'CMakeLists.txt': 'adds plugins/vst3 only with CONVOLVER_BUILD_VST3',
        'CMakePresets.json': 'local presets; CI uses only core-only (checked below)',
        'cmake/ConvolverDebugSymbols.cmake': 'a comment',
        'cmake/ConvolverPluginPackaging.cmake': 'packages the VST3 build, in the candidates (CI-34)',
        'scripts/release/candidate.py': 'the candidates (CI-34: main, weekly, by hand)',
        'tests/release/source_build.py': 'a local check of the source archive; CI does not run it',
        'tests/identities_tests.py': 'Lint runs it for every pull request',
        'tests/native/preset_tests.cpp': 'a comment',
        'tests/release/package_smoke.py': 'runs the VST3 validator in the candidates (CI-34: main, weekly, by hand)',
    },
    'au': {
        'CMakeLists.txt': 'adds plugins/au only with CONVOLVER_BUILD_AU',
        'cmake/ConvolverPluginPackaging.cmake': 'packages the Audio Unit on macOS, in the candidates (CI-34)',
        'scripts/release/candidate.py': 'the candidates (CI-34: main, weekly, by hand)',
        'tests/release/package_smoke.py': "runs au.bundle's checks on the package, in the candidates",
        'tests/release/macos_installer.py': "runs au.bundle's checks on the installed component, in the candidates",
        'tests/release/source_build.py': 'a local check of the source archive; CI does not run it',
        'tests/plugins/lifecycle.hpp': 'a comment',
    },
    'editor': {
        'plugins/vst3/CMakeLists.txt': 'compiles the window on macOS; editor changes run VST3',
        'plugins/vst3/editor.hpp': 'a comment',
        'plugins/vst3/editor.mm': 'shows the window; VST3 code',
        'tests/vst3/editor_tests.mm': 'tests the window; VST3 code',
        'plugins/au/CMakeLists.txt': 'compiles the window; editor changes run the Audio Unit',
        'plugins/au/view_factory.mm': 'shows the window; Audio Unit code',
        'tests/au/editor_tests.mm': 'tests the window; Audio Unit code',
        'scripts/check_repository.py': 'a comment',
        'tests/repository_checks_tests.py': 'made-up paths in its own test repository',
    },
    'lv2': {
        'CMakeLists.txt': 'adds plugins/lv2 only with CONVOLVER_BUILD_LV2',
        'tests/repository_checks_tests.py': 'made-up paths in its own test repository',
        'tests/identities_tests.py': 'a comment naming an LV2 specification file',
    },
}


def tracked():
    """Tracked files that a build or workflow may read, with their text."""
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


class FormatIsolation(unittest.TestCase):
    """A pull request that changes only one format's files runs only that
    format's workflows, so nothing else may build or read those files."""

    def test_only_listed_files_name_a_format(self):
        files = list(tracked())
        for name, folders in ci_scope.FORMATS.items():
            # A relative path such as parents[1] / 'vst3/x.py' counts too; a
            # URL path such as github.com/lv2/lv2 does not.
            pattern = (
                r'(?:plugins|tests)/' + name + r'\b|(?<![\w./-])' + name + r'/|CONVOLVER_BUILD_' + name.upper() + r'\b'
            )
            # The window's users include its headers by bare name, through
            # an include directory, so its file names count too.
            if name == 'editor':
                names = sorted(path.name for folder in folders for path in (ROOT / folder).iterdir())
                pattern += r'|\b(?:' + '|'.join(map(re.escape, names)) + r')\b'
            pattern = re.compile(pattern)
            own = (*folders, f'.github/workflows/{name}.yml')
            found = set()
            for path, contents in files:
                if path.startswith(own) or path in ('scripts/ci_scope.py', 'tests/ci_scope_tests.py'):
                    continue
                text = contents
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


# Files outside the release folders that name a release file other than those
# in RELEASE_SHARED, and why that does not make them use it (CI-34).
# Documentation, historical code and the candidate workflow are not checked.
RELEASE_REFERENCES = {
    '.pre-commit-config.yaml': 'Lint runs the trademark check for every pull request',
    'scripts/trademarks.py': 'Lint runs it; also a candidate trigger, as the package checks import it',
    'tests/identities_tests.py': 'Lint runs it for every pull request (also CTest identities)',
    'cmake/ConvolverDebugSymbols.cmake': 'a comment',
    'cmake/ConvolverPluginPackaging.cmake': 'used only with CONVOLVER_PACKAGE_PLUGINS, in the candidates',
    'tests/vst3/package_probe.cpp': 'a comment',
}


class ReleaseIsolation(unittest.TestCase):
    """A pull request that changes only release files runs only the
    candidate workflow, so no other workflow may use them."""

    def test_only_listed_files_name_a_release_file(self):
        pattern = re.compile(r'(?:scripts/release|tests/release|packaging)/[\w./-]*')
        own = (*ci_scope.RELEASE, '.github/workflows/release.yml', 'scripts/ci_scope.py', 'tests/ci_scope_tests.py')
        found = set()
        for path, contents in tracked():
            if path.startswith(own):
                continue
            names = {name.rstrip('.') for name in pattern.findall(contents)}
            if names - set(ci_scope.RELEASE_SHARED):
                found.add(path)
        self.assertEqual(found, set(RELEASE_REFERENCES))

    def test_shared_release_scripts_import_only_shared_ones(self):
        """A release module that a shared one imports is used by other
        workflows too, so it must be shared."""
        modules = {path.stem for path in (ROOT / 'scripts/release').glob('*.py')}
        for shared in ci_scope.RELEASE_SHARED:
            if not shared.endswith('.py'):
                continue
            text = (ROOT / shared).read_text(encoding='utf-8')
            imported = set(re.findall(r'^(?:import|from) (\w+)', text, re.MULTILINE)) & modules
            with self.subTest(module=shared):
                for name in imported:
                    self.assertIn(f'scripts/release/{name}.py', ci_scope.RELEASE_SHARED)


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
        for event in ('push', 'workflow_dispatch', 'schedule'):
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

    def test_guide_examples_read_from_both_sides_of_the_merge(self):
        """A guide's runnable commands are looked for in the base and in the
        merged page, so removing the last one still builds the candidates."""
        with tempfile.TemporaryDirectory() as temporary:
            repository = Path(temporary)

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

            def merged(page, text):
                git('checkout', '-q', '-B', 'topic', 'main')
                if text is None:
                    git('rm', '-q', page)
                elif isinstance(text, bytes):
                    (repository / page).write_bytes(text)
                else:
                    (repository / page).write_text(text)
                git('add', '-A')
                git('commit', '-q', '-m', 'edit')
                git('checkout', '-q', '--detach', 'main')
                git('merge', '-q', '--no-ff', '-m', 'merge', 'topic')
                return self.run_script('--workflow', 'release', cwd=repository)[0]

            git('init', '-q', '-b', 'main')
            (repository / 'docs/use').mkdir(parents=True)
            (repository / 'docs/use/prose.md').write_text('Prose.\n')
            (repository / 'docs/use/marked.md').write_text('<!-- executable-example -->\n```sh\nconvolver\n```\n')
            git('add', '.')
            git('commit', '-q', '-m', 'base')
            self.assertEqual(merged('docs/use/prose.md', 'Changed prose.\n'), 'build=false\npackage=false\n')
            self.assertEqual(merged('docs/use/marked.md', 'No commands now.\n'), 'build=true\npackage=true\n')
            self.assertEqual(
                merged('docs/use/prose.md', '<!-- refused-example -->\n```sh\nconvolver\n```\n'),
                'build=true\npackage=true\n',
            )
            self.assertEqual(merged('docs/use/added.md', 'New prose.\n'), 'build=false\npackage=false\n')
            self.assertEqual(merged('docs/use/marked.md', None), 'build=true\npackage=true\n')  # Deleted.
            self.assertEqual(merged('docs/use/prose.md', None), 'build=false\npackage=false\n')
            # A page that cannot be read counts as marked, even when the other side is prose.
            self.assertEqual(merged('docs/use/prose.md', b'\xff<!-- prose -->\n'), 'build=true\npackage=true\n')


if __name__ == '__main__':
    unittest.main()
