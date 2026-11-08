#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check repository-wide rules that keep the project navigable.

Each rule is described in docs/develop/workflow.md#automated-checks. The checks use
only the standard library and read files; they never change anything. Run
from anywhere: python3 scripts/check_repository.py [--root DIR].
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

# Dated records and preserved copies: never rewritten, so not held to these rules.
RECORDS = ('historical/', 'docs/historical/', 'docs/archive/', 'docs/review-2026-10/')
# Maintained pages under docs/ live in one of these reader folders.
READER_FOLDERS = ('docs/use/', 'docs/develop/', 'docs/project/')
# Pages whose backticked repository paths must exist.
PATH_SOURCES = ('AGENTS.md', 'CONTRIBUTING.md', 'docs/develop/architecture.md', 'docs/develop/workflow.md')

# Folders under plugins/ that hold code the adapters share, not an adapter:
# the windows (plugins/editor/, VST3-11), tested through each format's tests.
SHARED_PLUGIN_FOLDERS = {'editor'}

# What may sit at the top of the repository. Add to this list deliberately,
# and describe the new entry in the README's repository table.
TOP_LEVEL = {
    '.claude',
    '.clang-format',
    '.clang-format-ignore',
    '.clang-tidy',
    '.github',
    '.git-blame-ignore-revs',
    '.gitattributes',
    '.gitignore',
    '.markdownlint-cli2.yaml',
    '.pre-commit-config.yaml',
    '.yamllint.yaml',
    'AGENTS.md',
    'CHANGELOG.md',
    'CLAUDE.md',
    'CMakeLists.txt',
    'CMakePresets.json',
    'CONTRIBUTING.md',
    'COPYING',
    'README.md',
    'TODO.md',
    'VERSION',
    'cmake',
    'docs',
    'examples',
    'historical',
    'include',
    'packaging',
    'plugins',
    'portable',
    'ruff.toml',
    'scripts',
    'tests',
}

# Files whose license line is required. Data, fixtures and generated inputs are not.
SPDX_SUFFIXES = {'.c', '.cpp', '.h', '.hpp', '.mm', '.swift', '.lua', '.py', '.sh', '.ps1', '.cmake', '.yml', '.yaml'}
SPDX_EXEMPT = ('portable/third_party/', 'tests/fuzz/seeds/', 'tests/fuzz/regressions/', 'docs/benchmarks/')

# DOC-2: where each Convolver tool's options are parsed, and the page that
# documents them. Options are the "--name" string literals outside comments.
TOOL_SOURCES = {
    'convolver': ('portable/main.cpp', 'portable/inspection_cli.cpp', 'portable/compare_cli.cpp'),
    'convolver-preset': ('portable/preset_main.cpp',),
    'convolver-equalizer-apo': ('portable/equalizer_apo_main.cpp',),
}
TOOL_GUIDES = {
    'convolver': 'docs/use/command-line.md',
    'convolver-preset': 'docs/use/presets.md',
    'convolver-equalizer-apo': 'docs/use/equalizer-apo.md',
}
# The page that documents every CMake cache option.
BUILD_GUIDE = 'docs/develop/build.md'
CMAKE_OPTION = re.compile(r'\b(?:cmake_dependent_option|option)\(\s*(CONVOLVER_[A-Z0-9_]+)')
CMAKE_CACHE = re.compile(r'\bset\(\s*(CONVOLVER_[A-Z0-9_]+)\s[^)]*\bCACHE\b([^)]*)\)')
CMAKE_STRINGS = re.compile(r'set_property\(\s*CACHE\s+(CONVOLVER_[A-Z0-9_]+)\s+PROPERTY\s+STRINGS\s+([^)]*)\)')
CMAKE_USE = re.compile(r'-D(CONVOLVER_[A-Z0-9_]+)(?::[A-Z]+)?(?:=([A-Za-z0-9_.-]+))?')
COMMAND_SEPARATOR = re.compile(r'\|\|?|&&|;')
CLI_OPTION = re.compile(r'(?<![\w-])(--[A-Za-z][A-Za-z0-9-]*)')

