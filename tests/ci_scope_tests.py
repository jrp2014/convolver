#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for scripts/ci_scope.py and for the workflows that rely on it."""

import importlib.util
import json
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
GATE = re.compile(
    r"^    if: needs\.scope\.outputs\.(build|package) == 'true'( && needs\.scope\.outputs\.full == 'true')?$",
    re.MULTILINE,
)
# What gates a job to every platform (CI-54): a draft pull request skips it.
FULL_GATE = re.compile(r"^    if: .*needs\.scope\.outputs\.full == 'true'$", re.MULTILINE)
SCOPED_MATRIX = re.compile(
    r'^        include: \$\{\{ fromJSON\(needs\.scope\.outputs\.([a-z0-9]+)\) \}\}$', re.MULTILINE
)
# Advisory jobs, which only warn, also leave out pull requests (CI-49). Only
# the jobs listed here may: a required job that skipped pull requests would
# still pass every other check here.
ADVISORY = {('lv2.yml', 'upstream-lilv')}
ADVISORY_GATE = re.compile(
    r"^    if: needs\.scope\.outputs\.build == 'true' && github\.event_name != 'pull_request'$",
    re.MULTILINE,
)


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
            # The files kept from releases, Markdown included, and their checks (CI-42).
            'tests/upgrade/0.13.2/package/presets/README.md': 'upgrade',
            'tests/upgrade/0.13.2/expected/echo-44100-mono.wav': 'upgrade',
            'tests/upgrade/README.md': 'upgrade',
            'tests/upgrade/run_upgrade.py': 'upgrade',
            'tests/upgrade/vst3_render.cpp': 'upgrade',
            # The headless Ardour test (HOST-9); the fixtures it imports are
            # code (below).
            'tests/plugins/ardour_tests.py': 'ardour',
            'tests/plugins/ardour_session.lua': 'ardour',
            # The REAPER script jrp runs by hand, and its tests (HOST-11).
            'scripts/reaper_check.py': 'manual',
            'tests/reaper_check_tests.py': 'manual',
            # The record checks and the fragment folder, and the version
            # helper (DOC-20).
            'scripts/check_repository.py': 'records',
            'scripts/fold_changes.py': 'records',
            'tests/documentation_links_tests.py': 'records',
            'scripts/version_source.py': 'versioning',
            'tests/versioning_tests.py': 'versioning',
            'changes/DOC-20.md': 'documentation',
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

    def test_first_use_pages(self):
        """A page whose marked commands examples.first_use runs builds
        everything only when those commands change (CI-35)."""
        block = '<!-- first-use-example -->\n```sh\n./build/convolver --filter {} --output out.wav\n```\n'
        before = '# Convolver\n\n' + block.format('a.cfg')
        self.guides['README.md'] = [before.replace('# Convolver', '# Convolver\n\nDownload it.'), before]
        self.assertEqual(ci_scope.decide(['README.md']), {'build': False, 'package': False})
        self.guides['README.md'] = [before.replace('a.cfg', 'b.cfg'), before]
        self.assertEqual(ci_scope.decide(['README.md']), {'build': True, 'package': False})
        # A new or removed page, or one that cannot be read, counts as changed.
        self.guides['README.md'] = [before]
        self.assertEqual(ci_scope.decide(['README.md']), {'build': True, 'package': False})
        self.guides['README.md'] = None
        self.assertEqual(ci_scope.decide(['README.md']), {'build': True, 'package': False})
        # The packaged first-filter guide stays a packaged page when only its prose changes.
        self.guides['docs/use/first-filter.md'] = [before + 'More.\n', before]
        self.assertEqual(ci_scope.page_kind('docs/use/first-filter.md'), 'packaged')
        self.assertEqual(ci_scope.decide(['docs/use/first-filter.md']), {'build': False, 'package': False})
        # The other pages tests read still always run everything.
        self.assertEqual(ci_scope.page_kind('docs/review-2026-10/archive-documentation.json'), 'code')

    def test_first_use_marker_matches_the_test(self):
        """COMMANDED and the marker are the ones examples.first_use reads."""
        fixtures = (ROOT / 'tests/plugins/fixtures.py').read_text(encoding='utf-8')
        block = re.search(r'^DOCUMENTED = \((.*?)\)$', fixtures, re.MULTILINE | re.DOTALL).group(1)
        self.assertEqual(tuple(re.findall(r"REPOSITORY / '([^']+)'", block)), ci_scope.COMMANDED)
        marker = re.search(r"^MARKER = '([^']+)'$", fixtures, re.MULTILINE).group(1)
        # The block pattern documented_commands() puts after re.escape(MARKER).
        block = re.search(r"re\.escape\(MARKER\) \+ r'([^']+)'", fixtures).group(1)
        self.assertEqual(ci_scope.FIRST_USE.pattern, marker + block)
        for page in ci_scope.COMMANDED:
            with self.subTest(page=page):
                self.assertTrue(ci_scope.FIRST_USE.findall((ROOT / page).read_text(encoding='utf-8')))

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
        # Code the fuzz targets are not built from skips them too (CI-34).
        unfuzzed = everything - {'fuzz'}
        others = builds - {'fuzz'}
        # A format's own code runs its workflows; the candidates, which package
        # the VST3 plug-in, the Audio Unit and LV2, build for it only on main or
        # by hand (CI-34).
        self.assertEqual(runs(['plugins/au/effect.hpp', 'tests/au/harness_test.cpp', 'TODO.md']), {'au'})
        self.assertEqual(runs(['plugins/lv2/plugin.cpp']), {'lv2'})
        self.assertEqual(runs(['plugins/vst3/processor.cpp']), {'vst3'})
        self.assertEqual(runs(['plugins/au/effect.hpp', 'plugins/lv2/plugin.cpp']), {'au', 'lv2'})
        # What a format puts into its bundle builds the candidates too.
        self.assertEqual(runs(['plugins/au/Info.plist.in']), {'au', 'release'})
        self.assertEqual(runs(['plugins/vst3/CMakeLists.txt']), {'vst3', 'release'})
        self.assertEqual(runs(['plugins/lv2/CMakeLists.txt']), {'lv2', 'release'})
        self.assertEqual(runs(['plugins/lv2/generate_ttl.py']), {'lv2', 'release'})
        # The macOS window, which both the VST3 plug-in and the Audio Unit show
        # (CI-26), and which the candidates package.
        self.assertEqual(runs(['plugins/editor/mac/setting_rows.mm']), {'vst3', 'au', 'release'})
        self.assertEqual(
            runs(['plugins/editor/mac/setting_rows.mm', 'tests/au/editor_tests.mm']), {'vst3', 'au', 'release'}
        )
        self.assertEqual(runs(['plugins/editor/mac/setting_rows.mm', 'plugins/identities.json']), unfuzzed)
        self.assertEqual(runs(['plugins/editor/mac/setting_rows.mm', 'portable/host.cpp']), unfuzzed)
        self.assertEqual(runs(['plugins/editor/mac/setting_rows.mm', 'portable/state.cpp']), everything)
        self.assertEqual(runs(['plugins/au/effect.hpp', 'docs/use/plugins.md']), {'au', 'release'})
        self.guides['docs/use/plugins.md'] = ['Prose only.\n', 'Changed prose.\n']
        self.assertEqual(runs(['plugins/au/effect.hpp', 'docs/use/plugins.md']), {'au'})
        self.assertEqual(runs(['.github/workflows/fuzz.yml']), {'fuzz'})
        self.assertEqual(runs(['.github/workflows/release.yml']), {'release'})
        self.assertEqual(runs(['.github/workflows/lint.yml']), set())
        self.assertEqual(runs(['plugins/au/effect.hpp', 'portable/engine.cpp']), builds)
        self.assertEqual(runs(['plugins/au/effect.hpp', 'tests/plugins/harness.hpp']), others)
        self.assertEqual(runs(['tests/vst3/fft_symbols.py']), others)
        self.assertEqual(runs(['tests/vst3/fft_symbols.py', '.github/workflows/fuzz.yml']), builds)
        # Release files: only the candidates, unless other workflows use them too.
        self.assertEqual(runs(['scripts/release/candidate.py', 'tests/release/package_smoke.py']), {'release'})
        self.assertEqual(runs(['packaging/macos/Distribution.xml', 'TODO.md']), {'release'})
        self.assertEqual(runs(['scripts/release/resolve.py']), unfuzzed)
        self.assertEqual(runs(['packaging/release-policy.json']), unfuzzed)
        self.assertEqual(runs(['scripts/release/candidate.py', 'plugins/au/effect.hpp']), {'au', 'release'})
        # The files kept from releases run the native, VST3, LV2 and Audio
        # Unit workflows, whose upgrade.cli, vst3.upgrade, lv2.upgrade and
        # Audio Unit state check read them (CI-42, COMPAT-1).
        upgrade = {'regression', 'vst3', 'lv2', 'au'}
        self.assertEqual(runs(['tests/upgrade/0.13.2/expected/echo-44100-mono.wav']), upgrade)
        self.assertEqual(runs(['tests/upgrade/0.14.0/manifest.json', 'tests/upgrade/README.md', 'TODO.md']), upgrade)
        self.assertEqual(runs(['tests/upgrade/run_upgrade.py', 'tests/upgrade/vst3_render.cpp']), upgrade)
        self.assertEqual(runs(['tests/upgrade/run_upgrade.py', 'plugins/au/effect.hpp']), upgrade)
        self.assertEqual(runs(['tests/upgrade/run_upgrade.py', 'plugins/lv2/plugin.cpp']), upgrade)
        self.assertEqual(runs(['tests/upgrade/run_upgrade.py', 'portable/engine.cpp']), builds)
        self.assertEqual(runs(['tests/upgrade/run_upgrade.py', 'cmake/X.cmake']), everything)
        # The headless Ardour test runs only where it runs: the VST3 and LV2
        # workflows (HOST-9).
        ardour = {'vst3', 'lv2'}
        self.assertEqual(runs(['tests/plugins/ardour_tests.py', 'tests/plugins/ardour_reopen.lua']), ardour)
        self.assertEqual(runs(['tests/plugins/ardour_session.lua', 'plugins/au/effect.hpp']), ardour | {'au'})
        self.assertEqual(runs(['tests/plugins/ardour_tests.py', 'tests/plugins/harness.hpp']), others)
        # The REAPER script and its tests run only in lint's pre-commit (HOST-11).
        self.assertEqual(runs(['scripts/reaper_check.py', 'tests/reaper_check_tests.py', 'TODO.md']), set())
        self.assertEqual(runs(['scripts/reaper_check.py', 'plugins/au/effect.hpp']), {'au'})
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
            '.github/actions/newest-compiler/action.yml',
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
            'plugins/lv2/plugin.cpp',
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

    def test_fuzz_targets(self):
        """A pull request runs the fuzz targets only when it changes what they
        are built from, or the fuzz workflow (CI-34)."""
        fuzz = (
            'tests/fuzz/fuzz_config_text.cpp',
            'tests/fuzz/replay_main.cpp',
            'tests/fuzz/seeds/host_state/identity',
            'tests/fuzz/regressions/ir_file/sample-rate-out-of-range.wav',
            'tests/fuzz/scheduler_tests.py',
            'scripts/fuzz_campaigns.sh',
            'scripts/ci_scope.py',
            '.github/actions/use-every-cpu/action.yml',
            '.github/actions/newest-compiler/action.yml',
            '.github/workflows/fuzz.yml',
            'CMakeLists.txt',
            'cmake/FindConvolverSndFile.cmake',
            'portable/config_parse.cpp',
            'portable/sound_files.hpp',
            'portable/io.cpp',
            'portable/state.cpp',
            'portable/engine.cpp',
            'portable/kernels.inc',
            'include/convolver/host.hpp',
        )
        skip = (
            'tests/fuzz/README.md',  # Documentation.
            'portable/host.cpp',
            'portable/presets.cpp',
            'portable/filter_file.cpp',
            'portable/main.cpp',
            'portable/inspection.cpp',
            'portable/fft_ooura.cpp',  # Not the runner's FFT backend.
            'portable/third_party/ooura/fftsg.cpp',
            'include/convolver/inspection.hpp',
            'tests/native/engine_tests.cpp',
            'tests/plugins/harness.hpp',
            'tests/compatibility/README.md',
            'historical/Common/foo.cpp',
            'scripts/release/resolve.py',
            'plugins/vst3/processor.cpp',
            '.github/workflows/regression.yml',
            'CMakePresets.json',
            'VERSION',
            'TODO.md',
        )
        for path in fuzz:
            with self.subTest(path=path):
                self.assertIn('fuzz', ci_scope.needed([path]))
                self.assertTrue(ci_scope.decide([path], 'fuzz')['build'])
        for path in skip:
            with self.subTest(path=path):
                self.assertNotIn('fuzz', ci_scope.needed([path]))
                self.assertFalse(ci_scope.decide([path], 'fuzz')['build'])
        # One fuzzed file among others runs them; nothing listed, or no list, runs everything.
        self.assertIn('fuzz', ci_scope.needed(['portable/host.cpp', 'TODO.md', 'portable/state.cpp']))
        self.assertIn('fuzz', ci_scope.needed(['plugins/lv2/plugin.cpp', '.github/workflows/fuzz.yml']))
        self.assertEqual(ci_scope.needed([]), set(ci_scope.WORKFLOWS))
        # Other code still runs every other workflow.
        self.assertEqual(ci_scope.needed(['portable/host.cpp']), set(ci_scope.WORKFLOWS) - {'fuzz', 'release'})

    def test_fuzzed_entries_exist_and_hold_their_includes(self):
        """Every FUZZED entry exists, and the sources it lists include no
        repository header it leaves out. The fuzz workflow checks the rest,
        the files the linker brings in, from the targets themselves."""
        include = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
        for entry in ci_scope.FUZZED:
            with self.subTest(entry=entry):
                self.assertTrue((ROOT / entry).exists())
        for source, text in tracked():
            if not ci_scope.matches(source, ci_scope.FUZZED) or not source.endswith(('.cpp', '.hpp', '.inc', '.h')):
                continue
            for name in include.findall(text):
                for base in (ROOT / source).parent, ROOT / 'portable', ROOT / 'include':
                    header = (base / name).resolve()
                    if header.is_file():
                        relative = header.relative_to(ROOT).as_posix()
                        with self.subTest(source=source, header=relative):
                            self.assertTrue(ci_scope.matches(relative, ci_scope.FUZZED))
                        break

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
                    needs = re.search(r'^    needs: (\S+|\[[a-z0-9_, -]+\])$', body, re.MULTILINE)
                    self.assertIsNotNone(needs, f'{name} must need scope or another job')
                    if needs.group(1) == 'scope':
                        gate = ADVISORY_GATE if (workflow.name, name) in ADVISORY else GATE
                        self.assertRegex(body, gate, name)
                    # A job that runs despite a skipped dependency would undo the gate.
                    self.assertNotRegex(body, OVERRIDE, name)

    def test_scope_jobs_carry_and_limit_drafts(self):
        """Every gated workflow's scope job can read earlier runs, marks a full
        run with PROVING_STEP (CI-53), and a draft pull request starts no
        macOS or Windows job (CI-54): such a job is gated on full, needs one
        that is, or takes its matrix from the scope job."""
        for workflow in sorted(WORKFLOWS.glob('*.yml')):
            if workflow.stem not in ci_scope.WORKFLOWS:
                continue
            text = workflow.read_text(encoding='utf-8')
            found = jobs(text)
            with self.subTest(workflow=workflow.name):
                self.assertIn('types: [opened, synchronize, reopened, ready_for_review]', text)
                scope = found['scope']
                self.assertIn('      actions: read\n', scope)
                self.assertIn('GITHUB_TOKEN: ${{ github.token }}', scope)
                self.assertIn(f"      - name: '{ci_scope.PROVING_STEP}'\n", scope)
                self.assertIn("        if: steps.scope.outputs.proving == 'true'\n", scope)
                self.assertIn('      full: ${{ steps.scope.outputs.full }}\n', scope)
                limited = {name for name, body in found.items() if FULL_GATE.search(body)}
                for name, body in found.items():
                    runner = re.search(r'^    runs-on: (.+)$', body, re.MULTILINE).group(1)
                    if runner == '${{ matrix.os }}':
                        matrix = SCOPED_MATRIX.search(body)
                        self.assertIsNotNone(matrix, f'{name} must take its matrix from the scope job')
                        self.assertIn(f'--matrix {matrix.group(1)}=', scope)
                        self.assertIn(
                            f'      {matrix.group(1)}: ${{{{ steps.scope.outputs.{matrix.group(1)} }}}}', scope
                        )
                    elif not runner.startswith('ubuntu'):
                        needs = re.search(r'^    needs: (.+)$', body, re.MULTILINE).group(1).strip('[]')
                        self.assertTrue(
                            name in limited or limited & {need.strip() for need in needs.split(',')},
                            f'{name} runs on {runner}, so a draft must skip it',
                        )

    def test_scoped_matrices_parse(self):
        """Each matrix listed in a scope job is JSON once its comment lines go,
        names a runner for every entry, and keeps a Linux entry for drafts."""
        for workflow in sorted(WORKFLOWS.glob('*.yml')):
            scope = jobs(workflow.read_text(encoding='utf-8')).get('scope', '')
            for name, variable in re.findall(r'--matrix ([a-z0-9]+)=([A-Z0-9_]+)', scope):
                with self.subTest(workflow=workflow.name, matrix=name):
                    block = re.search(rf'^          {variable}: \|\n((?:            .*\n)+)', scope, re.MULTILINE)
                    self.assertIsNotNone(block)
                    text = block.group(1)
                    every = ci_scope.matrix(text, full=True)
                    drafts = ci_scope.matrix(text, full=False)
                    self.assertTrue(all(entry.get('os') for entry in every))
                    self.assertTrue(drafts)
                    self.assertTrue(all(entry['os'].startswith('ubuntu') for entry in drafts))
                    self.assertLess(len(drafts), len(every))

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
        'cmake/ConvolverPluginPackaging.cmake': 'packages the LV2 bundle on Linux, in the candidates (CI-34)',
        'scripts/release/candidate.py': 'the candidates (CI-34: main, weekly, by hand)',
        'tests/release/source_build.py': 'a local check of the source archive; CI does not run it',
        'tests/release/lv2_bundle.py': "imports generate_ttl.py in the candidates' package checks",
        '.github/workflows/publish.yml': 'a dry-run trigger path; runs only on its own paths and by hand from main',
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
    '.github/workflows/publish.yml': 'runs only on its own paths and by hand from main',
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


