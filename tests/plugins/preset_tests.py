# SPDX-License-Identifier: GPL-2.0-or-later
"""convolver-preset (PLUG-3): one filter, presets for VST3, AU and LV2.

Every preset is read back here without Convolver's code: the .vstpreset by its
documented layout, the .aupreset with plistlib, the LV2 bundle by its Turtle
(and with serdi, a real Turtle parser, when it is installed). All three must
carry the same encoded host state, and that state, decoded by the version-1
reader below, must hold the filter the .cfg describes.

Usage: preset_tests.py CONVOLVER_PRESET
"""

import base64
import json
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
REGISTRY = json.loads((REPOSITORY / 'plugins/identities.json').read_text(encoding='utf-8'))
CROSSOVER = REPOSITORY / 'examples/first-use/crossover'
TOOL = sys.argv[1]


def four_char(code):
    return int.from_bytes(code.encode('ascii'), 'big')


def run(*args, ok=True):
    result = subprocess.run([TOOL, *map(str, args)], capture_output=True, text=True, check=False)
    if ok and result.returncode != 0:
        raise AssertionError(f'{args}: {result.stderr}')
    if not ok and result.returncode == 0:
        raise AssertionError(f'{args} was accepted: {result.stdout}')
    return result


# --- Independent readers ------------------------------------------------------


def decode_state(data):
    """Host state as convolver-preset writes it (portable/state.cpp), as a
    dictionary: version 1, or version 5 with the preset's own name (PLUG-53)
    after the flags."""
    words = struct.unpack(f'<{len(data) // 4}I', data)
    assert len(data) % 4 == 0
    position = 0

    def word():
        nonlocal position
        position += 1
        return words[position - 1]

    def sample():
        return struct.unpack('<f', struct.pack('<I', word()))[0]

    assert word() == int.from_bytes(REGISTRY['host_state']['magic'].encode(), 'little')
    version = word()
    assert version in (1, 5), f'version 1 or 5, not {version}'
    flags = word()
    assert not flags & 8, 'convolver-preset writes no settings'
    state = {'bypass': bool(flags & 2), 'configuration': None, 'preset_name': ''}
    if version == 5:
        size = word()
        padded = (size + 3) // 4
        raw = struct.pack(f'<{padded}I', *(word() for _ in range(padded)))
        assert raw[size:] == b'\0' * (len(raw) - size), 'zero padding'
        state['preset_name'] = raw[:size].decode('utf-8')
        assert state['preset_name'], 'version 5 holds a name'
    if flags & 1:
        c = {'rate': word(), 'inputs': word(), 'outputs': word(), 'mask': word()}
        c['input_delays'] = [word() for _ in range(word())]
        c['output_delays'] = [word() for _ in range(word())]
        c['paths'] = []
        for _ in range(word()):
            impulse = [sample() for _ in range(word())]
            routes = [[(word(), sample()) for _ in range(word())] for _ in range(2)]
            c['paths'].append({'impulse': impulse, 'inputs': routes[0], 'outputs': routes[1]})
        state['configuration'] = c
    assert position == len(words), 'no trailing words'
    return state


def vstpreset_chunks(path):
    """The chunks of a .vstpreset as convolver-preset writes it: 'Comp', then
    'Cont' (PLUG-31) when it names the filter, back to back after the header."""
    data = path.read_bytes()
    magic, version, class_id, list_offset = struct.unpack_from('<4si32sq', data)
    assert magic == b'VST3' and version == 1
    assert class_id.decode() == REGISTRY['vst3']['processor_class_id']
    assert data[list_offset : list_offset + 4] == b'List'
    (count,) = struct.unpack_from('<i', data, list_offset + 4)
    assert count in (1, 2) and len(data) == list_offset + 8 + 20 * count
    chunks = {}
    end = 48
    for i in range(count):
        chunk, offset, size = struct.unpack_from('<4sqq', data, list_offset + 8 + 20 * i)
        assert chunk == (b'Comp', b'Cont')[i] and offset == end, (chunk, offset)
        end = offset + size
        chunks[chunk] = data[offset:end]
    assert end == list_offset
    return chunks


def read_vstpreset(path):
    comp = vstpreset_chunks(path)[b'Comp']
    (length,) = struct.unpack_from('<I', comp)
    assert length == len(comp) - 4
    return comp[4:]