TODO_DEFINITION = re.compile(r'^- \[([ xX])\] \*\*([A-Z][A-Z0-9]*-\d+) — ', re.MULTILINE)
# Finished TODO items move here, one line each; an ID ticked in one of these
# files counts as defined and done. The files are dated records, so an ID may
# appear in more than one of them.
TODO_ARCHIVE = 'docs/archive/todo/'
PLAN_NAME = re.compile(r'^\d{4}-\d{2}-\d{2}-[a-z0-9]+(?:-[a-z0-9]+)*\.md$')
CODE_SPAN = re.compile(r'`([^`\n]+)`')


def repository_files(root):
    """Tracked and untracked-but-not-ignored files, relative to root."""
    try:
        top = (
            subprocess.run(['git', 'rev-parse', '--show-toplevel'], cwd=root, capture_output=True, check=True)
            .stdout.decode()
            .strip()
        )
        if Path(top).resolve() != root.resolve():
            raise OSError('not the top of a Git work tree')
        listed = subprocess.run(
            ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
            cwd=root,
            capture_output=True,
            check=True,
        ).stdout.decode('utf-8')
        names = [name for name in listed.split('\0') if name]
        return sorted(name for name in names if (root / name).exists())
    except (OSError, subprocess.CalledProcessError):
        skip = {'.git', 'build'}
        return sorted(
            str(path.relative_to(root)).replace('\\', '/')
            for path in root.rglob('*')
            if path.is_file() and not (set(path.relative_to(root).parts) & skip)
        )


def is_record(name):
    return name.startswith(RECORDS)


def maintained_markdown(files):
    return [
        name
        for name in files
        if name.endswith('.md') and not is_record(name) and ('/' not in name or name.startswith('docs/'))
    ]


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
    archived = set()
    for name in files:
        if name.startswith(TODO_ARCHIVE) and name.endswith('.md'):
            archived.update(
                match.group(2) for match in TODO_DEFINITION.finditer(read(root, name)) if match.group(1) != ' '
            )
    for identifier in sorted(archived):
        if identifier in defined:
            errors.append(
                f'TODO.md: {identifier} is also listed as done in {TODO_ARCHIVE}; '
                'IDs must be unique, so remove one or give reopened work a new ID'
            )
        else:
            defined[identifier] = True
    prefixes = sorted({identifier.rsplit('-', 1)[0] for identifier in defined}, key=len, reverse=True)
    if not prefixes:
        return defined
    usage = re.compile(r'(?<![A-Za-z0-9_-])(' + '|'.join(map(re.escape, prefixes)) + r')-(\d+)(?![0-9])')
    for name in maintained_markdown(files):
        for number, line in enumerate(read(root, name).splitlines(), 1):
            for match in usage.finditer(line):
                identifier = match.group(1) + '-' + match.group(2)
                if identifier not in defined:
                    errors.append(
                        f'{name}:{number}: {identifier} is not defined in TODO.md; add the item there or correct the ID'
                    )
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
            errors.append(
                f'{name}: not listed in docs/README.md; add it to "All maintained pages" '
                'or the plans list so readers can find it'
            )


