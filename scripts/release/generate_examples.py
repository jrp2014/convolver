# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate exact IEEE float WAV fixtures and self-contained VST3 presets."""
import argparse
import json
from pathlib import Path
import struct
import subprocess


def generate(output, converter):
    output.mkdir(parents=True, exist_ok=True)
    examples = []
    for rate in (44100, 48000, 96000):
        for channels in (1, 2):
            for kind in ('identity', 'echo'):
                name = f'{kind}-{rate}-' + ('mono' if channels == 1 else 'stereo')
                folder = output / name
                folder.mkdir(exist_ok=True)
                # Exactly representable coefficients: identity; or -6 dB direct
                # plus a quarter-amplitude delayed tap, with headroom.
                impulse = [1.0] if kind == 'identity' else [0.5] + [0.0] * 256 + [0.25]
                payload = b''.join(struct.pack('<f', value) * channels for value in impulse)
                fmt = struct.pack('<HHIIHH', 3, channels, rate, rate * channels * 4, channels * 4, 32)
                wav = b'RIFF' + struct.pack('<I', 36 + len(payload)) + b'WAVEfmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(payload)) + payload
                (folder / 'impulse.wav').write_bytes(wav)
                zeroes = ' '.join(['0'] * channels)
                config = f'{rate} {channels} {channels} 0\n{zeroes}\n{zeroes}\n'
                for channel in range(channels):
                    config += f'impulse.wav\n{channel}\n{channel}\n{channel}\n'
                (folder / 'filter.cfg').write_text(config, encoding='utf-8')
                preset = folder / 'filter.vstpreset'
                if preset.exists():
                    preset.unlink()  # Only this generator's own output directory.
                subprocess.run([str(converter), str(folder / 'impulse.wav'), str(preset)], check=True)
                examples.append(dict(name=name, rate=rate, channels=channels, kind=kind,
                                     taps=[[0, 1.0]] if kind == 'identity' else [[0, 0.5], [257, 0.25]], latency=256))
    (output / 'examples.json').write_text(json.dumps(examples, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--converter', type=Path, required=True)
    args = parser.parse_args()
    generate(args.output, args.converter.resolve())
