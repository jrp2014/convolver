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
from pathlib import Path
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

REPOSITORY = Path(__file__).resolve().parents[2]
REGISTRY = json.loads((REPOSITORY / 'plugins/identities.json').read_text(encoding='utf-8'))
CROSSOVER = REPOSITORY / 'examples/first-use/crossover'
TOOL = sys.argv[1]


def four_char(code):
    return int.from_bytes(code.encode('ascii'), 'big')


def run(*args, ok=True):
    result = subprocess.run([TOOL, *map(str, args)], capture_output=True, text=True)
    if ok and result.returncode != 0:
        raise AssertionError(f'{args}: {result.stderr}')
    if not ok and result.returncode == 0:
        raise AssertionError(f'{args} was accepted: {result.stdout}')
    return result


# --- Independent readers ------------------------------------------------------

def decode_state(data):
    """Version-1 host state (portable/state.cpp), as a dictionary."""
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
    assert word() == 1, 'version 1'
    flags = word()
    state = {'bypass': bool(flags & 2), 'configuration': None}
    if flags & 1:
        c = {'rate': word(), 'inputs': word(), 'outputs': word(), 'mask': word()}
        c['input_delays'] = [word() for _ in range(word())]
        c['output_delays'] = [word() for _ in range(word())]
        c['paths'] = []
        for _ in range(word()):
            impulse = [sample() for _ in range(word())]
            routes = []
            for _ in range(2):
                routes.append([(word(), sample()) for _ in range(word())])
            c['paths'].append({'impulse': impulse, 'inputs': routes[0], 'outputs': routes[1]})
        state['configuration'] = c
    assert position == len(words), 'no trailing words'
    return state


def read_vstpreset(path):
    data = path.read_bytes()
    magic, version, class_id, list_offset = struct.unpack_from('<4si32sq', data)
    assert magic == b'VST3' and version == 1
    assert class_id.decode() == REGISTRY['vst3']['processor_class_id']
    assert data[list_offset:list_offset + 4] == b'List'
    (count,) = struct.unpack_from('<i', data, list_offset + 4)
    assert count == 1 and len(data) == list_offset + 8 + 20 * count
    chunk, offset, size = struct.unpack_from('<4sqq', data, list_offset + 8)
    assert chunk == b'Comp' and offset == 48 and offset + size == list_offset
    (length,) = struct.unpack_from('<I', data, offset)
    assert length == size - 4
    return data[offset + 4:offset + 4 + length]


def read_aupreset(path, name):
    with path.open('rb') as handle:
        plist = plistlib.load(handle)
    au = REGISTRY['au']
    assert plist['version'] == 0
    assert plist['type'] == four_char(au['type'])
    assert plist['subtype'] == four_char(au['subtype'])
    assert plist['manufacturer'] == four_char(au['manufacturer'])
    assert plist['name'] == name, plist['name']
    assert plist['data'] == b'', 'AUBase parameter data: empty, the AU has no parameters'
    assert set(plist) == {'version', 'type', 'subtype', 'manufacturer', 'name', 'data',
                          'convolver-state'}, sorted(plist)
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
    assert re.search(r'<preset\.ttl>\s+a pset:Preset ;\s+lv2:appliesTo <' + re.escape(plugin) +
                     r'> ;\s+rdfs:seeAlso <preset\.ttl> \.', manifest), manifest
    assert f'lv2:appliesTo <{plugin}> ;' in preset
    assert turtle_label(preset) == name
    match = re.search(r'state:state \[\s+<' + re.escape(base + 'state') +
                      r'> "([A-Za-z0-9+/=]*)"\^\^xsd:base64Binary\s+\] \.', preset)
    assert match, preset
    if shutil.which('serdi'):
        # A real Turtle parser: both files parse, and the triples say what we expect.
        for file in ('manifest.ttl', 'preset.ttl'):
            subprocess.run(['serdi', '-q', '-i', 'turtle', '-o', 'ntriples',
                            str(bundle / file)], check=True, capture_output=True)
        triples = subprocess.run(['serdi', '-i', 'turtle', '-o', 'ntriples', str(bundle / 'preset.ttl')],
                                 check=True, capture_output=True, text=True).stdout
        assert f'<http://lv2plug.in/ns/lv2core#appliesTo> <{plugin}>' in triples
        assert f'<{base}state> "{match.group(1)}"^^<http://www.w3.org/2001/XMLSchema#base64Binary>' in triples
    return base64.b64decode(match.group(1), validate=True)


# --- Fixtures -------------------------------------------------------------------