def check_reader_folders(files, errors):
    for name in maintained_markdown(files):
        if name.startswith('docs/') and name != 'docs/README.md' and not name.startswith(READER_FOLDERS):
            errors.append(
                f'{name}: maintained pages belong in a reader folder (docs/use/, '
                'docs/develop/ or docs/project/); dated records go in docs/archive/. '
                'See docs/develop/documentation.md#readers-and-folders'
            )


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
                errors.append(
                    f'{name}: every TODO item it names is done; move it to '
                    'docs/archive/plans/ with an archive notice and an index row'
                )
    archive = root / 'docs' / 'archive' / 'plans' / 'README.md'
    if archive.exists():
        index = read(root, 'docs/archive/plans/README.md')
        for name in files:
            archived = name.startswith('docs/archive/plans/') and name.endswith('.md')
            if archived and not name.endswith('/README.md') and '(' + name.rsplit('/', 1)[1] not in index:
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
    for source in PATH_SOURCES:
        if not (root / source).exists():
            errors.append(
                f'{source}: missing; its repository paths cannot be checked. Restore it, '
                'or update PATH_SOURCES in scripts/check_repository.py if it moved'
            )
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
                    errors.append(f'{source}:{number}: `{token}` does not exist; update the path or the text')


def check_spdx(root, files, errors):
    for name in files:
        if is_record(name) or name.startswith(SPDX_EXEMPT):
            continue
        suffix = Path(name).suffix.lower()
        wanted = (
            suffix in SPDX_SUFFIXES
            or name == 'CMakeLists.txt'
            or name.endswith('/CMakeLists.txt')
            or name in maintained_markdown([name])
        )
        if not wanted:
            continue
        with (root / name).open(encoding='utf-8', errors='replace') as handle:
            head = ''.join(handle.readline() for _ in range(5))
        if 'SPDX-License-Identifier:' not in head:
            errors.append(f'{name}: add an SPDX-License-Identifier line (GPL-2.0-or-later) in the first lines')


def check_layout(root, files, errors):
    for top in sorted({name.split('/', 1)[0] for name in files} - TOP_LEVEL):
        errors.append(
            f'{top}: not an expected top-level entry; put it in an existing folder, '
            'or add it to TOP_LEVEL in scripts/check_repository.py and the README'
        )
    adapters = sorted(
        {name.split('/')[1] for name in files if name.startswith('plugins/') and name.count('/') >= 2}
        - SHARED_PLUGIN_FOLDERS
    )
    for adapter in adapters:
        if not any(name.startswith(f'tests/{adapter}/') for name in files):
            errors.append(f'plugins/{adapter}/: adapter has no tests in tests/{adapter}/')
        guide = 'docs/use/plugins.md'
        if guide not in files or not re.search(rf'\b{re.escape(adapter)}\b', read(root, guide), re.IGNORECASE):
            errors.append(
                f'plugins/{adapter}/: adapter is not described in {guide}; add it to the '
                f'formats table there (the page must name {adapter.upper()})'
            )


def cmake_options(root, files):
    """CMake cache options defined by the build: name -> allowed values or None."""
    options = {}
    for name in files:
        if is_record(name) or not name.endswith(('CMakeLists.txt', '.cmake')):
            continue
        if name.startswith('tests/'):
            continue  # Test projects force the build's options; they define none.
        text = read(root, name)
        for match in CMAKE_OPTION.finditer(text):
            options.setdefault(match.group(1), None)
        for match in CMAKE_CACHE.finditer(text):
            if 'FORCE' not in match.group(2).split():
                options.setdefault(match.group(1), None)
        for match in CMAKE_STRINGS.finditer(text):
            options[match.group(1)] = set(match.group(2).split())
    return options


