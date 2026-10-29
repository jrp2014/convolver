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
    def rewrite(self, text, page='docs/use/presets.md'):
        shipped = {'docs/use/presets.md', 'docs/use/equalizer-apo.md'}
        return package_docs.rewrite(text, page, shipped, BASE, ['docs/use/images'])

    def test_links(self):
        self.assertEqual(
            self.rewrite('[a](equalizer-apo.md#install) [b](images/x.png)'),
            '[a](equalizer-apo.md#install) [b](images/x.png)',
        )
        self.assertEqual(self.rewrite('[a](images-old.md)'), f'[a]({BASE}/docs/use/images-old.md)')
        self.assertEqual(
            self.rewrite('[a](../develop/build.md#vst3) [b](../../TODO.md)'),
            f'[a]({BASE}/docs/develop/build.md#vst3) [b]({BASE}/TODO.md)',
        )
        self.assertEqual(
            self.rewrite('[a](#make-a-preset) [b](https://example.org/x.md)'),
            '[a](#make-a-preset) [b](https://example.org/x.md)',
        )
        # A link text may wrap onto the next line, as the guides' do.
        self.assertEqual(
            self.rewrite('[the\nguide\n](configuration.md)'), f'[the\nguide\n]({BASE}/docs/use/configuration.md)'
        )
        with self.assertRaises(ValueError):
            self.rewrite('[a](../../../outside.md)')

    def test_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            source, output = Path(temporary) / 'source', Path(temporary) / 'docs'
            (source / 'docs/use').mkdir(parents=True)
            (source / 'docs/use/presets.md').write_text(
                '<!-- SPDX -->\n# Presets\n\nSee [build](../develop/build.md) and [install](install.md).\n'
            )
            (source / 'docs/use/install.md').write_text('<!-- SPDX -->\n# Install\n')
            pages = ['docs/use/presets.md', 'docs/use/install.md']
            package_docs.package(source, pages, [], output, '1.2.3')
            text = (output / 'use/presets.md').read_text()
            self.assertIn('# Presets\n\n> The copy of `docs/use/presets.md` from Convolver 1.2.3.', text)
            self.assertIn('(https://github.com/jrp2014/convolver/tree/v1.2.3)', text)
            self.assertIn(f'See [build]({BASE}/docs/develop/build.md) and [install](install.md).', text)
            self.assertTrue((output / 'use/install.md').is_file())
            # The packaged commit, when known, is more exact than the tag.
            package_docs.package(source, pages, [], output, '1.2.3', 'abc123')
            text = (output / 'use/presets.md').read_text()
            self.assertIn('(https://github.com/jrp2014/convolver/tree/abc123)', text)
            self.assertIn('See [build](https://github.com/jrp2014/convolver/blob/abc123/docs/develop/build.md)', text)


if __name__ == '__main__':
    unittest.main()
