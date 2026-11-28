# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the macOS signer without a real keychain, certificate or notary submission."""

import base64
import contextlib
import hashlib
import io
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import macos_signing as signing  # noqa: E402

TEAM = 'TESTTEAM10'
SUBMISSION = 'c8cc20ef-65e0-4e49-a00e-b5df34ef64bf'


class SigningTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name).resolve()
        self.stage = self.work / 'stage'
        self.signing_dir = self.work / 'convolver-macos-signing'
        self.binaries = [self.stage / path for _, path in signing.TARGETS['Darwin']]
        metadata = self.stage / signing.BUNDLES[0] / 'Contents/Resources/moduleinfo.json'
        for file in [*self.binaries, metadata]:
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b'fixture')
        self.credentials = {name: 'secret-' + name for name in signing.SECRETS}
        for name in ('DEVELOPER_ID_APPLICATION_P12', 'DEVELOPER_ID_INSTALLER_P12', 'NOTARY_KEY_P8'):
            self.credentials[name] = base64.b64encode(name.encode()).decode()
        self.patch(mock.patch.dict(os.environ, {**self.credentials, 'RUNNER_TEMP': str(self.work)}))
        self.application = f'  1) {"A" * 40} "Developer ID Application: Example ({TEAM})"'
        self.installer = f'  2) {"B" * 40} "Developer ID Installer: Example ({TEAM})"'
        self.certificate_listing = f'SHA-256 hash: {signing.G2_SHA256.upper()}'
        self.notary_log = '{}'
        self.status = 'Accepted'
        self.submission_id = SUBMISSION
        self.failed_command = ''
        self.commands = []
        self.patch(mock.patch.object(signing.subprocess, 'run', side_effect=self.run_tool))
        self.code = self.patch(mock.patch.object(signing, 'verify_signed_code'))
        self.staple = self.patch(mock.patch.object(signing, 'verify_staple'))
        self.product = self.patch(mock.patch.object(signing, 'verify_signed_installer'))

    def patch(self, patcher):
        self.addCleanup(patcher.stop)
        return patcher.start()

    def run_tool(self, command, *, env, **kwargs):
        self.assertTrue(set(signing.SECRETS).isdisjoint(env))
        self.assertEqual(Path(command[0]).parent, Path('/usr/bin'))
        args = [Path(command[0]).name, *map(str, command[1:])]
        self.commands.append(args)
        output = ''
        if args[:2] == ['security', 'create-keychain']:
            Path(args[-1]).touch()
        elif args[:2] == ['security', 'list-keychains'] and '-s' not in args:
            output = '    "/Users/test/Library/Keychains/login with spaces.keychain-db"\n'
        elif args[:2] == ['security', 'import']:
            credential = Path(args[2])
            self.assertEqual(credential.read_bytes(), f'DEVELOPER_ID_{credential.stem}_P12'.encode())
            self.assertEqual(stat.S_IMODE(credential.stat().st_mode), 0o600)
        elif args[:2] == ['security', 'find-certificate']:
            output = self.certificate_listing
        elif args[:2] == ['security', 'find-identity']:
            output = self.application if 'codesigning' in args else self.installer
        elif args[:2] == ['xcrun', 'notarytool']:
            key = Path(args[args.index('--key') + 1])
            self.assertEqual(key.read_bytes(), b'NOTARY_KEY_P8')
            self.assertEqual(stat.S_IMODE(key.stat().st_mode), 0o600)
            if args[2] == 'log':
                self.assertEqual(args[3], SUBMISSION)
                return subprocess.CompletedProcess(command, 0, self.notary_log, self.credentials['NOTARY_KEY_ID'])
            output = json.dumps({'id': self.submission_id, 'status': self.status})
        if args[0] == self.failed_command or (args[:2] == ['xcrun', 'stapler'] and self.failed_command == 'stapler'):
            return subprocess.CompletedProcess(command, 65, 'tool output', 'verification failed')
        if args[:2] == ['xcrun', 'notarytool'] and args[2] == self.failed_command:
            return subprocess.CompletedProcess(command, 69, output, self.credentials['NOTARY_KEY_ID'])
        if args[1] == self.failed_command:
            return subprocess.CompletedProcess(command, 7, self.credentials['DEVELOPER_ID_APPLICATION_P12_PASSWORD'])
        return subprocess.CompletedProcess(command, 0, output, '')

    def test_signs_exact_files_and_removes_credentials(self):
        session = signing.SigningSession()
        self.assertTrue(set(signing.SECRETS).isdisjoint(os.environ))
        unsigned = self.work / 'unsigned.pkg'
        product = self.work / 'signed.pkg'
        with session as signer:
            directory = signer.directory
            self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o700)
            self.assertEqual(signer.team_id, TEAM)
            self.assertFalse(list(directory.glob('*.p12')))
            self.assertEqual(signer.sign_stage(self.stage), SUBMISSION)
            self.assertEqual(signer.sign_installer(unsigned, product), SUBMISSION)
        self.assertFalse(directory.exists())
        self.assertEqual(self.commands[-1][:2], ['security', 'delete-keychain'])
        imports = [cmd for cmd in self.commands if cmd[:2] == ['security', 'import']]
        self.assertEqual([cmd[-2:] for cmd in imports], [['-T', '/usr/bin/codesign'], ['-T', '/usr/bin/productsign']])
        self.assertTrue(all('-A' not in cmd for cmd in imports))
        paths = [self.stage / path for path in signing.BUNDLES]
        commands = [cmd for cmd in self.commands if cmd[0] == 'codesign']
        self.assertEqual([Path(cmd[-1]) for cmd in commands], [*self.binaries, *paths])
        for cmd in commands:
            self.assertIn('--timestamp', cmd)
            self.assertEqual(cmd[cmd.index('--options') + 1], 'runtime')
            self.assertEqual(cmd[cmd.index('--sign') + 1], 'A' * 40)
            self.assertNotIn('--deep', cmd)
        tools = [path for path in self.binaries if path.parent.name == 'bin']
        self.assertEqual(self.code.call_args_list, [mock.call(path, TEAM) for path in [*paths, *tools]])
        self.assertEqual(self.staple.call_args_list, [mock.call(path) for path in paths])
        copied = [Path(cmd[1]) for cmd in self.commands if cmd[0] == 'ditto' and cmd[1] != '-c']
        self.assertEqual(copied, [*paths, *tools])
        submissions = [cmd for cmd in self.commands if cmd[:3] == ['xcrun', 'notarytool', 'submit']]
        self.assertEqual([Path(cmd[3]) for cmd in submissions], [directory / 'submission.zip', product])
        waits = [cmd for cmd in self.commands if cmd[:3] == ['xcrun', 'notarytool', 'wait']]
        self.assertEqual([cmd[3] for cmd in waits], [SUBMISSION, SUBMISSION])
        self.assertFalse(any(cmd[:2] == ['security', 'add-certificates'] for cmd in self.commands))
        product_command = next(cmd for cmd in self.commands if cmd[0] == 'productsign')
        self.assertEqual(product_command[1:3], ['--sign', f'Developer ID Installer: Example ({TEAM})'])
        self.assertEqual(product_command[-3:], ['--timestamp', str(unsigned), str(product)])
        stapled = [Path(cmd[3]) for cmd in self.commands if cmd[:3] == ['xcrun', 'stapler', 'staple']]
        self.assertEqual(stapled, [*paths, product])
        self.product.assert_called_once_with(product, TEAM)
        signing.cleanup()

    def test_ambiguous_and_mismatched_identities(self):
        for name, value, error in (
            ('application', self.application + '\n' + self.application, 'expected one valid Developer ID Application'),
            ('installer', self.installer.replace(TEAM, 'OTHERTEAM1'), 'team IDs differ'),
            ('installer', self.installer + ' (CSSMERR_TP_CERT_EXPIRED)', 'expected one valid Developer ID Installer'),
        ):
            with self.subTest(name=name, value=value), mock.patch.object(self, name, value):
                with mock.patch.dict(os.environ, self.credentials):
                    session = signing.SigningSession()
                with self.assertRaisesRegex(ValueError, error), session:
                    self.fail('invalid identities were accepted')
                self.assertFalse(self.signing_dir.exists())
                self.assertEqual(self.commands[-1][:2], ['security', 'delete-keychain'])

    def test_import_failure_redacts_credentials_and_cleans_partial_setup(self):
        self.failed_command = 'import'
        with self.assertRaisesRegex(RuntimeError, r'^security import failed \(exit 7\)$'), signing.SigningSession():
            self.fail('failed import was accepted')
        self.assertFalse(self.signing_dir.exists())
        self.assertEqual(self.commands[-1][:2], ['security', 'delete-keychain'])

    def test_invalid_notarization_does_not_staple_or_accept_an_id(self):
        for status, value in [('Invalid', SUBMISSION), ('In Progress', SUBMISSION), ('Accepted', 'invalid')]:
            with self.subTest(status=status, value=value), mock.patch.dict(os.environ, self.credentials):
                self.status, self.submission_id = status, value
                with self.assertRaises(ValueError), signing.SigningSession() as signer:
                    signer.sign_installer(self.work / 'unsigned.pkg', self.work / 'signed.pkg')
                self.assertFalse(self.signing_dir.exists())
                self.product.assert_not_called()
                self.assertFalse(any(cmd[:2] == ['xcrun', 'stapler'] for cmd in self.commands))

    def test_missing_intermediate_imports_only_pinned_certificate(self):
        self.certificate_listing = ''
        certificate = self.work / 'DeveloperIDG2CA.cer'
        certificate.write_bytes(b'public test certificate')
        digest = hashlib.sha256(certificate.read_bytes()).hexdigest()
        with mock.patch.dict(os.environ, self.credentials), mock.patch.object(signing, 'G2_SHA256', digest):
            with signing.SigningSession() as signer:
                expected = ['security', 'add-certificates', '-k', str(signer.keychain), str(certificate)]
            self.assertIn(expected, self.commands)
        self.commands.clear()
        with self.assertRaisesRegex(ValueError, 'unexpected fingerprint'), signing.SigningSession():
            self.fail('an unpinned intermediate was accepted')
        self.assertFalse(any(cmd[:2] == ['security', 'add-certificates'] for cmd in self.commands))

    def test_nonzero_notary_status_retains_only_the_submission_id(self):
        for action in ('submit', 'wait'):
            self.failed_command = action
            with self.subTest(action=action), mock.patch.dict(os.environ, self.credentials):
                error = rf'^notarytool {action} failed for {SUBMISSION} \(exit 69\)$'
                with self.assertRaisesRegex(RuntimeError, error), signing.SigningSession() as signer:
                    signer.sign_installer(self.work / 'unsigned.pkg', self.work / 'signed.pkg')
                self.assertFalse(self.signing_dir.exists())
                self.product.assert_not_called()

    def test_signing_and_stapling_surface_tool_output(self):
        for tool in ('codesign', 'productsign', 'stapler', 'ditto'):
            self.failed_command = tool
            with self.subTest(tool=tool), mock.patch.dict(os.environ, self.credentials):
                with self.assertRaisesRegex(
                    RuntimeError, 'tool output.*verification failed'
                ), signing.SigningSession() as signer:
                    if tool in ('codesign', 'ditto'):
                        signer.sign_stage(self.stage)
                    else:
                        signer.sign_installer(self.work / 'unsigned.pkg', self.work / 'signed.pkg')
                self.assertFalse(self.signing_dir.exists())

    def test_rejection_fetches_apple_issues_without_echoing_authentication_diagnostics(self):
        self.status = 'Invalid'
        secret = self.credentials['NOTARY_KEY_ID']
        issue = {'severity': 'error', 'path': 'fx.component', 'message': 'Invalid signature', 'unrelated': secret}
        log = json.dumps({'jobId': SUBMISSION.upper(), 'statusSummary': 'Package rejected', 'issues': [issue]})
        for response, expected in [(log, 'Invalid signature'), ('invalid response', 'log unavailable')]:
            with self.subTest(response=response), mock.patch.dict(os.environ, self.credentials):
                self.notary_log = response
                output = io.StringIO()
                with contextlib.redirect_stdout(output), self.assertRaisesRegex(
                    ValueError, SUBMISSION
                ), signing.SigningSession() as signer:
                    signer.sign_installer(self.work / 'unsigned.pkg', self.work / 'signed.pkg')
                self.assertIn(expected, output.getvalue())
                self.assertNotIn(secret, output.getvalue())
                self.product.assert_not_called()
                self.assertFalse(self.signing_dir.exists())

    def test_unexpected_tool_rejected_before_signing(self):
        (self.stage / 'bin/other').touch()
        with signing.SigningSession() as signer, self.assertRaisesRegex(ValueError, 'unexpected'):
            signer.sign_stage(self.stage)
        self.assertFalse(any(cmd[0] in ('codesign', 'xcrun') for cmd in self.commands))

    def test_missing_credentials_rejected_before_keychain(self):
        del os.environ['NOTARY_KEY_ID']
        with self.assertRaisesRegex(ValueError, 'missing macOS signing credentials: NOTARY_KEY_ID'):
            signing.SigningSession()
        self.assertFalse(self.signing_dir.exists())
        self.assertFalse(self.commands)


if __name__ == '__main__':
    unittest.main()
