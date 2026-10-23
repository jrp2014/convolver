#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check repository-wide rules that keep the project navigable.

Each rule is described in docs/develop/workflow.md#automated-checks. The checks use
only the standard library and read files; they never change anything. Run
from anywhere: python3 scripts/check_repository.py [--root DIR].
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

# Dated records and preserved copies: never rewritten, so not held to these rules.
RECORDS = ('historical/', 'docs/historical/', 'docs/archive/', 'docs/review-2026-10/')

# What may sit at the top of the repository. Add to this list deliberately,
# and describe the new entry in the README's repository table.
TOP_LEVEL = {
    '.claude', '.clang-format', '.clang-format-ignore', '.clang-tidy', '.github',
    '.gitattributes', '.gitignore', '.markdownlint-cli2.yaml', '.pre-commit-config.yaml',
    '.yamllint.yaml', 'AGENTS.md', 'CHANGELOG.md', 'CLAUDE.md', 'CMakeLists.txt',
    'CMakePresets.json', 'CONTRIBUTING.md', 'COPYING', 'README.md', 'TODO.md',
    'VERSION', 'cmake', 'docs', 'examples', 'historical', 'include', 'packaging',
    'plugins', 'portable', 'scripts', 'tests',
}

# Files whose license line is required. Data, fixtures and generated inputs are not.
SPDX_SUFFIXES = {'.c', '.cpp', '.h', '.hpp', '.mm', '.py', '.sh', '.ps1', '.cmake', '.yml', '.yaml'}
SPDX_EXEMPT = ('portable/third_party/', 'tests/fuzz/seeds/', 'tests/fuzz/regressions/',
               'docs/benchmarks/')

TODO_DEFINITION = re.compile(r'^- \[([ xX])\] \*\*([A-Z][A-Z0-9]*-\d+) — ', re.M)
PLAN_NAME = re.compile(r'^\d{4}-\d{2}-\d{2}-[a-z0-9]+(?:-[a-z0-9]+)*\.md$')
CODE_SPAN = re.compile(r'`([^`\n]+)`')


def repository_files(root):
    """Tracked and untracked-but-not-ignored files, relative to root."""
    try:
        top = subprocess.run(['git', 'rev-parse', '--show-toplevel'], cwd=root,
                             capture_output=True, check=True).stdout.decode().strip()
        if Path(top).resolve() != root.resolve():
            raise OSError('not the top of a Git work tree')
        listed = subprocess.run(
            ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
            cwd=root, capture_output=True, check=True).stdout.decode('utf-8')
        names = [name for name in listed.split('\0') if name]
        return sorted(name for name in names if (root / name).exists())
    except (OSError, subprocess.CalledProcessError):
        skip = {'.git', 'build'}
        return sorted(str(path.relative_to(root)).replace('\\', '/')
                      for path in root.rglob('*')
                      if path.is_file() and not (set(path.relative_to(root).parts) & skip))


def is_record(name):
    return name.startswith(RECORDS)


def maintained_markdown(files):
    return [name for name in files if name.endswith('.md') and not is_record(name)
            and ('/' not in name or name.startswith('docs/'))]


def read(root, name):
    return (root / name).read_text(encoding='utf-8', errors='replace')


def check_todo_ids(root, files, errors):
    if not (root / 'TODO.md').exists():
        errors.append('TODO.md: missing')
        return {}
    todo = read(root, 'TODO.md')
    defined = {}
    for match in TODO_DEFINITION.finditer(todo):
        done, identifier = match.group(1) != ' ', match.group(2)
        if identifier in defined:
            errors.append(f'TODO.md: {identifier} is defined more than once; IDs must be unique')
        defined[identifier] = done
    prefixes = sorted({identifier.rsplit('-', 1)[0] for identifier in defined}, key=len, reverse=True)
    if not prefixes:
        return defined
    usage = re.compile(r'(?<![A-Za-z0-9_-])(' + '|'.join(map(re.escape, prefixes)) + r')-(\d+)(?![0-9])')
    for name in maintained_markdown(files):
        for number, line in enumerate(read(root, name).splitlines(), 1):
            for match in usage.finditer(line):
                identifier = match.group(1) + '-' + match.group(2)
                if identifier not in defined:
                    errors.append(f'{name}:{number}: {identifier} is not defined in TODO.md; '
                                  'add the item there or correct the ID')
    return defined


def check_docs_index(root, files, errors):
    index = root / 'docs' / 'README.md'
    if not index.exists():
        return
    linked = set()
    for match in re.finditer(r'\]\(([^)#\s]+)', read(root, 'docs/README.md')):
        target = (index.parent / match.group(1)).resolve()
        linked.add(target)
    for name in files:
        if not (name.startswith('docs/') and name.endswith('.md')) or is_record(name):
            continue
        if name == 'docs/README.md':
            continue
        if (root / name).resolve() not in linked:
            errors.append(f'{name}: not listed in docs/README.md; add it to "All maintained pages" '
                          'or the plans list so readers can find it')


