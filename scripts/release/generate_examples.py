# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate exact IEEE float WAV fixtures and self-contained VST3 presets."""

import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests' / 'plugins'))
from fixtures import float_wav_bytes  # noqa: E402  The shared PLUG-2/DOC-1 WAV writer.


def generate(output, converter):
    output.mkdir(parents=True, exist_ok=True)
    examples = []
    for rate in (44100, 48000, 96000):
        for channels in (1, 2):
            for kind in ('identity', 'echo', 'repeat'):
                name = f'{kind}-{rate}-' + ('mono' if channels == 1 else 'stereo')
                folder = output / name
                folder.mkdir(exist_ok=True)
                # Exactly representable coefficients, each kind plain by ear and
                # on a level meter (REL-10): identity; the echo, half at once and
                # three repeats 0.3 s apart, each 6 dB quieter (a sum of 0.9375,
                # for headroom); the repeat, half at once and one quarter-level
                # copy a quarter of a second later.
                if kind == 'identity':
                    taps = [[0, 1.0]]
                elif kind == 'echo':
                    step = rate * 3 // 10
                    taps = [[0, 0.5], [step, 0.25], [2 * step, 0.125], [3 * step, 0.0625]]
                else:
                    taps = [[0, 0.5], [rate // 4, 0.25]]
                impulse = [0.0] * (taps[-1][0] + 1)
                for position, value in taps:
                    impulse[position] = value
                (folder / 'impulse.wav').write_bytes(float_wav_bytes(rate, [impulse] * channels))
                zeroes = ' '.join(['0'] * channels)
                config = f'{rate} {channels} {channels} 0\n{zeroes}\n{zeroes}\n'
                for channel in range(channels):
                    config += f'impulse.wav\n{channel}\n{channel}\n{channel}\n'
                (folder / 'filter.cfg').write_text(config, encoding='utf-8')
                preset = folder / f'{name}.vstpreset'  # Hosts list presets by file name (PLUG-28).
                for old in (preset, folder / 'filter.vstpreset'):  # Before PLUG-28 every one was filter.vstpreset.
                    if old.exists():
                        old.unlink()  # Only this generator's own output directory.
                subprocess.run([str(converter), str(folder / 'impulse.wav'), str(preset)], check=True)
                examples.append(
                    {'name': name, 'rate': rate, 'channels': channels, 'kind': kind, 'taps': taps, 'latency': 0}
                )
    (output / 'examples.json').write_text(json.dumps(examples, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--converter', type=Path, required=True)
    args = parser.parse_args()
    generate(args.output, args.converter.resolve())
