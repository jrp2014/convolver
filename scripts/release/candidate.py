# SPDX-License-Identifier: GPL-2.0-or-later
"""Build an unsigned candidate from an exact source archive, then test its ZIP."""
import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from build_dependencies import build as build_dependencies
from contract import load_policy, platform_policy, sha256
from installers import prepare
from sources import extract
from verify_artifact import inventory, verify_files


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


def build(archive, identifier, work, output, cmake, nsis=None):
    if work.exists() or output.exists():
        raise ValueError('candidate work/output directories must be new')
    work.mkdir(parents=True)
    output.mkdir(parents=True)
    extract(archive, work)
    bundle = work / 'Convolver-source'
    manifest = json.loads((bundle / 'source-manifest.json').read_text())
    expected = manifest['files']
    actual = {p.relative_to(bundle).as_posix(): sha256(p) for p in bundle.rglob('*') if p.is_file() and p.name != 'source-manifest.json'}
    if actual != expected:
        raise ValueError('source archive inventory/hash mismatch')
    source = bundle / 'convolver'
    policy = load_policy(source / 'packaging/release-policy.json')
    candidate = platform_policy(policy, identifier)
    if platform.system() != candidate['os']:
        raise ValueError('candidate must be built on its target OS')
    deps = work / 'deps'
    sdk, prefix = build_dependencies(policy, bundle / 'dependencies', deps, cmake, architecture=candidate['architecture'] if platform.system() == 'Darwin' else None, backend=candidate['backend'])
    build_dir = work / 'build'
    configure = [cmake, '-S', source, '-B', build_dir, '-DCMAKE_BUILD_TYPE=Release',
                 '-DCONVOLVER_BUILD_VST3=ON', '-DCONVOLVER_PACKAGE_PLUGINS=ON',
                 '-DCONVOLVER_VST3_SDK=' + str(sdk), '-DCMAKE_PREFIX_PATH=' + str(prefix),
                 '-DCONVOLVER_DISTRIBUTION_LICENSE_DIR=' + str(deps / 'licenses'),
                 '-DCONVOLVER_FFT_BACKEND=' + candidate['backend'], '-DCONVOLVER_LEGACY_TESTS=OFF',
                 '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0']
    if platform.system() == 'Windows':
        configure += ['-A', 'x64']
    else:
        configure += ['-G', 'Ninja']
    run(configure)
    run([cmake, '--build', build_dir, '--config', 'Release', '--parallel', '2'])
    ctest = str(Path(cmake).with_name('ctest')) if Path(cmake).is_absolute() else 'ctest'
    # The CMake development package is not shipped: its consumers run in the native
    # workflow (docs/develop/testing.md#which-configurations-run-which-tests).
    run([ctest, '--test-dir', build_dir, '-C', 'Release', '--output-on-failure', '--parallel', '2',
         '-LE', 'consumer'])
    stage = work / 'stage'
    run([cmake, '--install', build_dir, '--config', 'Release', '--prefix', stage, '--component', 'PluginDistribution'])
    if nsis:
        version = subprocess.check_output([str(nsis), '/VERSION'], text=True).strip()
        if version != 'v' + policy['release_tools']['nsis']['version']:
            raise ValueError('unexpected NSIS version')
        for notice in ('COPYING', 'COPYING.txt'):
            if (nsis.parent / notice).is_file():
                shutil.copyfile(nsis.parent / notice, stage / 'licenses/NSIS.txt')
                break
        else:
            raise ValueError('NSIS licence notice missing')
    source_archive = output / archive.name
    shutil.copyfile(archive, source_archive)
    artifact_manifest = stage / 'artifact-manifest.json'
    run([sys.executable, source / 'scripts/release/verify_artifact.py', '--root', stage,
         '--manifest', artifact_manifest, '--source-archive', source_archive, '--platform', identifier,
         '--build-info', build_dir / 'release-build-info-Release.json', '--write'])
    filename = candidate['artifact'].format(version=policy['version'])
    zip_path = Path(shutil.make_archive(str(output / filename[:-4]), 'zip', root_dir=stage))
    relocated = work / 'final ZIP ü'
    extract_zip(zip_path, relocated)
    run([sys.executable, source / 'scripts/release/verify_artifact.py', '--root', relocated,
         '--manifest', relocated / 'artifact-manifest.json', '--source-archive', source_archive])
    executable = '.exe' if platform.system() == 'Windows' else ''
    probe_candidates = list(build_dir.glob('plugins/vst3/**/convolver_vst3_package_probe' + executable))
    validator = build_dir / 'bin/Release' / ('validator' + executable)
    if len(probe_candidates) != 1:
        raise ValueError('ambiguous/missing package probe')
    run([sys.executable, source / 'tests/release/package_smoke.py', '--root', relocated,
         '--validator', validator, '--probe', probe_candidates[0]])
    installer = None
    if nsis:
        generated = work / 'installer'
        prepare(stage, generated, 'Windows', policy)
        installer = output / filename.replace('.zip', '-unsigned-setup.exe')
        run([nsis, '/DGENERATED=' + str(generated), '/DOUTPUT=' + str(installer), source / 'packaging/windows/installer.nsi'])
    elif platform.system() == 'Darwin':
        # Installer receipts are permanent identities (PLUG-4): read them from the registry.
        identities = json.loads((source / 'plugins/identities.json').read_text())['macos_installer']
        generated = work / 'installer'
        prepare(stage, generated, 'Darwin', policy)
        run(['pkgbuild', '--root', generated / 'plugin-payload', '--component-plist', generated / 'component.plist',
             '--identifier', identities['vst3_package_id'], '--version', policy['version'],
             '--install-location', '/Library/Audio/Plug-Ins/VST3', generated / 'Convolver-vst3.pkg'])
        run(['pkgbuild', '--root', generated / 'tools-payload', '--identifier', identities['tools_package_id'],
             '--version', policy['version'], '--install-location', '/Library/Application Support/Convolver', generated / 'Convolver-tools.pkg'])
        installer = output / filename.replace('.zip', '-unsigned.pkg')
        run(['productbuild', '--distribution', source / 'packaging/macos/Distribution.xml', '--package-path', generated, installer])
    # Keep manifests outside archives too, for review without executing anything.
    sidecar = output / (identifier + '-artifact-manifest.json')
    shutil.copyfile(artifact_manifest, sidecar)
    assets = [dict(name=p.name, sha256=sha256(p), kind=kind) for p, kind in
              [(zip_path, 'binary'), (source_archive, 'source'), (sidecar, 'manifest')] + ([(installer, 'installer')] if installer else [])]
    record = dict(id=identifier, artifact=zip_path.name, source_commit=manifest['source_commit'], source=source_archive.name,
                  notices=True, signed=False, notarized=False, evidence={}, automated='passed',
                  source_rebuild='passed', final_archive='passed',
                  installers=[installer.name] if installer else [], installer_evidence={})
    (output / (identifier + '-candidate.json')).write_text(json.dumps(dict(schema=1, version=policy['version'], source_commit=manifest['source_commit'], assets=assets, platform=record), indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--platform', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--nsis', type=Path)
    args = parser.parse_args()
    build(args.archive.resolve(), args.platform, args.work.resolve(), args.output.resolve(), args.cmake,
          args.nsis.resolve() if args.nsis else None)