def write_pcm16(path, rate, channels):
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(len(channels))
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(b''.join(struct.pack(f'<{len(channels)}h', *frame)
                                 for frame in zip(*channels)))


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
        assert path['impulse'][:len(expected)] == expected, f'path {band} response'
        assert not any(path['impulse'][len(expected):])
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
        states = [read_vstpreset(root / names[0]),
                  read_aupreset(root / names[1], 'Crossover'),
                  read_lv2(root / names[2], 'Crossover', 'discrete-2-6')]
        assert states[0] == states[1] == states[2], 'one encoded state in every format'
        check_crossover(decode_state(states[0]))
        print('Crossover (2->6) as VST3, AU and LV2 presets with identical state')

        # Converting a .vstpreset keeps the state byte for byte, after the
        # original files are gone.
        work = root / 'work'
        work.mkdir()
        write_pcm16(work / 'ir ü.wav', 44100, [[16384, -8192, 4096], [0, 32767, -32768]])
        (work / 'mono.cfg').write_text('44100 1 2 0\n0\n0 0\nir ü.wav\n1\n0\n0.25 1\n',
                                       encoding='utf-8')
        run('--name', 'Raum «ü» <A&B> "q" \\', work / 'mono.cfg', root / 'Fork.vstpreset')
        shutil.rmtree(work)
        run(root / 'Fork.vstpreset', root / 'Fork.aupreset', root / 'Fork.preset.lv2')
        fork = read_vstpreset(root / 'Fork.vstpreset')
        assert read_aupreset(root / 'Fork.aupreset', 'Fork') == fork
        assert read_lv2(root / 'Fork.preset.lv2', 'Fork', 'discrete-1-2') == fork
        c = decode_state(fork)['configuration']
        # One input forked to two outputs: route 0.25 is output 0 at gain 0.25.
        assert (c['rate'], c['inputs'], c['outputs']) == (44100, 1, 2)
        assert c['paths'] == [{'impulse': [0.0, 32767 / 32768, -1.0],
                               'inputs': [(0, 1.0)], 'outputs': [(0, 0.25), (1, 1.0)]}], c['paths']
        print('Converted a .vstpreset to AU and LV2 without the original files')

        # Names: the default, and any text, escaped in XML and Turtle.
        name = 'Raum «ü» <A&B> "q" \\'
        run('--name', name, CROSSOVER / 'filter.cfg', root / 'n.aupreset', root / 'n.preset.lv2')
        read_aupreset(root / 'n.aupreset', name)
        read_lv2(root / 'n.preset.lv2', name, 'discrete-2-6')

        # Refusals leave nothing behind, and explain themselves.
        (root / 'three.cfg').write_text('48000 3 3 0\n0 0 0\n0 0 0\n'
                                        + ''.join(f'{CROSSOVER / "crossover.wav"}\n{i}\n{i}\n{i}\n'
                                                  for i in range(3)), encoding='utf-8')
        before = sorted(p.name for p in root.iterdir())
        result = run(root / 'three.cfg', root / 'three.vstpreset', root / 'three.preset.lv2', ok=False)
        assert '2->6' in result.stderr and '3 inputs' in result.stderr, result.stderr
        result = run(CROSSOVER / 'filter.cfg', root / 'a.vstpreset', root / names[1], ok=False)
        assert 'already exists' in result.stderr, result.stderr
        result = run(CROSSOVER / 'filter.cfg', root / 'filter.txt', ok=False)
        assert '.aupreset' in result.stderr, result.stderr
        result = run('--sample-rate', 48000, root / names[0], root / 'b.aupreset', ok=False)
        assert 'configuration lists' in result.stderr, result.stderr
        result = run('--name', 'a\tb', CROSSOVER / 'filter.cfg', root / 'c.aupreset', ok=False)
        assert 'control characters' in result.stderr, result.stderr
        foreign = bytearray((root / names[0]).read_bytes())
        foreign[8:40] = b'0123456789ABCDEF0123456789ABCDEF'
        (root / 'foreign.vstpreset').write_bytes(bytes(foreign))
        result = run(root / 'foreign.vstpreset', root / 'd.aupreset', ok=False)
        assert 'another plug-in' in result.stderr, result.stderr
        (root / 'foreign.vstpreset').unlink()
        assert sorted(p.name for p in root.iterdir()) == before, 'refusals wrote nothing'
        run(root / 'three.cfg', root / 'three.vstpreset', root / 'three.aupreset')
        print('Refusals: LV2 layouts, existing paths, unknown formats, names, other plug-ins')


if __name__ == '__main__':
    main()
