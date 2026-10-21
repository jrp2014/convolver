# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute marked quickstart examples and check packaged guide links."""
import argparse
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def check(package):
    guide = package / 'docs/plugins'
    for page in guide.glob('*.md'):
        for link in re.findall(r'\]\(([^)]+)\)', page.read_text(encoding='utf-8')):
            if '://' in link or link.startswith('#'):
                continue
            target = (page.parent / link.split('#', 1)[0]).resolve()
            if not target.exists():
                raise ValueError(f'broken packaged guide link in {page.name}: {link}')
    quickstart = (guide / 'quickstart.md').read_text(encoding='utf-8')
    commands = re.findall(r'<!-- executable-example -->\s*```sh\n(.*?)\n```', quickstart, re.S)
    if not commands:
        raise ValueError('no executable quickstart examples')
    with tempfile.TemporaryDirectory(prefix='convolver-guide-') as temporary:
        root = Path(temporary) / 'guide ü'
        shutil.copytree(package, root)
        for command in commands:
            tokens = shlex.split(command)
            if tokens[0] != './bin/convolver-vst3-preset' or len(tokens) != 3:
                raise ValueError('quickstart must use the bundled two-argument converter')
            tool = root / 'bin/convolver-vst3-preset'
            if not tool.exists():
                tool = tool.with_suffix('.exe')
            subprocess.run([str(tool), tokens[1], tokens[2]], cwd=root, check=True)
            assert (root / tokens[2]).is_file(), 'quickstart did not produce its stated output'
    print('Packaged guide links and executable quickstart passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    args = parser.parse_args()
    check(args.package.resolve())
