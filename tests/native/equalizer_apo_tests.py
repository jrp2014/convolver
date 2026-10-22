# SPDX-License-Identifier: GPL-2.0-or-later
"""convolver-equalizer-apo: exported filters, run through a model of Equalizer APO.

Equalizer APO is a Windows audio-engine component, so CI cannot run it. The model
below executes the commands the export writes (Eval, If/EndIf, Channel, Copy,
Delay, Convolution, Preamp, Include) the way Equalizer APO 1.4.2's sources do: the same line
splitting, the same channel-name lookup, Copy computed from the channels as they
were before the line, created ("virtual") channels, mono IRs applied to each
selected channel in place without latency, and the device's sample rate and
channel count. Anything else the model does not know fails the test, so the
export cannot drift into syntax nobody checked.

Each export is compared with the double-precision oracle of the shared fixtures
(the CLI's own reference), with the device channels the filter does not use, and
with the selection between a list's entries.

With --benchmark and --config, every case also runs through Equalizer APO
itself: its Benchmark.exe filters a WAV file with the configuration in the
folder that the registry value HKLM\\SOFTWARE\\EqualizerAPO\\ConfigPath names. The
test writes config.txt there, so that folder must be one made for the test; it
refuses a folder holding a config.txt it did not write. Windows only; CI
extracts a pinned Equalizer APO release instead of installing it.

Usage: equalizer_apo_tests.py CONVOLVER_EQUALIZER_APO [--benchmark BENCHMARK.EXE --config FOLDER]
"""
import argparse
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / 'tests/plugins'))
import fixtures  # noqa: E402  (shared WAV helpers and oracle)

ARGUMENTS = argparse.ArgumentParser(description=__doc__.split('\n')[0])
ARGUMENTS.add_argument('tool')
ARGUMENTS.add_argument('--benchmark', type=Path, help="Equalizer APO's Benchmark.exe")
ARGUMENTS.add_argument('--config', type=Path, help='the folder ConfigPath names')
OPTIONS = ARGUMENTS.parse_args()
TOOL = OPTIONS.tool
EXAMPLES = REPOSITORY / 'examples/first-use'
TOLERANCE = 1e-5  # Float32 IRs and inputs against the double oracle.
# Benchmark.exe writes 16-bit PCM: half a step of rounding, plus the above.
RUNTIME_TOLERANCE = 2.0 ** -16 + TOLERANCE

# Windows' default speaker masks for a channel count (ChannelHelper.cpp).
DEFAULT_NAMES = {
    1: ['C'], 2: ['L', 'R'], 4: ['L', 'R', 'RL', 'RR'],
    6: ['L', 'R', 'C', 'LFE', 'SL', 'SR'],
    8: ['L', 'R', 'C', 'LFE', 'RL', 'RR', 'SL', 'SR'],
}
ALIASES = {'SL': 'RL', 'SR': 'RR', 'RL': 'SL', 'RR': 'SR', 'SUB': 'LFE'}


class ModelError(AssertionError):
    pass


