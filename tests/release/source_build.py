# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a supplied source tree in a fresh directory; never fetch implicitly."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import json
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/release'))
from contract import load_policy, sha256, with_records
from sources import extract
from build_dependencies import build as build_dependencies


def run(source, sdk, prefix, backend, cmake, exclude_validator=False, license_dir=None):
    with tempfile.TemporaryDirectory(prefix='convolver-source-build-') as temporary:
        build = Path(temporary) / 'build'
        configure = [cmake, '-S', str(source), '-B', str(build),
                        '-DCMAKE_BUILD_TYPE=Release', '-DCONVOLVER_BUILD_VST3=ON',
                        '-DCONVOLVER_VST3_SDK=' + str(sdk), '-DCMAKE_PREFIX_PATH=' + str(prefix),
                        '-DCONVOLVER_FFT_BACKEND=' + backend, '-DCONVOLVER_LEGACY_TESTS=OFF',
                        '-DFETCHCONTENT_FULLY_DISCONNECTED=ON', '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0']
        if license_dir:
            configure += ['-DCONVOLVER_PACKAGE_PLUGINS=ON', '-DCONVOLVER_DISTRIBUTION_LICENSE_DIR=' + str(license_dir)]
        if exclude_validator:
            configure += ['-DCONVOLVER_VST3_RUNTIME_VALIDATION=OFF']
        subprocess.run(configure, check=True)
        subprocess.run([cmake, '--build', str(build), '--config', 'Release', '--parallel', '2'], check=True)
        ctest = str(Path(cmake).with_name('ctest')) if Path(cmake).is_absolute() else 'ctest'
        # As in the candidate build, the unshipped CMake package's consumers are left to native CI.
        command = [ctest, '--test-dir', str(build), '-C', 'Release', '--output-on-failure', '--parallel', '2',
                   '-LE', 'consumer']
        if exclude_validator:
            print('SDK bundle validator explicitly excluded; this is NOT release evidence.', flush=True)
        subprocess.run(command, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--prefix', type=Path)
    parser.add_argument('--backend', choices=['AUTO', 'FFTW', 'ACCELERATE', 'OOURA'], default='AUTO',
                        help='CONVOLVER_FFT_BACKEND; AUTO is Ooura, or Accelerate on macOS, for plug-in builds')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--exclude-validator', action='store_true')
    args = parser.parse_args()
    if args.archive:
        with tempfile.TemporaryDirectory(prefix='convolver-offline-source-') as temporary:
            root = Path(temporary)
            extract(args.archive, root)
            source = root / 'Convolver-source'
            manifest = json.loads((source / 'source-manifest.json').read_text())
            actual = {p.relative_to(source).as_posix(): sha256(p) for p in source.rglob('*')
                      if p.is_file() and p.name != 'source-manifest.json'}
            if actual != manifest['files']:
                raise ValueError('source archive file inventory/hash mismatch')
            # The exact dependency releases the archive records (CI-6).
            policy = with_records(load_policy(source / 'convolver/packaging/release-policy.json'),
                                  manifest['dependencies'])
            work = root / 'dependencies-build'
            sdk, prefix = build_dependencies(policy, source / 'dependencies', work, args.cmake, fetch=False,
                                             backend=args.backend)
            run(source / 'convolver', sdk, prefix, args.backend, args.cmake, args.exclude_validator, work / 'licenses')
    else:
        if not args.sdk or not args.prefix:
            parser.error('--sdk and --prefix are required without --archive')
        run(args.source, args.sdk, args.prefix, args.backend, args.cmake, args.exclude_validator)
