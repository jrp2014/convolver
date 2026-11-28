#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Behaviour tests for scripts/check_repository.py, on small synthetic trees."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

CHECKER = Path(__file__).resolve().parents[1] / 'scripts' / 'check_repository.py'
SPDX_MD = '<!-- SPDX-License-Identifier: GPL-2.0-or-later -->\n'
SPDX_CPP = '// SPDX-License-Identifier: GPL-2.0-or-later\n'

TODO = SPDX_MD + (
    '# TODO\n\n- [ ] **PLUG-2 — Harness.** Open.\n- [x] **PLUG-1 — FFT.** Done.\n- [ ] **LV2-1 — Adapter.** Open.\n'
)
BASE = {
    'README.md': SPDX_MD + '# Home\nSee PLUG-2.\n',
    'TODO.md': TODO,
    'AGENTS.md': SPDX_MD + '# Rules\nRead `docs/README.md`.\n',
    'CONTRIBUTING.md': SPDX_MD + '# Contributing\n',
    'docs/README.md': SPDX_MD
    + (
        '# Index\n[Guide](use/guide.md)\n[Map](develop/architecture.md)\n'
        '[Workflow](develop/workflow.md)\n'
        '[Plan](project/plans/2026-10-21-formats.md)\n'
    ),
    'docs/use/guide.md': SPDX_MD + '# Guide\nPLUG-1 is done; LV2-1 is next.\n',
    'docs/develop/architecture.md': SPDX_MD + '# Map\nSee `portable/engine.cpp`.\n',
    'docs/develop/workflow.md': SPDX_MD + '# Workflow\n',
    'docs/project/plans/2026-10-21-formats.md': SPDX_MD + '# Plan\nServes PLUG-2.\n',
    'portable/engine.cpp': SPDX_CPP,
}


