# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the packaged guide's marked examples and check its links."""
import argparse
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


TOOLS = ('convolver-preset', 'convolver-equalizer-apo')


def check(package):
    docs = package / 'docs'
    for page in docs.rglob('*.md'):
        for link in re.findall(r'\]\(([^)\s]+)\)', page.read_text(encoding='utf-8')):
            if '://' in link or link.startswith('#'):
                continue
            target = (page.parent / link.split('#', 1)[0]).resolve()
            if not target.exists():
                raise ValueError(f'broken packaged guide link in {page.relative_to(docs)}: {link}')
    examples = []
    for page in sorted((docs / 'plugins').glob('*.md')):
        text = page.read_text(encoding='utf-8')
        examples += [(page.name, command) for command in
                     re.findall(r'<!-- executable-example -->\s*```sh\n(.*?)\n```', text, re.S)]
    used = {shlex.split(command)[0] for _, command in examples}
    for tool in TOOLS:
        if './bin/' + tool not in used:
            raise ValueError(f'no executable example for {tool} in the packaged guide')
    with tempfile.TemporaryDirectory(prefix='convolver-guide-') as temporary:
        root = Path(temporary) / 'guide ü'
        shutil.copytree(package, root)
        for page, command in examples:
            tokens = shlex.split(command)
            name = tokens[0][len('./bin/'):] if tokens[0].startswith('./bin/') else tokens[0]
            # Plain INPUT OUTPUT... commands, so the output check below is exact.
            if name not in TOOLS or len(tokens) < 3 or any(t.startswith('-') for t in tokens[1:]):
                raise ValueError(f'{page}: examples must run a bundled tool as TOOL INPUT OUTPUT...: {command}')
            tool = root / 'bin' / name
            if not tool.exists():
                tool = tool.with_suffix('.exe')
            subprocess.run([str(tool), *tokens[1:]], cwd=root, check=True)
            for output in tokens[2:]:
                assert (root / output).exists(), f'{page}: {command} did not produce {output}'
            if name == 'convolver-equalizer-apo':
                assert (root / tokens[2] / 'convolver.txt').is_file(), f'{page}: no convolver.txt'
    print('Packaged guide links and executable examples passed')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    args = parser.parse_args()
    check(args.package.resolve())
