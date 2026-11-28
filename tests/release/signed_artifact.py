# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify the signed plug-ins, tools and tickets extracted from the final ZIP."""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
from macos_signatures import verify_signed_code, verify_staple  # noqa: E402
from symbols import check as check_symbols  # noqa: E402

TOOLS = ('convolver', 'convolver-preset', 'convolver-equalizer-apo')


def check_payload(vst3, component, bin_dir, team_id):
    for item in (vst3, component, *(bin_dir / name for name in TOOLS)):
        verify_signed_code(item, team_id)
    for bundle in (vst3, component):
        verify_staple(bundle)


def check_unpacked(root, team_id):
    check_payload(root / 'VST3/Convolver.vst3', root / 'Components/Convolver.component', root / 'bin', team_id)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True, help='the extracted final ZIP')
    parser.add_argument('--symbols', type=Path, required=True, help='dSYM files retained by the build')
    parser.add_argument('--team-id', required=True)
    args = parser.parse_args()
    check_symbols(args.symbols, args.root, 'Darwin')
    check_unpacked(args.root.resolve(), args.team_id)
