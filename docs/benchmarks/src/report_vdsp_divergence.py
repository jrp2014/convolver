# SPDX-License-Identifier: GPL-2.0-or-later
"""Diagnostic continuation of existing gates; reports failures and exits nonzero.

The registered test remains unchanged and stops at its first failed gate.
This compiles a temporary copy with the same numerical calculations and limits,
collecting failures to inspect all nine workloads, including the 512k filters.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
parser.add_argument('--compiler', default='clang++')
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
source = (root / 'tests/native/kernel_divergence_tests.cpp').read_text()
replacements = [
    ('void require(bool b,', 'int failures = 0;\nvoid require(bool b,'),
    ('    if (!b)\n        throw std::runtime_error(message);',
     '    if (!b) {\n        ++failures;\n'
     '        std::cerr << "FAILED GATE: " << message << "\\n";\n    }'),
    ('        std::cout << "kernel divergence tests passed\\n";',
     '        std::cout << "Diagnostic-only continuation; failed gates: " << failures << "\\n";\n'
     '        return failures ? 1 : 0;'),
]
for before, after in replacements:
    if source.count(before) != 1:
        raise RuntimeError('The source test changed; review this diagnostic helper.')
    source = source.replace(before, after, 1)
with tempfile.TemporaryDirectory(prefix='convolver-vdsp-') as temporary:
    cpp = Path(temporary) / 'divergence_all.cpp'
    binary = Path(temporary) / 'divergence_all'
    cpp.write_text(source)
    subprocess.run([args.compiler, '-std=c++20', '-O3', '-DNDEBUG',
                    '-I' + str(root / 'include'), '-I' + str(root / 'portable'), str(cpp),
                    str(args.build.resolve() / 'libconvolver_core.a'),
                    '-framework', 'Accelerate', '-o', str(binary)], check=True)
    raise SystemExit(subprocess.run([str(binary)]).returncode)
