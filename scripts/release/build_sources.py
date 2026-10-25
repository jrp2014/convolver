# SPDX-License-Identifier: GPL-2.0-or-later
"""Assemble committed Convolver plus the complete source archives of its dependencies.

Each dependency is resolved to its newest release (resolve.py), or taken from
--lock (a lock file or an earlier source manifest) to assemble the same
sources again. source-manifest.json records exactly what went in: version,
tag, commit, URL and checksum of each archive."""
import argparse
import gzip
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
from contract import load_policy, sha256, with_records
import resolve
from sources import acquire, extract


def assemble(policy_path, cache, output, fetch=False, revision='HEAD', lock=None, wanted=None, resolver=None):
    root = policy_path.resolve().parents[1]
    policy = load_policy(policy_path)
    commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', revision + '^{commit}'], text=True).strip()
    tracked_policy = subprocess.check_output(['git', '-C', str(root), 'show', commit + ':packaging/release-policy.json'])
    committed_policy = json.loads(tracked_policy)
    committed_policy['version'] = subprocess.check_output(
        ['git', '-C', str(root), 'show', commit + ':VERSION'], text=True).strip()
    if committed_policy != policy:
        raise ValueError('policy differs from the requested source commit; commit the policy first')
    if output.exists():
        raise ValueError('source archive output already exists')
    if lock:
        if wanted:
            raise ValueError('--version chooses releases to resolve; with --lock the records decide')
        records = resolve.load_records(lock)
        resolved_at = json.loads(Path(lock).read_text(encoding='utf-8')).get('resolved_at', 'not recorded')
    else:
        if not fetch:
            raise ValueError('resolving the newest releases downloads them; use --fetch, or --lock')
        records = (resolver or resolve.resolve)(policy, cache, wanted=wanted)
        resolved_at = resolve.lock(records)['resolved_at']
    policy = with_records(policy, records)
    with tempfile.TemporaryDirectory(prefix='convolver-source-') as temporary:
        work = Path(temporary)
        source = work / 'Convolver-source'
        source.mkdir()
        snapshot = work / 'snapshot.tar'
        with snapshot.open('wb') as stream:
            subprocess.run(['git', '-C', str(root), 'archive', '--format=tar', commit], stdout=stream, check=True)
        extract(snapshot, source / 'convolver')
        (source / 'convolver/SOURCE_COMMIT').write_text(commit + '\n')
        archives = source / 'dependencies'
        archives.mkdir()
        inventory = {}
        for name, dependency in policy['dependencies'].items():
            archive = acquire(dependency, cache, fetch)
            target = archives / dependency['archive']
            target.write_bytes(archive.read_bytes())
            if sha256(target) != dependency['sha256']:
                raise ValueError(f'{name}: archive changed while being copied')
            inventory[name] = dependency
        manifest = dict(schema=2, source_commit=commit, version=policy['version'], resolved_at=resolved_at,
                        dependencies=inventory,
                        files={p.relative_to(source).as_posix(): sha256(p) for p in sorted(source.rglob('*')) if p.is_file()})
        (source / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open('xb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', mtime=0, filename='') as compressed, tarfile.open(fileobj=compressed, mode='w') as tar:
            for path in sorted(source.rglob('*')):
                info = tar.gettarinfo(str(path), arcname='Convolver-source/' + path.relative_to(source).as_posix())
                info.uid = info.gid = info.mtime = 0
                info.uname = info.gname = ''
                if path.is_file():
                    with path.open('rb') as stream:
                        tar.addfile(info, stream)
                else:
                    tar.addfile(info)
    resolve.report(resolve.markdown(policy['dependencies'], 'Dependencies in the corresponding source'))
    print(json.dumps(dict(archive=output.name, sha256=sha256(output), source_commit=commit)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policy', type=Path, default=Path(__file__).resolve().parents[2] / 'packaging/release-policy.json')
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fetch', action='store_true', help='Explicitly allow downloads')
    parser.add_argument('--revision', default='HEAD')
    which = parser.add_mutually_exclusive_group(required=True)
    which.add_argument('--latest', action='store_true', help='Resolve each dependency to its newest release')
    which.add_argument('--lock', type=Path, help='A lock file or source manifest to assemble exactly')
    parser.add_argument('--version', action='append', metavar='NAME=VERSION',
                        help='With --latest: use this release instead of the newest (repeatable)')
    args = parser.parse_args()
    try:
        assemble(args.policy, args.cache.resolve(), args.output.resolve(), args.fetch, args.revision, args.lock,
                 resolve.parse_wanted(args.version))
    except ValueError as error:
        sys.exit(f'error: {error}')