def vstpreset_filter_name(path):
    """The filter's name in the controller state (PLUG-22's "CVCS" version 1:
    the name, then the filter's identity, each with a 32-bit length), or None."""
    cont = vstpreset_chunks(path).get(b'Cont')
    if cont is None:
        return None
    assert cont[:4] == b'CVCS' and struct.unpack_from('<I', cont, 4) == (1,)
    (name_size,) = struct.unpack_from('<I', cont, 8)
    name = cont[12 : 12 + name_size].decode('utf-8')
    (identity_size,) = struct.unpack_from('<I', cont, 12 + name_size)
    identity = cont[16 + name_size :].decode('ascii')
    assert len(identity) == identity_size == 16 and re.fullmatch('[0-9a-f]{16}', identity), identity
    return name


def read_aupreset(path, name, filter_name=None):
    with path.open('rb') as handle:
        plist = plistlib.load(handle)
    au = REGISTRY['au']
    assert plist['version'] == 0
    assert plist['type'] == four_char(au['type'])
    assert plist['subtype'] == four_char(au['subtype'])
    assert plist['manufacturer'] == four_char(au['manufacturer'])
    assert plist['name'] == name, plist['name']
    assert plist['data'] == b'', 'AUBase parameter data: empty, so settings keep their values'
    keys = {'version', 'type', 'subtype', 'manufacturer', 'name', 'data', 'convolver-state'}
    # PLUG-53: the filter file's name beside the preset's own.
    if filter_name is not None:
        keys.add('convolver-filter-name')
        assert plist['convolver-filter-name'] == filter_name, plist['convolver-filter-name']
    assert set(plist) == keys, sorted(plist)
    return plist['convolver-state']


def turtle_label(text):
    match = re.search(r'rdfs:label "((?:[^"\\]|\\.)*)" ;', text)
    return re.sub(r'\\(.)', r'\1', match.group(1))


