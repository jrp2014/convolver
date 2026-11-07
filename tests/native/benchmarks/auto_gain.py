# SPDX-License-Identifier: GPL-2.0-or-later
"""What automatic gain would do to Convolver's filters (7 November 2026 experiment).

Convolution reverbs commonly scale a loaded response so that the wet signal
has a consistent level ("auto gain"). This script measures, on a seeded set of
synthetic reverb responses and room-correction and crossover filters, the wet
level relative to the dry signal before and after each normalisation found in
other plug-ins, and the peak gain that results:

  none      the response as recorded (Convolver today)
  peak      largest sample scaled to 0.5 (quantumVerb)
  energy    1 / sqrt(sum h^2) (KlangFalter; JUCE's dsp::Convolution)
  energy<=1 the same, but never boosting (REEV-R)
  loudness  K-weighted (ITU-R BS.1770) pink-noise loudness of the wet signal
            matched to the dry signal: the measure behind "perceived volume"
            compensation

Levels are computed from the frequency response, so no audio is rendered:
the wet/dry level for an input with power spectrum S(f) is
sum |H(f)|^2 S(f) / sum S(f). Needs numpy only.
Usage: python3 tests/native/benchmarks/auto_gain.py
"""

from __future__ import annotations

import numpy as np

RATE = 48000
OCTAVES = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]


def k_weighting_power(freqs: np.ndarray, rate: int) -> np.ndarray:
    """|K(f)|^2 for the BS.1770 pre-filter and RLB high-pass (48 kHz coefficients)."""
    stages = [
        ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]),
        ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]),
    ]
    z = np.exp(-2j * np.pi * freqs / 48000.0)  # evaluate at 48 kHz-equivalent frequency
    power = np.ones_like(freqs)
    for b, a in stages:
        num = b[0] + b[1] * z + b[2] * z * z
        den = a[0] + a[1] * z + a[2] * z * z
        power = power * np.abs(num / den) ** 2
    del rate
    return power


def levels(h: np.ndarray, rate: int, slope_db: float = -3.0) -> dict[str, float]:
    """Wet/dry levels in dB: white noise, K-weighted loudness for an input whose
    spectrum falls by slope_db per octave (pink noise is -3), and the peaks."""
    n = 1 << int(np.ceil(np.log2(max(len(h), 2) * 2)))
    spectrum = np.abs(np.fft.rfft(h, n)) ** 2
    freqs = np.fft.rfftfreq(n, 1.0 / rate)
    band = (freqs >= 20) & (freqs <= 20000)
    weight = np.zeros_like(freqs)
    # Power per bin of an input falling by slope_db per octave.
    weight[band] = (freqs[band] / 1000.0) ** (slope_db / (10 * np.log10(2)))
    weight = weight * k_weighting_power(freqs, rate)
    return {
        'white': 10 * np.log10(np.sum(h.astype(np.float64) ** 2)),
        'loudness': 10 * np.log10(np.sum(spectrum * weight) / np.sum(weight)),
        'max_response': 10 * np.log10(np.max(spectrum[band])),
        'sample_peak': 20 * np.log10(np.max(np.abs(h))),
    }


def reverb(rt60: float, damping: float, rate: int, seed: int, direct: float, predelay: float) -> np.ndarray:
    """Octave-band noise decaying at each band's own RT60, high bands faster by `damping`."""
    rng = np.random.default_rng(seed)
    taps = int(rate * (predelay + 1.2 * rt60))
    t = np.arange(taps) / rate
    noise = np.fft.rfft(rng.standard_normal(taps))
    freqs = np.fft.rfftfreq(taps, 1.0 / rate)
    h = np.zeros(taps)
    for centre in OCTAVES:
        mask = (freqs >= centre / np.sqrt(2)) & (freqs < centre * np.sqrt(2))
        band = np.fft.irfft(noise * mask, taps)
        band_rt = rt60 * (1.0 if centre <= 1000 else (1000.0 / centre) ** damping)
        envelope = np.exp(-6.91 * np.maximum(t - predelay, 0) / band_rt) * (t >= predelay)
        h += band * envelope
    h *= 0.05
    if direct:
        h[0] += direct
    return h


