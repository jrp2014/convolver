# SPDX-License-Identifier: GPL-2.0-or-later
"""Assemble committed Convolver plus complete pinned dependency source archives."""
import argparse
import gzip
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
from contract import load_policy, sha256
from sources import acquire, extract


def assemble(policy_path, cache, output, fetch=False, revision='HEAD'):
    root = policy_path.resolve().parents[1]
    policy = load_policy(policy_path)
    commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', revision + '^{commit}'], text=True).strip()
    tracked_policy = subprocess.check_output(['git', '-C', str(root), 'show', commit + ':packaging/release-policy.json'])
    if json.loads(tracked_policy) != policy:
        raise ValueError('policy differs from the requested source commit; commit the policy first')
    if output.exists():
        raise ValueError('source archive output already exists')
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
            inventory[name] = dict(archive=target.name, sha256=sha256(target), license=dependency['license'], version=dependency['version'])
        manifest = dict(schema=1, source_commit=commit, version=policy['version'], dependencies=inventory,
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
    print(json.dumps(dict(archive=output.name, sha256=sha256(output), source_commit=commit)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policy', type=Path, default=Path(__file__).resolve().parents[2] / 'packaging/release-policy.json')
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fetch', action='store_true')
    parser.add_argument('--revision', default='HEAD')
    args = parser.parse_args()
    assemble(args.policy, args.cache.resolve(), args.output.resolve(), args.fetch, args.revision)