# Files outside tests/upgrade/ that name its files, and the workflows whose
# builds they put them in (CI-42). Documentation, historical code and this
# script and its tests are not checked.
UPGRADE_REFERENCES = {
    'CMakeLists.txt': 'registers upgrade.cli wherever the CLI is built',
    'plugins/vst3/CMakeLists.txt': 'registers vst3.upgrade and builds convolver_vst3_render',
    'plugins/lv2/CMakeLists.txt': 'registers lv2.upgrade and builds convolver_lv2_render',
    '.github/workflows/au.yml': 'restores the Audio Unit state with run_upgrade.py --au-render',
    '.gitattributes': 'keeps the version folders byte for byte; builds nothing',
    '.markdownlint-cli2.yaml': 'Lint leaves the version folders alone',
    '.pre-commit-config.yaml': 'Lint leaves the version folders alone',
    'tests/identities_tests.py': "skips the version folders' release-made files; Lint runs it for every pull request",
}


class UpgradeIsolation(unittest.TestCase):
    """A pull request that changes only the files kept from releases runs
    the native, VST3, LV2 and Audio Unit workflows, so only the tests those workflows
    run may read them (CI-42)."""

    def test_only_the_vst3_and_lv2_workflows_run_the_ardour_test(self):
        pattern = re.compile(r'\bardour_(?:tests|session|reopen|harness_tests)\b')
        own = (*ci_scope.ARDOUR, 'scripts/ci_scope.py', 'tests/ci_scope_tests.py')
        found = {path for path, contents in tracked() if not path.startswith(own) and pattern.search(contents)}
        # Lint runs the harness tests through pre-commit for every pull request.
        self.assertEqual(found, {'.github/workflows/vst3.yml', '.github/workflows/lv2.yml', '.pre-commit-config.yaml'})

    def test_no_workflow_runs_the_manual_host_scripts(self):
        pattern = re.compile(r'\breaper_check(?:_tests)?\b')
        own = (*ci_scope.MANUAL, 'scripts/ci_scope.py', 'tests/ci_scope_tests.py')
        found = {path for path, contents in tracked() if path not in own and pattern.search(contents)}
        # Only lint's pre-commit runs their tests.
        self.assertEqual(found, {'.pre-commit-config.yaml'})

    def test_record_checks_build_nothing_and_versioning_runs_native(self):
        # DOC-20: #257 changed only these and still ran every plug-in workflow.
        self.assertEqual(ci_scope.needed([*ci_scope.RECORD_CHECKS, 'changes/DOC-20.md', 'AGENTS.md']), set())
        self.assertEqual(ci_scope.needed([*ci_scope.VERSIONING, *ci_scope.RECORD_CHECKS]), {'regression'})
        self.assertEqual(
            ci_scope.needed(['scripts/version_source.py', '.github/workflows/version.yml']), {'regression'}
        )
        # Code beside them still runs every plug-in workflow.
        self.assertIn('vst3', ci_scope.needed(['scripts/fold_changes.py', 'portable/engine.cpp']))

    def test_only_pre_commit_runs_the_record_checks(self):
        pattern = re.compile(
            r'\b(?:check_repository|check_doc_links|fold_changes|repository_checks_tests|documentation_links_tests)\b'
        )
        own = (*ci_scope.RECORD_CHECKS, 'scripts/ci_scope.py', 'tests/ci_scope_tests.py')
        found = {
            path
            for path, contents in tracked()
            if path not in own and not path.endswith('.md') and pattern.search(contents)
        }
        self.assertEqual(found, {'.pre-commit-config.yaml'})

    def test_only_ctest_pre_commit_and_the_version_workflow_run_the_version_helper(self):
        pattern = re.compile(r'\b(?:version_source|versioning_tests)\b')
        own = (*ci_scope.VERSIONING, 'scripts/ci_scope.py', 'tests/ci_scope_tests.py')
        found = {
            path
            for path, contents in tracked()
            if path not in own and not path.endswith('.md') and pattern.search(contents)
        }
        self.assertEqual(found, {'CMakeLists.txt', '.pre-commit-config.yaml', '.github/workflows/version.yml'})

    def test_only_listed_files_name_the_upgrade_files(self):
        # A relative path such as parents[1] / 'upgrade/x' counts too.
        pattern = re.compile(
            r'tests/upgrade\b|(?<![\w./-])upgrade/|\brun_upgrade\b|\b(?:vst3|lv2)_render\.cpp\b|\bcapture(?:_lv2|_au)?\.py\b'
        )
        own = (*ci_scope.UPGRADE, 'scripts/ci_scope.py', 'tests/ci_scope_tests.py')
        found = {path for path, contents in tracked() if not path.startswith(own) and pattern.search(contents)}
        self.assertEqual(found, set(UPGRADE_REFERENCES))

    def test_the_tests_that_read_them(self):
        """upgrade.cli is registered with the CLI, which the native jobs
        build, vst3.upgrade with the VST3 plug-in and lv2.upgrade with the
        LV2 plug-in; no other CTest entry runs the checker."""
        registered = {}
        for path in UPGRADE_REFERENCES:
            text = (ROOT / path).read_text(encoding='utf-8')
            for name, body in re.findall(r'add_test\(NAME (\S+) (.*?)\)\n', text, re.DOTALL):
                if 'run_upgrade.py' in body:
                    registered[name] = path
        self.assertEqual(
            registered,
            {
                'upgrade.cli': 'CMakeLists.txt',
                'vst3.upgrade': 'plugins/vst3/CMakeLists.txt',
                'lv2.upgrade': 'plugins/lv2/CMakeLists.txt',
            },
        )
        self.assertEqual(ci_scope.classify('plugins/vst3/CMakeLists.txt'), 'format:vst3')
        self.assertEqual(ci_scope.classify('plugins/lv2/CMakeLists.txt'), 'format:lv2')
        self.assertLessEqual(ci_scope.UPGRADE_WORKFLOWS, set(ci_scope.WORKFLOWS))
        self.assertIn('vst3', ci_scope.UPGRADE_WORKFLOWS)
        self.assertIn('regression', ci_scope.UPGRADE_WORKFLOWS)
        self.assertIn('lv2', ci_scope.UPGRADE_WORKFLOWS)
        self.assertIn('au', ci_scope.UPGRADE_WORKFLOWS)


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
        self.assertEqual(written, 'build=false\npackage=false\nfull=true\nproving=false\n')
        self.assertIn('Only documentation changed', printed)

    def test_push_and_manual_runs_test_everything(self):
        for event in ('push', 'workflow_dispatch', 'schedule'):
            with self.subTest(event=event):
                self.assertEqual(self.run_script(event=event)[0], 'build=true\npackage=true\nfull=true\nproving=true\n')

    def test_fuzz_workflow_answer(self):
        written, printed = self.run_script('--workflow', 'fuzz', 'portable/host.cpp')
        self.assertEqual(written, 'build=false\npackage=false\nfull=true\nproving=false\n')
        self.assertIn('The fuzz targets do not run', printed)
        written, printed = self.run_script('--workflow', 'fuzz', 'portable/config_parse.cpp')
        self.assertEqual(written, 'build=true\npackage=false\nfull=true\nproving=true\n')
        self.assertNotIn('The fuzz targets do not run', printed)

    def test_fuzz_sources(self):
        """--fuzz-sources passes for the listed sources, ignores files outside
        the repository's tracked ones, and fails for an unlisted repository
        file or a list with no harness in it."""

        def check(*lines):
            return subprocess.run(
                [sys.executable, str(SCRIPT), '--fuzz-sources', '-'],
                check=False,
                cwd=ROOT,
                env=CLEAN,
                input=''.join(line + '\n' for line in lines),
                capture_output=True,
                text=True,
            )

        harnesses = sorted(str(path) for path in (ROOT / 'tests/fuzz').glob('fuzz_*.cpp'))
        self.assertEqual(len(harnesses), 4)
        listed = (*harnesses, str(ROOT / 'portable/state.cpp'), 'include/convolver/host.hpp')
        outside = ('/usr/include/c++/13/string', str(ROOT / 'build-fuzz/<stdin>'), '<stdin>')
        result = check(*listed, *outside)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = check(*listed, str(ROOT / 'portable/host.cpp'))
        self.assertEqual(result.returncode, 1)
        self.assertIn('  portable/host.cpp', result.stderr)
        self.assertNotIn('state.cpp', result.stderr)
        # A file the build wrote (a generated header) has inputs FUZZED may not name.
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            written = Path(temporary) / 'generated' / 'convolver_identities.hpp'
            written.parent.mkdir()
            written.write_text('// generated\n')
            result = check(*listed, str(written))
            self.assertEqual(result.returncode, 1)
            self.assertIn('written by the build', result.stderr)
            self.assertIn(written.relative_to(ROOT).as_posix(), result.stderr)
            with mock.patch.object(ci_scope, 'FUZZED_GENERATED', ('convolver_identities.hpp',)):
                self.assertEqual(ci_scope.fuzz_sources([*listed, str(written)], ROOT), ([], [], []))
        # Every harness must be there: a list from fewer targets, or none, fails.
        result = check(*listed[1:])
        self.assertEqual(result.returncode, 1)
        self.assertIn('  tests/fuzz/' + Path(harnesses[0]).name, result.stderr)
        self.assertEqual(check().returncode, 1)

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
            self.assertEqual(written, 'build=false\npackage=false\nfull=true\nproving=false\n')
            self.assertIn('documentation docs/guide.md', printed)
            self.assertIn('documentation docs/guidé.md', printed)
            self.assertNotIn('engine.cpp', printed)
            # Without a merge commit the changes are unknown, so everything runs.
            git('checkout', '-q', 'topic')
            written, printed = self.run_script(cwd=repository)
            self.assertEqual(written, 'build=true\npackage=true\nfull=true\nproving=true\n')
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
            self.assertEqual(
                merged('docs/use/prose.md', 'Changed prose.\n'),
                'build=false\npackage=false\nfull=true\nproving=false\n',
            )
            self.assertEqual(
                merged('docs/use/marked.md', 'No commands now.\n'),
                'build=true\npackage=true\nfull=true\nproving=true\n',
            )
            self.assertEqual(
                merged('docs/use/prose.md', '<!-- refused-example -->\n```sh\nconvolver\n```\n'),
                'build=true\npackage=true\nfull=true\nproving=true\n',
            )
            self.assertEqual(
                merged('docs/use/added.md', 'New prose.\n'), 'build=false\npackage=false\nfull=true\nproving=false\n'
            )
            self.assertEqual(
                merged('docs/use/marked.md', None), 'build=true\npackage=true\nfull=true\nproving=true\n'
            )  # Deleted.
            self.assertEqual(
                merged('docs/use/prose.md', None), 'build=false\npackage=false\nfull=true\nproving=false\n'
            )
            # A page that cannot be read counts as marked, even when the other side is prose.
            self.assertEqual(
                merged('docs/use/prose.md', b'\xff<!-- prose -->\n'),
                'build=true\npackage=true\nfull=true\nproving=true\n',
            )


