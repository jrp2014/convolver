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