class EqualizerApo:
    """Load a configuration file for one device, then process planar audio."""

    def __init__(self, path, rate, channels):
        self.rate = rate
        self.device_channels = channels
        self.names = DEFAULT_NAMES.get(channels, [str(c + 1) for c in range(channels)])[:]
        self.selection = self.names[:]
        self.variables = {'sampleRate': float(rate), 'outputChannelCount': channels,
                          'inputChannelCount': channels}
        self.filters = []  # (kind, data) in load order
        self.load(Path(path))

    # --- Loading ---------------------------------------------------------------
    def index(self, word, allow_new=False):
        if word[:1].isdigit():
            number = int(re.match(r'\d+', word).group()) - 1
            if 0 <= number < len(self.names):
                return number
            raise ModelError(f'channel number {word} out of range 1-{len(self.names)}')
        for candidate in (word, ALIASES.get(word)):
            if candidate in self.names:
                return self.names.index(candidate)
        if allow_new:
            return -1
        raise ModelError(f'invalid channel {word!r}')

    def evaluate(self, expression):
        match = re.fullmatch(r'\s*([A-Za-z]\w*)\s*(==|>|<)\s*(-?\d+)\s*', expression)
        if not match:
            raise ModelError(f'unexpected expression {expression!r}')
        name, operator, value = match.groups()
        if name not in self.variables:
            raise ModelError(f'undefined variable {name}')
        left, right = self.variables[name], int(value)
        return {'==': left == right, '>': left > right, '<': left < right}[operator]

    def load(self, path):
        skipping = []  # one flag per open If: True when that If is false
        for line in path.read_text(encoding='utf-8').split('\n'):
            line = line.rstrip('\r')
            if ':' not in line:
                if line.strip() and not line.lstrip().startswith('#'):
                    raise ModelError(f'line without a command: {line!r}')
                continue
            key, value = line.split(':', 1)
            key = key.strip()
            if key.startswith('#'):
                continue
            if key == 'If':
                skipping.append(any(skipping) or not self.evaluate(value))
                continue
            if key == 'EndIf':
                if not skipping:
                    raise ModelError('EndIf without If')
                skipping.pop()
                continue
            if key in ('ElseIf', 'Else'):
                raise ModelError(f'{key} is not expected in an export')
            if any(skipping):
                continue
            getattr(self, 'command_' + key, self.unknown(key))(value, path)
        if skipping:
            raise ModelError('If was not closed by EndIf')

    def unknown(self, key):
        def fail(value, path):
            raise ModelError(f'unexpected command {key}: {value}')
        return fail

    def command_Eval(self, value, path):
        match = re.fullmatch(r'\s*([A-Za-z]\w*)\s*=\s*(-?\d+)\s*', value)
        if not match:
            raise ModelError(f'unexpected Eval {value!r}')
        self.variables[match.group(1)] = int(match.group(2))

    def command_Channel(self, value, path):
        words = value.upper().split()
        chosen = [False] * len(self.names)
        for word in words:
            if word == 'ALL':
                chosen = [True] * len(self.names)
            else:
                chosen[self.index(word)] = True
        self.selection = [n for n, c in zip(self.names, chosen) if c]

    def command_Copy(self, value, path):
        assignments = []
        for text in value.split(' '):
            parts = text.split('=')
            if len(parts) != 2:
                if text:
                    raise ModelError(f'unexpected Copy part {text!r}')
                continue
            target, source = parts
            terms = []
            for summand in source.split('+'):
                factors = summand.split('*')
                if len(factors) == 2:
                    factor, channel = factors
                elif len(factors) == 1 and (factors[0] == '0' or '.' in factors[0]):
                    factor, channel = factors[0], ''
                elif len(factors) == 1:
                    factor, channel = '', factors[0]
                else:
                    raise ModelError(f'unexpected Copy term {summand!r}')
                if factor.lower().endswith('db'):
                    raise ModelError('decibel factors are not expected in an export')
                gain = float(factor) if factor else 1.0
                terms.append((gain, self.names[self.index(channel)] if channel else None))
            index = self.index(target, allow_new=True)
            name = self.names[index] if index >= 0 else target
            if index < 0:
                if not re.fullmatch(r'[A-Z][A-Z0-9]*', name):
                    raise ModelError(f'created channel {name!r} would not match Channel')
                self.names.append(name)
            assignments.append((name, terms))
        self.filters.append(('copy', assignments))

    def command_Delay(self, value, path):
        match = re.fullmatch(r'\s*(\d+)\s+samples\s*', value)
        if not match:
            raise ModelError(f'unexpected Delay {value!r}')
        self.filters.append(('delay', (self.selection[:], int(match.group(1)))))

    def command_Include(self, value, path):
        # Relative to the including file; the channel selection is restored after.
        saved = self.selection[:]
        self.load(path.parent / value.strip().replace('\\', '/'))
        self.selection = saved

    def command_Preamp(self, value, path):
        match = re.fullmatch(r'\s*(-?\d+(?:\.\d*)?)\s*dB\s*', value)
        if not match:
            raise ModelError(f'unexpected Preamp {value!r}')
        gain = 10.0 ** (float(match.group(1)) / 20.0)
        self.filters.append(('gain', (self.selection[:], gain)))

    def command_Convolution(self, value, path):
        file = path.parent / value.strip()
        channels, rate = fixtures.read_float_wav(file, with_rate=True)
        if abs(rate - self.rate) > 1:
            # Equalizer APO logs this and leaves the channels unfiltered; the
            # export's If lines must make it impossible, so the model fails.
            raise ModelError(f'{file.name} is {rate} Hz on a {self.rate} Hz device')
        taps = [[(k, v) for k, v in enumerate(c) if v] for c in channels]
        self.filters.append(('convolution', [(name, taps[i % len(taps)])
                                             for i, name in enumerate(self.selection)]))

    # --- Processing ------------------------------------------------------------
    def process(self, inputs):
        frames = len(inputs[0])
        signal = {name: list(inputs[c]) for c, name in enumerate(self.names[:self.device_channels])}
        for name in self.names[self.device_channels:]:
            signal[name] = [0.0] * frames
        for kind, data in self.filters:
            if kind == 'copy':
                before = {name: values[:] for name, values in signal.items()}
                for target, terms in data:
                    result = [0.0] * frames
                    for gain, source in terms:
                        for n in range(frames):
                            result[n] += gain * (before[source][n] if source else 1.0)
                    signal[target] = result
            elif kind == 'delay':
                names, delay = data
                for name in names:
                    signal[name] = ([0.0] * delay + signal[name])[:frames]
            elif kind == 'gain':
                names, gain = data
                for name in names:
                    signal[name] = [gain * v for v in signal[name]]
            else:
                for name, taps in data:
                    x = signal[name]
                    y = [0.0] * frames
                    for k, tap in taps:
                        for n in range(k, frames):
                            y[n] += tap * x[n - k]
                    signal[name] = y
        # The audio engine receives 32-bit floats.
        return [[fixtures.to_float32(v) for v in signal[name]]
                for name in self.names[:self.device_channels]]


