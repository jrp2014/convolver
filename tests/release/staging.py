# SPDX-License-Identifier: GPL-2.0-or-later
"""Install only PluginDistribution to a private prefix, then relocate it."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from package_smoke import check

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', required=True)
parser.add_argument('--config', required=True)
parser.add_argument('--cmake', required=True)
parser.add_argument('--validator', type=Path, required=True)
parser.add_argument('--probe', type=Path, required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='convolver-stage-') as temporary:
    subprocess.run([args.cmake, '--install', args.build, '--config', args.config,
                    '--prefix', temporary, '--component', 'PluginDistribution'], check=True)
    assert not (Path(temporary) / 'include').exists(), 'library export leaked into distribution'
    check(temporary, args.validator, args.probe)
