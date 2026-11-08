# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise generated presets through the real SDK adapter, with relocated assets."""

import pathlib
import struct
import subprocess
import sys
import tempfile
import wave

# The tools, from CMake: convolver-vst3-preset, the adapter tests, and
# convolver-preset when it is built.
SHARED = sys.argv[3] if len(sys.argv) > 3 else None


def chunks(path):
    """A .vstpreset's chunk IDs and contents, in list order."""
    data = path.read_bytes()
    (list_offset,) = struct.unpack_from('<q', data, 40)
    assert data[list_offset : list_offset + 4] == b'List'
    (count,) = struct.unpack_from('<i', data, list_offset + 4)
    found = []
    for i in range(count):
        chunk, offset, size = struct.unpack_from('<4sqq', data, list_offset + 8 + 20 * i)
        found.append((chunk, data[offset : offset + size]))
    return found


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    impulse = root / 'impulse ü.wav'
    with wave.open(str(impulse), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(48000)
        wav.writeframes(struct.pack('<hh', 16384, 8192))
    preset = root / 'filter ü.vstpreset'
    subprocess.run([sys.argv[1], str(impulse), str(preset)], check=True)
    original = preset.read_bytes()
    result = subprocess.run([sys.argv[1], str(impulse), str(preset)], capture_output=True, check=False)
    assert result.returncode != 0 and preset.read_bytes() == original
    # PLUG-31: convolver-preset names the IR file, with its extension; the
    # old tool keeps its original bytes, without a name.
    assert [chunk for chunk, _ in chunks(preset)] == [b'Comp']
    named = root / 'named ü.vstpreset'
    if SHARED:
        subprocess.run([SHARED, str(impulse), str(named)], check=True, capture_output=True)
    impulse.unlink()
    subprocess.run([sys.argv[2], str(preset), ''], check=True)
    if SHARED:
        subprocess.run([sys.argv[2], str(named), impulse.name], check=True)
    result = subprocess.run([sys.argv[1], str(root / 'missing.wav'), str(preset)], capture_output=True, check=False)
    assert result.returncode != 0 and preset.read_bytes() == original
    print('Preset embeds filters and survives removal of original IR')

    # Configuration lists select by rate and channel counts; layouts up to 8 channels.
    with wave.open(str(root / 'ir.wav'), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(48000)
        wav.writeframes(struct.pack('<h', 16384))
    (root / 'stereo.cfg').write_text('48000 2 2 0\n0 0\n0 0\nir.wav\n0\n0\n0 1\n')
    (root / 'upmix.cfg').write_text('48000 2 6 0\n0 0\n0 0 0 0 0 0\nir.wav\n0\n0\n0 1 2 3 4 5\n')
    nine = ' '.join(['0'] * 9)
    (root / 'nine.cfg').write_text(f'48000 9 9 0\n{nine}\n{nine}\nir.wav\n0\n0\n0\n')
    (root / 'list.txt').write_text('stereo.cfg\nupmix.cfg\n')
    tool = sys.argv[1]

    def make(name, *options):
        target = root / name
        return subprocess.run(
            [tool, *options, str(root / 'list.txt'), str(target)], capture_output=True, check=False
        ), target

    result, upmix = make('upmix.vstpreset', '--sample-rate', '48000', '--output-channels', '6')
    assert result.returncode == 0, result.stderr
    result, stereo = make('stereo.vstpreset', '--input-channels', '2')
    assert result.returncode == 0, result.stderr
    assert upmix.read_bytes() != stereo.read_bytes()
    for options in (
        ['--output-channels', '3'],
        ['--sample-rate', '44100'],
        ['--output-channels', '0'],
        ['--input-channels', 'x'],
        ['--frobnicate', '1'],
    ):
        result, target = make('rejected.vstpreset', *options)
        assert result.returncode != 0 and not target.exists(), options
    result = subprocess.run(
        [tool, str(root / 'nine.cfg'), str(root / 'nine.vstpreset')], capture_output=True, text=True, check=False
    )
    assert result.returncode != 0 and '1 to 8' in result.stderr, result.stderr
    print('Preset selection by channel counts and multichannel limits')

    # PLUG-3: convolver-vst3-preset is the shared tool under its old name, so
    # both write the same audio state for the same filter and selection; only
    # convolver-preset adds the filter's name (PLUG-31).
    if SHARED:
        shared = root / 'shared.vstpreset'
        subprocess.run(
            [SHARED, '--sample-rate', '48000', '--output-channels', '6', str(root / 'list.txt'), str(shared)],
            check=True,
            capture_output=True,
        )
        old, new = chunks(upmix), chunks(shared)
        assert [chunk for chunk, _ in old] == [b'Comp'] and [chunk for chunk, _ in new] == [b'Comp', b'Cont']
        assert old[0] == new[0] and b'list.txt' in new[1][1]
        print('convolver-preset writes the same audio state as convolver-vst3-preset, and a name')
