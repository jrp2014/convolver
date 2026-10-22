# SPDX-License-Identifier: GPL-2.0-or-later
"""Copy repository guides into a package, keeping every link usable.

The package mirrors the repository's docs/ layout, so a link between two
shipped pages stays relative. A link to anything the package does not contain
(another guide, TODO.md, an example folder) becomes a link to the same path in
the source repository at the packaged source revision (its commit, or the
version tag when the commit is unknown).
"""
import argparse
from pathlib import Path, PurePosixPath
import posixpath
import re

LINK = re.compile(r'\]\(([^)\s]+)\)')
DEFAULT_REPOSITORY = 'https://github.com/jrp2014/convolver'


def rewrite(text, page, shipped, base, shipped_dirs=()):
    """page, shipped and shipped_dirs are repository-relative POSIX paths."""
    def replace(match):
        target = match.group(1)
        if '://' in target or target.startswith(('#', 'mailto:')):
            return match.group(0)
        path, hash_, anchor = target.partition('#')
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(page), path))
        if resolved.startswith('..'):
            raise ValueError(f'{page}: link leaves the repository: {target}')
        if resolved in shipped or any(resolved.startswith(d.rstrip('/') + '/') for d in shipped_dirs):
            return match.group(0)
        return f']({base}/{resolved}{hash_}{anchor})'
    return LINK.sub(replace, text)


def package(source, pages, packaged_dirs, output, version, revision=None, repository=DEFAULT_REPOSITORY):
    """Write each page under output, at its path below docs/."""
    revision = revision or f'v{version}'
    base = f'{repository}/blob/{revision}'
    shipped = set(pages)
    for page in pages:
        text = (source / page).read_text(encoding='utf-8')
        lines = text.split('\n')
        heading = next(i for i, line in enumerate(lines) if line.startswith('# '))
        note = (f'> The copy of `{page}` from Convolver {version}. Links to pages not in '
                f'this package open the [source repository]({repository}/tree/{revision}).')
        lines[heading + 1:heading + 1] = ['', note]
        target = output / PurePosixPath(page).relative_to('docs')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(rewrite('\n'.join(lines), page, shipped, base, packaged_dirs), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='repository root')
    parser.add_argument('--output', type=Path, required=True, help='staged docs/ folder')
    parser.add_argument('--version', required=True)
    parser.add_argument('--stamp', type=Path, required=True, help='file written last, for the build')
    parser.add_argument('--revision', help='source commit to link to; default vVERSION')
    parser.add_argument('--packaged-dir', action='append', default=[],
                        help='repository folder the package ships at the same path (repeatable)')
    parser.add_argument('pages', nargs='+', help='repository-relative pages below docs/')
    args = parser.parse_args()
    revision = args.revision if args.revision and args.revision != 'unknown' else None
    package(args.source, args.pages, args.packaged_dir, args.output, args.version, revision)
    args.stamp.write_text('\n'.join(args.pages) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
