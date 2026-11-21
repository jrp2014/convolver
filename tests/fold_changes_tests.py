#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Behaviour tests for scripts/fold_changes.py (DOC-20), on small synthetic trees."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts' / 'fold_changes.py'
SPDX_MD = '<!-- SPDX-License-Identifier: GPL-2.0-or-later -->\n'

CHANGELOG = SPDX_MD + (
    '# Changelog\n\n## [Unreleased]\n\n### Changed\n\n- Old change (PLUG-1).\n\n'
    '### For contributors\n\n- Old tooling (CI-1).\n\n## [0.1.0] - 2026-11-01\n\n### Fixed\n\n- Released fix.\n'
)
TODO = SPDX_MD + (
    '# TODO\n\n## Open\n\n- [ ] **PLUG-3 — Open.** Open.\n\n## Done\n\nFinished items move out.\n\n'
    'Items finished since then are listed here, one line each, until the next\nmove to the archive.\n\n'
    '- [x] **PLUG-1 — First.** 1 November 2026.\n'
)
DECISIONS = SPDX_MD + (
    '# Decisions\n\n## Direction\n\n- **1 November 2026.** Old direction.\n  Continued.\n\n'
    '## Releases\n\n- **1 November 2026.** Old release.\n\n## Documentation\n\n- **2 November 2026.** Old docs.\n'
)


def fragment(title, body):
    return SPDX_MD + f'# {title}\n\n' + body


