# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise generated presets through the real SDK adapter, with relocated assets."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import wave

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
    result = subprocess.run([sys.argv[1], str(impulse), str(preset)], capture_output=True)
    assert result.returncode != 0 and preset.read_bytes() == original
    impulse.unlink()
    subprocess.run([sys.argv[2], str(preset)], check=True)
    result = subprocess.run([sys.argv[1], str(root / 'missing.wav'), str(preset)], capture_output=True)
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
        return subprocess.run([tool, *options, str(root / 'list.txt'), str(target)],
                              capture_output=True), target

    result, upmix = make('upmix.vstpreset', '--sample-rate', '48000', '--output-channels', '6')
    assert result.returncode == 0, result.stderr
    result, stereo = make('stereo.vstpreset', '--input-channels', '2')
    assert result.returncode == 0, result.stderr
    assert upmix.read_bytes() != stereo.read_bytes()
    for options in (['--output-channels', '3'], ['--sample-rate', '44100'],
                    ['--output-channels', '0'], ['--input-channels', 'x'],
                    ['--frobnicate', '1']):
        result, target = make('rejected.vstpreset', *options)
        assert result.returncode != 0 and not target.exists(), options
    result = subprocess.run([tool, str(root / 'nine.cfg'), str(root / 'nine.vstpreset')],
                            capture_output=True, text=True)
    assert result.returncode != 0 and '1 to 8' in result.stderr, result.stderr
    print('Preset selection by channel counts and multichannel limits')