class Runtime:
    """Equalizer APO itself, through its Benchmark.exe (Windows only)."""

    MARKER = '# Written by equalizer_apo_tests.py; replaced on every run.'

    def __init__(self, benchmark, config):
        import winreg  # noqa: PLC0415  (Windows only)
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\EqualizerAPO') as key:
            registered = Path(winreg.QueryValueEx(key, 'ConfigPath')[0])
        if registered.resolve() != config.resolve():
            raise SystemExit(f'ConfigPath is {registered}, not {config}: refusing to replace '
                             'the configuration of an Equalizer APO installation')
        existing = config / 'config.txt'
        if existing.exists() and not existing.read_text(encoding='utf-8',
                                                        errors='replace').startswith(self.MARKER):
            raise SystemExit(f'{existing} was not written by this test; use an empty folder')
        self.benchmark = benchmark
        self.config = config
        self.runs = 0

    def process(self, text, rate, inputs):
        """Filter planar inputs at a rate with config.txt holding `text`."""
        self.runs += 1
        (self.config / 'config.txt').write_text(f'{self.MARKER}\n{text}\n', encoding='utf-8')
        source = self.config / f'input-{self.runs}.wav'
        result = self.config / f'output-{self.runs}.wav'
        fixtures.write_float_wav(source, rate, inputs)
        run = subprocess.run([str(self.benchmark), '--nopause', '--verbose', '-i', str(source),
                              '-o', str(result)], capture_output=True, text=True,
                             errors='replace', timeout=120)
        log = run.stdout + run.stderr
        if run.returncode != 0 or not result.exists():
            raise AssertionError(f'Benchmark.exe failed ({run.returncode}):\n{log}')
        outputs = read_wav(result)
        source.unlink()
        result.unlink()
        return outputs, log

    def check(self, label, include, rate, inputs, wanted):
        actual, log = self.process(f'Include: {include}', rate, inputs)
        error = fixtures.worst_error(actual, wanted)
        if error > RUNTIME_TOLERANCE:
            raise AssertionError(f'{label}: Equalizer APO worst error {error:.3g}\n{log}')
        print(f'{label}: Equalizer APO worst error {error:.3g}')


def read_wav(path):
    """Planar samples of a 32-bit float or 16-bit PCM WAV."""
    data = Path(path).read_bytes()
    fmt = data.find(b'fmt ')
    tag, _, _, _, _, bits = struct.unpack('<HHIIHH', data[fmt + 8:fmt + 24])
    if bits == 32:
        return fixtures.read_float_wav(path)
    with wave.open(str(path), 'rb') as file:
        assert file.getsampwidth() == 2, f'{path}: {file.getsampwidth() * 8}-bit samples'
        channels = file.getnchannels()
        frames = file.readframes(file.getnframes())
    samples = struct.unpack(f'<{len(frames) // 2}h', frames)
    return [[v / 32768.0 for v in samples[c::channels]] for c in range(channels)]


