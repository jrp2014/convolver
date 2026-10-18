# SPDX-License-Identifier: GPL-2.0-or-later
"""Build pinned, static baseline libraries and materialize the SDK, explicitly."""
import argparse
import json
from pathlib import Path
import platform
import shutil
import subprocess
from contract import load_policy
from sources import unpack_dependencies


def build(policy, cache, work, cmake='cmake', fetch=False, architecture=None):
    work = work.resolve()
    source = work / 'sources'
    source.mkdir(parents=True, exist_ok=True)
    unpack_dependencies(policy, cache.resolve(), source, fetch)
    prefix = work / 'prefix'
    common = ['-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF',
              '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', '-DCMAKE_INSTALL_LIBDIR=lib',
              '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
              '-DCMAKE_INSTALL_PREFIX=' + str(prefix)]
    if platform.system() == 'Darwin':
        common += ['-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0']
        if architecture:
            common += ['-DCMAKE_OSX_ARCHITECTURES=' + architecture]
    for name, options in (
        ('fftw', ['-DENABLE_FLOAT=ON', '-DBUILD_TESTS=OFF', '-DENABLE_SSE=OFF',
                  '-DENABLE_SSE2=OFF', '-DENABLE_AVX=OFF', '-DENABLE_AVX2=OFF',
                  '-DENABLE_OPENMP=OFF', '-DENABLE_THREADS=OFF']),
        ('libsndfile', ['-DBUILD_TESTING=OFF', '-DBUILD_PROGRAMS=OFF', '-DBUILD_EXAMPLES=OFF',
                       '-DENABLE_EXTERNAL_LIBS=OFF', '-DENABLE_MPEG=OFF', '-DENABLE_CPACK=OFF'])):
        output = work / ('build-' + name)
        subprocess.run([cmake, '-S', str(source / name), '-B', str(output)] + common + options, check=True)
        subprocess.run([cmake, '--build', str(output), '--config', 'Release', '--parallel', '2'], check=True)
        subprocess.run([cmake, '--install', str(output), '--config', 'Release'], check=True)
    inventory = {name: {key: value for key, value in dep.items() if key in ('version', 'sha256', 'license', 'revision')}
                 for name, dep in policy['dependencies'].items()}
    licenses = work / 'licenses'
    licenses.mkdir(exist_ok=True)
    for name, dep in policy['dependencies'].items():
        dep_root = source / dep['directory']
        notice = dep_root / ('LICENSE.txt' if name.startswith('vst3') else 'COPYING')
        shutil.copyfile(notice, licenses / (name + '.txt'))
        # Preserve additional upstream notices alongside the main licence.
        for path in dep_root.rglob('*'):
            if path.is_file() and path.name.lower().startswith(('copying', 'license', 'copyright', 'authors')):
                target = licenses / 'upstream' / name / path.relative_to(dep_root)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
    (licenses / 'DEPENDENCIES.json').write_text(json.dumps(inventory, indent=2) + '\n')
    (work / 'dependency-inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
    return source / 'vst3sdk', prefix


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policy', type=Path, default=Path(__file__).resolve().parents[2] / 'packaging/release-policy.json')
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--fetch', action='store_true', help='Explicitly allow downloads of missing pinned archives')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--architecture', choices=['arm64', 'x86_64'])
    args = parser.parse_args()
    build(load_policy(args.policy), args.cache, args.work, args.cmake, args.fetch, args.architecture)
