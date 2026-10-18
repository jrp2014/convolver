#!/usr/bin/env python3
"""Regenerate the committed fuzz seeds from the compatibility corpus.

Usage: make_seeds.py [OUTPUT_DIRECTORY]   (default: tests/fuzz/seeds)
host_state seeds are produced by the C++ encoder and are kept as committed files.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'compatibility'))
import generate_fixtures as fixtures  # noqa: E402


def main(argv):
    out = Path(argv[1]) if len(argv) > 1 else HERE / 'seeds'
    manifest = fixtures.load_manifest()
    text_dir, list_dir, ir_dir = out / 'config_text', out / 'config_list', out / 'ir_file'
    for d in (text_dir, list_dir, ir_dir):
        d.mkdir(parents=True, exist_ok=True)
    for case in manifest['cases']:
        texts = []
        for name, spec in sorted(case['files'].items()):
            if 'text' in spec:
                data = spec['text'].replace('{root}', '/fuzz').encode('utf-8')
                if spec.get('crlf'):
                    data = data.replace(b'\n', b'\r\n')
                if spec.get('bom'):
                    data = b'\xef\xbb\xbf' + data
                texts.append((name, data))
                (text_dir / ('%s-%s' % (case['id'], name.replace('/', '_')))).write_bytes(data)
            elif 'rate' in spec:
                path = ir_dir / ('%s-%s' % (case['id'], name.replace('/', '_')))
                fixtures.write_wav(path, spec['rate'], spec['channels'], spec['format'])
        if texts:
            # The filter first (as f0), other texts after, references renamed to f<i>.
            texts.sort(key=lambda t: t[0] != case['filter'])
            names = {n: 'f%d' % i for i, (n, _) in enumerate(texts[:8])}
            blobs = []
            for _, data in texts[:8]:
                for original, short in names.items():
                    data = data.replace(original.encode('utf-8'), short.encode())
                blobs.append(data)
            (list_dir / case['id']).write_bytes(b'\0'.join(blobs))
    print('wrote seeds to', out)


if __name__ == '__main__':
    main(sys.argv)
