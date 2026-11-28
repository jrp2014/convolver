# SPDX-License-Identifier: GPL-2.0-or-later
"""Sign and notarize the macOS distribution with a short-lived runner keychain."""

import argparse
import base64
import binascii
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import uuid
from pathlib import Path

from macos_signatures import verify_signed_code, verify_signed_installer, verify_staple
from symbols import TARGETS

SECRETS = (
    'DEVELOPER_ID_APPLICATION_P12',
    'DEVELOPER_ID_APPLICATION_P12_PASSWORD',
    'DEVELOPER_ID_INSTALLER_P12',
    'DEVELOPER_ID_INSTALLER_P12_PASSWORD',
    'SIGNING_KEYCHAIN_PASSWORD',
    'NOTARY_KEY_P8',
    'NOTARY_KEY_ID',
    'NOTARY_ISSUER_ID',
)
BUNDLES = (Path('VST3/Convolver.vst3'), Path('Components/Convolver.component'))


# Apple's published Developer ID G2 intermediate (DeveloperIDG2CA.cer).
G2_SHA256 = 'f16cd3c54c7f83cea4bf1a3e6a0819c8aaa8e4a1528fd144715f350643d2df3a'


def _result(program, *args):
    # security import and keychain setup take passwords as arguments. Never
    # print their commands, output or a CalledProcessError containing either.
    # Use only Apple tools and keep the credentials out of child environments.
    env = {k: os.environ[k] for k in ('HOME', 'TMPDIR') if k in os.environ}
    env.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin', LC_ALL='C')
    return subprocess.run(
        [f'/usr/bin/{program}', *args],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )


def _run(program, *args):
    result = _result(program, *args)
    if result.returncode:
        message = f'{program} {args[0]} failed (exit {result.returncode})'
        if program in ('codesign', 'productsign', 'ditto') or (program == 'xcrun' and args[0] == 'stapler'):
            # These commands take no secret arguments. Stapler can use stdout
            # for errors. Quote both streams as data on one runner log line.
            detail = '\n'.join(s.strip() for s in (result.stdout, result.stderr) if s and s.strip())
            if detail:
                message += ': ' + json.dumps(detail)
        raise RuntimeError(message)
    return result.stdout


def _directory():
    return Path(os.environ['RUNNER_TEMP']).resolve() / 'convolver-macos-signing'


def cleanup():
    """Also called by the workflow's always() step after an interrupted job."""
    directory = _directory()
    if directory.is_symlink():
        raise ValueError('signing directory is a symbolic link')
    if directory.exists():
        keychain = directory / 'signing.keychain-db'
        try:
            if keychain.exists():
                # delete-keychain also removes it from the user's search list.
                _run('security', 'delete-keychain', keychain)
        finally:
            shutil.rmtree(directory)


def _write_private(path, encoded):
    try:
        data = base64.b64decode(''.join(encoded.split()), validate=True)
    except (ValueError, binascii.Error):
        raise ValueError(f'invalid base64 for {path.suffix} signing credential') from None
    with path.open('xb') as file:
        path.chmod(0o600)
        file.write(data)


def _identity(keychain, kind):
    # Installer certificates lack the code-signing EKU. Use basic policy for
    # them; the signed product is subsequently assessed by Gatekeeper.
    policy = 'codesigning' if kind == 'Application' else 'basic'
    listing = _run('security', 'find-identity', '-v', '-p', policy, keychain)
    pattern = rf'^\s*\d+\) ([A-Fa-f0-9]{{40}}) "(Developer ID {kind}: .+ \(([A-Z0-9]{{10}})\))"\s*$'
    matches = re.findall(pattern, listing, re.MULTILINE)
    if len(matches) != 1:
        raise ValueError(f'expected one valid Developer ID {kind} identity in the temporary keychain')
    return matches[0]


