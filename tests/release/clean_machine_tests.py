# SPDX-License-Identifier: GPL-2.0-or-later
"""The clean-machine check (REL-9) compares a loaded plug-in's class IDs in
each platform's byte order, and refuses files it cannot check."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/release'))
import clean_machine  # noqa: E402


class CleanMachineTests(unittest.TestCase):
    def test_class_id_byte_order(self):
        hex_id = '00112233445566778899AABBCCDDEEFF'
        self.assertEqual(clean_machine.class_id_bytes(hex_id, 'Linux'), bytes.fromhex(hex_id))
        # The SDK's INLINE_UID with COM_COMPATIBLE (Windows): the first field
        # little-endian, then each half of the second swapped, then the rest.
        self.assertEqual(
            clean_machine.class_id_bytes(hex_id, 'Windows'), bytes.fromhex('33221100554477668899AABBCCDDEEFF')
        )

    def test_class_info_layout(self):
        # Steinberg::PClassInfo: 16 + 4 + 32 + 64 bytes, no padding.
        self.assertEqual(clean_machine.ctypes.sizeof(clean_machine.ClassInfo), 116)

    def test_refuses_a_missing_platform_or_a_changed_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            system = clean_machine.platform.system()
            identifier = {'Linux': 'linux-x86_64', 'Windows': 'windows-x86_64'}.get(system, 'linux-x86_64')
            name = f'Convolver-0.0.0-preview-{identifier}.zip'
            (root / name).write_bytes(b'package')
            record = {'id': identifier, 'artifact': name, 'installers': []}
            (root / 'candidate.json').write_text(json.dumps({'platforms': [record]}))
            (root / 'SHA256SUMS').write_text(f'{"0" * 64}  {name}\n')
            with self.assertRaisesRegex(AssertionError, 'has no macos-arm64 package'):
                clean_machine.check(root, 'macos-arm64')
            if system in ('Linux', 'Windows'):
                with self.assertRaisesRegex(AssertionError, 'does not match its SHA256SUMS line'):
                    clean_machine.check(root, identifier)
            else:
                with self.assertRaisesRegex(AssertionError, 'needs a Linux machine'):
                    clean_machine.check(root, identifier)


if __name__ == '__main__':
    unittest.main()
