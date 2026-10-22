# SPDX-License-Identifier: GPL-2.0-or-later
"""Packaged guide copies keep shipped links relative and send the rest online."""
from pathlib import Path
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import package_docs  # noqa: E402

BASE = 'https://github.com/jrp2014/convolver/blob/v1.2.3'


class PackagedDocsTests(unittest.TestCase):
    def rewrite(self, text, page='docs/presets.md'):
        shipped = {'docs/presets.md', 'docs/equalizer-apo.md', 'docs/plugins/index.md'}
        return package_docs.rewrite(text, page, shipped, BASE)

    def test_links(self):
        self.assertEqual(self.rewrite('[a](equalizer-apo.md#install) [b](plugins/index.md)'),
                         '[a](equalizer-apo.md#install) [b](plugins/index.md)')
        self.assertEqual(self.rewrite('[a](vst3.md#loading-filters) [b](../TODO.md)'),
                         f'[a]({BASE}/docs/vst3.md#loading-filters) [b]({BASE}/TODO.md)')
        self.assertEqual(self.rewrite('[a](#make-a-preset) [b](https://example.org/x.md)'),
                         '[a](#make-a-preset) [b](https://example.org/x.md)')
        # A link text may wrap onto the next line, as the guides' do.
        self.assertEqual(self.rewrite('[the\nguide\n](configuration.md)'), f'[the\nguide\n]({BASE}/docs/configuration.md)')
        with self.assertRaises(ValueError):
            self.rewrite('[a](../../outside.md)')

    def test_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            source, output = Path(temporary) / 'source', Path(temporary) / 'docs'
            (source / 'docs').mkdir(parents=True)
            (source / 'docs/presets.md').write_text('<!-- SPDX -->\n# Presets\n\nSee [VST3](vst3.md).\n')
            package_docs.package(source, ['docs/presets.md'], [], output, '1.2.3')
            text = (output / 'presets.md').read_text()
            self.assertIn('# Presets\n\n> The copy of `docs/presets.md` from Convolver 1.2.3.', text)
            self.assertIn('(https://github.com/jrp2014/convolver/tree/v1.2.3)', text)
            self.assertIn(f'See [VST3]({BASE}/docs/vst3.md).', text)


if __name__ == '__main__':
    unittest.main()
