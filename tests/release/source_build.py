# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a supplied source tree in a fresh directory; never fetch implicitly."""

import argparse
import json
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/release'))
from build_dependencies import add_jobs_option, parallel
from build_dependencies import build as build_dependencies
from contract import load_policy, sha256, system_formats, with_records
from sources import extract


def run(
    source,
    sdk,
    prefix,
    backend,
    cmake,
    *,
    exclude_validator=False,
    license_dir=None,
    au_sdk=None,
    lv2_sdk=None,
    jobs=None,
):
    with tempfile.TemporaryDirectory(prefix='convolver-source-build-') as temporary:
        build = Path(temporary) / 'build'
        configure = [
            cmake,
            '-S',
            str(source),
            '-B',
            str(build),
            '-DCMAKE_BUILD_TYPE=Release',
            '-DCONVOLVER_BUILD_VST3=ON',
            '-DCONVOLVER_VST3_SDK=' + str(sdk),
            '-DCMAKE_PREFIX_PATH=' + str(prefix),
            '-DCONVOLVER_FFT_BACKEND=' + backend,
            '-DCONVOLVER_LEGACY_TESTS=OFF',
            '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
            '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0',
        ]
        if au_sdk:
            # AU-5: the macOS packages hold the Audio Unit too.
            configure += ['-DCONVOLVER_BUILD_AU=ON', '-DCONVOLVER_AU_SDK=' + str(au_sdk)]
        if lv2_sdk:
            # LV2-5: the Linux packages hold the LV2 plug-in too; its tests need lilv.
            configure += ['-DCONVOLVER_BUILD_LV2=ON', '-DCONVOLVER_LV2_SDK=' + str(lv2_sdk)]
        if license_dir:
            configure += ['-DCONVOLVER_PACKAGE_PLUGINS=ON', '-DCONVOLVER_DISTRIBUTION_LICENSE_DIR=' + str(license_dir)]
        if exclude_validator:
            configure += ['-DCONVOLVER_VST3_RUNTIME_VALIDATION=OFF']
        subprocess.run(configure, check=True)
        subprocess.run([cmake, '--build', str(build), '--config', 'Release', *parallel(jobs)], check=True)
        ctest = str(Path(cmake).with_name('ctest')) if Path(cmake).is_absolute() else 'ctest'
        # As in the candidate build, the unshipped CMake package's consumers are
        # left to native CI.
        command = [
            ctest,
            '--test-dir',
            str(build),
            '-C',
            'Release',
            '--output-on-failure',
            *parallel(jobs),
            '-LE',
            'consumer',
        ]
        if exclude_validator:
            print('SDK bundle validator explicitly excluded; this is NOT release evidence.', flush=True)
        subprocess.run(command, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--prefix', type=Path)
    parser.add_argument(
        '--backend',
        choices=['AUTO', 'FFTW', 'ACCELERATE', 'OOURA'],
        default='AUTO',
        help='CONVOLVER_FFT_BACKEND; AUTO is Ooura, or Accelerate on macOS, for plug-in builds',
    )
    parser.add_argument('--au-sdk', type=Path, help='AudioUnitSDK, to build the Audio Unit too (macOS)')
    parser.add_argument('--lv2-sdk', type=Path, help='lv2 source release, to build the LV2 plug-in too (Linux)')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--exclude-validator', action='store_true')
    add_jobs_option(parser)
    args = parser.parse_args()
    if args.archive:
        with tempfile.TemporaryDirectory(prefix='convolver-offline-source-') as temporary:
            root = Path(temporary)
            extract(args.archive, root)
            source = root / 'Convolver-source'
            manifest = json.loads((source / 'source-manifest.json').read_text())
            actual = {
                p.relative_to(source).as_posix(): sha256(p)
                for p in source.rglob('*')
                if p.is_file() and p.name != 'source-manifest.json'
            }
            if actual != manifest['files']:
                raise ValueError('source archive file inventory/hash mismatch')
            # The exact dependency releases the archive records (CI-6).
            policy = with_records(
                load_policy(source / 'convolver/packaging/release-policy.json'), manifest['dependencies']
            )
            work = root / 'dependencies-build'
            # The plug-ins this system's packages hold: VST3, the Audio Unit on
            # macOS and LV2 on Linux.
            formats = system_formats(policy, platform.system())
            sdk, prefix = build_dependencies(
                policy,
                source / 'dependencies',
                work,
                args.cmake,
                fetch=False,
                backend=args.backend,
                formats=formats,
                jobs=args.jobs,
            )
            au_sdk = work / 'sources' / policy['dependencies']['audiounitsdk']['directory'] if 'au' in formats else None
            lv2_sdk = work / 'sources' / policy['dependencies']['lv2']['directory'] if 'lv2' in formats else None
            run(
                source / 'convolver',
                sdk,
                prefix,
                args.backend,
                args.cmake,
                exclude_validator=args.exclude_validator,
                license_dir=work / 'licenses',
                au_sdk=au_sdk,
                lv2_sdk=lv2_sdk,
                jobs=args.jobs,
            )
    else:
        if not args.sdk or not args.prefix:
            parser.error('--sdk and --prefix are required without --archive')
        run(
            args.source,
            args.sdk,
            args.prefix,
            args.backend,
            args.cmake,
            exclude_validator=args.exclude_validator,
            au_sdk=args.au_sdk,
            lv2_sdk=args.lv2_sdk,
            jobs=args.jobs,
        )
