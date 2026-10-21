#!/usr/bin/env python3
"""Exercise recovered descriptor/format helpers with a stub libsndfile boundary.

Compiles actual CWaveFileHandle, exception classes, SndFileFormat and tables.
The bundled libsndfile header supplies declarations and format constants; no
native Windows build, real descriptor I/O, or codec implementation is tested.
--archive tests the corroborated historical 4.4 source before its RAW-mask and
empty-handle cleanup fixes; the newer explicit status/close tests are omitted.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SUFFIX = '/My Documents/Visual Studio 2008/Projects/convolver/convolver-/convolution/'


def fragments(wave, exception, tables):
    # Remove platform includes only; keep the complete libsndfile wrapper and
    # format helper. Adapt old MSVC exception specifications to modern C++.
    wave = wave[wave.index('class CWaveFileHandle'):wave.index('\n#else\n// Pull in Common DX classes')]
    exception = exception[exception.index('class convolutionException'):exception.index('\nclass filterException')]
    exception = exception.replace('virtual ~convolutionException()', 'virtual ~convolutionException() throw()')
    exception = exception.replace('virtual const char* what() const', 'virtual const char* what() const throw()')
    tables = '\n'.join(line for line in tables.splitlines() if not line.startswith('#include'))
    return exception + '\n' + wave + '\n' + tables


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=ROOT / 'historical/convolution')
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    if args.archive:
        with zipfile.ZipFile(args.archive) as archive:
            # Locate the corroborated source without recording its personal home prefix.
            prefixes = {name[:-len('wavefile.h')] for name in archive.namelist()
                        if name.endswith(SOURCE_SUFFIX + 'wavefile.h')}
            if len(prefixes) != 1:
                parser.error('archive must contain exactly one matching 4.4 source directory')
            prefix = prefixes.pop()
            sources = [archive.read(prefix + name).decode('latin-1').replace('\r\n', '\n')
                       for name in ('wavefile.h', 'exception.h', 'wavefile.cpp')]
    else:
        sources = [(args.source_dir / name).read_text(encoding='latin-1')
                   for name in ('wavefile.h', 'exception.h', 'wavefile.cpp')]
    has_status_api = not args.archive or ('int close()' in sources[0] and 'int error() const' in sources[0])
    harness = Path(__file__).with_suffix('.cpp').read_text()
    production = fragments(*sources)
    code = harness.replace('// INSERT_SOURCE', production)
    if has_status_api:
        code = '#define HAS_STATUS_API\n' + code
    compiler = shutil.which(os.environ.get('CXX', 'clang++'))
    if compiler is None:
        parser.error('Clang is required; set CXX to its executable path')
    failures = 0
    cases = ['fd_raw', 'fd_autodetect', 'fd_write', 'invalid_write', 'open_failure', 'diagnostics', 'formats', 'reject_formats', 'path_compatibility', 'default_handle']
    if has_status_api:
        cases += ['stream_error', 'explicit_close', 'close_failure', 'destructor_failure', 'sync_failure', 'sync_and_close_failure']
    with tempfile.TemporaryDirectory(prefix='convolver-historical-io-') as temp:
        source = Path(temp) / 'regressions.cpp'
        source.write_text(code)
        # The harness uses C++11 conveniences; production must retain the older
        # language subset used by the historical Windows projects.
        compatibility = Path(temp) / 'compatibility.cpp'
        preamble = harness.split('// INSERT_SOURCE')[0]
        preamble = preamble.replace('#include <cstdint>', '').replace('uint32_t DWORD', 'unsigned long DWORD')
        compatibility.write_text(preamble + production)
        subprocess.run([compiler, '-std=c++98', '-fsyntax-only', '-I', str(ROOT / 'historical'), str(compatibility)], check=True)
        print('PASS production C++98 syntax', flush=True)
        if not args.archive:
            tables_only = '\n'.join(line for line in sources[2].splitlines() if not line.startswith('#include'))
            compatibility.write_text(tables_only)
            subprocess.run([compiler, '-std=c++98', '-fsyntax-only', str(compatibility)], check=True)
            print('PASS format tables disabled without LIBSNDFILE', flush=True)
        for build, options in [('debug', ['-D_DEBUG']), ('release', ['-DNDEBUG'])]:
            executable = Path(temp) / build
            subprocess.run([compiler, '-std=c++11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(ROOT / 'historical'), *options, str(source), '-o', str(executable)], check=True)
            for case in cases:
                result = subprocess.run([str(executable), case], capture_output=True, text=True)
                print(f"{'PASS' if result.returncode == 0 else 'FAIL'} {build}: {case}", flush=True)
                if result.returncode:
                    failures += 1
                    print(result.stdout + result.stderr, end='', flush=True)
    print(f'{len(cases) * 2 - failures}/{len(cases) * 2} cases passed')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
