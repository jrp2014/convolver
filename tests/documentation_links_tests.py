#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Behavior tests for maintained documentation's local-link checker."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CHECKER = Path(__file__).resolve().parents[1] / 'scripts' / 'check_doc_links.py'


class DocumentationLinks(unittest.TestCase):
    def run_check(self, files):
        with tempfile.TemporaryDirectory(prefix='convolver-doc-links-') as tmp:
            root = Path(tmp)
            for name, content in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding='utf-8')
            return subprocess.run([sys.executable, str(CHECKER), '--root', str(root)],
                                  capture_output=True, text=True)

    def test_links_anchors_and_ignored_examples(self):
        result = self.run_check({
            'README.md': '# Home\n[Guide](docs/guide.md#api-foo)\n'
                         '[Repeat](docs/guide.md#again-1)\n'
                         '[Unicode](docs/guide.md#café)\n'
                         '[Space](<docs/a b.md>)\n[Source](src/a.cpp)\n'
                         '[HTML](old.html#kept)\n[Web](https://example.invalid)\n'
                         '`[not a link](missing-inline)`\n'
                         '```md\n[not a link](missing-fenced)\n```\n',
            'docs/guide.md': '# API `foo`\n## Again\n## Again\n## Café\n',
            'docs/a b.md': '# Space\n',
            'src/a.cpp': '// source\n',
            'old.html': '<a name="kept"></a>',
        })
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_target_and_fragment_are_both_reported(self):
        result = self.run_check({
            'README.md': '# Home\n[Missing](absent.md)\n[Wrong](docs/a.md#missing)\n',
            'docs/a.md': '# Exists\n',
        })
        self.assertEqual(result.returncode, 1)
        self.assertIn('README.md:2', result.stderr)
        self.assertIn('absent.md', result.stderr)
        self.assertIn('README.md:3', result.stderr)
        self.assertIn('missing', result.stderr)

    def test_reference_links_and_same_page_fragments(self):
        result = self.run_check({
            'README.md': '# Home\n[Target][ref]\n[Local](#home)\n\n'
                         '[ref]: docs/target.md#section\n',
            'docs/target.md': '# Section\n',
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_check({'README.md': '# Home\n[X][ref]\n\n[ref]: missing.md\n'})
        self.assertEqual(result.returncode, 1)
        self.assertIn('missing.md', result.stderr)

    def test_dated_records_excluded_but_linked_targets_checked(self):
        result = self.run_check({
            'README.md': '# Home\n[Old](docs/archive/old.md#record)\n',
            'docs/archive/old.md': '# Record\n[Past](absent.md)\n',
            'docs/historical/old.md': '[Past](absent.md)\n',
            'docs/superpowers/plans/old.md': '[Past](absent.md)\n',
        })
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_html_anchors_in_markdown_code_are_not_destinations(self):
        result = self.run_check({
            'README.md': '# Home\n[Fenced](docs/target.md#fenced)\n'
                         '[Inline](docs/target.md#inline)\n'
                         '[Real](docs/target.md#real)\n'
                         '[HTML](page.html#html)\n',
            'docs/target.md': '# Target\n```html\n<a id="fenced"></a>\n```\n'
                              '`<a id="inline"></a>`\n<a id="real"></a>\n',
            'page.html': '<div id="html"></div>',
        })
        self.assertEqual(result.returncode, 1)
        self.assertIn('missing anchor: docs/target.md#fenced', result.stderr)
        self.assertIn('missing anchor: docs/target.md#inline', result.stderr)
        self.assertNotIn('missing anchor: docs/target.md#real', result.stderr)
        self.assertNotIn('missing anchor: page.html#html', result.stderr)


if __name__ == '__main__':
    unittest.main()
