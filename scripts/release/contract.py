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


# Every supporting library builds may use. Entries with 'formats' are used only
# by builds of those plug-in formats; the others (FFTW, libsndfile) by every
# build. The policy says where each comes from; resolve.py picks the newest
# release and records it (CI-6).
REQUIRED_DEPENDENCIES = {'vst3sdk', 'vst3-base', 'vst3-cmake', 'vst3-pluginterfaces', 'vst3-public-sdk',
                         'lv2', 'audiounitsdk', 'fftw', 'libsndfile'}
PLUGIN_FORMATS = {'vst3', 'lv2', 'au'}


def used_dependencies(policy, formats=('vst3',), backend='AUTO'):
    """Names of the dependencies a build of these formats compiles.

    FFTW is used only when the FFT backend is FFTW."""
    unknown = set(formats) - PLUGIN_FORMATS
    if unknown:
        raise ValueError(f'unknown plug-in formats: {sorted(unknown)}')
    return {name for name, dep in policy['dependencies'].items()
            if (name != 'fftw' or backend == 'FFTW') and
            (dep.get('formats') is None or set(dep['formats']) & set(formats))}


def load_policy(path):
    path = Path(path)
    policy = json.loads(path.read_text(encoding='utf-8'))
    source = path.resolve().parents[1]
    policy['version'] = (source / 'VERSION').read_text(encoding='utf-8').strip()
    validate_policy(policy, source)
    return policy


def validate_policy(policy, source=None):
    if policy.get('schema') != 2:
        raise ValueError('unsupported policy schema')
    if not re.fullmatch(r'\d+\.\d+\.\d+', policy.get('version', '')):
        raise ValueError('invalid version')
    if source:
        version = (Path(source) / 'VERSION').read_text(encoding='utf-8').strip()
        if version != policy['version']:
            raise ValueError('policy version conflicts with VERSION')
    if not re.fullmatch(r'[0-9a-f]{40}', policy.get('base_commit', '')):
        raise ValueError('base_commit must be an immutable revision')
    if set(policy.get('dependencies', {})) != REQUIRED_DEPENDENCIES:
        raise ValueError('missing or unexpected dependencies')
    for name, rule in policy['dependencies'].items():
        _validate_rule(name, rule, policy['dependencies'])
    for section in ('release_tools', 'test_tools'):
        for name, rule in policy.get(section, {}).items():
            _validate_source(f'{section}.{name}', rule)
    # Git sources that only advisory CI jobs build (a checkout, no archive).
    for name, rule in policy.get('test_sources', {}).items():
        _validate_source(f'test_sources.{name}', rule, archive=False)
    if not policy.get('vcpkg'):
        raise ValueError('missing vcpkg settings')
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


def _validate_source(name, rule, archive=True):
    """Where a dependency or tool comes from: its repository, how its tags name
    releases, and its archive's URL."""
    if not str(rule.get('upstream', '')).startswith('https://'):
        raise ValueError(f'{name}: missing https upstream repository')
    if archive and not str(rule.get('url', '')).startswith('https://'):
        raise ValueError(f'{name}: missing https archive URL template')
    if 'tag_pattern' in rule:
        try:
            re.compile(rule['tag_pattern'])
        except re.error as error:
            raise ValueError(f'{name}: invalid tag_pattern: {error}') from error
    elif 'tag_prefix' not in rule and 'submodule_of' not in rule:
        raise ValueError(f'{name}: missing tag_prefix or tag_pattern')
    for key in ('sha256', 'revision', 'version'):
        if key in rule:
            raise ValueError(f'{name}: the policy does not pin a {key}; builds resolve the newest release '
                             '(scripts/release/resolve.py)')


def _validate_rule(name, rule, rules):
    _validate_source(name, rule)
    if not rule.get('license') or not rule.get('archive') or not rule.get('directory'):
        raise ValueError(f'{name}: missing license, archive or directory')
    formats = rule.get('formats')
    if formats is not None and (not formats or not set(formats) <= PLUGIN_FORMATS):
        raise ValueError(f'{name}: formats must name plug-in formats from {sorted(PLUGIN_FORMATS)}')
    parent = rule.get('submodule_of')
    if parent is not None:
        if parent not in rules or 'submodule_of' in rules[parent] or not rule.get('path'):
            raise ValueError(f'{name}: submodule_of must name a top-level dependency, with a path')
        if '{revision}' not in rule['url']:
            raise ValueError(f'{name}: a submodule archive is fetched by commit ({{revision}})')


def validate_records(records):
    """Resolved dependencies (a lock file, or a source manifest's) name an exact
    release: its version, the commit its tag points at, an https archive URL
    and the archive's checksum."""
    for name, record in records.items():
        if not re.fullmatch(r'[0-9a-f]{64}', record.get('sha256', '')):
            raise ValueError(f'{name}: missing/invalid sha256')
        if not record.get('url', '').startswith('https://') or not record.get('license') or not record.get('version'):
            raise ValueError(f'{name}: missing URL, version or license')
        if not re.fullmatch(r'[0-9a-f]{40}', record.get('revision', '')):
            raise ValueError(f'{name}: missing immutable revision')
        if not record.get('archive') or not record.get('directory'):
            raise ValueError(f'{name}: missing archive or directory')
        formats = record.get('formats')
        if formats is not None and (not formats or not set(formats) <= PLUGIN_FORMATS):
            raise ValueError(f'{name}: formats must name plug-in formats from {sorted(PLUGIN_FORMATS)}')


def with_records(policy, records):
    """The policy, with its dependency rules replaced by resolved records."""
    missing = set(policy['dependencies']) - set(records)
    if missing:
        raise ValueError(f'no record for dependencies: {sorted(missing)}')
    validate_records(records)
    return dict(policy, dependencies={name: records[name] for name in policy['dependencies']})


def platform_policy(policy, identifier):
    return next(p for p in policy['platforms'] if p['id'] == identifier)
