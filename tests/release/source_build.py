# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a supplied source tree in a fresh directory; never fetch implicitly."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def run(source, sdk, prefix, backend, cmake, exclude_validator=False):
    with tempfile.TemporaryDirectory(prefix='convolver-source-build-') as temporary:
        build = Path(temporary) / 'build'
        subprocess.run([cmake, '-S', str(source), '-B', str(build),
                        '-DCMAKE_BUILD_TYPE=Release', '-DCONVOLVER_BUILD_VST3=ON',
                        '-DCONVOLVER_VST3_SDK=' + str(sdk), '-DCMAKE_PREFIX_PATH=' + str(prefix),
                        '-DCONVOLVER_FFT_BACKEND=' + backend, '-DCONVOLVER_LEGACY_TESTS=OFF'], check=True)
        subprocess.run([cmake, '--build', str(build), '--config', 'Release', '--parallel', '2'], check=True)
        ctest = str(Path(cmake).with_name('ctest')) if Path(cmake).is_absolute() else 'ctest'
        command = [ctest, '--test-dir', str(build), '-C', 'Release', '--output-on-failure']
        if exclude_validator:
            command += ['-E', '^vst3.validator$']
            print('SDK bundle validator explicitly excluded; this is NOT release evidence.', flush=True)
        subprocess.run(command, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--backend', choices=['FFTW', 'ACCELERATE'], default='FFTW')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--exclude-validator', action='store_true')
    args = parser.parse_args()
    run(args.source, args.sdk, args.prefix, args.backend, args.cmake, args.exclude_validator)
