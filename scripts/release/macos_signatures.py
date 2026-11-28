# SPDX-License-Identifier: GPL-2.0-or-later
"""Check Developer ID signatures and notarization on the distributed files."""

import os
import re
import subprocess

TEAM_ID = re.compile(r'[A-Z0-9]{10}')


def details(command):
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env={**os.environ, 'LC_ALL': 'C'},
        check=False,
    )
    print(result.stdout, flush=True)
    result.check_returncode()
    return result.stdout


def verify_signed_code(path, team_id):
    if not TEAM_ID.fullmatch(team_id):
        raise ValueError('invalid Developer ID Team ID')
    requirement = (
        '=anchor apple generic and certificate 1[field.1.2.840.113635.100.6.2.6] exists '
        'and certificate leaf[field.1.2.840.113635.100.6.1.13] exists '
        f'and certificate leaf[subject.OU] = "{team_id}"'
    )
    subprocess.run(['codesign', '--verify', '--strict', '--test-requirement', requirement, path], check=True)
    signature = details(['codesign', '--display', '--verbose=4', path])
    if not re.search(r'^CodeDirectory .*\bflags=0x[0-9a-f]+\([^)]*\bruntime\b', signature, re.MULTILINE):
        raise ValueError(f'{path}: hardened runtime flag missing')
    if not re.search(r'^Timestamp=\S', signature, re.MULTILINE):
        raise ValueError(f'{path}: secure timestamp missing')


def verify_staple(path):
    subprocess.run(['xcrun', 'stapler', 'validate', path], check=True)


def verify_signed_installer(installer, team_id):
    if not TEAM_ID.fullmatch(team_id):
        raise ValueError('invalid Developer ID Team ID')
    signature = details(['pkgutil', '--check-signature', installer])
    if not re.search(rf'^\s*1\. Developer ID Installer: .+ \({team_id}\)\s*$', signature, re.MULTILINE):
        raise ValueError(f'{installer}: expected Developer ID Installer for Team ID {team_id}')
    if 'Signed with a trusted timestamp' not in signature:
        raise ValueError(f'{installer}: trusted timestamp missing')
    verify_staple(installer)
    subprocess.run(['spctl', '--assess', '--type', 'install', '--verbose=2', installer], check=True)