def check_plans(root, files, defined, errors):
    for name in files:
        if name.startswith('docs/project/plans/') and name.endswith('.md'):
            base = name.rsplit('/', 1)[1]
            if base == 'README.md':
                continue
            if not PLAN_NAME.match(base):
                errors.append(f'{name}: plan names start with their date, as YYYY-MM-DD-topic.md')
            text = read(root, name)
            ids = {m.group(0) for m in re.finditer(r'\b[A-Z][A-Z0-9]*-\d+\b', text)} & set(defined)
            if not ids:
                errors.append(f'{name}: an open plan must name the TODO item(s) it serves')
            elif all(defined[i] for i in ids):
                errors.append(f'{name}: every TODO item it names is done; move it to '
                              'docs/archive/plans/ with an archive notice and an index row')
    archive = root / 'docs' / 'archive' / 'plans' / 'README.md'
    if archive.exists():
        index = read(root, 'docs/archive/plans/README.md')
        for name in files:
            if name.startswith('docs/archive/plans/') and name.endswith('.md') \
                    and not name.endswith('/README.md'):
                if '(' + name.rsplit('/', 1)[1] not in index:
                    errors.append(f'{name}: archived plan missing from docs/archive/plans/README.md')


def path_like(token):
    token = token.strip().rstrip('.,;:')
    if ' ' in token or token.startswith(('-', '$', '/', '~', 'http')) or '://' in token:
        return None
    if any(mark in token for mark in '<>{}'):
        return None  # A placeholder such as plugins/<format>/, not a real path.
    first = token.split('/', 1)[0]
    if '/' not in token and '.' not in first:
        return None
    return token


def check_agent_paths(root, files, errors):
    tracked_top = {name.split('/', 1)[0] for name in files}
    for source in ('AGENTS.md', 'CONTRIBUTING.md', 'docs/develop/architecture.md', 'docs/develop/workflow.md'):
        if not (root / source).exists():
            continue
        for number, line in enumerate(read(root, source).splitlines(), 1):
            for match in CODE_SPAN.finditer(line):
                token = path_like(match.group(1))
                if not token or token.split('/', 1)[0] not in tracked_top:
                    continue
                if '*' in token or '?' in token:
                    found = list(root.glob(token))
                else:
                    found = [root / token] if (root / token.rstrip('/')).exists() else []
                if not found:
                    errors.append(f'{source}:{number}: `{token}` does not exist; '
                                  'update the path or the text')


def check_spdx(root, files, errors):
    for name in files:
        if is_record(name) or name.startswith(SPDX_EXEMPT):
            continue
        suffix = Path(name).suffix.lower()
        wanted = suffix in SPDX_SUFFIXES or name in {'CMakeLists.txt'} \
            or name.endswith('/CMakeLists.txt') or name in maintained_markdown([name])
        if not wanted:
            continue
        with open(root / name, encoding='utf-8', errors='replace') as handle:
            head = ''.join(handle.readline() for _ in range(5))
        if 'SPDX-License-Identifier:' not in head:
            errors.append(f'{name}: add an SPDX-License-Identifier line '
                          '(GPL-2.0-or-later) in the first lines')


def check_layout(root, files, errors):
    for top in sorted({name.split('/', 1)[0] for name in files} - TOP_LEVEL):
        errors.append(f'{top}: not an expected top-level entry; put it in an existing folder, '
                      'or add it to TOP_LEVEL in scripts/check_repository.py and the README')
    adapters = sorted({name.split('/')[1] for name in files
                       if name.startswith('plugins/') and name.count('/') >= 2})
    for adapter in adapters:
        if not any(name.startswith(f'tests/{adapter}/') for name in files):
            errors.append(f'plugins/{adapter}/: adapter has no tests in tests/{adapter}/')
        guide = 'docs/use/plugins.md'
        if guide not in files or not re.search(rf'\b{re.escape(adapter)}\b', read(root, guide), re.I):
            errors.append(f'plugins/{adapter}/: adapter is not described in {guide}; add it to the '
                          f'formats table there (the page must name {adapter.upper()})')


def check(root):
    files = repository_files(root)
    errors = []
    defined = check_todo_ids(root, files, errors)
    check_docs_index(root, files, errors)
    check_plans(root, files, defined, errors)
    check_agent_paths(root, files, errors)
    check_spdx(root, files, errors)
    check_layout(root, files, errors)
    return len(files), errors


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    count, errors = check(args.root.resolve())
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        print(f'{len(errors)} repository rule(s) broken; see docs/develop/workflow.md#automated-checks',
              file=sys.stderr)
        return 1
    print(f'Repository rules hold for {count} files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