class RepositoryChecks(unittest.TestCase):
    def run_check(self, changes=None, remove=()):
        files = dict(BASE)
        files.update(changes or {})
        for name in remove:
            del files[name]
        with tempfile.TemporaryDirectory(prefix='convolver-repo-rules-') as tmp:
            root = Path(tmp)
            for name, content in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding='utf-8')
            # A commit hook in a linked worktree exports GIT_DIR, which would
            # make these synthetic trees look like the real repository.
            env = {key: value for key, value in os.environ.items() if not key.startswith('GIT_')}
            return subprocess.run(
                [sys.executable, str(CHECKER), '--root', str(root)],
                capture_output=True,
                text=True,
                check=False,
                env=env,
            )

    def assertBroken(self, result, *fragments):
        self.assertEqual(result.returncode, 1, result.stdout)
        for fragment in fragments:
            self.assertIn(fragment, result.stderr)

    def test_consistent_tree_passes(self):
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_undefined_and_duplicate_todo_ids(self):
        self.assertBroken(
            self.run_check({'docs/use/guide.md': SPDX_MD + 'See PLUG-9 and UTF-8.\n'}),
            'docs/use/guide.md:2: PLUG-9 is not defined',
        )
        result = self.run_check({'docs/use/guide.md': SPDX_MD + 'See PLUG-9 and UTF-8.\n'})
        self.assertNotIn('UTF-8', result.stderr)
        self.assertBroken(
            self.run_check({'TODO.md': TODO + '- [ ] **PLUG-2 — Again.**\n'}), 'PLUG-2 is defined more than once'
        )

    def test_archived_todo_ids_count_as_done(self):
        archive = SPDX_MD + '# Done\n\n- [x] **PLUG-3 — Presets.** Done.\n'
        result = self.run_check(
            {
                'docs/archive/todo/2026-11-08-done-items.md': archive,
                'docs/archive/todo/2026-11-06-done-entries.md': archive,
                'docs/use/guide.md': SPDX_MD + 'PLUG-3 is done; LV2-1 is next.\n',
            }
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertBroken(
            self.run_check(
                {
                    'docs/README.md': BASE['docs/README.md'] + '[Old](project/plans/2026-10-18-old.md)\n',
                    'docs/archive/todo/2026-11-08-done-items.md': archive,
                    'docs/project/plans/2026-10-18-old.md': SPDX_MD + 'Delivered PLUG-3.\n',
                }
            ),
            'docs/project/plans/2026-10-18-old.md: every TODO item it names is done',
        )
        self.assertBroken(
            self.run_check(
                {'docs/archive/todo/2026-11-08-done-items.md': SPDX_MD + '- [x] **PLUG-2 — Harness.** Done.\n'}
            ),
            'PLUG-2 is also listed as done in docs/archive/todo/',
        )

    def test_change_fragments_define_finished_ids(self):
        # DOC-20: a finished item's done line waits in its fragment until folded.
        done = SPDX_MD + '# PLUG-3\n\n## Done\n\n- [x] **PLUG-3 — Presets.** Done.\n'
        result = self.run_check(
            {
                'changes/README.md': SPDX_MD + '# Fragments\n',
                'changes/PLUG-3.md': done,
                'docs/use/guide.md': SPDX_MD + 'PLUG-3 is done; LV2-1 is next.\n',
            }
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertBroken(
            self.run_check({'changes/PLUG-2.md': SPDX_MD + '## Done\n\n- [x] **PLUG-2 — Harness.** Done.\n'}),
            'changes/PLUG-2.md: PLUG-2 is already defined in TODO.md',
        )
        self.assertBroken(
            self.run_check({'changes/PLUG-4.md': SPDX_MD + '## Done\n\n- [ ] **PLUG-4 — Open.** Open.\n'}),
            'changes/PLUG-4.md: PLUG-4 is not ticked',
        )
        self.assertBroken(
            self.run_check({'changes/PLUG-3.md': SPDX_MD + '## Changed\n\n- Uses PLUG-9.\n'}),
            'changes/PLUG-3.md:4: PLUG-9 is not defined',
        )
        self.assertBroken(self.run_check({'changes/PLUG-3.md': '# PLUG-3\n'}), 'changes/PLUG-3.md: add an SPDX')
        # A ticked line outside Done would fold into another page: it defines nothing.
        misplaced = SPDX_MD + '# PLUG-3\n\n## Changed\n\n- [x] **PLUG-3 — Presets.** Done.\n'
        self.assertBroken(
            self.run_check(
                {'changes/PLUG-3.md': misplaced, 'docs/use/guide.md': SPDX_MD + 'PLUG-3 is done; LV2-1 is next.\n'}
            ),
            'docs/use/guide.md:2: PLUG-3 is not defined',
        )

    def test_unindexed_page(self):
        self.assertBroken(
            self.run_check({'docs/new.md': SPDX_MD + '# New\n'}), 'docs/new.md: not listed in docs/README.md'
        )

    def test_records_are_exempt(self):
        result = self.run_check({'docs/archive/old.md': '# Old\nPLUG-99\n', 'historical/a.cpp': 'int a;\n'})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_plan_rules(self):
        index = (
            SPDX_MD + '[Plan](project/plans/2026-10-21-formats.md)\n[Bad](project/plans/notes.md)\n'
            '[Done](project/plans/2026-10-18-old.md)\n'
        )
        result = self.run_check(
            {
                'docs/README.md': index,
                'docs/project/plans/notes.md': SPDX_MD + 'PLUG-2\n',
                'docs/project/plans/2026-10-18-old.md': SPDX_MD + 'Delivered PLUG-1.\n',
            }
        )
        self.assertBroken(
            result,
            'docs/project/plans/notes.md: plan names start with their date',
            'docs/project/plans/2026-10-18-old.md: every TODO item it names is done',
        )
        self.assertBroken(
            self.run_check(
                {'docs/archive/plans/README.md': '# Archive\n', 'docs/archive/plans/2026-10-01-x.md': '# X\n'}
            ),
            'archived plan missing from docs/archive/plans/README.md',
        )

    def test_paths_in_agent_guidance(self):
        self.assertBroken(
            self.run_check({'AGENTS.md': SPDX_MD + 'Edit `portable/missing.cpp` and `portable/*.cpp`.\n'}),
            'AGENTS.md:2: `portable/missing.cpp` does not exist',
        )
        result = self.run_check(
            {'AGENTS.md': SPDX_MD + 'Edit `portable/*.cpp` or `plugins/<format>/`, run `ctest -L x`.\n'}
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_path_source_is_reported(self):
        self.assertBroken(
            self.run_check(remove=('docs/develop/workflow.md',)),
            'docs/develop/workflow.md: missing; its repository paths cannot be checked',
        )

    def test_maintained_pages_sit_in_reader_folders(self):
        index = BASE['docs/README.md'] + '[Loose](loose.md)\n'
        self.assertBroken(
            self.run_check({'docs/loose.md': SPDX_MD + '# Loose\n', 'docs/README.md': index}),
            'docs/loose.md: maintained pages belong in a reader folder',
        )
        result = self.run_check({'docs/archive/old.md': '# Old record\n'})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_spdx_required_on_sources_but_not_vendored_code(self):
        self.assertBroken(
            self.run_check({'portable/new.cpp': 'int x;\n'}), 'portable/new.cpp: add an SPDX-License-Identifier line'
        )
        result = self.run_check({'portable/third_party/lib/x.cpp': 'int x;\n'})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_top_level_layout_and_adapter_completeness(self):
        self.assertBroken(self.run_check({'notes.txt': 'scratch\n'}), 'notes.txt: not an expected top-level entry')
        # The adapters' shared window code is not an adapter.
        result = self.run_check({'plugins/editor/mac/view.mm': SPDX_CPP})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertBroken(
            self.run_check({'plugins/lv2/plugin.cpp': SPDX_CPP}),
            'plugins/lv2/: adapter has no tests in tests/lv2/',
            'adapter is not described in docs/use/plugins.md',
        )
        plugins_page = {
            'docs/use/plugins.md': SPDX_MD + '# Plug-ins\n| VST3 |\n',
            'docs/README.md': BASE['docs/README.md'] + '[Plug-ins](use/plugins.md)\n',
        }
        self.assertBroken(
            self.run_check({'plugins/lv2/plugin.cpp': SPDX_CPP, 'tests/lv2/host_test.cpp': SPDX_CPP, **plugins_page}),
            'plugins/lv2/: adapter is not described in docs/use/plugins.md',
        )
        result = self.run_check(
            {
                'plugins/lv2/plugin.cpp': SPDX_CPP,
                'tests/lv2/host_test.cpp': SPDX_CPP,
                'docs/use/plugins.md': SPDX_MD + '# Plug-ins\n| VST3 | LV2 |\n',
                'docs/README.md': plugins_page['docs/README.md'],
            }
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_build_options(self):
        # DOC-2: -D options in guides and CI must exist, with allowed values,
        # and the build guide must describe every option.
        spdx_cmake = '# SPDX-License-Identifier: GPL-2.0-or-later\n'
        tree = {
            'CMakeLists.txt': spdx_cmake
            + 'option(CONVOLVER_BUILD_X "X" OFF)\n'
            + 'set(CONVOLVER_FFT_BACKEND "AUTO" CACHE STRING "FFT")\n'
            + 'set_property(CACHE CONVOLVER_FFT_BACKEND PROPERTY STRINGS AUTO FFTW)\n',
            'tests/consumer/CMakeLists.txt': spdx_cmake + 'set(CONVOLVER_FORCED ON CACHE BOOL "" FORCE)\n',
            'docs/develop/build.md': SPDX_MD + '# Build\n`CONVOLVER_BUILD_X` and `CONVOLVER_FFT_BACKEND`.\n',
            'docs/README.md': BASE['docs/README.md'] + '[Build](develop/build.md)\n',
            'docs/use/guide.md': SPDX_MD
            + '# Guide\n`cmake -DCONVOLVER_FFT_BACKEND=FFTW -DCONVOLVER_BUILD_X=ON`\n'
            + '`-DCONVOLVER_FFT_BACKEND=fftw` works too: CMake upper-cases it.\n',
        }
        result = self.run_check(tree)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertBroken(
            self.run_check(
                {**tree, '.github/workflows/ci.yml': '# SPDX-License-Identifier: x\n-DCONVOLVER_BUILD_Y=ON\n'}
            ),
            '.github/workflows/ci.yml:2: -DCONVOLVER_BUILD_Y is not a CMake option',
        )
        self.assertBroken(
            self.run_check({**tree, 'docs/use/guide.md': SPDX_MD + '`-DCONVOLVER_FFT_BACKEND=FFTW3`\n'}),
            'docs/use/guide.md:2: CONVOLVER_FFT_BACKEND=FFTW3 is not one of AUTO, FFTW',
        )
        self.assertBroken(
            self.run_check({**tree, 'docs/use/guide.md': SPDX_MD + '`-DCONVOLVER_FFT_BACKEND=FFTW-x`\n'}),
            'CONVOLVER_FFT_BACKEND=FFTW-x is not one of',
        )
        # Options a test project forces are not the build's options.
        self.assertBroken(
            self.run_check({**tree, 'docs/use/guide.md': SPDX_MD + '`-DCONVOLVER_FORCED=ON`\n'}),
            '-DCONVOLVER_FORCED is not a CMake option',
        )
        self.assertBroken(
            self.run_check({**tree, 'docs/develop/build.md': SPDX_MD + '# Build\n`CONVOLVER_BUILD_X`\n'}),
            'docs/develop/build.md: does not describe the CMake option CONVOLVER_FFT_BACKEND',
        )
        # Dated records and open plans may name options that do not exist (yet).
        result = self.run_check(
            {
                **tree,
                'CHANGELOG.md': SPDX_MD + 'Removed `-DCONVOLVER_OLD`.\n',
                'docs/project/plans/2026-10-21-formats.md': SPDX_MD + 'Serves PLUG-2 with `-DCONVOLVER_NEW`.\n',
            }
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_cli_options(self):
        # DOC-2: options shown with a Convolver tool must be ones it parses
        # (comments do not count), and its guide must describe each of them.
        tree = {
            'portable/main.cpp': SPDX_CPP + 'if (o == "--gain-db" || o == "--help") {}\n// "--old"\n',
            'docs/use/command-line.md': SPDX_MD
            + '# CLI\n```sh\nconvolver --gain-db -3 --help\nflac --best a.wav\n```\n'
            + 'Use `--gain-db`, or `./bin/convolver --help`.\n',
            'docs/README.md': BASE['docs/README.md'] + '[CLI](use/command-line.md)\n',
        }
        result = self.run_check(tree)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertBroken(
            self.run_check({**tree, 'docs/use/guide.md': SPDX_MD + '```sh\nconvolver --old x\n```\n'}),
            'docs/use/guide.md:3: `convolver --old x`: --old is not an option of convolver',
        )
        self.assertBroken(
            self.run_check({**tree, 'docs/use/guide.md': SPDX_MD + 'Add `--gain-dB 3`.\n'}),
            '--gain-dB is not an option of any Convolver tool',
        )
        # Any path to the tool, each command of a pipeline, and continued lines.
        for command, line in (
            ('./build/convolver --old x', 3),
            ('.\\bin\\convolver.exe --old x', 3),
            ('ffmpeg -i a.flac - | convolver --old x', 3),
            ('convolver --help && convolver --old x', 3),
            ('convolver --gain-db -3 \\\n  --old x', 3),
        ):
            self.assertBroken(
                self.run_check({**tree, 'docs/use/guide.md': SPDX_MD + f'```sh\n{command}\n```\n'}),
                f'docs/use/guide.md:{line}: ',
            )
        # Another program's options after a pipe are not Convolver's.
        result = self.run_check(
            {**tree, 'docs/use/guide.md': SPDX_MD + '```sh\nconvolver --help | flac --best -\n```\n'}
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertBroken(
            self.run_check({**tree, 'docs/use/command-line.md': SPDX_MD + '# CLI\n`--help`\n'}),
            'docs/use/command-line.md: does not describe convolver --gain-db',
        )


MIB = 1024 * 1024


def binary(size):
    """Incompressible-looking bytes with a NUL, which Git treats as binary."""
    return (b'\0' + bytes(range(1, 256))) * (size // 256) + b'\0' * (size % 256)


class LargeFiles(unittest.TestCase):
    """CI-58: what a commit or pull request would add to Git history."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='convolver-large-files-')
        self.root = Path(self.tmp.name)
        # A commit hook's GIT_INDEX_FILE and the like must not reach the
        # scratch repository, or the real index could be changed.
        self.env = {key: value for key, value in os.environ.items() if not key.startswith('GIT_')}
        self.env.update(
            {
                'GIT_AUTHOR_NAME': 'Test',
                'GIT_AUTHOR_EMAIL': 'test@example.invalid',
                'GIT_COMMITTER_NAME': 'Test',
                'GIT_COMMITTER_EMAIL': 'test@example.invalid',
                'GIT_CONFIG_GLOBAL': os.devnull,
                'GIT_CONFIG_NOSYSTEM': '1',
            }
        )
        self.git('init', '-q', '-b', 'main')
        for name, content in BASE.items():
            self.write(name, content.encode())
        self.write('tests/data/fixture.wav', binary(3 * MIB))
        self.write('tests/data/small.wav', binary(MIB // 2))
        self.git('add', '-A')
        self.git('commit', '-q', '-m', 'base')

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *args):
        subprocess.run(['git', *args], cwd=self.root, env=self.env, check=True, capture_output=True)

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def stage(self, name, data):
        self.write(name, data)
        self.git('add', '--', name)

    def run_check(self, *args):
        return subprocess.run(
            [sys.executable, str(CHECKER), '--root', str(self.root), *args],
            capture_output=True,
            text=True,
            check=False,
            env=self.env,
        )

    def assertPasses(self, *args):
        result = self.run_check(*args)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_existing_history_is_not_flagged(self):
        # The base commit holds a 3 MiB fixture; nothing staged adds to it.
        self.assertPasses()

    def test_staged_large_binary_is_flagged_with_the_three_choices(self):
        self.stage('tests/host/project.aup3', binary(MIB + 1))
        result = self.run_check()
        self.assertEqual(result.returncode, 1, result.stdout)
        for fragment in (
            'tests/host/project.aup3: this change adds a 1.0 MiB binary file',
            'a deliberate fixture',
            '      /tests/host/project.aup3 large-fixture\n',
            'generated on demand',
            'test evidence',
        ):
            self.assertIn(fragment, result.stderr)
        # Only the staged blob counts: unstaging it leaves nothing to flag.
        self.git('rm', '-q', '--cached', '--', 'tests/host/project.aup3')
        self.assertPasses()

    def test_small_binaries_text_and_wav_fixtures_marked_deliberate_pass(self):
        self.stage('tests/data/short.wav', binary(MIB - 1))
        self.stage('docs/timings.csv', b'filter,ms\n' * (MIB // 4))  # 2.5 MiB of text
        self.assertPasses()
        self.stage('tests/data/long.wav', binary(2 * MIB))
        self.assertIn('tests/data/long.wav: this change adds a 2.0 MiB', self.run_check().stderr)
        self.stage('.gitattributes', b'tests/data/long.wav large-fixture\n')
        self.assertPasses()

    def test_enlarging_by_the_limit_is_flagged_and_moving_is_not(self):
        self.stage('tests/data/small.wav', binary(MIB // 2 + MIB))
        self.assertIn(
            'tests/data/small.wav: this change enlarges a binary file from 512 KiB to 1.5 MiB', self.run_check().stderr
        )
        self.stage('tests/data/small.wav', binary(MIB))  # Grows by half a MiB.
        self.assertPasses()
        self.git('reset', '-q', '--hard')
        self.git('mv', 'tests/data/fixture.wav', 'tests/data/renamed.wav')
        self.assertPasses()

    def test_base_checks_the_commits_since_it(self):
        self.stage('tests/out/convolver.o', binary(2 * MIB))
        self.git('commit', '-q', '-m', 'artifact')
        self.assertPasses()  # Nothing is staged now.
        result = self.run_check('--base', 'HEAD~1')
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn('tests/out/convolver.o: this change adds a 2.0 MiB binary file', result.stderr)
        self.assertPasses('--base', 'HEAD')
        # In CI the work tree's .gitattributes marks a deliberate fixture.
        self.stage('.gitattributes', b'tests/out/convolver.o large-fixture\n')
        self.git('commit', '-q', '-m', 'mark')
        self.assertPasses('--base', 'HEAD~2')

    def test_base_outside_a_repository_fails(self):
        with tempfile.TemporaryDirectory() as other:
            result = subprocess.run(
                [sys.executable, str(CHECKER), '--root', other, '--base', 'HEAD~1'],
                capture_output=True,
                text=True,
                check=False,
                env=self.env,
            )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn('not the top of a Git work tree', result.stderr)

    def test_the_suggested_attribute_line_marks_awkward_names(self):
        for name in ('tests/data/with space.wav', 'tests/data/glob[1]*?.wav', 'tests/data/quo"te b.wav'):
            with self.subTest(name=name):
                self.stage(name, binary(2 * MIB))
                result = self.run_check()
                self.assertEqual(result.returncode, 1, result.stdout)
                lines = result.stderr.splitlines()
                suggestion = lines[
                    lines.index('  - a deliberate fixture: keep it, and add this line to .gitattributes:') + 1
                ]
                self.stage('.gitattributes', suggestion.strip().encode() + b'\n')
                self.assertPasses()
                self.git('reset', '-q', '--hard')
                self.git('clean', '-q', '-fd')

    def test_the_suggested_attribute_line_marks_only_that_file(self):
        # Without the leading slash, `recording.wav large-fixture` would also
        # mark tests/data/recording.wav (jrp's review). The layout check
        # rejects a new top-level file, so only the large-file lines count.
        def large_files(result):
            return [line for line in result.stderr.splitlines() if ': this change adds' in line]

        self.stage('recording.wav', binary(2 * MIB))
        result = self.run_check()
        self.assertEqual(
            large_files(result), [line for line in result.stderr.splitlines() if 'recording.wav: this' in line]
        )
        self.assertTrue(large_files(result)[0].lstrip('- ').startswith('recording.wav: this change adds a 2.0 MiB'))
        lines = result.stderr.splitlines()
        suggestion = lines[lines.index('  - a deliberate fixture: keep it, and add this line to .gitattributes:') + 1]
        self.assertEqual(suggestion, '      /recording.wav large-fixture')
        self.stage('.gitattributes', suggestion.strip().encode() + b'\n')
        self.assertEqual(large_files(self.run_check()), [])
        self.stage('tests/data/recording.wav', binary(5 * MIB // 2))  # Not fixture.wav's bytes.
        flagged = [line.lstrip('- ') for line in large_files(self.run_check())]
        self.assertEqual(len(flagged), 1, flagged)
        self.assertTrue(flagged[0].startswith('tests/data/recording.wav: this change adds a 2.5 MiB'), flagged)

    def test_copies_merges_and_a_first_commit(self):
        # An unchanged copy of a file already in the tree adds no new content.
        self.stage('tests/data/copy.wav', (self.root / 'tests/data/fixture.wav').read_bytes())
        self.assertPasses()
        self.git('reset', '-q', '--hard')
        self.git('clean', '-q', '-fd')
        # Merging a branch that already holds a binary adds nothing new here.
        self.git('checkout', '-q', '-b', 'side')
        self.stage('tests/data/side.wav', binary(2 * MIB))
        self.git('commit', '-q', '-m', 'side')
        self.git('checkout', '-q', 'main')
        self.stage('docs/use/guide.md', BASE['docs/use/guide.md'].encode() + b'More.\n')
        self.git('commit', '-q', '-m', 'main')
        self.git('merge', '-q', '--no-commit', '--no-ff', 'side')
        self.assertPasses()
        self.git('merge', '--abort')
        # A repository's first commit has no HEAD to compare with.
        with tempfile.TemporaryDirectory() as fresh:
            root, self.root = self.root, Path(fresh)
            try:
                self.git('init', '-q', '-b', 'main')
                for name, content in BASE.items():
                    self.stage(name, content.encode())
                self.stage('tests/data/first.wav', binary(2 * MIB))
                self.assertIn('tests/data/first.wav: this change adds a 2.0 MiB', self.run_check().stderr)
            finally:
                self.root = root


if __name__ == '__main__':
    unittest.main()
