# SPDX-License-Identifier: GPL-2.0-or-later
"""Inspect native binaries and seal/verify a staged release file inventory."""
import argparse
import json
from pathlib import Path, PurePosixPath
import platform
import re
import subprocess
import tarfile
from contract import load_policy, platform_policy, sha256, used_dependencies, with_records


def check_dependencies(system, dependencies, policy):
    allowed = {name.lower() for name in policy['system_libraries'].get(system, [])}
    for dependency in dependencies:
        if system == 'Darwin':
            accepted = dependency.startswith(('/usr/lib/', '/System/Library/Frameworks/'))
        elif system == 'Windows':
            accepted = dependency.lower() in allowed or bool(re.fullmatch(r'api-ms-win-[a-z0-9-]+\.dll', dependency.lower()))
        else:
            accepted = dependency.lower() in allowed
        if not accepted:
            raise ValueError(f'forbidden or unresolved runtime dependency: {dependency}')


def check_symbol_versions(text, maximum):
    versions = re.findall(r'\bGLIBC_(\d+(?:\.\d+)+)', text)
    for version in versions:
        if tuple(map(int, version.split('.'))) > tuple(map(int, maximum.split('.'))):
            raise ValueError(f'GLIBC_{version} exceeds candidate baseline GLIBC_{maximum}')
    return sorted(set(versions))


def inventory(root):
    files = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('symlinks are not permitted in the sealed distribution')
        if path.is_file() and path != root / 'artifact-manifest.json':
            files[path.relative_to(root).as_posix()] = sha256(path)
    return files


def verify_files(root, expected):
    actual = inventory(root)
    if actual.keys() != expected.keys():
        raise ValueError('file inventory differs from manifest')
    for name, checksum in expected.items():
        if actual[name] != checksum:
            raise ValueError(f'file hash mismatch: {name}')


def source_manifest(archive):
    if not archive.is_file():
        raise ValueError('matching corresponding-source archive is required')
    with tarfile.open(archive) as tar:
        member = tar.getmember('Convolver-source/source-manifest.json')
        with tar.extractfile(member) as stream:
            source = json.load(stream)
    if not re.fullmatch(r'[0-9a-f]{40}', source.get('source_commit', '')):
        raise ValueError('invalid source archive commit')
    return source


def source_record(archive):
    source = source_manifest(archive)
    return dict(name=archive.name, sha256=sha256(archive), source_commit=source['source_commit'], version=source['version'])


def used_records(policy, archive, backend):
    """The exact dependency releases (CI-6) this build used, from the source archive."""
    resolved = with_records(policy, source_manifest(archive)['dependencies'])
    used = used_dependencies(resolved, ('vst3',), backend)
    return {name: dep for name, dep in resolved['dependencies'].items() if name in used}


def output(command):
    return subprocess.check_output(command, text=True, errors='replace')


def inspect_binary(path, candidate, policy):
    system = candidate['os']
    arch = candidate['architecture']
    record = {}
    if system == 'Darwin':
        slices = output(['lipo', '-archs', str(path)]).strip().split()
        if slices != [arch]:
            raise ValueError(f'incorrect architecture slices in {path.name}: {slices}')
        libraries = [line.strip().split(' (', 1)[0] for line in output(['otool', '-L', str(path)]).splitlines()[1:] if line.strip()]
        loads = output(['otool', '-l', str(path)])
        rpaths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset', loads)
        if rpaths:
            raise ValueError(f'unexpected runtime search paths in {path.name}: {rpaths}')
        minimum = re.findall(r'\bminos (\S+)', loads) or re.findall(r'\bversion (\d+\.\d+)', loads)
        if not minimum or any(tuple(map(int, v.split('.'))) > (14, 0, 0) for v in minimum):
            raise ValueError('macOS binary exceeds the proposed 14.0 deployment target')
        record['minimum_os'] = minimum
    elif system == 'Linux':
        headers = output(['readelf', '-h', str(path)])
        if arch != 'x86_64' or 'Advanced Micro Devices X86-64' not in headers:
            raise ValueError(f'incorrect ELF architecture: {path.name}')
        dynamic = output(['readelf', '-d', str(path)])
        libraries = re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic)
        if re.search(r'\((RPATH|RUNPATH)\)', dynamic):
            raise ValueError(f'unexpected runtime search path: {path.name}')
        record['glibc_versions'] = check_symbol_versions(output(['readelf', '--version-info', str(path)]), candidate['max_glibc'])
    elif system == 'Windows':
        headers = output(['dumpbin', '/headers', str(path)])
        if arch != 'x86_64' or not re.search(r'8664 machine', headers, re.IGNORECASE):
            raise ValueError(f'incorrect PE architecture: {path.name}')
        libraries = re.findall(r'^\s+([\w.\-]+\.dll)\s*$', output(['dumpbin', '/dependents', str(path)]), re.MULTILINE | re.IGNORECASE)
    else:
        raise ValueError('unsupported binary platform')
    check_dependencies(system, libraries, policy)
    record.update(architecture=arch, dependencies=libraries)
    return record


