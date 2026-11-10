# SPDX-License-Identifier: GPL-2.0-or-later
"""LV2-5: how VERSION becomes lv2:minorVersion and lv2:microVersion.

Every release must read as a release to LV2 (minor even and not 0, micro
even), and a later VERSION must give a higher (minor, micro), because lilv
loads the highest it finds: 1.0.0 must not lose to a 0.x left in ~/.lv2.
"""

import itertools
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'plugins/lv2'))
from generate_ttl import lv2_version


class Mapping(unittest.TestCase):
    def test_examples(self):
        self.assertEqual(lv2_version(0, 13, 6), (26, 12))
        self.assertEqual(lv2_version(0, 13, 7), (26, 14))
        self.assertEqual(lv2_version(1, 0, 0), (200, 0))
        self.assertEqual(lv2_version(1, 2, 3), (204, 6))

    def test_releases_are_even_and_ordered(self):
        versions = sorted(itertools.product(range(3), range(0, 100, 7), range(0, 300, 11)))
        mapped = [lv2_version(*version) for version in versions if version[:2] != (0, 0)]
        for minor, micro in mapped:
            self.assertTrue(minor > 0 and minor % 2 == 0 and micro % 2 == 0, (minor, micro))
        self.assertEqual(mapped, sorted(mapped))
        self.assertEqual(len(mapped), len(set(mapped)))

    def test_minor_of_100_needs_a_new_rule(self):
        with self.assertRaises(SystemExit):
            lv2_version(0, 100, 0)


if __name__ == '__main__':
    unittest.main()
