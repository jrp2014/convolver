# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate exact IEEE float WAV fixtures and self-contained VST3 presets."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests' / 'plugins'))
from fixtures import float_wav_bytes  # noqa: E402  The shared PLUG-2/DOC-1 WAV writer.


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
                (folder / 'impulse.wav').write_bytes(float_wav_bytes(rate, [impulse] * channels))
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
