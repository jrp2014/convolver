#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline local-link checks for maintained Markdown, using the standard library.

Checks inline and reference-definition destinations, GitHub-style ATX heading
anchors, and HTML id/name anchors. Fenced code and inline code are examples, not
links. Dated archives/plans are not scanned as current instructions, but a link
to one must still resolve. This is intentionally not a full Markdown renderer
or an external-URL availability check.
"""
import argparse
import html
from html.parser import HTMLParser
from pathlib import Path
import re
import sys
import unicodedata
from urllib.parse import unquote, urlsplit

EXCLUDED = {'archive', 'historical', 'review-2026-10', 'superpowers'}
DESTINATION = r'(<[^>\n]+>|(?:[^\s()]|\([^()]*\))+)'  # Includes one level of URL parentheses.
INLINE = re.compile(r'\[[^\]\n]*\]\(\s*' + DESTINATION + r'(?:\s+["\'][^\n]*["\'])?\s*\)')
REFERENCE = re.compile(r'^\s{0,3}\[[^\]\n]+\]:\s*' + DESTINATION)


def prose_lines(text):
    """Yield physical line numbers outside fenced code blocks."""
    fence = None
    for number, line in enumerate(text.splitlines(), 1):
        marker = re.match(r'^\s{0,3}(`{3,}|~{3,})', line)
        if marker:
            run = marker.group(1)
            if fence is None:
                fence = run
            elif run[0] == fence[0] and len(run) >= len(fence):
                fence = None
            continue
        if fence is None:
            yield number, line


class HtmlAnchors(HTMLParser):
    def __init__(self):
        super().__init__()
        self.anchors = set()

    def handle_starttag(self, tag, attrs):
        self.anchors.update(value for name, value in attrs
                            if value and (name == 'id' or (tag == 'a' and name == 'name')))


def anchors(path):
    text = path.read_text(encoding='utf-8')
    parser = HtmlAnchors()
    is_markdown = path.suffix.lower() == '.md'
    html_text = '\n'.join(re.sub(r'(`+).*?\1', '', line)
                          for _, line in prose_lines(text)) if is_markdown else text
    parser.feed(html_text)
    found = parser.anchors
    if not is_markdown:
        return found
    for _, line in prose_lines(text):
        heading = re.match(r'^ {0,3}#{1,6}\s+(.+?)\s*#*\s*$', line)
        if not heading:
            continue
        label = re.sub(r'<[^>]+>', '', heading.group(1))
        label = re.sub(r'\[([^\]]+)\]\([^)]*\)', r'\1', label)
        label = html.unescape(label).lower().replace('`', '')
        slug = ''.join(c for c in label if c in '-_' or
                       unicodedata.category(c)[0] not in {'P', 'S'})
        slug = slug.replace(' ', '-')
        candidate, index = slug, 0
        while candidate in found:
            index += 1
            candidate = slug + '-' + str(index)
        found.add(candidate)
    return found


def documents(root):
    yield from sorted(root.glob('*.md'))
    for path in sorted((root / 'docs').rglob('*.md')):
        if not (set(path.relative_to(root / 'docs').parts[:-1]) & EXCLUDED):
            yield path


def check(root):
    errors, count, cached_anchors = [], 0, {}
    for source in documents(root):
        count += 1
        for number, line in prose_lines(source.read_text(encoding='utf-8')):
            # A code example can mention a link without promising it resolves.
            prose = re.sub(r'(`+).*?\1', '', line)
            matches = list(INLINE.finditer(prose))
            reference = REFERENCE.match(prose)
            if reference:
                matches.append(reference)
            for match in matches:
                raw = match.group(1).strip('<>')
                url = urlsplit(html.unescape(raw))
                if url.scheme or url.netloc:
                    continue
                target = source.parent / unquote(url.path) if url.path else source
                if url.path.startswith('/'):
                    target = root / unquote(url.path).lstrip('/')
                if not target.exists():
                    errors.append(f'{source.relative_to(root)}:{number}: missing target: {raw}')
                    continue
                fragment = unquote(url.fragment)
                if fragment and target.suffix.lower() in {'.md', '.html', '.htm'}:
                    target = target.resolve()
                    if target not in cached_anchors:
                        cached_anchors[target] = anchors(target)
                    if fragment not in cached_anchors[target]:
                        errors.append(f'{source.relative_to(root)}:{number}: missing anchor: {raw}')
    return count, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    count, errors = check(args.root.resolve())
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f'Local links and anchors checked in {count} maintained Markdown files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
