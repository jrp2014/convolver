#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check change fragments, or fold them into the shared records and delete them.

Each pull request adds its own file under changes/ (changes/README.md shows
the form) instead of editing the top of CHANGELOG.md, TODO.md's done list or
docs/project/decisions.md, so that two pull requests never edit the same
lines. Folding moves every fragment's entries into those three pages and
deletes the fragments (DOC-20, docs/develop/workflow.md#change-fragments).

    python3 scripts/fold_changes.py --check   # validate only (pre-commit)
    python3 scripts/fold_changes.py           # fold, then delete the fragments

Uses only the standard library. Run from anywhere; --root names the checkout.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

FRAGMENTS = 'changes'
CHANGELOG = 'CHANGELOG.md'
TODO = 'TODO.md'
DECISIONS = 'docs/project/decisions.md'

# CHANGELOG groups under [Unreleased], in the order the changelog keeps them.
GROUPS = ('Added', 'Changed', 'Deprecated', 'Removed', 'Fixed', 'Security', 'For contributors')
DONE = 'Done'
DECISION_PREFIX = 'Decisions: '
# decisions.md runs each section oldest first, except this one, newest first.
NEWEST_FIRST_DECISIONS = {'Releases'}
# The paragraph that opens TODO's list of items finished since the last move
# to docs/archive/todo/; new lines go at the top of the list after it.
DONE_LIST_LEAD = 'Items finished since then are listed here'

DONE_LINE = re.compile(r'^- \[[xX]\] \*\*[A-Z][A-Z0-9]*-\d+ — ')
INLINE_LINK = re.compile(r'(\]\()(\s*<?)([^\s()<>]+)')
FRAGMENT_NAME = re.compile(r'^[A-Z][A-Z0-9]*-\d+(?:-[a-z0-9]+)*\.md$')


class FragmentError(ValueError):
    pass


def fragment_paths(root):
    folder = root / FRAGMENTS
    if not folder.is_dir():
        return []
    return sorted(path for path in folder.glob('*.md') if path.name != 'README.md')


def parse(path):
    """Return [(section, [lines])] for one fragment, or raise FragmentError."""
    name = f'{FRAGMENTS}/{path.name}'
    if not FRAGMENT_NAME.match(path.name):
        raise FragmentError(
            f'{name}: name a fragment after its TODO ID, with an optional lower-case suffix '
            '(PLUG-40.md, PLUG-39-step6.md), so that each pull request adds its own file'
        )
    sections, current, title = [], None, False
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        stripped = line.strip()
        if current is None and (not stripped or (stripped.startswith('<!--') and stripped.endswith('-->'))):
            continue
        if line.startswith('# ') and not title and current is None:
            title = True
            continue
        heading = re.match(r'^##\s+(.+?)\s*$', line)
        if heading:
            section = heading.group(1)
            if not (section in GROUPS or section == DONE or section.startswith(DECISION_PREFIX)):
                allowed = ', '.join((*GROUPS, DONE, DECISION_PREFIX + '<section of decisions.md>'))
                raise FragmentError(f'{name}:{number}: unknown section "{section}"; use one of: {allowed}')
            if any(section == seen for seen, _ in sections):
                raise FragmentError(f'{name}:{number}: section "{section}" appears twice; merge them')
            current = (section, [])
            sections.append(current)
            continue
        if current is None:
            raise FragmentError(
                f'{name}:{number}: text before the first "## " section; start with "# <ID>" then sections'
            )
        if line.startswith('#'):
            raise FragmentError(f'{name}:{number}: only "## " sections are allowed in a fragment')
        if stripped:  # The records keep tight lists: blank lines are dropped.
            current[1].append(line.rstrip())
    if not title:
        raise FragmentError(f'{name}: start with a "# " title, such as "# PLUG-40"')
    if not sections:
        raise FragmentError(f'{name}: no sections; add at least one, such as "## Changed"')
    for section, lines in sections:
        if not lines:
            raise FragmentError(f'{name}: section "{section}" is empty')
        if not lines[0].startswith('- '):
            raise FragmentError(f'{name}: section "{section}" must be a list; start each entry with "- "')
        if section == DONE:
            for line in lines:
                if line.startswith('- ') and not DONE_LINE.match(line):
                    raise FragmentError(
                        f'{name}: "{line[:40]}…" in Done is not a ticked TODO line, '
                        'such as "- [x] **DOC-20 — Title.** 21 November 2026, PR #257 (…)."'
                    )
    return sections


def rewrite_links(lines, page):
    """Fragment links are written from the repository root, as in CHANGELOG.md
    and TODO.md; make them relative to the page they are folded into."""
    base = str(Path(page).parent)
    if base == '.':
        return lines

    def relative(match):
        target = match.group(3)
        if re.match(r'^[a-z][a-z0-9+.-]*:', target, re.IGNORECASE) or target.startswith(('#', '/')):
            return match.group(0)
        path, hash_mark, fragment = target.partition('#')
        moved = os.path.relpath(path, base).replace(os.sep, '/') if path else ''
        return match.group(1) + match.group(2) + moved + hash_mark + fragment

    return [INLINE_LINK.sub(relative, line) for line in lines]


def section_bounds(lines, heading, level, start=0, end=None):
    """(first line after the heading, end of the section) or None."""
    end = len(lines) if end is None else end
    marker = '#' * level + ' '
    for index in range(start, end):
        if lines[index].rstrip() == marker + heading:
            stop = index + 1
            while stop < end and not re.match(rf'^#{{1,{level}}} ', lines[stop]):
                stop += 1
            return index + 1, stop
    return None


def insert_top(lines, body_start, body_end, entries):
    """Insert entries before a section's first list item, or after its prose
    when it has no list yet."""
    for index in range(body_start, body_end):
        if lines[index].startswith('- '):
            lines[index:index] = entries
            return
    insert_bottom(lines, body_start, body_end, entries)


def insert_bottom(lines, body_start, body_end, entries):
    """Insert entries after a section's last non-blank line, keeping one blank
    line between a list and the prose or heading around it."""
    index = body_end
    while index > body_start and not lines[index - 1].strip():
        index -= 1
    block = list(entries)
    if index == body_start or not lines[index - 1].startswith(('- ', '  ')):
        block.insert(0, '')
    if index < len(lines) and lines[index].strip():
        block.append('')
    lines[index:index] = block


def fold_changelog(lines, section, entries):
    unreleased = section_bounds(lines, '[Unreleased]', 2)
    if unreleased is None:
        raise FragmentError(f'{CHANGELOG}: no "## [Unreleased]" section to fold into')
    group = section_bounds(lines, section, 3, *unreleased)
    if group is None:
        # Create the group in its place among the others.
        later = GROUPS[GROUPS.index(section) + 1 :]
        index = unreleased[1]
        for name in later:
            found = section_bounds(lines, name, 3, *unreleased)
            if found:
                index = found[0] - 1
                break
        while index > unreleased[0] and not lines[index - 1].strip():
            index -= 1
        lines[index:index] = ['', '### ' + section, '', *entries]
        return
    insert_top(lines, group[0], group[1], entries)


def fold_done(lines, entries):
    done = section_bounds(lines, 'Done', 2)
    if done is None:
        raise FragmentError(f'{TODO}: no "## Done" section to fold into')
    start = done[0]
    for index in range(done[0], done[1]):
        if lines[index].startswith(DONE_LIST_LEAD):
            start = index
            break
    insert_top(lines, start, done[1], entries)


def fold_decision(lines, section, entries):
    bounds = section_bounds(lines, section, 2)
    if bounds is None:
        raise FragmentError(f'{DECISIONS}: no "## {section}" section; use one of its existing sections')
    if section in NEWEST_FIRST_DECISIONS:
        insert_top(lines, bounds[0], bounds[1], entries)
    else:
        insert_bottom(lines, bounds[0], bounds[1], entries)


def added_time(root, path):
    """When the fragment was first committed (seconds), so entries keep the
    order their pull requests merged in; uncommitted fragments come last."""
    try:
        stamp = subprocess.run(
            ['git', 'log', '--diff-filter=A', '--format=%ct', '-1', '--', str(path.relative_to(root))],
            cwd=root,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        stamp = ''
    return int(stamp) if stamp else sys.maxsize


def read_page(root, name):
    path = root / name
    if not path.exists():
        raise FragmentError(f'{name}: missing; fragments fold into it')
    return path.read_text(encoding='utf-8').splitlines()


def check(root):
    """Parse every fragment and make sure each section has somewhere to go."""
    parsed = [(path, parse(path)) for path in fragment_paths(root)]
    decisions = None
    for path, sections in parsed:
        for section, _ in sections:
            if section.startswith(DECISION_PREFIX):
                decisions = decisions if decisions is not None else read_page(root, DECISIONS)
                target = section[len(DECISION_PREFIX) :]
                if section_bounds(decisions, target, 2) is None:
                    raise FragmentError(
                        f'{FRAGMENTS}/{path.name}: "{section}" names no "## {target}" section in {DECISIONS}'
                    )
    return parsed


def fold(root):
    parsed = check(root)
    if not parsed:
        return []
    parsed.sort(key=lambda item: (added_time(root, item[0]), item[0].name))
    pages = {name: read_page(root, name) for name in (CHANGELOG, TODO, DECISIONS)}
    # Oldest first: newest-first lists insert at the top, so the newest ends up
    # there; oldest-first decision sections append.
    for _, sections in parsed:
        for section, entries in sections:
            if section in GROUPS:
                fold_changelog(pages[CHANGELOG], section, rewrite_links(entries, CHANGELOG))
            elif section == DONE:
                fold_done(pages[TODO], rewrite_links(entries, TODO))
            else:
                target = section[len(DECISION_PREFIX) :]
                fold_decision(pages[DECISIONS], target, rewrite_links(entries, DECISIONS))
    for name, lines in pages.items():
        (root / name).write_text('\n'.join(lines) + '\n', encoding='utf-8')
    for path, _ in parsed:
        path.unlink()
    return [path for path, _ in parsed]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--check', action='store_true', help='validate the fragments; change nothing')
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        if args.check:
            count = len(check(root))
            print(f'{count} change fragment(s) are well formed')
            return 0
        folded = fold(root)
    except FragmentError as error:
        print(f'Change fragments: {error}. See {FRAGMENTS}/README.md', file=sys.stderr)
        return 1
    if not folded:
        print('No change fragments to fold')
        return 0
    for path in folded:
        print(f'Folded and deleted {FRAGMENTS}/{path.name}')
    print(f'Folded {len(folded)} fragment(s) into {CHANGELOG}, {TODO} and {DECISIONS}; review and commit the result')
    return 0


if __name__ == '__main__':
    sys.exit(main())