class SigningSession:
    """Own the keys from entry until the signed files have been verified."""

    def __init__(self):
        # The caller can inspect the input before opening a keychain. Children
        # of that inspection must not inherit the signing step's secrets.
        self._credentials = {name: os.environ.pop(name, '') for name in SECRETS}
        missing = [name for name in SECRETS if not self._credentials[name]]
        if missing:
            raise ValueError('missing macOS signing credentials: ' + ', '.join(missing))

    def __enter__(self):
        self.directory = _directory()
        # A second invocation cannot reuse an abandoned keychain.
        self.directory.mkdir(mode=0o700)
        self.keychain = self.directory / 'signing.keychain-db'
        try:
            self._import(self._credentials)
        except BaseException:
            cleanup()
            raise
        finally:
            self._credentials.clear()
        return self

    def _import(self, credentials):
        password = credentials['SIGNING_KEYCHAIN_PASSWORD']
        _run('security', 'create-keychain', '-p', password, self.keychain)
        _run('security', 'set-keychain-settings', '-lut', '10800', self.keychain)
        _run('security', 'unlock-keychain', '-p', password, self.keychain)
        existing = shlex.split(_run('security', 'list-keychains', '-d', 'user'))
        if str(self.keychain) not in existing:
            _run('security', 'list-keychains', '-d', 'user', '-s', self.keychain, *existing)
        for kind, tool in (('APPLICATION', 'codesign'), ('INSTALLER', 'productsign')):
            name = f'DEVELOPER_ID_{kind}_P12'
            path = self.directory / f'{kind}.p12'
            _write_private(path, credentials[name])
            _run(
                'security',
                'import',
                path,
                '-f',
                'pkcs12',
                '-k',
                self.keychain,
                '-P',
                credentials[name + '_PASSWORD'],
                '-T',
                f'/usr/bin/{tool}',
            )
            path.unlink()
        # An exported identity can include its intermediate. Otherwise add
        # Apple's public G2 certificate, obtained before the protected step.
        certificates = _run('security', 'find-certificate', '-a', '-Z', self.keychain)
        if G2_SHA256 not in re.findall(r'^sha-256 hash: ([0-9a-f]{64})$', certificates.lower(), re.MULTILINE):
            issuer = self.directory.parent / 'DeveloperIDG2CA.cer'
            if hashlib.sha256(issuer.read_bytes()).hexdigest() != G2_SHA256:
                raise ValueError('unexpected fingerprint for Apple Developer ID G2 intermediate')
            _run('security', 'add-certificates', '-k', self.keychain, issuer)
        _run('security', 'set-key-partition-list', '-S', 'apple-tool:,apple:', '-s', '-k', password, self.keychain)
        self.application_hash, _, self.team_id = _identity(self.keychain, 'Application')
        _, self.installer_name, installer_team = _identity(self.keychain, 'Installer')
        if installer_team != self.team_id:
            raise ValueError('Developer ID Application and Installer team IDs differ')
        self.notary_key = self.directory / 'notary.p8'
        _write_private(self.notary_key, credentials['NOTARY_KEY_P8'])
        self.key_id = credentials['NOTARY_KEY_ID']
        self.issuer = credentials['NOTARY_ISSUER_ID']

    def __exit__(self, *_exc):
        cleanup()

    def _report_notary_issues(self, submission, auth):
        # Read Apple's structured log. Authentication diagnostics and unrelated
        # response fields stay out of Actions logs.
        log = _result('xcrun', 'notarytool', 'log', submission, *auth)
        try:
            report = json.loads(log.stdout) if not log.returncode else None
        except ValueError:
            report = None
        if not isinstance(report, dict) or str(report.get('jobId', '')).lower() != submission:
            print(f'Apple notarization log unavailable for {submission}', flush=True)
            return
        print('Apple notarization summary: ' + json.dumps(report.get('statusSummary')), flush=True)
        issues = report.get('issues')
        if isinstance(issues, list):
            for issue in issues:
                if isinstance(issue, dict):
                    fields = {k: issue[k] for k in ('severity', 'path', 'architecture', 'message') if k in issue}
                    print('Apple notarization issue: ' + json.dumps(fields), flush=True)

    def _submit(self, path):
        auth = ['--key', self.notary_key, '--key-id', self.key_id, '--issuer', self.issuer, '--output-format', 'json']
        # Retain the submission ID even if waiting times out or Apple's
        # response to a rejected submission has a nonzero status. The ID can
        # be passed to notarytool log; raw tool diagnostics may contain keys.
        upload = _result('xcrun', 'notarytool', 'submit', path, *auth)
        for report in (upload.stdout, upload.stderr):
            try:
                submission = str(uuid.UUID(str(json.loads(report)['id'])))
                break
            except (KeyError, ValueError, TypeError):
                continue
        else:
            raise ValueError(f'notarytool submit returned no valid submission ID (exit {upload.returncode})')
        if upload.returncode:
            self._report_notary_issues(submission, auth)
            raise RuntimeError(f'notarytool submit failed for {submission} (exit {upload.returncode})')
        print(f'Apple notarization submission {submission}', flush=True)
        wait = _result('xcrun', 'notarytool', 'wait', submission, *auth)
        if wait.returncode:
            self._report_notary_issues(submission, auth)
            raise RuntimeError(f'notarytool wait failed for {submission} (exit {wait.returncode})')
        try:
            report = json.loads(wait.stdout)
        except ValueError:
            report = None
        if (
            not isinstance(report, dict)
            or report.get('status') != 'Accepted'
            or str(report.get('id', '')).lower() != submission
        ):
            self._report_notary_issues(submission, auth)
            raise ValueError(f'Apple did not accept notarization submission {submission}')
        return submission

    def sign_stage(self, stage):
        binaries = [Path(path) for _, path in TARGETS['Darwin']]
        # The distribution and its installer reject symlinks. Do this before
        # using the signing tools or copying the submission on their behalf.
        if stage.is_symlink() or any(p.is_symlink() for p in stage.rglob('*')):
            raise ValueError('signing stage must not contain symlinks')
        if any(not (stage / path).is_file() for path in binaries):
            raise ValueError('missing staged macOS executable')
        if not (stage / BUNDLES[0] / 'Contents/Resources/moduleinfo.json').is_file():
            raise ValueError('the VST3 module information must be present before signing')
        tools = [path for path in binaries if path.parts[0] == 'bin']
        if set((stage / 'bin').iterdir()) != {stage / path for path in tools}:
            raise ValueError('unexpected macOS command-line tool')
        for path in (*binaries, *BUNDLES):
            _run(
                'codesign',
                '--force',
                '--options',
                'runtime',
                '--timestamp',
                '--keychain',
                self.keychain,
                '--sign',
                self.application_hash,
                stage / path,
            )
        for path in (*BUNDLES, *tools):
            verify_signed_code(stage / path, self.team_id)
        # Include only the shipping bundles and the three tools. Neither the
        # keychain nor the private key is underneath the submission directory.
        contents = self.directory / 'submission'
        for path in (*BUNDLES, *tools):
            target = contents / path
            target.parent.mkdir(parents=True, exist_ok=True)
            _run('ditto', stage / path, target)
        archive = self.directory / 'submission.zip'
        _run('ditto', '-c', '-k', contents, archive)
        submission = self._submit(archive)
        for path in BUNDLES:
            _run('xcrun', 'stapler', 'staple', stage / path)
            verify_staple(stage / path)
        return submission

    def sign_installer(self, unsigned_path, signed_path):
        _run(
            'productsign',
            '--sign',
            self.installer_name,
            '--keychain',
            self.keychain,
            '--timestamp',
            unsigned_path,
            signed_path,
        )
        submission = self._submit(signed_path)
        _run('xcrun', 'stapler', 'staple', signed_path)
        verify_signed_installer(signed_path, self.team_id)
        return submission


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--cleanup', required=True, action='store_true', help='remove the runner signing keychain and files'
    )
    parser.parse_args()
    cleanup()
