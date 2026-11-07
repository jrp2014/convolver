# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate a quiet stereo listening clip for manual host checks, using only Python's standard library."""

import math
import struct
import wave
from pathlib import Path

RATE = 48000
SECONDS = 20
PEAK = 4096  # -18.06 dBFS in signed PCM16.
DESTINATION = Path(__file__).with_name('transients-48000-stereo.wav')


def generate():
    channels = [[0.0] * (RATE * SECONDS) for _ in range(2)]
    # L, R, both, twice; then three centred clicks after the sustained section.
    events = [(1, (0,)), (2, (1,)), (3, (0, 1)), (4, (0,)), (5, (1,)), (6, (0, 1))]
    events += [(second, (0, 1)) for second in (16, 17, 18)]
    length = RATE // 25  # 40 ms; leaves ample room for the 250 ms repeat.
    for second, active in events:
        for i in range(length):
            t = i / RATE
            envelope = min(1.0, t / 0.0002) * math.exp(-t / 0.004) * (1 - i / length) ** 2
            click = envelope * sum(math.cos(2 * math.pi * frequency * t) for frequency in (700, 1700, 3700)) / 3
            for channel in active:
                channels[channel][second * RATE + i] = click
    # Different harmonic tones per side: a continuous source for gain, mix
    # and bypass, with 200 ms fades so the source itself introduces no clicks.
    length = 7 * RATE
    for channel, fundamental in enumerate((220, 330)):
        for i in range(length):
            t = i / RATE
            envelope = min(1.0, i / (RATE / 5), (length - 1 - i) / (RATE / 5))
            tone = sum(
                weight * math.sin(2 * math.pi * fundamental * harmonic * t)
                for harmonic, weight in ((1, 0.55), (3, 0.3), (5, 0.15))
            )
            channels[channel][8 * RATE + i] = 0.45 * envelope * tone
    scale = PEAK / max(abs(sample) for channel in channels for sample in channel)
    pcm = bytearray()
    for left, right in zip(*channels):
        pcm.extend(struct.pack('<hh', round(left * scale), round(right * scale)))
    with wave.open(str(DESTINATION), 'wb') as output:
        output.setparams((2, 2, RATE, RATE * SECONDS, 'NONE', 'not compressed'))
        output.writeframes(pcm)
    print(f'{DESTINATION}: {SECONDS} s, {RATE} Hz, stereo PCM16, peak {20 * math.log10(PEAK / 32768):.2f} dBFS')


if __name__ == '__main__':
    generate()
