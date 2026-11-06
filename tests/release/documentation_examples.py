# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the packaged guide's marked examples and check its links."""

import argparse
import re
import shlex
import shutil
import subprocess
import tempfile
import textwrap
from pathlib import Path

TOOLS = ('convolver', 'convolver-preset', 'convolver-equalizer-apo')
EXAMPLE = re.compile(
    r'<!-- (executable|refused)-example -->\s*```sh\n(.*?)\n[ \t]*```'
    r'(?:(?:(?!<!--|```).)*?```text\n(.*?)\n[ \t]*```)?',
    re.DOTALL,
)


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
    for page in sorted((docs / 'use').glob('*.md')):
        text = page.read_text(encoding='utf-8')
        # A refused example is a render the guide says is refused: the
        # ```text block after it is the start of the message it prints.
        examples += [
            (page.name, textwrap.dedent(match[2]), match[1] == 'refused', ' '.join((match[3] or '').split()))
            for match in re.finditer(EXAMPLE, text)
        ]
    used = {shlex.split(command)[0] for _, command, _, _ in examples}
    for tool in TOOLS:
        if './bin/' + tool not in used:
            raise ValueError(f'no executable example for {tool} in the packaged guide')
    with tempfile.TemporaryDirectory(prefix='convolver-guide-') as temporary:
        root = Path(temporary) / 'guide ü'
        shutil.copytree(package, root)
        for page, command, refused, message in examples:
            tokens = shlex.split(command)
            name = tokens[0][len('./bin/') :] if tokens[0].startswith('./bin/') else tokens[0]
            tool = root / 'bin' / name
            if not tool.exists():
                tool = tool.with_suffix('.exe')
            if name == 'convolver':
                # convolver --filter F --input I --output O renders O, and
                # convolver --compare OUTPUT EXPECTED must find a match.
                options = dict(zip(tokens[1::2], tokens[2::2]))
                if tokens[1:2] == ['--compare'] and len(tokens) == 4:
                    outputs = []
                elif len(tokens) == 7 and set(options) == {'--filter', '--input', '--output'}:
                    outputs = [options['--output']]
                else:
                    raise ValueError(f'{page}: convolver examples render or compare: {command}')
            # Plain INPUT OUTPUT... commands, so the output check below is exact.
            elif name not in TOOLS or len(tokens) < 3 or any(t.startswith('-') for t in tokens[1:]):
                raise ValueError(f'{page}: examples must run a bundled tool as TOOL INPUT OUTPUT...: {command}')
            else:
                outputs = tokens[2:]
            if refused:
                if name != 'convolver' or not outputs or not message:
                    raise ValueError(f'{page}: a refused example renders and quotes its message: {command}')
                result = subprocess.run([str(tool), *tokens[1:]], cwd=root, capture_output=True, text=True, check=False)
                printed = ' '.join(result.stderr.split())
                assert result.returncode == 1, f'{page}: {command} was not refused: {result.stderr}'
                assert message in printed, f'{page}: {command} printed {result.stderr!r}, not {message!r}'
                assert not (root / outputs[0]).exists(), f'{page}: {command} wrote {outputs[0]}'
                continue
            subprocess.run([str(tool), *tokens[1:]], cwd=root, check=True)
            for output in outputs:
                assert (root / output).exists(), f'{page}: {command} did not produce {output}'
            if name == 'convolver-equalizer-apo':
                assert (root / tokens[2] / 'convolver.txt').is_file(), f'{page}: no convolver.txt'
    print('Packaged guide links and executable examples passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    args = parser.parse_args()
    check(args.package.resolve())