RUNTIME = None  # Set in main() when --benchmark is given.


# --- Helpers ---------------------------------------------------------------------

def run(*args, ok=True):
    result = subprocess.run([TOOL, *map(str, args)], capture_output=True, text=True)
    if ok and result.returncode != 0:
        raise AssertionError(f'{args}: {result.stderr}')
    if not ok and result.returncode == 0:
        raise AssertionError(f'{args} was accepted: {result.stdout}')
    return result


def check(label, folder, rate, device, inputs, expected, used):
    """Run the export on a device; channels beyond `used` must be silent."""
    frames = len(expected[0]) if expected else len(inputs[0])
    extra = [fixtures.noise(frames, 900 + c) for c in range(device - len(inputs))]
    padded = [c + [0.0] * (frames - len(c)) for c in inputs] + extra
    model = EqualizerApo(folder / 'convolver.txt', rate, device)
    actual = model.process(padded)
    wanted = [list(c) for c in expected]
    wanted += [[0.0] * frames for _ in range(len(expected), used)]
    wanted += [[0.0] * frames for _ in range(used, device)]
    error = fixtures.worst_error(actual, wanted)
    assert error <= TOLERANCE, f'{label}: worst error {error:.3g}'
    print(f'{label}: worst error {error:.3g}')
    if RUNTIME:
        RUNTIME.check(label, folder.resolve() / 'convolver.txt', rate, padded, wanted)
    return error


def silent(label, folder, rate, device, frames=200):
    """No filter fits: every channel must be silent, not unfiltered."""
    model = EqualizerApo(folder / 'convolver.txt', rate, device)
    actual = model.process([fixtures.noise(frames, 700 + c) for c in range(device)])
    loudest = max(abs(v) for channel in actual for v in channel)
    assert loudest == 0.0, f'{label}: not silent ({loudest:.3g})'
    assert not any(kind == 'convolution' for kind, _ in model.filters), label
    print(f'{label}: silent')
    if RUNTIME:
        RUNTIME.check(label, folder.resolve() / 'convolver.txt', rate,
                      [fixtures.noise(frames, 700 + c) for c in range(device)],
                      [[0.0] * frames for _ in range(device)])


def write_cfg(path, text):
    Path(path).write_text(text, encoding='utf-8')


# --- Tests -----------------------------------------------------------------------

def test_first_use(work):
    """The documented examples, on a device with their channels and with more."""
    for name in fixtures.FIXTURE_NAMES:
        description = fixtures.fixtures()[name]
        source = EXAMPLES / name / description['filter']
        folder = work / f'first-use-{name}'
        run(source, folder)
        inputs = fixtures.read_float_wav(EXAMPLES / name / 'input.wav')
        expected = fixtures.read_float_wav(EXAMPLES / name / 'expected.wav')
        used = max(description['inputs'], description['outputs'])
        for device in (used, used + 2):
            check(f'{name} on {device} channels', folder, fixtures.RATE, device, inputs,
                  expected, used)
        # Another sample rate, or a device too large to silence channel by
        # channel, gets silence rather than unfiltered audio.
        for rate, device in ((44100, used), (fixtures.RATE, 33)):
            silent(f'{name} at {rate} Hz on {device} channels', folder, rate, device)


