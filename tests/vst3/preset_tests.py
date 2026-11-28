# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise generated presets through the real SDK adapter, with relocated assets."""

import pathlib
import struct
import subprocess
import sys
import tempfile
import wave

# The tools, from CMake: convolver-preset and the adapter tests.
TOOL, ADAPTER = sys.argv[1], sys.argv[2]


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


def without_name(chunk):
    """A component chunk (the host state's size, then the state) with the
    preset's own name taken out (PLUG-53: version 5 holds it after the
    flags), so two tools' audio state can be compared, and the name."""
    magic, version, flags = struct.unpack_from('<4sII', chunk, 4)
    if version != 5:
        return chunk, ''
    (size,) = struct.unpack_from('<I', chunk, 16)
    end = 20 + (size + 3) // 4 * 4
    state = magic + struct.pack('<II', 1, flags) + chunk[end:]
    return struct.pack('<I', len(state)) + state, chunk[20 : 20 + size].decode()


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    impulse = root / 'impulse ü.wav'
    with wave.open(str(impulse), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(48000)
        wav.writeframes(struct.pack('<hh', 16384, 8192))
    preset = root / 'filter ü.vstpreset'
    subprocess.run([TOOL, str(impulse), str(preset)], check=True, capture_output=True)
    original = preset.read_bytes()
    result = subprocess.run([TOOL, str(impulse), str(preset)], capture_output=True, check=False)
    assert result.returncode != 0 and preset.read_bytes() == original
    # PLUG-31: the preset names the IR file, with its extension, which the
    # adapter shows after the IR itself is gone.
    assert [chunk for chunk, _ in chunks(preset)] == [b'Comp', b'Cont']
    impulse.unlink()
    subprocess.run([ADAPTER, str(preset), impulse.name], check=True)
    result = subprocess.run([TOOL, str(root / 'missing.wav'), str(preset)], capture_output=True, check=False)
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

    def make(name, *options):
        target = root / name
        return subprocess.run(
            [TOOL, *options, str(root / 'list.txt'), str(target)], capture_output=True, check=False
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
        [TOOL, str(root / 'nine.cfg'), str(root / 'nine.vstpreset')], capture_output=True, text=True, check=False
    )
    assert result.returncode != 0 and '1 to 8' in result.stderr, result.stderr
    print('Preset selection by channel counts and multichannel limits')

    # PLUG-53: each preset holds its own name, its file's, and PLUG-31 the
    # filter file's name.
    state, name = without_name(chunks(upmix)[0][1])
    assert name == 'upmix' and b'list.txt' in chunks(upmix)[1][1], name
    assert state != without_name(chunks(stereo)[0][1])[0]
    print('Presets hold their own name and the filter file name')
