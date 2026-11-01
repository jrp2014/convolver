#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Behaviour tests for scripts/check_repository.py, on small synthetic trees."""

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
            return subprocess.run(
                [sys.executable, str(CHECKER), '--root', str(root)], capture_output=True, text=True, check=False
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


if __name__ == '__main__':
    unittest.main()
