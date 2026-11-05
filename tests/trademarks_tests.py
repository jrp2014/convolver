#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""scripts/trademarks.py finds a missing ®, notice or logo, and --fix writes the closing block."""

import hashlib
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import trademarks  # noqa: E402


class TrademarkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        for name in (trademarks.WORDING, trademarks.INSTALLER, 'docs/use/images/VST_Compatible_Logo_Steinberg.png'):
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, self.root / name)
        self.write('README.md', '# Convolver\n\nPlug-ins: VST® 3 and Audio Unit. Then VST3 again.\n')
        self.write('docs/use/plugins.md', '# Plug-ins\n\nA VST® 3 plug-in.\n')
        self.write('docs/use/command-line.md', '# Command line\n\nNo plug-ins here; `room.vstpreset`.\n')

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, name, text):
        (self.root / name).parent.mkdir(parents=True, exist_ok=True)
        (self.root / name).write_text(text, encoding='utf-8')

    def read(self, name):
        return (self.root / name).read_text(encoding='utf-8')

    def test_fix_writes_blocks_with_relative_logo_links(self):
        self.assertEqual(len(trademarks.check(self.root)), 2)
        self.assertEqual(trademarks.check(self.root, fix=True), [])
        self.assertEqual(trademarks.check(self.root), [])
        readme, guide = self.read('README.md'), self.read('docs/use/plugins.md')
        self.assertIn('<img src="docs/use/images/VST_Compatible_Logo_Steinberg.png"', readme)
        self.assertIn('<img src="images/VST_Compatible_Logo_Steinberg.png"', guide)
        wording = json.loads(self.read(trademarks.WORDING))
        self.assertIn(f'{wording["support"]} {wording["notice"]}', guide)
        # A page without VST gets nothing.
        self.assertNotIn(trademarks.BEGIN, self.read('docs/use/command-line.md'))
        # Fixing again changes nothing, and an edited block is put back.
        trademarks.check(self.root, fix=True)
        self.assertEqual(self.read('docs/use/plugins.md'), guide)
        self.write('docs/use/plugins.md', guide.replace('registered trademark', 'trademark'))
        self.assertTrue(trademarks.check(self.root))
        trademarks.check(self.root, fix=True)
        self.assertEqual(self.read('docs/use/plugins.md'), guide)

    def test_first_mention_needs_the_mark(self):
        self.write('docs/use/plugins.md', '# Plug-ins\n\nA VST3 plug-in, then VST® 3.\n')
        errors = trademarks.check(self.root, fix=True)
        self.assertEqual(len(errors), 1)
        self.assertIn('docs/use/plugins.md: write the first mention', errors[0])

    def test_code_and_link_targets_do_not_count(self):
        self.write(
            'docs/use/plugins.md',
            '# Plug-ins\n\n```sh\nVST3=1\n```\n\nSee `VST3/` and [the guide](VST3.md): a VST® 3 plug-in.\n',
        )
        self.assertEqual(trademarks.check(self.root, fix=True), [])
        self.assertIsNone(trademarks.first_mention_marked(trademarks.prose('`VST3` [x](VST.md) vstpreset')))
        self.assertTrue(trademarks.first_mention_marked('VST® 3, VST3'))
        self.assertFalse(trademarks.first_mention_marked('the VST 3 SDK'))

    def test_changed_logo_and_installer(self):
        trademarks.check(self.root, fix=True)
        logo = self.root / 'docs/use/images/VST_Compatible_Logo_Steinberg.png'
        logo.write_bytes(logo.read_bytes() + b'\0')
        installer = self.root / trademarks.INSTALLER
        installer.write_text(installer.read_text(encoding='utf-8').replace('VST®', 'VST'), encoding='utf-8')
        errors = trademarks.check(self.root, fix=True)
        self.assertEqual(len(errors), 2, errors)
        self.assertIn('not the unmodified Steinberg logo', errors[0])
        self.assertIn('Distribution.xml: write the first mention', errors[1])

    def test_repository_logo_is_steinbergs(self):
        wording = json.loads((ROOT / trademarks.WORDING).read_text(encoding='utf-8'))
        data = (ROOT / wording['logo']['file']).read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), wording['logo']['sha256'])
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')


if __name__ == '__main__':
    unittest.main()
