#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for run_upgrade.py's Audio Unit comparison, without macOS (COMPAT-1).

The Audio Unit check drops the frames before the reported latency and then
compares the rest. These feed it a render, as avaudioengine_render would
write one, for 0.14.0's first-use-stereo-settings state (Latency 256), and
check that damage in the dropped frames fails it. They run in pre-commit.
"""

import contextlib
import importlib.util
import io
import json
import math
import pathlib
import struct
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('run_upgrade', HERE / 'run_upgrade.py')
run_upgrade = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(run_upgrade)

FOLDER = HERE / '0.14.0'
CASE = next(
    c
    for c in json.loads((FOLDER / 'au.json').read_text(encoding='utf-8'))['cases']
    if c['name'] == 'first-use-stereo-settings'
)


class AudioUnitPrefix(unittest.TestCase):
    def setUp(self):
        self.expected = run_upgrade.fixtures.read_float_wav(FOLDER / CASE['expected'])
        self.latency = CASE['latency']
        self.assertGreater(self.latency, 0, 'the case must have frames before its latency')

    def check(self, change=None):
        """check_au's result for the release's audio after silence, changed by change(channels)."""
        channels = [[0.0] * self.latency + list(channel) for channel in self.expected]
        if change:
            change(channels)
        frames = len(channels[0])
        interleaved = [channels[c][i] for i in range(frames) for c in range(len(channels))]

        def render(args):
            pathlib.Path(args[4]).write_bytes(struct.pack(f'<{len(interleaved)}f', *interleaved))
            return 0, f'latency {self.latency}\n'

        with tempfile.TemporaryDirectory() as work, contextlib.redirect_stdout(io.StringIO()):
            original, run_upgrade.run = run_upgrade.run, render
            try:
                return run_upgrade.check_au(FOLDER, CASE, 'render', 509, pathlib.Path(work), {})
            finally:
                run_upgrade.run = original

    def test_release_audio_after_silence_passes(self):
        self.assertIsNone(self.check())

    def test_nan_before_the_latency_fails(self):
        def change(channels):
            channels[1][10] = math.nan

        failure = self.check(change)
        self.assertIsNotNone(failure)
        self.assertIn('sample 10 of channel 1 is nan, before the reported latency of 256 frames', failure)

    def test_infinity_before_the_latency_fails(self):
        def change(channels):
            channels[0][self.latency - 1] = -math.inf

        self.assertIn('is -inf, before the reported latency', self.check(change) or '')

    def test_sound_before_the_latency_fails(self):
        def change(channels):
            channels[0][100] = 0.5

        self.assertIn('0.5 before the reported latency of 256 frames', self.check(change) or '')

    def test_nan_after_the_latency_fails(self):
        def change(channels):
            channels[0][self.latency + 5] = math.nan

        self.assertIn('sample 5 of channel 0 is nan', self.check(change) or '')


if __name__ == '__main__':
    unittest.main()
