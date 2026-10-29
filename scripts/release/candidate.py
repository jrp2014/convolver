# SPDX-License-Identifier: GPL-2.0-or-later
"""Build an unsigned candidate from an exact source archive, then test its ZIP."""

import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import zipfile
from build_dependencies import add_jobs_option, build as build_dependencies, parallel
from contract import load_policy, platform_policy, sha256, with_records
from installers import prepare
import resolve
from sources import extract
from symbols import check as check_symbols


def run(command, **kwargs):
    subprocess.run([str(arg) for arg in command], check=True, **kwargs)


def extract_zip(archive, destination):
    with zipfile.ZipFile(archive) as package:
        for info in package.infolist():
            path = Path(info.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in info.filename or ':' in info.filename:
                raise ValueError('unsafe ZIP member')
            target = destination / path
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with package.open(info) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output)
                target.chmod((info.external_attr >> 16) & 0o777 or 0o644)


def keep_symbols(build_dir, stage, symbols, identifier, commit, version, system=None):
    """Copies the packaged binaries' debugging symbols (CONVOLVER_DEBUG_SYMBOLS)
    to `symbols`, which the workflow keeps as its own artifact, never in the
    package. Every packaged binary in `stage` must have symbols that hold
    debugging information and match it (symbols.py)."""
    kept = build_dir / 'symbols'
    identities = check_symbols(kept, stage, system or platform.system())
    shutil.copytree(kept, symbols)
    listing = ''.join(f'  {name}: {identity}\n' for name, identity in sorted(identities.items()))
    (symbols / 'README.txt').write_text(
        f'Debugging symbols for the Convolver {version} preview, {identifier}, built from {commit}.\n'
        'They match only the binaries of the same candidate run, checked when it was built:\n'
        f'{listing}'
        'See docs/develop/releasing.md#debugging-symbols.\n'
    )


