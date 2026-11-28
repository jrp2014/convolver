# SPDX-License-Identifier: GPL-2.0-or-later
"""The clean-machine check (REL-9, REL-11) compares a loaded plug-in's class
IDs in each platform's byte order, refuses files it cannot check, and checks
a package against the version it was built as."""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/release'))
import clean_machine  # noqa: E402
import macos_signatures  # noqa: E402
import scratch  # noqa: E402
import signed_artifact  # noqa: E402


class CleanMachineTests(unittest.TestCase):
    def test_class_id_byte_order(self):
        hex_id = '00112233445566778899AABBCCDDEEFF'
        self.assertEqual(clean_machine.class_id_bytes(hex_id, 'Linux'), bytes.fromhex(hex_id))
        self.assertEqual(clean_machine.class_id_bytes(hex_id, 'Darwin'), bytes.fromhex(hex_id))
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
            identifier = {'Linux': 'linux-x86_64', 'Windows': 'windows-x86_64', 'Darwin': 'macos-arm64'}[system]
            name = f'Convolver-0.0.0-{identifier}.zip'
            (root / name).write_bytes(b'package')
            record = {'id': identifier, 'artifact': name, 'installers': []}
            (root / 'candidate.json').write_text(json.dumps({'platforms': [record]}))
            (root / 'SHA256SUMS').write_text(f'{"0" * 64}  {name}\n')
            with self.assertRaisesRegex(AssertionError, 'has no macos-x86_64 package'):
                clean_machine.check(root, 'macos-x86_64')
            with self.assertRaisesRegex(AssertionError, 'does not match its SHA256SUMS line'):
                clean_machine.check(root, identifier)
            # Every platform's files need their own system.
            other = 'windows-x86_64' if system != 'Windows' else 'macos-arm64'
            needed = clean_machine.SYSTEMS[other.split('-', maxsplit=1)[0]]
            record = {'id': other, 'artifact': name, 'installers': []}
            (root / 'candidate.json').write_text(json.dumps({'platforms': [record]}))
            with self.assertRaisesRegex(AssertionError, f'needs a {needed} machine'):
                clean_machine.check(root, other)

    def test_verifies_each_signed_download_before_smoke_and_legacy_downloads_still_work(self):
        team = clean_machine.load_policy(ROOT / 'packaging/release-policy.json')['macos_signing_team_id']
        signed = {
            'signed': True,
            'notarized': True,
            'team_id': team,
            'notary_submission_id': 'a1234567-89ab-cdef-0123-456789abcdef',
        }
        record = dict(
            signed,
            artifact='test.zip',
            installers=['test.pkg'],
            installer_evidence={'test.pkg': dict(signed, notary_submission_id='b1234567-89ab-cdef-0123-456789abcdef')},
        )
        folder, unpacked = Path('downloads'), Path('unpacked')
        with mock.patch.object(clean_machine, 'check_unpacked') as payload, mock.patch.object(
            clean_machine, 'verify_signed_installer'
        ) as product:
            self.assertEqual(clean_machine.verified_macos_downloads(record, folder, unpacked), team)
            payload.assert_called_once_with(unpacked, team)
            product.assert_called_once_with(folder / 'test.pkg', team)
            payload.reset_mock()
            product.reset_mock()
            self.assertIsNone(
                clean_machine.verified_macos_downloads(
                    {'artifact': 'test.zip', 'installers': ['test-unsigned.pkg']}, folder, unpacked
                )
            )
            payload.assert_not_called()
            product.assert_not_called()
            with self.assertRaisesRegex(ValueError, 'unexpected Developer ID'):
                clean_machine.verified_macos_downloads(dict(record, team_id='OTHER12345'), folder, unpacked)
            with self.assertRaisesRegex(ValueError, 'missing Developer ID signing evidence'):
                clean_machine.verified_macos_downloads(dict(record, installer_evidence={}), folder, unpacked)

    def test_zip_verification_checks_both_bundles_and_all_tools_but_staples_only_bundles(self):
        stage, team = Path('package'), 'ABCDE12345'
        with mock.patch.object(signed_artifact, 'verify_signed_code') as code, mock.patch.object(
            signed_artifact, 'verify_staple'
        ) as staple:
            signed_artifact.check_unpacked(stage, team)
        bundles = [stage / 'VST3/Convolver.vst3', stage / 'Components/Convolver.component']
        tools = [stage / 'bin' / n for n in ('convolver', 'convolver-preset', 'convolver-equalizer-apo')]
        self.assertEqual(code.call_args_list, [mock.call(path, team) for path in bundles + tools])
        self.assertEqual(staple.call_args_list, [mock.call(path) for path in bundles])

    def test_developer_id_checks_require_runtime_and_secure_timestamp(self):
        good = (
            'CodeDirectory v=20500 size=16 flags=0x10000(runtime) hashes=1+7 location=embedded\nTimestamp=12 Nov 2026'
        )
        with mock.patch.object(macos_signatures.subprocess, 'run') as run, mock.patch.object(
            macos_signatures, 'details', return_value=good
        ) as details:
            macos_signatures.verify_signed_code('tool', 'ABCDE12345')
            requirement = run.call_args.args[0][-2]
            for check in ('anchor apple generic', 'field.1.2.840.113635.100.6.1.13', 'subject.OU] = "ABCDE12345"'):
                self.assertIn(check, requirement)
            for original, expected in [('(runtime)', 'hardened runtime'), ('Timestamp=', 'secure timestamp')]:
                details.return_value = good.replace(original, '(none)')
                with self.subTest(expected=expected), self.assertRaisesRegex(ValueError, expected):
                    macos_signatures.verify_signed_code('tool', 'ABCDE12345')

    def test_product_requires_installer_leaf_team_trusted_timestamp_ticket_and_gatekeeper(self):
        good = 'Signed with a trusted timestamp on: ...\n   1. Developer ID Installer: Example (ABCDE12345)\n'
        with mock.patch.object(macos_signatures.subprocess, 'run') as run, mock.patch.object(
            macos_signatures, 'details', return_value=good
        ) as details:
            macos_signatures.verify_signed_installer('test.pkg', 'ABCDE12345')
            self.assertEqual(
                [call.args[0] for call in run.call_args_list],
                [
                    ['xcrun', 'stapler', 'validate', 'test.pkg'],
                    ['spctl', '--assess', '--type', 'install', '--verbose=2', 'test.pkg'],
                ],
            )
            for original, expected in [
                ('Installer:', 'Developer ID Installer'),
                ('ABCDE12345', 'Team ID'),
                ('trusted timestamp', 'trusted timestamp'),
            ]:
                details.return_value = good.replace(original, 'other')
                with self.subTest(expected=expected), self.assertRaisesRegex(ValueError, expected):
                    macos_signatures.verify_signed_installer('test.pkg', 'ABCDE12345')

    def test_package_version(self):
        # A dry run checks an earlier tag's files with later scripts, so the
        # Audio Unit's version is checked against the package's own.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'VERSION.txt').write_text(
                'Convolver 0.18.29\nDeveloper preview; production qualification incomplete.\n'
            )
            self.assertEqual(clean_machine.package_version(root), '0.18.29')
            (root / 'VERSION.txt').write_text('Convolver 0.18.29-rc\n')
            with self.assertRaisesRegex(AssertionError, 'does not start with'):
                clean_machine.package_version(root)
            (root / 'VERSION.txt').write_text('Something else\n')
            with self.assertRaisesRegex(AssertionError, 'does not start with'):
                clean_machine.package_version(root)