class CarryForward(unittest.TestCase):
    """CI-53: a push carries a passed result forward when nothing the workflow
    needs changed since its last full run; CI-54: drafts run Linux only."""

    REPOSITORY = 'owner/name'
    BASE = 'a' * 40
    HEAD = 'b' * 40

    def setUp(self):
        environment = {
            'GITHUB_TOKEN': 'token',
            'GITHUB_REPOSITORY': self.REPOSITORY,
            'GITHUB_RUN_ID': '99',
            'GITHUB_RUN_ATTEMPT': '1',
            'GITHUB_REF': 'refs/heads/main',
            'GITHUB_SHA': self.HEAD,
        }
        patcher = mock.patch.dict(os.environ, environment)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.runs = [
            self.run_record(99, self.HEAD),  # This run: never compared with.
            self.run_record(7, self.BASE, proving=False),  # Carried or draft-limited.
            self.run_record(5, self.BASE),
        ]
        self.comparison = {'status': 'ahead', 'files': [{'filename': 'TODO.md'}]}
        self.requests = []
        patcher = mock.patch.object(ci_scope, 'github', self.github)
        patcher.start()
        self.addCleanup(patcher.stop)

    def run_record(self, number, sha, *, proving=True):
        return {
            'id': number,
            'head_sha': sha,
            'html_url': f'https://example.invalid/{number}',
            'head_repository': {'full_name': self.REPOSITORY},
            'proving': proving,
        }

    def github(self, path):
        self.requests.append(path)
        if '/compare/' in path:
            return self.comparison
        if path.endswith('/jobs?filter=latest&per_page=100'):
            number = int(path.split('/runs/')[1].split('/')[0])
            run = next(run for run in self.runs if run['id'] == number)
            conclusion = 'success' if run['proving'] else 'skipped'
            steps = [{'name': 'Run actions/checkout@v7', 'conclusion': 'success'}]
            steps.append({'name': ci_scope.PROVING_STEP, 'conclusion': conclusion})
            return {'jobs': [{'name': 'sdk', 'steps': []}, {'name': 'scope', 'steps': steps}]}
        return {'workflow_runs': self.runs}

    def pull_request(self, *, draft=False, repository=REPOSITORY):
        return {
            'pull_request': {
                'draft': draft,
                'head': {'ref': 'topic', 'sha': self.HEAD, 'repo': {'full_name': repository}},
            }
        }

    def test_compares_with_the_last_full_passed_run(self):
        paths, run = ci_scope.carried_from('vst3', 'pull_request', self.pull_request())
        self.assertEqual((paths, run['id']), (['TODO.md'], 5))
        self.assertIn('event=pull_request&branch=topic&status=success', self.requests[0])
        self.assertTrue(self.requests[-1].endswith(f'/compare/{self.BASE}...{self.HEAD}'))
        paths, run = ci_scope.carried_from('vst3', 'push', {})
        self.assertEqual(run['id'], 5)
        self.assertIn('event=push&branch=main&status=success', self.requests[-4])
        # A run of another repository's branch of the same name is passed over.
        self.runs[2]['head_repository'] = {'full_name': 'fork/name'}
        self.runs.append(self.run_record(3, self.BASE))
        _, run = ci_scope.carried_from('vst3', 'pull_request', self.pull_request())
        self.assertEqual(run['id'], 3)
        # The same commit (a pull request marked ready again): nothing changed.
        self.comparison = {'status': 'identical', 'files': []}
        self.assertEqual(ci_scope.carried_from('vst3', 'pull_request', self.pull_request())[0], [])

    def test_renames_count_both_names(self):
        self.comparison['files'] = [{'filename': 'docs/new.md', 'previous_filename': 'portable/old.cpp'}]
        paths, _ = ci_scope.carried_from('regression', 'pull_request', self.pull_request())
        self.assertEqual(paths, ['docs/new.md', 'portable/old.cpp'])

    def test_nothing_carried(self):
        def why(event='pull_request', payload=None):
            paths, reason = ci_scope.carried_from('vst3', event, self.pull_request() if payload is None else payload)
            self.assertIsNone(paths)
            return reason

        self.assertIn('another repository', why(payload=self.pull_request(repository='fork/name')))
        self.assertIn('carry nothing', why('workflow_dispatch'))
        for status in ('diverged', 'behind'):
            self.comparison['status'] = status
            self.assertIn('not an ancestor', why())
        self.comparison = {'status': 'ahead', 'files': [{'filename': f'f{n}'} for n in range(300)]}
        self.assertIn('more than 299 files', why())
        self.runs[2]['proving'] = False
        self.assertIn('no full passed run', why())
        with mock.patch.dict(os.environ, {'GITHUB_RUN_ATTEMPT': '2'}):
            self.assertIn('re-ran', why())
        with mock.patch.dict(os.environ, {'GITHUB_TOKEN': ''}):
            self.assertIn('no token', why())
        with mock.patch.object(ci_scope, 'github', side_effect=OSError('refused')):
            self.assertIn('could not look up', why())

    def answer(self, event, changed, since, workflow='vst3', *, draft=False):
        """What main() writes for a run, given the pull request's files and
        those changed since the last full run (None: nothing to carry)."""
        with tempfile.TemporaryDirectory() as temporary:
            output, payload = Path(temporary) / 'output', Path(temporary) / 'event.json'
            payload.write_text(json.dumps(self.pull_request(draft=draft) if event == 'pull_request' else {}))
            carried = (since, self.run_record(5, self.BASE)) if since is not None else (None, 'no run')
            environment = {'GITHUB_EVENT_PATH': str(payload), 'GITHUB_STEP_SUMMARY': ''}
            with mock.patch.dict(os.environ, environment), mock.patch.object(
                ci_scope, 'changed_files', return_value=changed
            ), mock.patch.object(ci_scope, 'carried_from', return_value=carried), mock.patch('sys.stdout'):
                ci_scope.main(['--event', event, '--workflow', workflow, '--github-output', str(output)])
            return dict(line.split('=', 1) for line in output.read_text().splitlines())

    def test_pull_request_decisions(self):
        code = ['portable/host.cpp']
        # Only documentation since the last full run: the result stands.
        self.assertEqual(
            self.answer('pull_request', code, ['TODO.md']),
            {'build': 'false', 'package': 'false', 'full': 'true', 'proving': 'false'},
        )
        # Nothing changed at all (marked ready again, say): it stands too.
        self.assertEqual(self.answer('pull_request', code, [])['build'], 'false')
        # Another format's files: this workflow's result stands, that one runs.
        self.assertEqual(self.answer('pull_request', code, ['plugins/au/adapter.mm'])['build'], 'false')
        self.assertEqual(self.answer('pull_request', code, ['plugins/au/adapter.mm'], 'au')['build'], 'true')
        # Code since then runs, and later pushes compare with it.
        self.assertEqual(self.answer('pull_request', code, code)['proving'], 'true')
        # No run to compare with: as before CI-53.
        self.assertEqual(self.answer('pull_request', code, None)['proving'], 'true')
        # A draft builds, on Linux only, and proves nothing for later pushes.
        self.assertEqual(
            self.answer('pull_request', code, code, draft=True),
            {'build': 'true', 'package': 'false', 'full': 'false', 'proving': 'false'},
        )
        # A page whose first-use commands a test runs counts as code once
        # carried, whatever page_kind() says when comparing with main.
        with mock.patch.object(ci_scope, 'commands_changed', return_value=False):
            self.assertEqual(self.answer('pull_request', code, ['README.md'])['build'], 'true')
        # A pull request this workflow does not need stays skipped.
        self.assertEqual(self.answer('pull_request', ['TODO.md'], code)['build'], 'false')

    def test_main_runs_everything_after_code(self):
        """jrp, 21 November 2026: main keeps the full run when code or tests
        change; a merge of documentation alone runs Lint."""
        self.assertEqual(self.answer('push', None, ['TODO.md', 'docs/develop/build.md'])['build'], 'false')
        self.assertEqual(self.answer('push', None, ['TODO.md'], 'release')['package'], 'false')
        # The version bot's commit after the previous merge is always there.
        self.assertEqual(self.answer('push', None, ['VERSION', 'CHANGELOG.md'])['build'], 'false')
        changes = (
            ['plugins/au/adapter.mm'],
            ['tests/plugins/fixtures.py'],
            ['portable/host.cpp'],
            # No workflow but Lint runs these, yet they are code and tests.
            ['scripts/reaper_check.py'],
            ['tests/reaper_check_tests.py'],
            ['.github/workflows/lint.yml'],
            ['.github/workflows/publish.yml'],
            ['README.md', 'VERSION'],
        )
        for since in changes:
            with self.subTest(since=since):
                self.assertEqual(
                    self.answer('push', None, since),
                    {'build': 'true', 'package': 'true', 'full': 'true', 'proving': 'true'},
                )
        self.assertEqual(self.answer('push', None, None)['proving'], 'true')

    def test_draft_matrices(self):
        text = '[\n  # A comment\n  {"os": "ubuntu-latest"},\n  {"os": "macos-latest"},\n  {"os": "windows-2022"}\n]'
        self.assertEqual(
            ci_scope.matrix(text, full=True), [{'os': 'ubuntu-latest'}, {'os': 'macos-latest'}, {'os': 'windows-2022'}]
        )
        self.assertEqual(ci_scope.matrix(text, full=False), [{'os': 'ubuntu-latest'}])
        self.assertFalse(ci_scope.full_run('pull_request', self.pull_request(draft=True)))
        self.assertTrue(ci_scope.full_run('pull_request', self.pull_request()))
        self.assertTrue(ci_scope.full_run('push', {}))


if __name__ == '__main__':
    unittest.main()
