# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit hash-verified source acquisition and safe archive extraction.

Run directly to fetch, verify and unpack chosen pins, for example the LV2
headers or AudioUnitSDK in CI:
python3 scripts/release/sources.py --fetch --cache DIR --destination DIR lv2
"""
import argparse
from pathlib import Path, PurePosixPath
import posixpath
import shutil
import tarfile
import tempfile
import urllib.request
from contract import load_policy, sha256


def verify(path, dependency):
    if sha256(path) != dependency['sha256']:
        raise ValueError(f'source hash mismatch: {Path(path).name}')


def acquire(dependency, cache, fetch=False):
    path = cache / dependency['archive']
    if not path.is_file():
        if not fetch:
            raise ValueError(f'missing pinned source archive: {path.name}; use --fetch explicitly')
        cache.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=cache, delete=False) as temporary:
            temp = Path(temporary.name)
        try:
            urllib.request.urlretrieve(dependency['url'], temp)
            verify(temp, dependency)
            temp.replace(path)
        finally:
            temp.unlink(missing_ok=True)
    verify(path, dependency)
    return path


def _link_source(member, regular):
    """The regular file an in-archive relative symlink names, or None."""
    link = member.linkname
    if not link or link.startswith('/') or '\\' in link or ':' in link:
        return None
    target = posixpath.normpath(posixpath.join(posixpath.dirname(member.name), link))
    return target if target in regular else None


def extract(archive, destination):
    """Pinned source archives contain regular files and directories, plus
    symlinks to regular files in the same archive (lv2 has one), which are
    written as copies so nothing can point outside the destination."""
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        members = tar.getmembers()
        regular = {posixpath.normpath(m.name) for m in members if m.isfile()}
        links = {}
        for member in members:
            path = PurePosixPath(member.name)
            safe_type = member.isfile() or member.isdir()
            if member.issym():
                links[member.name] = _link_source(member, regular)
                safe_type = links[member.name] is not None
            if (path.is_absolute() or '..' in path.parts or '\\' in member.name or
                    ':' in member.name or not safe_type):
                raise ValueError(f'unsafe source archive member: {member.name}')
            target = destination / Path(*path.parts)
            try:
                target.resolve().relative_to(destination.resolve())
            except ValueError as error:
                raise ValueError('unsafe source extraction destination') from error
        for member in members:
            target = destination / member.name
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                source_member = tar.getmember(links[member.name]) if member.issym() else member
                target.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(source_member) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output)
                target.chmod(source_member.mode & 0o777)


def unpack_dependencies(policy, cache, destination, fetch=False, names=None):
    """Unpack every pinned dependency, or only those in names."""
    if names is not None and not set(names) <= set(policy['dependencies']):
        raise ValueError(f'unknown dependencies: {sorted(set(names) - set(policy["dependencies"]))}')
    # Parent SDK before its submodules; never trust an old extracted tree.
    ordered = sorted(((name, dep) for name, dep in policy['dependencies'].items() if names is None or name in names),
                     key=lambda item: item[1]['directory'].count('/'))
    for name, dependency in ordered:
        archive = acquire(dependency, cache, fetch)
        target = destination / dependency['directory']
        stamp = target / '.convolver-source-sha256'
        if stamp.exists():
            if stamp.read_text().strip() != dependency['sha256']:
                raise ValueError(f'{name}: stale extracted sources; use a fresh work directory')
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
        stamp.write_text(dependency['sha256'] + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--policy', type=Path, default=Path(__file__).resolve().parents[2] / 'packaging/release-policy.json')
    parser.add_argument('--cache', type=Path, required=True, help='Directory of downloaded archives')
    parser.add_argument('--destination', type=Path, required=True, help='Directory to unpack into')
    parser.add_argument('--fetch', action='store_true', help='Explicitly allow downloads of missing pinned archives')
    parser.add_argument('names', nargs='+', help='Dependency names from the policy, such as lv2 or audiounitsdk')
    args = parser.parse_args()
    policy = load_policy(args.policy)
    args.destination.mkdir(parents=True, exist_ok=True)
    unpack_dependencies(policy, args.cache.resolve(), args.destination.resolve(), args.fetch, set(args.names))
    for name in args.names:
        print(f"{name} {policy['dependencies'][name]['version']}: {args.destination / policy['dependencies'][name]['directory']}")