def test_list(work):
    """Every list entry, selected by rate and device channels; routes and delays."""
    folder = work / 'list'
    folder.mkdir()
    a = [fixtures.sparse_ir(40, [(0, 0.75), (9, -0.25)]),
         fixtures.sparse_ir(40, [(0, 0.5), (39, 0.125)])]
    b = [fixtures.sparse_ir(64, [(0, 0.5), (3, 0.25), (63, -0.125)]),
         fixtures.sparse_ir(64, [(2, -0.5), (17, 0.375)])]
    c = fixtures.sparse_ir(33, [(0, 1.0), (32, -0.5)])
    fixtures.write_float_wav(folder / 'a.wav', 44100, a)
    fixtures.write_float_wav(folder / 'b.wav', 48000, b)
    fixtures.write_float_wav(folder / 'c.wav', 48000, [c])
    write_cfg(folder / 'a.cfg', '44100 2 2 3\n0 0\n0 0\na.wav\n0\n0\n0\na.wav\n1\n1\n1\n')
    # 3 -> 2 downmix: input 1 delayed 0.5 ms, output 0 delayed 1 ms; mixed and
    # inverted routes ("-0" is channel 0 inverted), one IR shared by two paths.
    write_cfg(folder / 'b.cfg',
              '48000 3 2 0\n0 0.5 0\n1 0\n'
              'b.wav\n0\n0 2.5\n0\n'
              'b.wav\n1\n1\n1 0.25\n'
              'b.wav\n0\n-2\n1.5\n'
              'b.wav\n1\n-0\n-1.125\n')
    # Same rate and device channels as b.cfg: Equalizer APO cannot tell them apart.
    write_cfg(folder / 'd.cfg', '48000 2 3 0\n0 0\n0 0 0\nb.wav\n0\n0\n2\n')
    write_cfg(folder / 'list.txt', 'a.cfg\nb.cfg\nc.wav\nd.cfg\n')
    out = work / 'list-export'
    result = run(folder / 'list.txt', out)
    assert 'd.cfg' in result.stdout and 'left out' in result.stdout, result.stdout
    text = (out / 'convolver.txt').read_text(encoding='utf-8')
    assert text.count('Convolution:') == 5, text  # a: 2, b: 2 shared, c: 1
    assert len(list(out.glob('*.wav'))) == 5

    frames = 300
    def signal(channels, seed):
        return [[fixtures.to_float32(v) for v in fixtures.noise(frames, seed + k)]
                for k in range(channels)]

    def oracle(inputs, paths, outputs, input_delays=None, output_delays=None):
        rendered = fixtures.render(inputs, paths, outputs, input_delays, output_delays)
        return [channel[:frames] for channel in rendered]

    x2 = signal(2, 300)
    a_paths = [dict(ir=a[0], inputs=[(0, 1.0)], outputs=[(0, 1.0)]),
               dict(ir=a[1], inputs=[(1, 1.0)], outputs=[(1, 1.0)])]
    check('list: a.cfg at 44.1 kHz', out, 44100, 2, x2, oracle(x2, a_paths, 2), 2)

    x3 = signal(3, 400)
    b_paths = [dict(ir=b[0], inputs=[(0, 1.0), (2, 0.5)], outputs=[(0, 1.0)]),
               dict(ir=b[1], inputs=[(1, 1.0)], outputs=[(1, 1.0), (0, 0.25)]),
               dict(ir=b[0], inputs=[(2, -1.0)], outputs=[(1, 0.5)]),
               dict(ir=b[1], inputs=[(0, -1.0)], outputs=[(1, -0.125)])]
    b_expected = oracle(x3, b_paths, 2, [0, 24, 0], [48, 0])
    check('list: b.cfg at 48 kHz, 3 channels', out, 48000, 3, x3, b_expected, 3)
    # A 4-channel device has no exact match; b.cfg is the first that fits.
    check('list: b.cfg at 48 kHz, 4 channels', out, 48000, 4, x3, b_expected, 3)

    x1 = signal(1, 500)
    c_paths = [dict(ir=c, inputs=[(0, 1.0)], outputs=[(0, 1.0)])]
    check('list: c.wav at 48 kHz, 1 channel', out, 48000, 1, x1, oracle(x1, c_paths, 1), 1)
    # Stereo at 48 kHz: c.wav fits (filters channel 1); channel 2 is silent.
    check('list: c.wav at 48 kHz, 2 channels', out, 48000, 2, x1, oracle(x1, c_paths, 1), 1)
    # No entry at 96 kHz: silence.
    silent('list: 96 kHz', out, 96000, 2)