def correction(rate: int, seed: int, taps: int, *, linear_phase: bool) -> np.ndarray:
    """A room-correction filter: the inverse of a smooth room curve with peaks
    and dips, limited to +6 dB of boost and scaled to 0 dB maximum gain, as
    correction software ships it."""
    rng = np.random.default_rng(seed)
    n = taps
    freqs = np.fft.rfftfreq(n, 1.0 / rate)
    log_f = np.log10(np.maximum(freqs, 10))
    room_db = np.zeros_like(freqs)
    for _ in range(12):
        centre = rng.uniform(np.log10(30), np.log10(500))
        room_db += rng.uniform(-8, 10) * np.exp(-0.5 * ((log_f - centre) / 0.05) ** 2)
    room_db += -3 * (log_f - 3).clip(0)  # treble roll-off in the room
    target_db = np.clip(-room_db, -15, 6)
    target_db -= target_db[(freqs > 20) & (freqs < 20000)].max()
    magnitude = 10 ** (target_db / 20)
    if linear_phase:
        h = np.fft.irfft(magnitude, n)
        h = np.roll(h, n // 2) * np.hanning(n)
    else:  # minimum phase by the real cepstrum
        cepstrum = np.fft.irfft(np.log(np.maximum(magnitude, 1e-6)), n)
        fold = np.zeros(n)
        fold[0] = cepstrum[0]
        fold[1 : n // 2] = 2 * cepstrum[1 : n // 2]
        fold[n // 2] = cepstrum[n // 2]
        h = np.fft.irfft(np.exp(np.fft.rfft(fold)), n) * np.hanning(2 * n)[n:]
    return h


def crossover_low(rate: int, taps: int, cutoff: float) -> np.ndarray:
    """A linear-phase low-pass crossover leg (windowed sinc)."""
    t = np.arange(taps) - (taps - 1) / 2
    return 2 * cutoff / rate * np.sinc(2 * cutoff / rate * t) * np.blackman(taps)


def gains(h: np.ndarray, measured: dict[str, float]) -> dict[str, float]:
    """The gain in dB each method would apply."""
    energy = -measured['white']
    return {
        'none': 0.0,
        'peak': 20 * np.log10(0.5 / np.max(np.abs(h))),
        'energy': energy,
        'energy<=1': min(energy, 0.0),
        'loudness': -measured['loudness'],
    }


def resample_band_limited(h: np.ndarray, factor: int) -> np.ndarray:
    """The same frequency response at factor times the rate: the spectrum is
    kept and zero-padded above the old Nyquist frequency (ideal resampling)."""
    n = len(h)
    spectrum = np.fft.rfft(h, n)
    padded = np.zeros(n * factor // 2 + 1, dtype=complex)
    padded[: len(spectrum)] = spectrum
    return np.fft.irfft(padded, n * factor)


METHODS = ['none', 'peak', 'energy', 'energy<=1', 'loudness']
SLOPES = [(-1.5, 'bright music (-1.5 dB/oct)'), (-3.0, 'pink (-3 dB/oct)'), (-4.5, 'dark music (-4.5 dB/oct)')]


def summary(items: list[tuple[str, np.ndarray]]) -> None:
    """Per method: the spread of wet loudness across the responses for three
    input spectra (loudness gain is set on pink, so the other two test it), the
    loudest wet signal and the highest response peak."""
    print(
        '| Method | '
        + ' | '.join(f'spread, {label}' for _, label in SLOPES)
        + ' | loudest wet vs dry (pink) | highest response peak |'
    )
    print('| --- ' * (len(SLOPES) + 3) + '|')
    for method in METHODS:
        spreads = []
        loudest = -1e9
        peak = -1e9
        for slope, _ in SLOPES:
            values = []
            for _, h in items:
                pink = levels(h, RATE)
                g = gains(h, pink)[method]
                values.append(levels(h, RATE, slope)['loudness'] + g)
                if slope == -3.0:
                    loudest = max(loudest, pink['loudness'] + g)
                    peak = max(peak, pink['max_response'] + g)
            spreads.append(max(values) - min(values))
        print(
            f'| {method} | ' + ' | '.join(f'{x:.1f} dB' for x in spreads) + f' | {loudest:+.1f} dB | {peak:+.1f} dB |'
        )


def main() -> None:
    reverbs = []
    seed = 1
    for rt60 in [0.4, 1.0, 2.5, 6.0, 12.0]:
        for damping, tone in [(0.0, 'bright'), (1.0, 'dark')]:
            for direct, kind in [(0.0, 'send'), (1.0, 'with direct sound')]:
                h = reverb(rt60, damping, RATE, seed, direct, 0.01)
                seed += 1
                # Responses are usually distributed peak-normalised.
                h = h / np.max(np.abs(h)) * 0.98
                reverbs.append((f'RT60 {rt60:g} s, {tone}, {kind}', h))
    filters = [
        ('room correction, minimum phase, 16k taps', correction(RATE, 101, 16384, linear_phase=False)),
        ('room correction, linear phase, 65k taps', correction(RATE, 102, 65536, linear_phase=True)),
        ('crossover low-pass leg, 500 Hz, 8k taps', crossover_low(RATE, 8191, 500.0)),
    ]

    print('## Reverb responses (peak-normalised, as distributed)\n')
    print('Wet loudness relative to dry as distributed (K-weighted, pink-noise input), and the')
    print('gain each method applies, in dB.\n')
    print('| Response | wet vs dry as is | peak | energy | energy<=1 | loudness |')
    print('| --- | --- | --- | --- | --- | --- |')
    for name, h in reverbs:
        m = levels(h, RATE)
        g = gains(h, m)
        print(f'| {name} | {m["loudness"]:+.1f} | ' + ' | '.join(f'{g[k]:+.1f}' for k in METHODS[1:]) + ' |')
    print()
    summary(reverbs)

    print('\n## Room-correction and crossover filters\n')
    print('Wet loudness relative to dry as designed, the gain each method would apply,')
    print('and the response peak after loudness matching, in dB.\n')
    print('| Filter | wet vs dry as is | peak | energy | energy<=1 | loudness | response peak after loudness |')
    print('| --- | --- | --- | --- | --- | --- | --- |')
    for name, h in filters:
        m = levels(h, RATE)
        g = gains(h, m)
        print(
            f'| {name} | {m["loudness"]:+.1f} | '
            + ' | '.join(f'{g[k]:+.1f}' for k in METHODS[1:])
            + f' | {m["max_response"] + g["loudness"]:+.1f} |'
        )

    print('\n## Sample rate\n')
    print('One reverb (RT60 2.5 s, dark, with direct sound) at 48 kHz, and the same frequency')
    print('response at 96 kHz (ideal band-limited resampling), wet vs dry in dB:\n')
    print('| Rate | white-noise energy | loudness as is | after energy | after loudness |')
    print('| --- | --- | --- | --- | --- |')
    h48 = reverb(2.5, 1.0, RATE, 77, 1.0, 0.01)
    for rate, h in [(48000, h48), (96000, resample_band_limited(h48, 2))]:
        m = levels(h, rate)
        g = gains(h, m)
        print(
            f'| {rate} | {m["white"]:+.1f} | {m["loudness"]:+.1f} | '
            f'{m["loudness"] + g["energy"]:+.1f} | {m["loudness"] + g["loudness"]:+.1f} |'
        )


if __name__ == '__main__':
    main()
