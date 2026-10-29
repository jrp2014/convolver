# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit release-only NSIS acquisition: the newest NSIS 3 release (CI-6).

Downloads it into --output, unpacks it, records what was used in
--output/nsis.json and the job summary, and prints makensis.exe's path (also
as makensis= in --github-output). --version picks another release.
"""

import argparse
import json
from pathlib import Path
import sys
from candidate import extract_zip
from contract import load_policy
import resolve

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--version', help='use this NSIS release instead of the newest, such as 3.11')
parser.add_argument('--github-output', type=Path, help='append makensis=PATH here')
args = parser.parse_args()
policy = load_policy(Path(__file__).resolve().parents[2] / 'packaging/release-policy.json')
output = args.output.resolve()
try:
    record, archive = resolve.resolve_tool(policy, 'nsis', output / 'downloads', wanted=args.version)
except resolve.ResolveError as error:
    sys.exit(f'error: {error}')
extract_zip(archive, output)
makensis = output / ('nsis-' + record['version']) / 'makensis.exe'
if not makensis.is_file():
    sys.exit(f'error: NSIS {record["version"]} from {record["url"]} has no {makensis.relative_to(output)}')
(output / 'nsis.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
resolve.report(resolve.tool_markdown('NSIS', record))
if args.github_output:
    with args.github_output.open('a', encoding='utf-8') as stream:
        stream.write(f'makensis={makensis.as_posix()}\n')
print(makensis)