class ScratchTests(unittest.TestCase):
    """The package checks' cleanup waits for Windows to release a file it
    still holds, and fails at once anywhere else."""

    @mock.patch('builtins.print')
    @mock.patch.object(scratch.time, 'sleep')
    @mock.patch.object(scratch.sys, 'platform', 'win32')
    def test_windows_retries_until_the_file_is_released(self, sleep, _print):
        calls = []

        def rmtree(path):
            calls.append(path)
            if len(calls) < 3:
                raise PermissionError('held')

        writable = mock.patch.object(scratch, 'make_writable')
        with mock.patch.object(scratch.shutil, 'rmtree', rmtree), writable as writable:
            scratch.remove_tree('folder')
        self.assertEqual(writable.call_count, 2)
        self.assertEqual(calls, ['folder'] * 3)
        self.assertEqual(sleep.call_count, 2)

    @mock.patch('builtins.print')
    @mock.patch.object(scratch.time, 'sleep')
    @mock.patch.object(scratch.sys, 'platform', 'win32')
    @mock.patch.object(scratch.shutil, 'rmtree', side_effect=PermissionError('held'))
    def test_windows_gives_up_after_a_minute(self, rmtree, _sleep, _print):
        with self.assertRaises(PermissionError), mock.patch.object(scratch, 'make_writable'):
            scratch.remove_tree('folder')
        self.assertEqual(rmtree.call_count, scratch.ATTEMPTS)
        self.assertGreaterEqual(scratch.ATTEMPTS * scratch.PAUSE, 60)

    @mock.patch.object(scratch.time, 'sleep')
    @mock.patch.object(scratch.sys, 'platform', 'linux')
    @mock.patch.object(scratch.shutil, 'rmtree', side_effect=PermissionError('held'))
    def test_other_systems_fail_at_once(self, rmtree, sleep):
        with self.assertRaises(PermissionError):
            scratch.remove_tree('folder')
        self.assertEqual(rmtree.call_count, 1)
        sleep.assert_not_called()


if __name__ == '__main__':
    unittest.main()