def build(archive, identifier, work, output, cmake, nsis=None, jobs=None):
    if work.exists() or output.exists():
        raise ValueError('candidate work/output directories must be new')
    work.mkdir(parents=True)
    output.mkdir(parents=True)
    extract(archive, work)
    bundle = work / 'Convolver-source'
    manifest = json.loads((bundle / 'source-manifest.json').read_text())
    expected = manifest['files']
    actual = {
        p.relative_to(bundle).as_posix(): sha256(p)
        for p in bundle.rglob('*')
        if p.is_file() and p.name != 'source-manifest.json'
    }
    if actual != expected:
        raise ValueError('source archive inventory/hash mismatch')
    if manifest.get('schema') != 2:
        raise ValueError(
            'source manifest schema 2 (resolved dependency records) required; build an older '
            'source archive with the scripts it contains'
        )
    source = bundle / 'convolver'
    # The exact releases the source archive carries (CI-6), not a fresh resolution.
    policy = with_records(load_policy(source / 'packaging/release-policy.json'), manifest['dependencies'])
    resolve.report(resolve.markdown(policy['dependencies'], f'Dependencies of the {identifier} candidate'))
    candidate = platform_policy(policy, identifier)
    if platform.system() != candidate['os']:
        raise ValueError('candidate must be built on its target OS')
    deps = work / 'deps'
    sdk, prefix = build_dependencies(
        policy,
        bundle / 'dependencies',
        deps,
        cmake,
        architecture=candidate['architecture'] if platform.system() == 'Darwin' else None,
        backend=candidate['backend'],
        jobs=jobs,
    )
    build_dir = work / 'build'
    configure = [
        cmake,
        '-S',
        source,
        '-B',
        build_dir,
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCONVOLVER_BUILD_VST3=ON',
        '-DCONVOLVER_PACKAGE_PLUGINS=ON',
        '-DCONVOLVER_VST3_SDK=' + str(sdk),
        '-DCMAKE_PREFIX_PATH=' + str(prefix),
        '-DCONVOLVER_DISTRIBUTION_LICENSE_DIR=' + str(deps / 'licenses'),
        '-DCONVOLVER_FFT_BACKEND=' + candidate['backend'],
        '-DCONVOLVER_LEGACY_TESTS=OFF',
        '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0',
        # DIAG-1: matching debugging symbols, kept outside the package.
        '-DCONVOLVER_DEBUG_SYMBOLS=ON',
        # Everything comes from the archive; nothing may be downloaded.
        '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
    ]
    # Linux keeps CMake's default generator (Unix Makefiles), which someone
    # building the corresponding source gets; macOS uses Ninja, so the
    # candidates cover both (CI-10).
    if platform.system() == 'Windows':
        configure += ['-A', 'x64']
    elif platform.system() == 'Darwin':
        configure += ['-G', 'Ninja']
    run(configure)
    run([cmake, '--build', build_dir, '--config', 'Release'] + parallel(jobs))
    ctest = str(Path(cmake).with_name('ctest')) if Path(cmake).is_absolute() else 'ctest'
    # The CMake development package is not shipped: its consumers run in the native
    # workflow (docs/develop/testing.md#which-configurations-run-which-tests).
    run([ctest, '--test-dir', build_dir, '-C', 'Release', '--output-on-failure'] + parallel(jobs) + ['-LE', 'consumer'])
    stage = work / 'stage'
    run([cmake, '--install', build_dir, '--config', 'Release', '--prefix', stage, '--component', 'PluginDistribution'])
    keep_symbols(build_dir, stage, work / 'symbols', identifier, manifest['source_commit'], policy['version'])
    nsis_version = None
    if nsis:
        # The newest NSIS release, from fetch_nsis.py; the candidate record names it.
        nsis_version = subprocess.check_output([str(nsis), '/VERSION'], text=True).strip()
        if not nsis_version.startswith('v3.'):
            raise ValueError(f'unexpected NSIS version {nsis_version!r}; the installer script needs NSIS 3')
        for notice in ('COPYING', 'COPYING.txt'):
            if (nsis.parent / notice).is_file():
                shutil.copyfile(nsis.parent / notice, stage / 'licenses/NSIS.txt')
                break
        else:
            raise ValueError('NSIS licence notice missing')
    source_archive = output / archive.name
    shutil.copyfile(archive, source_archive)
    artifact_manifest = stage / 'artifact-manifest.json'
    run(
        [
            sys.executable,
            source / 'scripts/release/verify_artifact.py',
            '--root',
            stage,
            '--manifest',
            artifact_manifest,
            '--source-archive',
            source_archive,
            '--platform',
            identifier,
            '--build-info',
            build_dir / 'release-build-info-Release.json',
            '--write',
        ]
    )
    filename = candidate['artifact'].format(version=policy['version'])
    zip_path = Path(shutil.make_archive(str(output / filename[:-4]), 'zip', root_dir=stage))
    relocated = work / 'final ZIP ü'
    extract_zip(zip_path, relocated)
    run(
        [
            sys.executable,
            source / 'scripts/release/verify_artifact.py',
            '--root',
            relocated,
            '--manifest',
            relocated / 'artifact-manifest.json',
            '--source-archive',
            source_archive,
        ]
    )
    executable = '.exe' if platform.system() == 'Windows' else ''
    probe_candidates = list(build_dir.glob('plugins/vst3/**/convolver_vst3_package_probe' + executable))
    validator = build_dir / 'bin/Release' / ('validator' + executable)
    if len(probe_candidates) != 1:
        raise ValueError('ambiguous/missing package probe')
    run(
        [
            sys.executable,
            source / 'tests/release/package_smoke.py',
            '--root',
            relocated,
            '--validator',
            validator,
            '--probe',
            probe_candidates[0],
        ]
    )
    installer = None
    if nsis:
        generated = work / 'installer'
        prepare(stage, generated, 'Windows', policy)
        installer = output / filename.replace('.zip', '-unsigned-setup.exe')
        run(
            [
                nsis,
                '/DGENERATED=' + str(generated),
                '/DOUTPUT=' + str(installer),
                source / 'packaging/windows/installer.nsi',
            ]
        )
    elif platform.system() == 'Darwin':
        # Installer receipts are permanent identities (PLUG-4): read them from the registry.
        identities = json.loads((source / 'plugins/identities.json').read_text())['macos_installer']
        generated = work / 'installer'
        prepare(stage, generated, 'Darwin', policy)
        run(
            [
                'pkgbuild',
                '--root',
                generated / 'plugin-payload',
                '--component-plist',
                generated / 'component.plist',
                '--identifier',
                identities['vst3_package_id'],
                '--version',
                policy['version'],
                '--install-location',
                '/Library/Audio/Plug-Ins/VST3',
                generated / 'Convolver-vst3.pkg',
            ]
        )
        run(
            [
                'pkgbuild',
                '--root',
                generated / 'tools-payload',
                '--identifier',
                identities['tools_package_id'],
                '--version',
                policy['version'],
                '--install-location',
                '/Library/Application Support/Convolver',
                generated / 'Convolver-tools.pkg',
            ]
        )
        installer = output / filename.replace('.zip', '-unsigned.pkg')
        run(
            [
                'productbuild',
                '--distribution',
                source / 'packaging/macos/Distribution.xml',
                '--package-path',
                generated,
                installer,
            ]
        )
    # Keep manifests outside archives too, for review without executing anything.
    sidecar = output / (identifier + '-artifact-manifest.json')
    shutil.copyfile(artifact_manifest, sidecar)
    assets = [
        dict(name=p.name, sha256=sha256(p), kind=kind)
        for p, kind in [(zip_path, 'binary'), (source_archive, 'source'), (sidecar, 'manifest')]
        + ([(installer, 'installer')] if installer else [])
    ]
    record = dict(
        id=identifier,
        artifact=zip_path.name,
        source_commit=manifest['source_commit'],
        source=source_archive.name,
        notices=True,
        signed=False,
        notarized=False,
        evidence={},
        automated='passed',
        source_rebuild='passed',
        final_archive='passed',
        installers=[installer.name] if installer else [],
        installer_evidence={},
        dependencies={
            name: {key: dep[key] for key in ('version', 'revision', 'sha256')}
            for name, dep in policy['dependencies'].items()
        },
    )
    if nsis_version:
        record['installer_compiler'] = {'reported': 'NSIS ' + nsis_version}
        # fetch_nsis.py's record: release, URL and checksum of the archive.
        fetched = nsis.parent.parent / 'nsis.json'
        if fetched.is_file():
            record['installer_compiler'].update(json.loads(fetched.read_text(encoding='utf-8')))
    (output / (identifier + '-candidate.json')).write_text(
        json.dumps(
            dict(
                schema=1,
                version=policy['version'],
                source_commit=manifest['source_commit'],
                assets=assets,
                platform=record,
            ),
            indent=2,
        )
        + '\n'
    )


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--platform', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--nsis', type=Path)
    add_jobs_option(parser)
    args = parser.parse_args()
    build(
        args.archive.resolve(),
        args.platform,
        args.work.resolve(),
        args.output.resolve(),
        args.cmake,
        args.nsis.resolve() if args.nsis else None,
        args.jobs,
    )
