# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared release contracts. No third-party Python packages are required."""
import hashlib
import json
from pathlib import Path
import re


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def load_policy(path):
    path = Path(path)
    policy = json.loads(path.read_text(encoding='utf-8'))
    source = path.resolve().parents[1]
    policy['version'] = (source / 'VERSION').read_text(encoding='utf-8').strip()
    validate_policy(policy, source)
    return policy


def validate_policy(policy, source=None):
    if policy.get('schema') != 1:
        raise ValueError('unsupported policy schema')
    if not re.fullmatch(r'\d+\.\d+\.\d+', policy.get('version', '')):
        raise ValueError('invalid version')
    if source:
        version = (Path(source) / 'VERSION').read_text(encoding='utf-8').strip()
        if version != policy['version']:
            raise ValueError('policy version conflicts with VERSION')
    if not re.fullmatch(r'[0-9a-f]{40}', policy.get('base_commit', '')):
        raise ValueError('base_commit must be an immutable revision')
    required = {'vst3sdk', 'vst3-base', 'vst3-cmake', 'vst3-pluginterfaces', 'vst3-public-sdk', 'fftw', 'libsndfile'}
    if set(policy.get('dependencies', {})) != required:
        raise ValueError('missing or unexpected dependencies')
    for name, dep in policy['dependencies'].items():
        if not re.fullmatch(r'[0-9a-f]{64}', dep.get('sha256', '')):
            raise ValueError(f'{name}: missing/invalid sha256')
        if not dep.get('url', '').startswith('https://') or not dep.get('license') or not dep.get('version'):
            raise ValueError(f'{name}: missing URL, version or license')
        if name.startswith('vst3') and not re.fullmatch(r'[0-9a-f]{40}', dep.get('revision', '')):
            raise ValueError(f'{name}: missing immutable revision')
    if not re.fullmatch(r'[0-9a-f]{40}', policy.get('vcpkg', {}).get('baseline', '')):
        raise ValueError('missing vcpkg baseline')
    if policy['vcpkg'].get('triplet') != 'x64-windows-static-md':
        raise ValueError('Windows requires static dependencies and dynamic MSVC runtime')
    names, ids = set(), set()
    if not policy.get('platforms'):
        raise ValueError('missing platform artifacts')
    for platform in policy['platforms']:
        name = platform.get('artifact', '')
        if not name or '/' in name or '\\' in name or '{version}' not in name:
            raise ValueError('missing/invalid artifact name')
        if name in names or platform.get('id') in ids:
            raise ValueError('duplicate artifact or platform')
        names.add(name)
        ids.add(platform.get('id'))
        if not platform.get('architecture') or platform.get('backend') not in ('FFTW', 'ACCELERATE', 'OOURA'):
            raise ValueError('missing architecture/backend')
        if platform.get('status') not in ('tested', 'build-only', 'unverified'):
            raise ValueError('invalid support status')
        if platform['status'] == 'tested' and not platform.get('evidence'):
            raise ValueError('tested platform requires evidence')


def platform_policy(policy, identifier):
    return next(p for p in policy['platforms'] if p['id'] == identifier)