def inspect(root, candidate, policy):
    if platform.system() != candidate['os']:
        raise ValueError('binary inspection must run on the target OS')
    records = {}
    magics = (b'\x7fELF', b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe')
    for path in sorted(root.rglob('*')):
        if not path.is_file():
            continue
        with path.open('rb') as stream:
            magic = stream.read(4)
        if magic in magics or magic[:2] == b'MZ':
            records[path.relative_to(root).as_posix()] = inspect_binary(path, candidate, policy)
    if not any(name.startswith('VST3/Convolver.vst3/') for name in records):
        raise ValueError('missing native plug-in binary')
    for tool in ('bin/convolver-preset', 'bin/convolver-equalizer-apo'):
        if tool not in records and tool + '.exe' not in records:
            raise ValueError('missing tool binary: ' + tool)
    return records


def verify(root, manifest, policy, source_archive):
    candidate = platform_policy(policy, manifest['platform'])
    if manifest.get('schema') != 1 or manifest['version'] != policy['version'] or manifest['backend'] != candidate['backend']:
        raise ValueError('artifact policy mismatch')
    if source_record(source_archive) != manifest['source']:
        raise ValueError('source archive differs from artifact manifest')
    if manifest['source']['source_commit'] != manifest['source_commit']:
        raise ValueError('source commit mismatch')
    verify_files(root, manifest['files'])
    if inspect(root, candidate, policy) != manifest['binaries']:
        raise ValueError('runtime binary inventory mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--policy', type=Path, default=Path(__file__).resolve().parents[2] / 'packaging/release-policy.json')
    parser.add_argument('--source-archive', type=Path, required=True)
    parser.add_argument('--write', action='store_true', help='Inspect stage and create an unsigned developer-preview manifest')
    parser.add_argument('--platform')
    parser.add_argument('--build-info', type=Path)
    args = parser.parse_args()
    policy = load_policy(args.policy)
    root = args.root.resolve()
    if args.write:
        if not args.platform or not args.build_info:
            parser.error('--write requires --platform and --build-info')
        if args.manifest.exists():
            raise ValueError('manifest already exists; sealing never overwrites')
        candidate = platform_policy(policy, args.platform)
        build = json.loads(args.build_info.read_text())
        source = source_record(args.source_archive)
        if build.get('source_dirty', True):
            raise ValueError('cannot seal a build from a dirty source tree')
        if build['version'] != policy['version'] or build['backend'] != candidate['backend'] or build['source_commit'] != source['source_commit']:
            raise ValueError('actual build, source archive and policy disagree')
        if build['configuration'] != 'Release' or re.search(r'(-march=native|-mcpu=native|/arch:AVX)', build['flags'], re.I):
            raise ValueError('nonportable release build flags or configuration')
        notices = {'FFTW': ('licenses/fftw.txt',), 'OOURA': ('licenses/Ooura.txt',)}.get(candidate['backend'], ())
        for required in ('licenses/Convolver.txt', 'licenses/DEPENDENCIES.json', 'licenses/libsndfile.txt', *notices, 'licenses/vst3sdk.txt', 'docs/use/README.md', 'docs/use/first-sound-in-a-host.md', 'docs/use/presets.md', 'docs/use/equalizer-apo.md', 'presets/examples.json', 'VST3/Convolver.vst3/Contents/Resources/moduleinfo.json', 'VERSION.txt'):
            if not (root / required).is_file():
                raise ValueError(f'missing release content: {required}')
        manifest = dict(schema=1, version=policy['version'], platform=args.platform, source_commit=build['source_commit'],
                        backend=candidate['backend'], channel='developer-preview', source=source, build=build,
                        build_os=platform.platform(), dependencies=used_records(policy, args.source_archive,
                                                                                candidate['backend']),
                        binaries=inspect(root, candidate, policy),
                        files=inventory(root))
        args.manifest.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    else:
        manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    verify(root, manifest, policy, args.source_archive)
    print('Artifact inventory, source identity, architecture and runtime dependencies verified')


if __name__ == '__main__':
    main()
