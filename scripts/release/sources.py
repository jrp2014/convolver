# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit hash-verified source acquisition and safe archive extraction."""
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import tempfile
import urllib.request
from contract import sha256


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


def extract(archive, destination):
    """Pinned source archives contain regular files/directories only."""
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        members = tar.getmembers()
        for member in members:
            path = PurePosixPath(member.name)
            if (path.is_absolute() or '..' in path.parts or '\\' in member.name or
                    ':' in member.name or not (member.isfile() or member.isdir())):
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
                target.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(member) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output)
                target.chmod(member.mode & 0o777)


def unpack_dependencies(policy, cache, destination, fetch=False):
    # Parent SDK before its submodules; never trust an old extracted tree.
    ordered = sorted(policy['dependencies'].items(), key=lambda item: item[1]['directory'].count('/'))
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