def check_build_options(root, files, errors):
    """DOC-2: -D options named in guides and CI exist, with allowed values, and the
    build guide names every option."""
    options = cmake_options(root, files)
    if not options:
        return
    sources = [
        name
        for name in maintained_markdown(files)
        if name != 'CHANGELOG.md' and not name.startswith('docs/project/plans/')
    ]
    sources += [name for name in files if name.startswith('.github/workflows/') and name.endswith('.yml')]
    sources += [name for name in files if name.startswith('scripts/') and name.endswith(('.py', '.sh'))]
    for source in sources:
        for number, line in enumerate(read(root, source).splitlines(), 1):
            for match in CMAKE_USE.finditer(line):
                option, value = match.groups()
                if option not in options:
                    errors.append(
                        f'{source}:{number}: -D{option} is not a CMake option of this build; '
                        f'correct the name, or define and document it in {BUILD_GUIDE}'
                    )
                elif (
                    value
                    and options[option] is not None
                    and value.upper() not in {allowed.upper() for allowed in options[option]}
                ):  # CMake upper-cases these values (string(TOUPPER)).
                    allowed = ', '.join(sorted(options[option]))
                    errors.append(f'{source}:{number}: {option}={value} is not one of {allowed}')
    if BUILD_GUIDE in files:
        guide = read(root, BUILD_GUIDE)
        for option in sorted(options):
            if not re.search(rf'\b{option}\b', guide):
                errors.append(f'{BUILD_GUIDE}: does not describe the CMake option {option}')


def tool_options(root, files, tool):
    found = set()
    for name in TOOL_SOURCES[tool]:
        if name in files:
            code = re.sub(r'//[^\n]*', '', read(root, name))
            found.update(re.findall(r'"(--[a-z][a-z0-9-]*)"', code))
    return found


def command_lines(text):
    """(line number, text) for each command of a fenced block, with lines ending
    in a backslash joined to the next, and each inline code span."""
    fenced = False
    pending = None
    for number, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith('```'):
            fenced = not fenced
            if pending:
                yield pending
            pending = None
        elif fenced:
            pending = (pending[0], pending[1] + ' ' + line.strip()) if pending else (number, line)
            if not pending[1].endswith('\\'):
                yield pending
                pending = None
            else:
                pending = (pending[0], pending[1][:-1])
        else:
            for span in CODE_SPAN.findall(line):
                yield number, span


def check_cli_options(root, files, errors):
    """DOC-2: options shown with a Convolver tool exist, and each tool's guide
    names every option it accepts."""
    known = {tool: tool_options(root, files, tool) for tool in TOOL_SOURCES}
    if not any(known.values()):
        return
    every = set().union(*known.values())
    for page in maintained_markdown(files):
        if page == 'CHANGELOG.md' or page.startswith('docs/project/plans/'):
            continue  # Past and proposed options.
        for number, line in command_lines(read(root, page)):
            # Each command of a pipeline or list, run by any path (./bin/,
            # ./build/, .\bin\ on Windows).
            for command in COMMAND_SEPARATOR.split(line):
                words = command.split()
                if not words:
                    continue
                first = re.split(r'[/\\]', words[0])[-1].removesuffix('.exe')
                if first in known:
                    allowed, what = known[first], first
                elif first.startswith('--') and page.startswith('docs/use/'):
                    allowed, what = every, 'any Convolver tool'
                else:
                    continue  # Another program's command, or prose.
                for option in CLI_OPTION.findall(command):
                    if option not in allowed:
                        errors.append(f'{page}:{number}: `{line.strip()}`: {option} is not an option of {what}')
    for tool, guide in TOOL_GUIDES.items():
        if guide not in files or not known[tool]:
            continue
        text = read(root, guide)
        for option in sorted(known[tool]):
            if not re.search(rf'(?<![\w-]){re.escape(option)}(?![\w-])', text):
                errors.append(f'{guide}: does not describe {tool} {option}')


def check(root):
    files = repository_files(root)
    errors = []
    defined = check_todo_ids(root, files, errors)
    check_docs_index(root, files, errors)
    check_reader_folders(files, errors)
    check_plans(root, files, defined, errors)
    check_agent_paths(root, files, errors)
    check_spdx(root, files, errors)
    check_layout(root, files, errors)
    check_build_options(root, files, errors)
    check_cli_options(root, files, errors)
    return len(files), errors


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    count, errors = check(args.root.resolve())
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        print(
            f'{len(errors)} repository rule(s) broken; see docs/develop/workflow.md#automated-checks', file=sys.stderr
        )
        return 1
    print(f'Repository rules hold for {count} files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
