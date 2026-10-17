#!/usr/bin/env python3
"""Check the preserved website offline; no homes.zip or third-party packages."""
from collections import Counter
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = Path('docs/historical/convolver-4.4')
MANIFEST = Path('docs/review-2026-10/archive-documentation.json')
AUDIT = Path('docs/review-2026-10/sourceforge-documentation-check.json')
PAGES = set('binaural commands comparewavs config configegs convolvercmd '
            'convolverpipe download feedback index links makeir news perftest '
            'troubleshooting usage vst zoom'.split())
PAGES = {name + '.html' for name in PAGES}
CSS_URL = re.compile(r'url\(\s*[\"\']?([^\)\"\']*)[\"\']?\s*\)', re.I)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def contained(base, path):
    """Resolve before reading, including symlinks, and reject escaping paths."""
    result = path.resolve()
    base = base.resolve()
    require(result == base or base in result.parents, 'path escapes archive: ' + str(path))
    return result


class Page(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.anchors = set()
        self.in_style = False

    def handle_starttag(self, tag, attrs):
        self.in_style = self.in_style or tag == 'style'
        for key, value in attrs:
            if value is None:
                continue
            if key in ('href', 'src', 'background', 'action'):
                self.links.append((key, value))
            if key == 'id' or (tag == 'a' and key == 'name'):
                self.anchors.add(value)
            if key == 'style':
                self.links.extend(('css-url', v.strip()) for v in CSS_URL.findall(value))

    def handle_endtag(self, tag):
        if tag == 'style':
            self.in_style = False

    def handle_data(self, data):
        if self.in_style:
            self.links.extend(('css-url', v.strip()) for v in CSS_URL.findall(data))


def check_files(root, entries):
    base = root / ARCHIVE
    paths = set()
    for entry in entries:
        relative = PurePosixPath(entry['path'])
        require(not relative.is_absolute() and '..' not in relative.parts,
                'unsafe manifest path: ' + str(relative))
        path = contained(base, root / relative)
        require(path not in paths, 'duplicate manifest path: ' + str(relative))
        paths.add(path)
        require(path.is_file(), 'missing archived file: ' + str(relative))
        data = path.read_bytes()
        require(len(data) == entry['bytes'], 'length mismatch: ' + str(relative))
        require(hashlib.sha256(data).hexdigest() == entry['sha256'],
                'SHA256 mismatch: ' + str(relative))
    return paths


def check_links(base, paths):
    base = base.resolve()
    pages = {}
    links = []
    for path in sorted(paths):
        if path.suffix.lower() not in ('.html', '.css'):
            continue
        data = path.read_bytes()
        try:
            text = data.decode('utf-8')
        except UnicodeDecodeError:
            text = data.decode('cp1252')
        if path.suffix.lower() == '.html':
            page = Page()
            page.feed(text)
            pages[path] = page
            references = page.links
        else:
            references = [('css-url', v.strip()) for v in CSS_URL.findall(text)]
        links.extend((path, attr, url) for attr, url in references)
    counts = Counter()
    for source, attribute, url in links:
        parsed = urlsplit(url.strip())
        if parsed.scheme or parsed.netloc:
            counts['external'] += 1
            continue
        # Root-relative website paths are relative to the archived site root.
        name = unquote(parsed.path)
        target = (base / name.lstrip('/') if name.startswith('/')
                  else source.parent / name) if name else source
        target = contained(base, target)
        require(target in paths and target.is_file(),
                'missing local target: %s -> %s' % (source.name, url))
        counts['local'] += 1
        if parsed.fragment and target.suffix.lower() == '.html':
            require(unquote(parsed.fragment) in pages[target].anchors,
                    'missing fragment: %s -> %s' % (source.name, url))
            counts['fragments'] += 1
    inventory = Counter((str(path.relative_to(base)), attr, url)
                        for path, attr, url in links)
    return counts, inventory


def check_archive(root):
    manifest = json.loads((root / MANIFEST).read_text())
    require(len(manifest['files']) == 99, 'expected 99 preserved files')
    paths = check_files(root, manifest['files'])
    base = (root / ARCHIVE).resolve()
    require({str(p.relative_to(base)) for p in paths if p.suffix == '.html'} == PAGES,
            'original 18-page inventory changed')
    # README.md is the modern provenance note, not an archived website file.
    actual = {contained(base, p) for p in base.rglob('*')
              if p.is_file() and p != base / 'README.md'}
    require(actual == paths, 'archive contains files absent from the manifest')
    counts, inventory = check_links(base, paths)
    recorded = Counter((link['from'], link['attribute'], link['url'])
                       for link in manifest['links'])
    require(inventory == recorded, 'parsed reference inventory differs from manifest')
    # This checks the saved audit against the preserved pages, not the live site.
    audit = json.loads((root / AUDIT).read_text())
    require(len(audit['pages']) == 18 and {p['page'] for p in audit['pages']} == PAGES,
            'saved SourceForge audit page inventory changed')
    for page in audit['pages']:
        require(hashlib.sha256((base / page['page']).read_bytes()).hexdigest()
                == page['archive_sha256'], 'saved audit hash mismatch: ' + page['page'])
    return counts


def check_failure_detection():
    """Exercise failures without editing any preserved files."""
    with tempfile.TemporaryDirectory(prefix='convolver-docs-') as temp:
        root = Path(temp)
        base = root / ARCHIVE
        base.mkdir(parents=True)
        fixtures = {'index.html': b'<a href="other.html#anchor">link</a><link href="a.css">',
                    'other.html': b'<a name="anchor"></a><img src="image.gif">',
                    'a.css': b'body {background: url("image.gif")}',
                    'image.gif': b'GIF89a'}
        entries = []
        for name, data in fixtures.items():
            (base / name).write_bytes(data)
            entries.append({'path': str(ARCHIVE / name), 'bytes': len(data),
                            'sha256': hashlib.sha256(data).hexdigest()})
        paths = check_files(root, entries)
        check_links(base, paths)

        def fails(action, expected):
            try:
                action()
            except ValueError as error:
                require(expected in str(error), 'unexpected failure: ' + str(error))
            else:
                raise ValueError('checker accepted ' + expected)

        image = base / 'image.gif'
        image.write_bytes(b'GIF89b')  # Same length: must exercise the digest check.
        fails(lambda: check_files(root, entries), 'SHA256 mismatch')
        image.write_bytes(b'GIF89ab')
        fails(lambda: check_files(root, entries), 'length mismatch')
        image.unlink()
        fails(lambda: check_files(root, entries), 'missing archived file')
        fails(lambda: check_links(base, paths), 'missing local target')
        image.write_bytes(fixtures['image.gif'])
        (base / 'other.html').write_bytes(b'<a name="different"></a>')
        fails(lambda: check_links(base, paths), 'missing fragment')
        unsafe = dict(entries[0], path=str(ARCHIVE / '..' / 'outside'))
        fails(lambda: check_files(root, [unsafe]), 'unsafe manifest path')
        (base / 'index.html').write_bytes(b'<a href="../outside">escape</a>')
        fails(lambda: check_links(base, paths), 'path escapes archive')
    return 7


def main():
    try:
        negative_checks = check_failure_detection()
        counts = check_archive(ROOT)
    except (ValueError, OSError, KeyError, TypeError) as error:
        print('FAIL: documentation: ' + str(error), file=sys.stderr)
        return 1
    print('PASS: documentation: 99 file lengths/SHA256, 18 pages, '
          '%d local references, %d fragments; %d external references skipped; '
          '%d failure-detection checks' %
          (counts['local'], counts['fragments'], counts['external'], negative_checks))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