def test_choice_and_include(work):
    """A larger device gets the largest entry that fits; Include keeps the rest of config.txt."""
    # Copies beside the list: a relative path from the temporary folder to the
    # repository can cross a symbolic link (macOS /var) or a drive (Windows).
    folder = work / 'choice'
    for name in ('stereo', 'crossover'):
        shutil.copytree(EXAMPLES / name, folder / name)
    write_cfg(folder / 'list.txt', 'stereo/filter.cfg\ncrossover/filter.cfg\n')
    config = work / 'config'
    config.mkdir()
    run(folder / 'list.txt', config / 'room')
    stereo, crossover = (fixtures.read_float_wav(EXAMPLES / n / 'expected.wav')
                         for n in ('stereo', 'crossover'))
    inputs = fixtures.read_float_wav(EXAMPLES / 'stereo/input.wav')
    crossover_inputs = fixtures.read_float_wav(EXAMPLES / 'crossover/input.wav')
    check('choice: 4 channels get the stereo entry', config / 'room', fixtures.RATE, 4, inputs,
          stereo, 2)
    check('choice: 8 channels get the crossover', config / 'room', fixtures.RATE, 8,
          crossover_inputs, crossover, 6)

    # As installed: the export included from config.txt between other commands.
    write_cfg(config / 'config.txt', 'Channel: 1\nInclude: room\\convolver.txt\nPreamp: -6 dB\n')
    model = EqualizerApo(config / 'config.txt', fixtures.RATE, 2)
    actual = model.process([list(c) for c in inputs])
    gain = 10 ** (-6 / 20)
    wanted = [[gain * v for v in stereo[0]], list(stereo[1])]
    frames = min(len(actual[0]), len(wanted[0]))
    error = fixtures.worst_error([c[:frames] for c in actual], [c[:frames] for c in wanted])
    assert error <= TOLERANCE, f'include: worst error {error:.3g}'
    print(f'include: worst error {error:.3g}')
    if RUNTIME:
        padded = [c + [0.0] * (len(wanted[0]) - len(c)) for c in inputs]
        actual, log = RUNTIME.process(
            f'Channel: 1\nInclude: {(config / "room/convolver.txt").resolve()}\nPreamp: -6 dB',
            fixtures.RATE, padded)
        error = fixtures.worst_error(actual, wanted)
        assert error <= RUNTIME_TOLERANCE, f'include: Equalizer APO worst error {error:.3g}\n{log}'
        print(f'include: Equalizer APO worst error {error:.3g}')


def test_names(work):
    """File names cannot add commands; folder names must fit Equalizer APO's paths."""
    if os.name != 'nt':  # Windows file names cannot hold a line break.
        folder = work / 'names'
        folder.mkdir()
        source = folder / 'x\nPreamp: 20 dB\n.cfg'
        source.write_bytes((EXAMPLES / 'stereo/filter.cfg').read_bytes())
        (folder / 'room.wav').write_bytes((EXAMPLES / 'stereo/room.wav').read_bytes())
        run(source, work / 'names-export')
        text = (work / 'names-export/convolver.txt').read_text(encoding='utf-8')
        assert not any(line.startswith('Preamp: 20') for line in text.split('\n')), text
    result = run(EXAMPLES / 'stereo/filter.cfg', work / ('n' * 215), ok=False)
    assert 'too long' in result.stderr, result.stderr
    assert not (work / ('n' * 215)).exists()


def test_refusals(work):
    """Existing folders are kept; broken inputs leave nothing behind."""
    source = EXAMPLES / 'stereo/filter.cfg'
    existing = work / 'existing'
    existing.mkdir()
    result = run(source, existing, ok=False)
    assert 'already exists' in result.stderr, result.stderr
    assert list(existing.iterdir()) == []

    broken = work / 'broken'
    broken.mkdir()
    write_cfg(broken / 'missing.cfg', '48000 1 1 0\n0\n0\nnot-there.wav\n0\n0\n0\n')
    result = run(broken / 'missing.cfg', work / 'never', ok=False)
    assert 'not-there.wav' in result.stderr, result.stderr
    assert not (work / 'never').exists()

    usage = run('--help')
    assert 'Include:' in usage.stdout
    assert run(ok=False).returncode == 2


def main():
    global RUNTIME
    if OPTIONS.benchmark or OPTIONS.config:
        if not (OPTIONS.benchmark and OPTIONS.config):
            ARGUMENTS.error('--benchmark and --config go together')
        RUNTIME = Runtime(OPTIONS.benchmark, OPTIONS.config)
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        test_first_use(work)
        test_list(work)
        test_choice_and_include(work)
        test_names(work)
        test_refusals(work)
    print('Equalizer APO export: all checks passed' +
          (f', {RUNTIME.runs} of them also in Equalizer APO' if RUNTIME else ''))


if __name__ == '__main__':
    main()