class FoldChanges(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='convolver-fold-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.write('CHANGELOG.md', CHANGELOG)
        self.write('TODO.md', TODO)
        self.write('docs/project/decisions.md', DECISIONS)
        self.write('changes/README.md', SPDX_MD + '# Change fragments\n')
        self.git('init', '-b', 'main')
        self.git('config', 'user.name', 'Fold test')
        self.git('config', 'user.email', 'fold@example.invalid')
        self.git('config', 'commit.gpgsign', 'false')
        self.commit('Records', '2026-11-01T00:00:00Z')

    def git(self, *args, env=None):
        return subprocess.run(['git', *args], cwd=self.root, text=True, capture_output=True, check=True, env=env)

    def commit(self, message, date):
        self.git('add', '.')
        stamp = {'GIT_AUTHOR_DATE': date, 'GIT_COMMITTER_DATE': date}
        self.git('commit', '-m', message, env={**os.environ, **stamp})

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def read(self, name):
        return (self.root / name).read_text(encoding='utf-8')

    def run_fold(self, *args, ok=True):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), '--root', str(self.root), *args], capture_output=True, text=True, check=False
        )
        self.assertEqual(result.returncode == 0, ok, result.stdout + result.stderr)
        return result

    def test_no_fragments_changes_nothing(self):
        self.run_fold('--check')
        self.assertIn('No change fragments', self.run_fold().stdout)
        self.assertEqual(self.read('CHANGELOG.md'), CHANGELOG)

    def test_fold_places_entries_in_merge_order_and_deletes_fragments(self):
        self.write(
            'changes/PLUG-3.md',
            fragment(
                'PLUG-3',
                '## Changed\n\n- Earlier change (PLUG-3), [guide](docs/use/guide.md#a).\n  Continued.\n\n'
                '## Done\n\n- [x] **PLUG-3 — Open.** 20 November 2026, PR #3.\n\n'
                '## Decisions: Direction\n\n- **20 November 2026.** Earlier direction. See '
                '[workflow](docs/develop/workflow.md#while-working), [web](https://example.invalid/x).\n',
            ),
        )
        self.commit('PLUG-3', '2026-11-20T00:00:00Z')
        self.write(
            'changes/CI-2-tools.md',
            fragment(
                'CI-2',
                '## For contributors\n\n- Later tooling (CI-2).\n\n## Fixed\n\n- New fix (CI-2).\n\n'
                '## Done\n\n- [x] **CI-2 — Tools.** 21 November 2026, PR #4.\n\n'
                '## Decisions: Releases\n\n- **21 November 2026.** Later release.\n\n'
                '## Decisions: Direction\n\n- **21 November 2026.** Later direction.\n',
            ),
        )
        self.commit('CI-2', '2026-11-21T00:00:00Z')
        self.run_fold('--check')
        self.run_fold()
        self.assertEqual(sorted(p.name for p in (self.root / 'changes').iterdir()), ['README.md'])
        changelog = self.read('CHANGELOG.md')
        self.assertIn(
            '### Changed\n\n- Earlier change (PLUG-3), [guide](docs/use/guide.md#a).\n  Continued.\n'
            '- Old change (PLUG-1).\n\n### Fixed\n\n- New fix (CI-2).\n\n'
            '### For contributors\n\n- Later tooling (CI-2).\n- Old tooling (CI-1).\n\n## [0.1.0]',
            changelog,
        )
        todo = self.read('TODO.md')
        self.assertIn(
            'move to the archive.\n\n- [x] **CI-2 — Tools.** 21 November 2026, PR #4.\n'
            '- [x] **PLUG-3 — Open.** 20 November 2026, PR #3.\n- [x] **PLUG-1 — First.**',
            todo,
        )
        decisions = self.read('docs/project/decisions.md')
        self.assertIn(
            '- **1 November 2026.** Old direction.\n  Continued.\n- **20 November 2026.** Earlier direction. See '
            '[workflow](../develop/workflow.md#while-working), [web](https://example.invalid/x).\n'
            '- **21 November 2026.** Later direction.\n\n## Releases\n\n'
            '- **21 November 2026.** Later release.\n- **1 November 2026.** Old release.\n',
            decisions,
        )
        # Each page still ends with exactly one newline.
        for text in (changelog, todo, decisions):
            self.assertTrue(text.endswith('\n') and not text.endswith('\n\n'))

    def test_uncommitted_fragment_folds_last(self):
        self.write('changes/PLUG-5.md', fragment('PLUG-5', '## Changed\n\n- Uncommitted (PLUG-5).\n'))
        self.write('changes/PLUG-4.md', fragment('PLUG-4', '## Changed\n\n- Committed (PLUG-4).\n'))
        self.git('add', 'changes/PLUG-4.md')
        self.git('commit', '-m', 'PLUG-4')
        self.run_fold()
        self.assertIn('- Uncommitted (PLUG-5).\n- Committed (PLUG-4).\n- Old change', self.read('CHANGELOG.md'))

    def test_malformed_fragments_are_refused_before_anything_changes(self):
        cases = {
            'changes/plug-3.md': (fragment('PLUG-3', '## Changed\n\n- Entry.\n'), 'name a fragment after its TODO ID'),
            'changes/PLUG-6.md': (fragment('PLUG-6', '## Improved\n\n- Entry.\n'), 'unknown section "Improved"'),
            'changes/PLUG-7.md': (fragment('PLUG-7', '## Changed\n\n'), 'section "Changed" is empty'),
            'changes/PLUG-8.md': (fragment('PLUG-8', '## Changed\n\nProse.\n'), 'must be a list'),
            'changes/PLUG-9.md': (fragment('PLUG-9', '## Done\n\n- PLUG-9 finished.\n'), 'not a ticked TODO line'),
            'changes/PLUG-10.md': (SPDX_MD + '## Changed\n\n- Entry.\n', 'start with a "# " title'),
            'changes/PLUG-11.md': (
                fragment('PLUG-11', '## Decisions: Licences\n\n- **1 November 2026.** Entry.\n'),
                'names no "## Licences" section',
            ),
        }
        for name, (text, message) in cases.items():
            with self.subTest(name=name):
                self.write(name, text)
                for mode in (('--check',), ()):
                    result = self.run_fold(*mode, ok=False)
                    self.assertIn(message, result.stderr)
                self.assertEqual(self.read('CHANGELOG.md'), CHANGELOG)
                self.assertTrue((self.root / name).exists())
                (self.root / name).unlink()

    def test_missing_group_is_created_in_order(self):
        self.write('changes/PLUG-3.md', fragment('PLUG-3', '## Added\n\n- New feature (PLUG-3).\n'))
        self.run_fold()
        self.assertIn(
            '## [Unreleased]\n\n### Added\n\n- New feature (PLUG-3).\n\n### Changed\n', self.read('CHANGELOG.md')
        )


if __name__ == '__main__':
    unittest.main()