def read_lv2(bundle, name, layout):
    base = REGISTRY['lv2']['base_uri']
    plugin = base + layout
    assert sorted(p.name for p in bundle.iterdir()) == ['manifest.ttl', 'preset.ttl']
    manifest = (bundle / 'manifest.ttl').read_text(encoding='utf-8')
    preset = (bundle / 'preset.ttl').read_text(encoding='utf-8')
    assert re.search(
        r'<preset\.ttl>\s+a pset:Preset ;\s+lv2:appliesTo <'
        + re.escape(plugin)
        + r'> ;\s+rdfs:seeAlso <preset\.ttl> \.',
        manifest,
    ), manifest
    assert f'lv2:appliesTo <{plugin}> ;' in preset
    assert turtle_label(preset) == name
    match = re.search(
        r'state:state \[\s+<' + re.escape(base + 'state') + r'> "([A-Za-z0-9+/=]*)"\^\^xsd:base64Binary\s+\] \.', preset
    )
    assert match, preset
    if shutil.which('serdi'):
        # A real Turtle parser: both files parse, and the triples say what we expect.
        for file in ('manifest.ttl', 'preset.ttl'):
            subprocess.run(
                ['serdi', '-q', '-i', 'turtle', '-o', 'ntriples', str(bundle / file)], check=True, capture_output=True
            )
        triples = subprocess.run(
            ['serdi', '-i', 'turtle', '-o', 'ntriples', str(bundle / 'preset.ttl')],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        assert f'<http://lv2plug.in/ns/lv2core#appliesTo> <{plugin}>' in triples
        assert f'<{base}state> "{match.group(1)}"^^<http://www.w3.org/2001/XMLSchema#base64Binary>' in triples
    return base64.b64decode(match.group(1), validate=True)


# --- Fixtures -------------------------------------------------------------------


def write_pcm16(path, rate, channels):
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(len(channels))
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(b''.join(struct.pack(f'<{len(channels)}h', *frame) for frame in zip(*channels)))


def read_float_wav(path):
    """The DOC-1 fixtures: canonical 32-bit float WAV, planar result."""
    data = path.read_bytes()
    channels = struct.unpack_from('<H', data, 22)[0]
    at = data.index(b'data') + 8
    samples = struct.unpack_from(f'<{(len(data) - at) // 4}f', data, at)
    return [list(samples[c::channels]) for c in range(channels)]


def check_crossover(state):
    """The 2→6 crossover fixture: one input forked to three outputs per side."""
    c = state['configuration']
    assert (c['rate'], c['inputs'], c['outputs'], c['mask']) == (48000, 2, 6, 0)
    assert c['input_delays'] == [0, 0] and c['output_delays'] == [0, 0, 24, 0, 0, 24]
    responses = read_float_wav(CROSSOVER / 'crossover.wav')
    assert len(c['paths']) == 6
    for band, path in enumerate(c['paths']):
        expected = responses[band]
        while expected and expected[-1] == 0:
            expected = expected[:-1]
        assert path['impulse'][: len(expected)] == expected, f'path {band} response'
        assert not any(path['impulse'][len(expected) :])
        assert path['inputs'] == [(band // 3, 1.0)], path['inputs']
        assert path['outputs'] == [(band, 0.5 if band in (2, 5) else 1.0)], path['outputs']


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        assert run('--help').stdout.startswith('Usage: convolver-preset')

        # 2→6 crossover: every format, from one command.
        names = ['Crossover.vstpreset', 'Crossover.aupreset', 'Crossover.preset.lv2']
        out = run(CROSSOVER / 'filter.cfg', *(root / n for n in names)).stdout
        assert out.count('Created self-contained') == 3, out
        assert REGISTRY['lv2']['base_uri'] + 'discrete-2-6' in out, out
        states = [
            read_vstpreset(root / names[0]),
            read_aupreset(root / names[1], 'Crossover', 'filter.cfg'),
            read_lv2(root / names[2], 'Crossover', 'discrete-2-6'),
        ]
        assert states[0] == states[1] == states[2], 'one encoded state in every format'
        check_crossover(decode_state(states[0]))
        # PLUG-53: the preset's own name in the state, the same in every format.
        assert decode_state(states[0])['preset_name'] == 'Crossover'

        # PLUG-31: the .vstpreset names the filter file, with its extension.
        assert vstpreset_filter_name(root / names[0]) == 'filter.cfg'
        print('Crossover (2->6) as VST3, AU and LV2 presets with identical state')

        # Converting a .vstpreset keeps the state byte for byte, after the
        # original files are gone.
        work = root / 'work'
        work.mkdir()
        write_pcm16(work / 'ir ü.wav', 44100, [[16384, -8192, 4096], [0, 32767, -32768]])
        (work / 'mono.cfg').write_text('44100 1 2 0\n0\n0 0\nir ü.wav\n1\n0\n0.25 1\n', encoding='utf-8')
        run('--name', 'Raum «ü» <A&B> "q" \\', work / 'mono.cfg', root / 'Fork.vstpreset')
        # A raw filter is embedded as f32le, and the tool says so.
        (work / 'drc.pcm').write_bytes(b'\0\0\0\x3f')
        (work / 'raw.cfg').write_text('44100 1 1 0\n0\n0\ndrc.pcm\n0\n0\n0\n')
        note = run(work / 'raw.cfg', root / 'Raw.vstpreset').stderr
        assert "raw filter 'drc.pcm' read as mono f32le at 44100 Hz" in note, note
        assert vstpreset_filter_name(root / 'Raw.vstpreset') == 'raw.cfg'
        # The filter file's name, not --name (the AU and LV2 preset name).
        assert vstpreset_filter_name(root / 'Fork.vstpreset') == 'mono.cfg'
        shutil.rmtree(work)
        run(root / 'Fork.vstpreset', root / 'Fork.aupreset', root / 'Fork.preset.lv2', root / 'Fork 2.vstpreset')
        # A converted .vstpreset keeps the filter and its file's name; its own
        # name (PLUG-53) is the conversion's, as for its AU and LV2 presets.
        original, converted = (decode_state(read_vstpreset(root / n)) for n in ('Fork.vstpreset', 'Fork 2.vstpreset'))
        assert original['preset_name'] == 'Raum «ü» <A&B> "q" \\' and converted['preset_name'] == 'Fork'
        assert {**original, 'preset_name': ''} == {**converted, 'preset_name': ''}
        assert vstpreset_filter_name(root / 'Fork 2.vstpreset') == 'mono.cfg'
        # A name saved for another filter is not carried over.
        data = bytearray((root / 'Fork.vstpreset').read_bytes())
        (list_offset,) = struct.unpack_from('<q', data, 40)
        data[list_offset - 16 : list_offset] = b'0' * 16
        (root / 'Other.vstpreset').write_bytes(bytes(data))
        run(root / 'Other.vstpreset', root / 'Other 2.vstpreset')
        assert vstpreset_filter_name(root / 'Other 2.vstpreset') is None
        assert decode_state(read_vstpreset(root / 'Other 2.vstpreset')) == {**original, 'preset_name': 'Other 2'}
        # One command writes one state, its own name included, in every format.
        fork = read_vstpreset(root / 'Fork 2.vstpreset')
        assert read_aupreset(root / 'Fork.aupreset', 'Fork', 'mono.cfg') == fork
        assert read_lv2(root / 'Fork.preset.lv2', 'Fork', 'discrete-1-2') == fork
        c = decode_state(fork)['configuration']
        # One input forked to two outputs: route 0.25 is output 0 at gain 0.25.
        assert (c['rate'], c['inputs'], c['outputs']) == (44100, 1, 2)
        assert c['paths'] == [
            {'impulse': [0.0, 32767 / 32768, -1.0], 'inputs': [(0, 1.0)], 'outputs': [(0, 0.25), (1, 1.0)]}
        ], c['paths']
        print('Converted a .vstpreset to AU and LV2 without the original files')

        # Names: the default, and any text, escaped in XML and Turtle.
        name = 'Raum «ü» <A&B> "q" \\'
        run('--name', name, CROSSOVER / 'filter.cfg', root / 'n.aupreset', root / 'n.preset.lv2')
        assert decode_state(read_aupreset(root / 'n.aupreset', name, 'filter.cfg'))['preset_name'] == name
        read_lv2(root / 'n.preset.lv2', name, 'discrete-2-6')

        # Refusals leave nothing behind, and explain themselves.
        (root / 'three.cfg').write_text(
            '48000 3 3 0\n0 0 0\n0 0 0\n'
            + ''.join(f'{CROSSOVER / "crossover.wav"}\n{i}\n{i}\n{i}\n' for i in range(3)),
            encoding='utf-8',
        )
        before = sorted(p.name for p in root.iterdir())
        result = run(root / 'three.cfg', root / 'three.vstpreset', root / 'three.preset.lv2', ok=False)
        assert '2->6' in result.stderr and '3 inputs' in result.stderr, result.stderr
        result = run(CROSSOVER / 'filter.cfg', root / 'a.vstpreset', root / names[1], ok=False)
        assert 'already exists' in result.stderr, result.stderr
        # PERF-37: an existing output is refused before the filter is read,
        # so a filter that would fail to load is never opened.
        result = run(root / 'missing.cfg', root / names[1], ok=False)
        assert 'already exists' in result.stderr and 'missing.cfg' not in result.stderr, result.stderr
        result = run(CROSSOVER / 'filter.cfg', root / 'filter.txt', ok=False)
        assert '.aupreset' in result.stderr, result.stderr
        result = run('--sample-rate', 48000, root / names[0], root / 'b.aupreset', ok=False)
        assert 'choose an entry from' in result.stderr and 'configuration list' in result.stderr, result.stderr
        result = run('--name', 'a\tb', CROSSOVER / 'filter.cfg', root / 'c.aupreset', ok=False)
        assert 'control characters' in result.stderr, result.stderr
        foreign = bytearray((root / names[0]).read_bytes())
        foreign[8:40] = b'0123456789ABCDEF0123456789ABCDEF'
        (root / 'foreign.vstpreset').write_bytes(bytes(foreign))
        result = run(root / 'foreign.vstpreset', root / 'd.aupreset', ok=False)
        assert 'another plug-in' in result.stderr, result.stderr
        (root / 'foreign.vstpreset').unlink()
        result = run('--name', 'a\ufffeb', CROSSOVER / 'filter.cfg', root / 'e.aupreset', ok=False)
        assert '--name' in result.stderr, result.stderr
        # PLUG-54 (jrp's review of 7bd024f): a --name the filter's state
        # cannot hold is refused for every format, a .vstpreset included,
        # instead of a preset written without it.
        result = run('--name', 'x' * 201, CROSSOVER / 'filter.cfg', root / 'long.vstpreset', ok=False)
        assert '201 bytes long; a preset keeps at most 200' in result.stderr, result.stderr
        assert 'shorter name with --name' in result.stderr, result.stderr
        result = run('--name', 'a\tb', CROSSOVER / 'filter.cfg', root / 'tab.vstpreset', ok=False)
        assert 'given with --name contains control characters' in result.stderr, result.stderr
        # AU and LV2 too (0.18.29 wrote these), and 201 bytes in letters of
        # more than one byte.
        for name in ('x' * 201, 'x' * 199 + 'ü', 'x' * 197 + '😀'):
            result = run('--name', name, CROSSOVER / 'filter.cfg', root / 'l.aupreset', root / 'l.preset.lv2', ok=False)
            assert '201 bytes long' in result.stderr, (name, result.stderr)
        if sys.platform.startswith('linux'):
            # Text that is not UTF-8, as bytes a POSIX shell can pass.
            done = subprocess.run(
                [TOOL, b'--name', b'ab\xffcd', str(CROSSOVER / 'filter.cfg').encode(), bytes(root / 'u.vstpreset')],
                capture_output=True,
                check=False,
            )
            assert done.returncode != 0 and b'given with --name cannot be stored' in done.stderr, done.stderr
        # A failure part-way (a missing folder) removes what the run wrote,
        # names the output, and reports nothing as created.
        result = run(CROSSOVER / 'filter.cfg', root / 'f.vstpreset', root / 'nodir' / 'f.aupreset', ok=False)
        assert 'Created' not in result.stdout, result.stdout
        assert 'f.aupreset' in result.stderr and 'No presets were kept' in result.stderr, result.stderr
        assert sorted(p.name for p in root.iterdir()) == before, 'refusals wrote nothing'
        # 200 bytes, in two-byte letters, are kept in every format.
        longest = 'ü' * 100
        run('--name', longest, CROSSOVER / 'filter.cfg', root / 'longest.vstpreset', root / 'longest.aupreset')
        assert decode_state(read_vstpreset(root / 'longest.vstpreset'))['preset_name'] == longest
        assert decode_state(read_aupreset(root / 'longest.aupreset', longest, 'filter.cfg'))['preset_name'] == longest
        for path in (root / 'longest.vstpreset', root / 'longest.aupreset'):
            path.unlink()
        # A file name too long to keep as the name still gives a preset,
        # without the name (the documented fallback). 202 bytes in 101
        # letters, so the path stays inside Windows' 260-character limit.
        long_file = root / ('ü' * 101 + '.vstpreset')
        run(CROSSOVER / 'filter.cfg', long_file)
        assert not decode_state(read_vstpreset(long_file)).get('preset_name'), 'a 202-byte file name was kept'
        long_file.unlink()
        message_tests(root)
        long_path_tests(root)
        interrupted_write_tests(root)
        # A .vstpreset stores no name, so its file name may be anything the
        # file system allows; an AU or LV2 name comes from the first of those.
        if sys.platform.startswith('linux'):
            target = bytes(root) + b'/R\xe4um\tx.vstpreset'
            done = subprocess.run(
                [TOOL, str(CROSSOVER / 'filter.cfg').encode(), target, bytes(root / 'Named.aupreset')],
                capture_output=True,
                check=False,
            )
            assert done.returncode == 0, done.stderr
            read_aupreset(root / 'Named.aupreset', 'Named', 'filter.cfg')
            Path(target.decode('utf-8', 'surrogateescape')).unlink()
            (root / 'Named.aupreset').unlink()
        run(root / 'three.cfg', root / 'three.vstpreset', root / 'three.aupreset')
        print('Refusals: LV2 layouts, existing paths, unknown formats, names, other plug-ins')


def interrupted_write_tests(root):
    """IO-15: a preset whose writing stops part-way (here a file-size limit
    lets the .vstpreset finish and cuts the .aupreset short) leaves nothing
    behind, so the same command works on a retry, and the run says that
    nothing was kept only when nothing was."""
    if sys.platform == 'win32':
        return
    import resource  # POSIX only
    import signal

    sizes = {}
    for name in ('size.vstpreset', 'size.aupreset'):
        run(CROSSOVER / 'filter.cfg', root / name)
        sizes[name] = (root / name).stat().st_size
        (root / name).unlink()
    limit = sizes['size.vstpreset']
    assert sizes['size.aupreset'] > limit, sizes

    def limited():
        # Ignored, so that writing past the limit fails with EFBIG instead
        # of ending the process; an ignored signal stays ignored after exec.
        signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
        resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))

    outputs = [root / 'cut.vstpreset', root / 'cut.aupreset']
    command = [TOOL, str(CROSSOVER / 'filter.cfg'), *map(str, outputs)]
    result = subprocess.run(command, capture_output=True, text=True, check=False, preexec_fn=limited)
    assert result.returncode != 0, 'a preset past the file-size limit was accepted'
    assert 'cut.aupreset' in result.stderr and 'the incomplete file was removed' in result.stderr, result.stderr
    assert 'No presets were kept from this run.' in result.stderr, result.stderr
    assert 'the disk may be full' in result.stderr, result.stderr
    assert 'Created' not in result.stdout, result.stdout
    assert not [p for p in outputs if p.exists()], 'an interrupted run left a preset behind'
    # The same command, without the limit, now works.
    run(CROSSOVER / 'filter.cfg', *outputs)
    for path in outputs:
        path.unlink()
    print('Interrupted writing: nothing left behind, and a retry works')


def long_path_tests(root):
    """IO-13: on Windows, a preset path over its length limit is refused with advice that names
    its length rather than the folder's permissions (the tools are not long-path aware), and
    nothing is left behind."""
    if sys.platform != 'win32':
        return
    # Over the limits whatever the temporary folder, and under NTFS's 255 for the name.
    stem = 'p' * max(240, 270 - len(str(root)))
    for name in (stem + '.vstpreset', stem + '.preset.lv2'):
        target = root / name
        extended = Path('\\\\?\\' + str(target))  # Reachable whatever its length.
        try:
            result = subprocess.run(
                [TOOL, str(CROSSOVER / 'filter.cfg'), str(target)], capture_output=True, text=True, check=False
            )
            assert result.returncode != 0, f'{name}: a path over the limit was written'
            assert f'Its full path is {len(str(target))} characters long' in result.stderr, result.stderr
            assert 'shorter' in result.stderr and 'writable' not in result.stderr, result.stderr
            assert not extended.exists(), 'a refused preset was left behind'
        finally:
            if extended.is_dir():
                shutil.rmtree(extended)
            elif extended.exists():
                extended.unlink()
    print('Paths too long for Windows: refused with their length')


def message_tests(root):
    """DIAG-1: refusals name the program, the file or value, and what to do instead."""
    cfg = CROSSOVER / 'filter.cfg'
    junk = root / 'junk.vstpreset'
    junk.write_bytes(b'not a preset at all')
    nine = root / 'nine.wav'
    write_pcm16(nine, 48000, [[16384, 0]] * 9)
    cases = (
        ((junk, root / 'g.aupreset'), [str(junk), "no 'VST3' header", 'make the preset from the filter']),
        ((nine, root / 'g.aupreset'), ['9 inputs and 9 outputs', 'command-line tool, convolver']),
        (
            ('--sample-rate', 44100, cfg, root / 'g.vstpreset'),
            [
                'the requested selection: its sample rate is 48000 Hz, not 44100 Hz',
                'choose 48000 Hz with --sample-rate',
            ],
        ),
        ((cfg, root / 'g.vstpreset', '--sample-rate', 44100), ['the requested selection: its sample rate is 48000 Hz']),
        (
            ('--input-channels', 3, cfg, root / 'g.vstpreset'),
            ['--input-channels asked for 3 input channels', 'omit --input-channels'],
        ),
        ((cfg, root / 'g.vstpreset', '--sample-rate'), ['--sample-rate needs a value']),
        ((cfg, root / 'g.vstpreset', '--bogus'), ['error: invalid option: --bogus']),
        (('--bogus', 1, cfg, root / 'g.vstpreset'), ['error: invalid option: --bogus']),
        (
            ('--sample-rate', 'fast', cfg, root / 'g.vstpreset'),
            ['--sample-rate fast is not a whole number from 1 to 768000', 'such as --sample-rate 48000'],
        ),
    )
    for args, texts in cases:
        message = run(*args, ok=False).stderr
        assert message.startswith('convolver-preset: '), (args, message)
        for text in texts:
            assert text in message, (args, text, message)
    # PLUG-25's sentence about a host's export rate is for plug-in hosts only.
    message = run('--sample-rate', 44100, cfg, root / 'g.vstpreset', ok=False).stderr
    assert 'exports at' not in message, message
    # The CLI hint belongs to the channel limit only.
    message = run('--name', 'a\tb', nine, root / 'g.aupreset', ok=False).stderr
    assert 'control characters' in message and 'command-line tool' not in message, message
    # Help is shown wherever --help is written.
    assert run(cfg, root / 'g.vstpreset', '--help').stdout.startswith('Usage: convolver-preset')
    assert not (root / 'g.vstpreset').exists()
    # Options may follow the file names.
    run(cfg, root / 'g.vstpreset', '--sample-rate', 48000, '--input-channels', 2)
    same = decode_state(read_vstpreset(root / 'Crossover.vstpreset'))
    assert decode_state(read_vstpreset(root / 'g.vstpreset')) == {**same, 'preset_name': 'g'}
    for path in (junk, nine, root / 'g.vstpreset'):
        path.unlink()
    print('Messages: program name, file, value and next step; options after the file names')


if __name__ == '__main__':
    main()
