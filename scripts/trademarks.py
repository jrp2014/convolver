#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check, or write, the VST trademark wording (PLUG-5).

The wording and the logo come from packaging/trademarks/vst.json, the one
source (packaging/trademarks/README.md). Steinberg's usage guidelines ask for
three things, which this checks:

- ® on the first use of "VST" in each document: README.md, every page of
  docs/use/ that mentions VST (they all ship in packages) and the macOS
  installer's text. Code (`VST3`, code blocks) and link targets do not count.
- The trademark sentence in each of them.
- The VST Compatible logo in each of those pages, unmodified: here a closing
  block with the logo and both sentences, which --fix writes or updates.

Exit status 1 lists every problem; --fix writes the closing blocks and then
reports only what it cannot fix (a first mention without ®, a changed logo).
"""

import argparse
import hashlib
import json
import posixpath
import re
import sys
from pathlib import Path

WORDING = 'packaging/trademarks/vst.json'
INSTALLER = 'packaging/macos/Distribution.xml'
BEGIN = '<!-- VST trademark notice: written by scripts/trademarks.py from packaging/trademarks/vst.json -->'
END = '<!-- End of the VST trademark notice -->'
BLOCK = re.compile(re.escape(BEGIN) + r'.*?' + re.escape(END), re.DOTALL)
FENCE = re.compile(r'^(```|~~~).*?^\1[^\n]*$', re.MULTILINE | re.DOTALL)
CODE = re.compile(r'`[^`\n]*`')
COMMENT = re.compile(r'<!--.*?-->', re.DOTALL)
TARGET = re.compile(r'\]\([^)\s]*\)')
# "VST" starting a word: VST3, VST 3 and VST® all count; "vstpreset" does not.
MENTION = re.compile(r'(?<![A-Za-z0-9_])VST')


def load(root):
    return json.loads((root / WORDING).read_text(encoding='utf-8'))


def prose(text):
    """The text a reader sees as words: no closing block, code, comments or link targets."""
    for pattern in (BLOCK, FENCE, COMMENT, CODE, TARGET):
        text = pattern.sub(' ', text)
    return text


def first_mention_marked(text):
    """None when the text never mentions VST, else whether its first mention carries ®."""
    match = MENTION.search(text)
    if match is None:
        return None
    return text[match.end() : match.end() + 1] == '®'


def documents(root):
    """README.md and the user guides that mention VST, repository-relative."""
    pages = ['README.md']
    for page in sorted((root / 'docs/use').glob('*.md')):
        name = page.relative_to(root).as_posix()
        text = page.read_text(encoding='utf-8')
        if first_mention_marked(prose(text)) is not None or BLOCK.search(text):
            pages.append(name)
    return pages


def block(wording, page):
    logo = wording['logo']
    source = posixpath.relpath(logo['file'], posixpath.dirname(page) or '.')
    return '\n'.join(
        [
            BEGIN,
            '',
            f'<img src="{source}" alt="{logo["alt"]}" width="{logo["width"]}">',
            '',
            f'{wording["support"]} {wording["notice"]}',
            '',
            END,
        ]
    )


def with_block(text, wanted):
    if BLOCK.search(text):
        return BLOCK.sub(lambda _: wanted, text, count=1)
    return text.rstrip('\n') + '\n\n' + wanted + '\n'


def check(root, *, fix=False):
    root = Path(root)
    wording = load(root)
    errors = []
    logo = root / wording['logo']['file']
    if not logo.is_file():
        errors.append(f'{wording["logo"]["file"]}: the VST Compatible logo is missing')
    elif hashlib.sha256(logo.read_bytes()).hexdigest() != wording['logo']['sha256']:
        errors.append(
            f'{wording["logo"]["file"]}: not the unmodified Steinberg logo recorded in {WORDING} '
            '(restore it from the upstream path given there)'
        )
    for page in documents(root):
        path = root / page
        text = path.read_text(encoding='utf-8')
        marked = first_mention_marked(prose(text))
        if marked is None:
            # Only the README can get here: every listed guide mentions VST.
            errors.append(f'{page}: mentions VST nowhere, so it needs no notice; remove its closing block')
            continue
        if not marked:
            errors.append(f'{page}: write the first mention of VST as "VST®" (Steinberg\'s usage guidelines)')
        wanted = with_block(text, block(wording, page))
        if wanted != text:
            if fix:
                path.write_text(wanted, encoding='utf-8')
            else:
                errors.append(
                    f'{page}: the closing VST trademark block (logo and notice) is missing or differs '
                    f'from {WORDING}; run python3 scripts/trademarks.py --fix'
                )
    installer = (root / INSTALLER).read_text(encoding='utf-8')
    if first_mention_marked(installer) is False:
        errors.append(f'{INSTALLER}: write the first mention of VST as "VST®"')
    if wording['notice'] not in installer:
        errors.append(f'{INSTALLER}: add the trademark sentence from {WORDING}')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--fix', action='store_true', help='write or update the closing blocks')
    args = parser.parse_args()
    errors = check(args.root.resolve(), fix=args.fix)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        print(f'{len(errors)} VST trademark problem(s); see packaging/trademarks/README.md', file=sys.stderr)
        return 1
    print('VST trademark wording, ® and logo are in place')
    return 0


if __name__ == '__main__':
    sys.exit(main())
