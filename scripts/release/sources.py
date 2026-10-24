# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit hash-verified source acquisition and safe archive extraction.

Run directly to fetch, verify and unpack chosen dependencies, for example the
LV2 headers or AudioUnitSDK in CI. --latest resolves each to its newest
release (resolve.py) and records what it used; --lock rebuilds exactly the
sources a lock file or source manifest records:

python3 scripts/release/sources.py --latest --fetch --cache DIR --destination DIR lv2
python3 scripts/release/sources.py --lock source-manifest.json --fetch --cache DIR --destination DIR lv2

Each unpacked directory gets a .convolver-source.json with its record, which
the plug-in builds read to name the SDK they were built with.
"""
import argparse
import json
from pathlib import Path, PurePosixPath
import posixpath
import shutil
import sys
import tarfile
import tempfile
from contract import load_policy, sha256, with_records
import resolve


STAMP = '.convolver-source.json'


def verify(path, dependency):
    actual = sha256(path)
    if actual != dependency['sha256']:
        raise ValueError(f'source hash mismatch: {Path(path).name} is {actual}, but the record says '
                         f'{dependency["sha256"]} ({dependency.get("url", "no URL recorded")})')


def acquire(dependency, cache, fetch=False):
    """The recorded archive from cache, downloaded first when fetch allows;
    its checksum must match the record."""
    path = cache / dependency['archive']
    if not path.is_file() or (fetch and sha256(path) != dependency['sha256']):
        if not fetch:
            raise ValueError(f'missing source archive: {path.name}; use --fetch explicitly')
        path.unlink(missing_ok=True)
        resolve.fetch_archive(dependency['url'], cache, dependency['archive'])
    verify(path, dependency)
    return path


def _link_source(member, regular):
    """The regular file member an in-archive relative symlink names, or None."""
    link = member.linkname
    if not link or link.startswith('/') or '\\' in link or ':' in link:
        return None
    return regular.get(posixpath.normpath(posixpath.join(posixpath.dirname(member.name), link)))


def extract(archive, destination):
    """Dependency source archives contain regular files and directories, plus
    symlinks to regular files in the same archive (lv2 has one), which are
    written as copies so nothing can point outside the destination. Each
    path may occur once."""
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        members = tar.getmembers()
        names = [posixpath.normpath(m.name) for m in members]
        if len(set(names)) != len(names):
            raise ValueError('unsafe source archive: a path occurs more than once')
        regular = {name: m for name, m in zip(names, members) if m.isfile()}
        sources = {}
        for member in members:
            path = PurePosixPath(member.name)
            source = member if member.isfile() else _link_source(member, regular) if member.issym() else None
            if (path.is_absolute() or '..' in path.parts or '\\' in member.name or
                    ':' in member.name or not (source or member.isdir())):
                raise ValueError(f'unsafe source archive member: {member.name}')
            target = destination / Path(*path.parts)
            try:
                target.resolve().relative_to(destination.resolve())
            except ValueError as error:
                raise ValueError('unsafe source extraction destination') from error
            sources[member.name] = source
        for member in members:
            target = destination / member.name
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                source_member = sources[member.name]
                target.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(source_member) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output)
                target.chmod(source_member.mode & 0o777)


def unpack_dependencies(policy, cache, destination, fetch=False, names=None):
    """Unpack every dependency the policy's resolved records name, or only
    those in names (see contract.with_records)."""
    if names is not None and not set(names) <= set(policy['dependencies']):
        raise ValueError(f'unknown dependencies: {sorted(set(names) - set(policy["dependencies"]))}')
    # Parent SDK before its submodules; never trust an old extracted tree.
    ordered = sorted(((name, dep) for name, dep in policy['dependencies'].items() if names is None or name in names),
                     key=lambda item: item[1]['directory'].count('/'))
    for name, dependency in ordered:
        archive = acquire(dependency, cache, fetch)
        target = destination / dependency['directory']
        stamp = target / STAMP
        if stamp.exists():
            if json.loads(stamp.read_text(encoding='utf-8')).get('sha256') != dependency['sha256']:
                raise ValueError(f'{name}: {target} holds other sources than the record; use a fresh '
                                 'work directory')
            continue
        if target.exists() and any(target.iterdir()):
            raise ValueError(f'{name}: unverified nonempty source directory')
        with tempfile.TemporaryDirectory(dir=destination) as temporary:
            unpacked = Path(temporary)
            extract(archive, unpacked)
            roots = list(unpacked.iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise ValueError(f'{name}: archive must have one top-level directory')
            shutil.copytree(roots[0], target, dirs_exist_ok=True)
        stamp.write_text(json.dumps(dict(dependency, name=name), indent=2) + '\n', encoding='utf-8')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--policy', type=Path, default=Path(__file__).resolve().parents[2] / 'packaging/release-policy.json')
    parser.add_argument('--cache', type=Path, required=True, help='Directory of downloaded archives')
    parser.add_argument('--destination', type=Path, required=True, help='Directory to unpack into')
    parser.add_argument('--fetch', action='store_true', help='Explicitly allow downloads of missing archives')
    which = parser.add_mutually_exclusive_group(required=True)
    which.add_argument('--latest', action='store_true', help='Resolve each to its newest release (needs --fetch)')
    which.add_argument('--lock', type=Path, help='A lock file or source manifest to rebuild exactly')
    parser.add_argument('--version', action='append', metavar='NAME=VERSION',
                        help='With --latest: use this release instead of the newest (repeatable)')
    parser.add_argument('--record', type=Path, help='Also write the records used to this lock file')
    parser.add_argument('--github-output', type=Path,
                        help='Append NAME=DIRECTORY (with forward slashes) for each named dependency here')
    parser.add_argument('names', nargs='+', help='Dependency names from the policy, such as lv2 or audiounitsdk')
    args = parser.parse_args(argv)
    policy = load_policy(args.policy)
    cache = args.cache.resolve()
    try:
        if args.latest:
            if not args.fetch:
                parser.error('--latest downloads the archives; add --fetch')
            records = resolve.resolve(policy, cache, set(args.names), resolve.parse_wanted(args.version))
        else:
            if args.version:
                parser.error('--version needs --latest')
            records = resolve.load_records(args.lock)
        names = resolve.expand(policy['dependencies'], set(args.names))
        records = {name: records[name] for name in names if name in records}
        missing = names - set(records)
        if missing:
            raise ValueError(f'{args.lock} has no record for {sorted(missing)}')
        resolved = with_records(dict(policy, dependencies={n: policy['dependencies'][n] for n in names}), records)
        args.destination.mkdir(parents=True, exist_ok=True)
        unpack_dependencies(resolved, cache, args.destination.resolve(), args.fetch, set(names))
    except ValueError as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    if args.record:
        resolved_at = None
        if args.lock:  # Keep when the records were made, not when they were copied.
            resolved_at = json.loads(args.lock.read_text(encoding='utf-8')).get('resolved_at')
        args.record.write_text(json.dumps(resolve.lock(records, resolved_at), indent=2) + '\n', encoding='utf-8')
    resolve.report(resolve.markdown(records))
    for name in sorted(args.names):
        print(f"{name} {records[name]['version']}: {args.destination / records[name]['directory']}")
    if args.github_output:
        with args.github_output.open('a', encoding='utf-8') as output:
            for name in sorted(args.names):
                output.write(f"{name}={(args.destination.resolve() / records[name]['directory']).as_posix()}\n")
    return 0


if __name__ == '__main__':
    sys.exit(main())
