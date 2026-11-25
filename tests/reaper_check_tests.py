#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for scripts/reaper_check.py (HOST-11) without REAPER.

The script runs only on jrp's Mac, and each change to it costs him a run,
so these check in pre-commit what can be checked anywhere: the Lua it
writes, how it reads what the ReaScripts record, and a whole run against a
stand-in REAPER and a stand-in `convolver --compare`.
"""

import contextlib
import importlib.util
import os
import pathlib
import re
import shutil
import stat
import struct
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location('reaper_check', ROOT / 'scripts' / 'reaper_check.py')
reaper_check = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(reaper_check)

# A stand-in REAPER: reads the ReaScript's CONFIG, records what a passing run
# records and writes the files REAPER would. FAKE_REAPER=exit quits at once,
# FAKE_REAPER=silent restores nothing on reopening, and FAKE_REAPER=stopped
# reads Status after the transport stopped, as REAPER did in jrp's first run.
# FAKE_REAPER=waiting and FAKE_REAPER=dry read, with the transport running,
# texts that start like the Active one but are not (jrp's review);
# FAKE_REAPER=dry_loaded reads the Mix 0% text after loading and reopening.
FAKE_REAPER = textwrap.dedent(
    r"""
    #!/usr/bin/env python3
    import os, re, shutil, sys
    script = next(a for a in sys.argv[1:] if a.endswith('.lua'))
    text = open(script, encoding='utf-8').read()
    config = dict(re.findall(r'^  (\w+) = "((?:[^"\\]|\\.)*)",$', text, re.M))
    mode = os.environ.get('FAKE_REAPER', 'pass')

    # The soak (HOST-12): FAKE_REAPER=underrun reports two new audio
    # underruns, noapi a REAPER without GetUnderrunTime, frozen a clock
    # that does not advance, stopped a transport that stopped early,
    # big_block a 512-frame buffer, fault one return's fault while playing,
    # refused a replacement preset that REAPER did not load, old_state a
    # replacement that REAPER accepted but left the old filter in place,
    # nostate a REAPER that gives no plug-in state, huge_state a state far
    # larger than either preset, and start_underrun an underrun only as
    # playback started.
    def soak(config, mode, duration, presets):
        loaded = 'Filter loaded: 48000 Hz, 2 in, 2 out.'
        ran = duration / 3 if mode == 'stopped' else duration
        after = {'underrun': '9000 0 99000', 'frozen': '0 0 1000', 'start_underrun': '3000 0 99000'}.get(
            mode, '0 0 99000')
        lines = [('stage', 'soak'), ('reaper_version', '7.99/macOS-arm64'), ('device_rate', '48000'),
                 ('device_block', '512' if mode == 'big_block' else '64'), ('device_output', 'Test device'),
                 ('underrun_api', 'no' if mode == 'noapi' else 'yes'), ('items', '40'), ('tracks', '8')]
        lines.append(('underrun_setup', 'unavailable' if mode == 'noapi' else '0 0 500'))
        for n in range(1, 5):
            lines += [(f'fx_name_{n}', 'VST3: Convolver (Convolver)'), (f'fx_type_{n}', 'VST3'),
                      (f'preset_applied_{n}', 'true'), (f'status_ready_{n}', loaded)]
        lines += [('underrun_before', 'unavailable' if mode == 'noapi' else '0 0 1000'), ('play_started', 'yes'),
                  ('underrun_playing', '3000 0 1100' if mode == 'start_underrun' else '0 0 1100'),
                  ('device_rate_playing', '48000'), ('status_readings', '40'),
                  ('status_other', '1' if mode == 'fault' else '0')]
        if mode == 'fault':
            lines.append(('status_change_1', '30.0 s, Reverb 2: Fault: output too large.'))
        if mode == 'underrun':
            lines += [('underrun_1', '20.0 s: audio 3000, media 0'), ('underrun_2', '80.0 s: audio 9000, media 0')]
        if mode == 'stopped':
            lines.append(('transport_stopped_at', f'{ran:.1f}'))
        else:
            lines += [('replacement_at', f'{duration / 2:.1f}'),
                      ('replacement_applied', 'false' if mode == 'refused' else 'true'),
                      ('replacement_status', loaded), ('replacement_settled_after', '2.0')]
        lines += [('soak_seconds', f'{ran:.1f}'), ('play_state', '0' if mode == 'stopped' else '1'),
                  ('play_position', f'{ran:.3f}'), ('underrun_after', 'unavailable' if mode == 'noapi' else after)]
        lines += [(f'status_end_{n}', loaded) for n in range(1, 5)]
        state = presets[0] if mode == 'old_state' else config['replacement']
        size = 'unavailable' if mode == 'nostate' else str(os.path.getsize(state) * 4 // 3)
        if mode == 'huge_state':
            size = str(os.path.getsize(state) * 200 * 4 // 3)
        lines.append(('state_chars_1', size))
        open(config['project'], 'w').write('<REAPER_PROJECT\n>\n')
        lines += [('project_saved', 'yes'), ('done', 'yes')]
        with open(config['results'], 'w') as f:
            for key, value in lines:
                f.write(key + '\t' + value + '\n')

    if mode == 'exit':
        sys.exit(3)
    if 'record("stage", "soak")' in text:
        presets = re.findall(r'"((?:[^"\\]|\\.)*)"', re.search(r'^  presets = \{(.*)\},$', text, re.M).group(1))
        soak(config, mode, float(re.search(r'^  duration = ([0-9.e+-]+),$', text, re.M).group(1)), presets)
        sys.exit(0)
    reopen = 'record("stage", "reopen")' in text
    loaded = 'Filter loaded: 48000 Hz, 2 in, 2 out.'
    status = 'Silent: no filter' if reopen and mode == 'silent' else loaded
    if mode == 'dry_loaded':
        status = loaded + ' Mix 0%: only the dry signal plays.'
    playing = {'waiting': 'Filter loaded: 48000 Hz, 2 in, 2 out; waiting for audio settings from the host.',
               'dry': 'Filter loaded: 48000 Hz, 2 in, 2 out. Mix 0%: only the dry signal plays.',
               'dry_loaded': loaded}.get(mode, status)
    lines = [('stage', 'reopen' if reopen else 'first'), ('reaper_version', '7.99/macOS-arm64'),
             ('fx_name', 'VST3: Convolver (Convolver)'), ('status_playing', playing),
             ('status_reopened' if reopen else 'status_after_preset', status),
             ('play_state', '0' if mode == 'stopped' else '1'), ('play_position', '1.500')]
    if reopen:
        opened = next(a for a in sys.argv[1:] if a.endswith('.rpp'))
        lines.append(('project_opened', '/elsewhere/other.rpp' if mode == 'other' else opened))
    else:
        lines.append(('preset_applied', 'false' if mode == 'refused' else 'true'))
    shutil.copy(os.environ['FAKE_RENDER'], os.path.join(config['output'], config['render'] + '.wav'))
    open(config['project'], 'w').write('<REAPER_PROJECT\n>\n')
    lines.append(('done', 'yes'))
    with open(config['results'], 'w') as f:
        for key, value in lines:
            f.write(key + '\t' + value + '\n')
    """
).lstrip()

# A stand-in convolver: --compare matches when the files are byte-identical.
FAKE_CLI = textwrap.dedent(
    """
    #!/usr/bin/env python3
    import sys
    if sys.argv[1] == '--version':
        print('convolver 0.0.0 (test), FFT: test')
        sys.exit(0)
    same = open(sys.argv[2], 'rb').read() == open(sys.argv[3], 'rb').read()
    print('Match' if same else 'Different: another level')
    sys.exit(0 if same else 1)
    """
).lstrip()


# A stand-in convolver-preset: writes a preset named after its source, and
# fails for a source whose name contains "refuse".
FAKE_PRESET_TOOL = textwrap.dedent(
    """
    #!/usr/bin/env python3
    import sys
    if 'refuse' in sys.argv[1]:
        print('convolver-preset: the filter is damaged')
        sys.exit(1)
    # The replacement is a 20 s filter, the replaced one 10 s: sizes differ.
    size = 2000 if 'replacement' in sys.argv[1] else 500
    open(sys.argv[2], 'w').write('preset of ' + sys.argv[1] + ' ' * size)
    """
).lstrip()


def executable(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


class LuaText(unittest.TestCase):
    def test_strings_survive_quotes_backslashes_and_newlines(self):
        self.assertEqual(reaper_check.lua_string('a "b"\\c\nd'), '"a \\"b\\"\\\\c\\nd"')

    def test_a_nul_byte_is_refused(self):
        with self.assertRaises(ValueError):
            reaper_check.lua_string('a\0b')

    def test_config_values(self):
        text = reaper_check.lua_config({'rate': 48000, 'names': ['x', 'y'], 'flag': True, 'path': '/p q'})
        self.assertIn('  rate = 48000,', text)
        self.assertIn('  names = {"x", "y"},', text)
        self.assertIn('  flag = true,', text)
        self.assertIn('  path = "/p q",', text)

    def test_each_stage_script_is_complete(self):
        for stage in ('first', 'reopen'):
            text = reaper_check.lua_script(stage, {'results': '/r'})
            self.assertTrue(text.startswith('-- SPDX-License-Identifier: GPL-2.0-or-later\nlocal CONFIG = {'))
            self.assertTrue(text.rstrip().endswith('reaper.defer(tick)'))
        for stage in ('first', 'reopen'):
            # Status is read while the transport runs (jrp's first run read it stopped).
            # The project's own loop and repeat come back, so the saved project is unchanged.
            text = reaper_check.lua_script(stage, {})
            self.assertIn('reaper.GetSetRepeatEx(project, 1)', text)
            self.assertIn('reaper.GetSetRepeatEx(project, repeat_before)', text)
            self.assertIn('reaper.GetSet_LoopTimeRange2(project, true, true, loop_start, loop_end, false)', text)
        self.assertEqual(reaper_check.lua_script('first', {}).count('"D_PANLAW", 1)'), 2)
        self.assertIn('TrackFX_SetPreset', reaper_check.lua_script('first', {}))
        # A brief waiting text after loading or reopening is waited past.
        self.assertIn('text:find("aiting", 1, true) == nil', reaper_check.lua_script('reopen', {}))
        self.assertNotIn('TrackFX_SetPreset', reaper_check.lua_script('reopen', {}))

    def test_the_soak_script(self):
        text = reaper_check.lua_script('soak', {'results': '/r', 'items': [[1.0, 7.5], [2.3]]})
        self.assertTrue(text.rstrip().endswith('reaper.defer(tick)'))
        self.assertIn('  items = {{1.0, 7.5}, {2.3}},', text)
        # Underruns before and after, the replacement while playing, and the
        # sends from each source to its own return.
        for wanted in (
            'record_underrun_times("underrun_before")',
            'record_underrun_times("underrun_after")',
            'pcall(reaper.GetUnderrunTime)',
            'reaper.TrackFX_SetPreset(entry.track, entry.fx, CONFIG.replacement)',
            'reaper.CreateTrackSend(sources[index], track)',
            'reaper.GetSetRepeatEx(project, 0)',
        ):
            self.assertIn(wanted, text)


class Records(unittest.TestCase):
    def test_lua_escapes_are_undone(self):
        results = reaper_check.parse_results('status\tNo filter.\\nLoad\\ta \\\\ preset\nempty\t\n')
        self.assertEqual(results, {'status': 'No filter.\nLoad\ta \\ preset', 'empty': ''})

    def test_a_line_without_a_tab_is_ignored_and_the_last_value_wins(self):
        self.assertEqual(reaper_check.parse_results('half\nkey\t1\nkey\t2\n'), {'key': '2'})

    def test_a_device_at_another_rate_is_named(self):
        self.assertEqual(reaper_check.rate_hint({'device_rate': '48000', 'device_rate_playing': '48000'}), '')
        self.assertEqual(reaper_check.rate_hint({'device_rate': '(not reported)'}), '')
        self.assertIn('ran at 44100 Hz: set it to 48000 Hz', reaper_check.rate_hint({'device_rate': '44100'}))

    def test_only_a_reading_with_the_transport_running_counts_as_playing(self):
        loaded = reaper_check.ACTIVE
        self.assertTrue(reaper_check.loaded_while_playing({'play_state': '1', 'status_playing': loaded}))
        self.assertTrue(reaper_check.loaded_while_playing({'play_state': '5', 'status_playing': loaded}))
        for state in ('0', '2', '', 'x'):
            self.assertFalse(reaper_check.loaded_while_playing({'play_state': state, 'status_playing': loaded}))
        self.assertFalse(reaper_check.loaded_while_playing({'status_playing': loaded}))
        self.assertIn('did not start within 10 s', reaper_check.playing_text({'play_state': '0', 'play_started': 'no'}))

    def test_only_the_whole_active_text_counts(self):
        # jrp's review: these start with the Active text but say the filter
        # is waiting, plays dry only, or has other labels.
        self.assertTrue(reaper_check.active('Filter loaded: 48000 Hz, 2 in, 2 out.'))
        for text in (
            'Filter loaded: 48000 Hz, 2 in, 2 out; waiting for audio settings from the host.',
            'Filter loaded: 48000 Hz, 2 in, 2 out. Mix 0%: only the dry signal plays.',
            'Filter loaded: 48000 Hz, 2 in, 2 out. Speaker labels differ.',
            'Filter loaded: 48000 Hz, 2 in, 2 out; plug-in input 3 ignored.',
            "Filter selected. Waiting for the host's audio settings.",
            '',
            None,
        ):
            self.assertFalse(reaper_check.active(text), text)

    def test_a_missing_file_records_nothing(self):
        self.assertEqual(reaper_check.read_results(pathlib.Path('/nonexistent/first.txt')), {})


class SoakMaterial(unittest.TestCase):
    def test_a_float_wav(self):
        data = reaper_check.wav_bytes(48000, 2, 2, [0.5, -0.5, 0.25, 0.0])
        self.assertEqual(data[:4], b'RIFF')
        self.assertEqual(data[8:16], b'WAVEfmt ')
        self.assertEqual(struct.unpack('<HHIIHH', data[20:36]), (3, 2, 48000, 384000, 8, 32))
        self.assertEqual(struct.unpack('<4f', data[44:]), (0.5, -0.5, 0.25, 0.0))
        with self.assertRaises(ValueError):
            reaper_check.wav_bytes(48000, 2, 3, [0.0])

    def test_a_reverb_decays_60_db_with_the_stated_energy(self):
        frames, samples = reaper_check.reverb_samples(0.5, 4, seed=7)
        self.assertEqual((frames, len(samples)), (24000, 96000))
        for path in range(4):
            channel = samples[path::4]
            self.assertAlmostEqual(sum(v * v for v in channel), reaper_check.SOAK_PATH_ENERGY, delta=0.02)
            start = max(abs(v) for v in channel[:480])
            end = max(abs(v) for v in channel[-480:])
            self.assertLess(end / start, 0.0015)
        self.assertEqual(reaper_check.reverb_samples(0.1, 2, seed=7), reaper_check.reverb_samples(0.1, 2, seed=7))

    def test_a_true_stereo_configuration_routes_each_input_to_both_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            config = reaper_check.write_reverb(pathlib.Path(directory), 'room', 0.01, 'true stereo', 1)
            self.assertEqual(config.name, 'room.cfg')
            lines = [line for line in config.read_text().splitlines() if not line.startswith('//')]
            self.assertEqual(lines[:3], ['48000 2 2 3', '0 0', '0 0'])
            paths = [lines[i : i + 4] for i in range(3, len(lines), 4)]
            self.assertEqual(
                paths,
                [
                    ['room.wav', '0', '0', '0'],
                    ['room.wav', '1', '0', '1'],
                    ['room.wav', '2', '1', '0'],
                    ['room.wav', '3', '1', '1'],
                ],
            )
            stereo = reaper_check.write_reverb(pathlib.Path(directory), 'hall', 0.01, 'stereo', 1)
            self.assertEqual(stereo.name, 'hall.wav')

    def test_each_sound_peaks_at_minus_12_db(self):
        for kind in reaper_check.SOAK_SOUNDS:
            frames, samples = reaper_check.sound_samples(kind)
            self.assertEqual(len(samples), 2 * frames)
            self.assertAlmostEqual(max(abs(v) for v in samples), 0.25, places=6, msg=kind)

    def test_the_schedule_keeps_the_quiet_windows_silent_and_runs_past_the_end(self):
        duration = 1800
        windows = reaper_check.quiet_windows(duration)
        self.assertEqual(windows, [(450.0, 510.0), (1170.0, 1230.0)])
        schedule = reaper_check.soak_schedule(duration)
        self.assertEqual(len(schedule), 4)
        self.assertEqual(schedule, reaper_check.soak_schedule(duration))
        for times in schedule:
            self.assertGreaterEqual(times[-1] + reaper_check.SOAK_SOUND_SECONDS, duration + 5)
            for start, following in zip(times, times[1:]):
                self.assertGreaterEqual(following - start, reaper_check.SOAK_SOUND_SECONDS + 3)
            for start in times:
                for begin, end in windows:
                    self.assertFalse(start < end and start + reaper_check.SOAK_SOUND_SECONDS > begin, (start, begin))
        # The replacement, halfway, falls between the quiet windows.
        self.assertTrue(windows[0][1] < duration / 2 < windows[1][0])
        # A short trial keeps windows in proportion.
        self.assertEqual(reaper_check.quiet_windows(120), [(30.0, 42.0), (78.0, 90.0)])


class ReplacementState(unittest.TestCase):
    sizes = (3_840_258, 15_360_319)  # the 10 s stereo and 20 s true-stereo presets

    def check(self, chars):
        return reaper_check.replacement_state({'state_chars_1': str(chars)}, self.sizes)

    def test_only_a_state_the_replacements_size_counts(self):
        self.assertTrue(self.check(15_360_000 * 4 // 3)[0])
        self.assertTrue(self.check(14_000_000 * 4 // 3)[0])
        self.assertFalse(self.check(3_840_000 * 4 // 3)[0])
        # jrp's review: a 750 MB state is nearer the replacement than the
        # original, but is neither.
        passed, evidence = self.check(750_000_000 * 4 // 3)
        self.assertFalse(passed)
        self.assertIn(
            '750.00 MB, the replacement preset 15.36 MB and the replaced one 3.84 MB: not within 10%', evidence
        )
        self.assertFalse(self.check(0)[0])

    def test_presets_too_close_in_size_cannot_tell_the_filters_apart(self):
        passed, evidence = reaper_check.replacement_state({'state_chars_1': '1333'}, (950, 1000))
        self.assertFalse(passed)
        self.assertIn('too close in size to tell apart', evidence)


class Underruns(unittest.TestCase):
    def result(self, **results):
        return reaper_check.underrun_result(results)[0]

    def test_the_three_results(self):
        self.assertEqual(self.result(underrun_before='0 0 1000', underrun_after='0 0 9000'), 'no new underrun')
        self.assertEqual(
            self.result(underrun_before='0 0 1000', underrun_after='5000 0 9000'), 'at least one new underrun'
        )
        self.assertEqual(
            self.result(underrun_before='0 70 1000', underrun_after='0 900 9000'), 'at least one new underrun'
        )
        self.assertEqual(
            self.result(underrun_api='no', underrun_before='unavailable', underrun_after='unavailable'),
            'underrun reporting unavailable',
        )
        self.assertEqual(self.result(underrun_before='0 0 1000'), 'underrun reporting unavailable')
        # A clock that did not advance cannot show that nothing happened.
        self.assertEqual(
            self.result(underrun_before='0 0 1000', underrun_after='0 0 1000'), 'underrun reporting unavailable'
        )

    def test_an_underrun_seen_during_the_soak_counts_even_if_the_times_end_equal(self):
        # The last-underrun time can wrap or be reset; a sighting still counts.
        result, evidence = reaper_check.underrun_result(
            {'underrun_before': '10 0 1000', 'underrun_after': '10 0 9000', 'underrun_1': '5.0 s: audio 4000, media 0'}
        )
        self.assertEqual(result, 'at least one new underrun')
        self.assertIn('Seen at: 5.0 s: audio 4000, media 0.', evidence)

    def test_earlier_underruns_do_not_count(self):
        # An underrun before the soak (while the filters loaded) leaves the
        # same last time before and after.
        self.assertEqual(self.result(underrun_before='800 0 1000', underrun_after='800 0 9000'), 'no new underrun')


class Run(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        base = pathlib.Path(self.directory.name)
        self.package = base / 'Convolver-0.0.0-preview-macos-arm64'
        for path in (reaper_check.INPUT, reaper_check.PRESET):
            (self.package / path).parent.mkdir(parents=True, exist_ok=True)
            (self.package / path).write_bytes(b'x')
        (self.package / reaper_check.EXPECTED).write_bytes(b'expected samples')
        executable(self.package / reaper_check.CLI, FAKE_CLI)
        self.reaper = base / 'REAPER'
        executable(self.reaper, FAKE_REAPER)
        (base / 'Convolver-0.0.0-preview-macos-arm64.zip').write_bytes(b'zip')
        self.log = base / 'Convolver.log'
        self.log.write_text('before the run\n')
        patches = [
            mock.patch.object(reaper_check, 'LOG', self.log),
            mock.patch.object(reaper_check, 'BUNDLES', (base / 'missing.vst3',)),
            mock.patch.object(reaper_check, 'reaper_running', lambda: False),
            mock.patch.dict(os.environ, {'FAKE_RENDER': str(self.package / reaper_check.EXPECTED)}),
        ]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)

    def run_check(self, *extra):
        with mock.patch('sys.stdout'):
            status = reaper_check.main(['--package', str(self.package), '--reaper', str(self.reaper), *extra])
        folders = sorted(self.package.glob('reaper-check-*'))
        self.assertEqual(len(folders), 1)
        return status, (folders[0] / 'result.md').read_text()

    def test_a_passing_run(self):
        status, result = self.run_check()
        self.assertEqual(status, 0, result)
        self.assertIn('**PASS: 8 of 8 checks passed.**', result)
        self.assertNotIn('FAIL', result)
        # What every host report lists (docs/develop/host-testing.md#every-report).
        for wanted in (
            '- REAPER: 7.99/macOS-arm64',
            '- macOS: ',
            'CPU: ',
            '- Package SHA-256: `',
            'FFT: test',
            '- Installed VST3: none found',
            'Hz; audio device: ',
            'block size',
            '- Channels: ',
            '## Steps',
            '## Convolver log',
        ):
            self.assertIn(wanted, result)
        self.assertRegex(result, re.escape(f'(`{self.package.name}.zip`)'))
        self.assertIn('quit (by itself)', result)
        self.assertIn('transport running, at 1.500 s', result)

    def test_reaper_that_never_runs_the_script(self):
        with mock.patch.dict(os.environ, {'FAKE_REAPER': 'exit'}):
            status, result = self.run_check()
        self.assertEqual(status, 1)
        self.assertIn('**FAIL: 0 of 8 checks passed.**', result)
        self.assertIn('REAPER exited (status 3) before the ReaScript finished.', result)
        self.assertIn('No render: host-render.wav was not written.', result)

    def test_a_reopened_project_that_lost_its_filter(self):
        with mock.patch.dict(os.environ, {'FAKE_REAPER': 'silent'}):
            status, result = self.run_check()
        self.assertEqual(status, 1)
        self.assertIn('| 7 | The reopened project restores the filter | **FAIL** | Status on reopening: Silent', result)
        self.assertIn('**FAIL: 7 of 8 checks passed.**', result)

    def test_status_read_after_the_transport_stopped(self):
        # jrp's first run (21 November 2026): both renders matched, but both
        # readings "while playing" were taken with the transport stopped.
        with mock.patch.dict(os.environ, {'FAKE_REAPER': 'stopped'}):
            status, result = self.run_check()
        self.assertEqual(status, 1)
        self.assertIn('transport stopped, so this is not a reading during playback', result)
        self.assertIn('| 3 | The .vstpreset loads the stereo example | **FAIL** |', result)
        self.assertIn('| 7 | The reopened project restores the filter | **FAIL** |', result)
        self.assertIn('**FAIL: 6 of 8 checks passed.**', result)

    def test_a_waiting_or_dry_only_status_while_playing(self):
        # jrp's review: with the transport running, a filter still waiting for
        # the host's settings, or Mix 0%, must not pass checks 3 and 7.
        # dry_loaded: the readings after loading and reopening say Mix 0%.
        for mode in ('waiting', 'dry', 'dry_loaded'):
            with self.subTest(mode=mode), mock.patch.dict(os.environ, {'FAKE_REAPER': mode}):
                for folder in self.package.glob('reaper-check-*'):
                    shutil.rmtree(folder)
                status, result = self.run_check()
                self.assertEqual(status, 1)
                self.assertIn('transport running', result)
                self.assertIn('| 3 | The .vstpreset loads the stereo example | **FAIL** |', result)
                self.assertIn('| 7 | The reopened project restores the filter | **FAIL** |', result)
                self.assertIn('**FAIL: 6 of 8 checks passed.**', result)

    def test_a_preset_call_that_failed(self):
        # Status and both renders look right, but REAPER said the preset did
        # not load: the run cannot claim it loaded (jrp's review).
        with mock.patch.dict(os.environ, {'FAKE_REAPER': 'refused'}):
            status, result = self.run_check()
        self.assertEqual(status, 1)
        self.assertIn(
            '| 3 | The .vstpreset loads the stereo example | **FAIL** | TrackFX_SetPreset returned false', result
        )
        self.assertIn('**FAIL: 7 of 8 checks passed.**', result)

    def test_another_project_reopened(self):
        # The second REAPER reports a project other than the one saved: the
        # renders match, but the save and reopen is not shown (jrp's review).
        with mock.patch.dict(os.environ, {'FAKE_REAPER': 'other'}):
            status, result = self.run_check()
        self.assertEqual(status, 1)
        self.assertRegex(
            result, r'\| 6 \| REAPER reopened the saved project [^|]*\| \*\*FAIL\*\* \|[^|]*/elsewhere/other\.rpp, not '
        )
        self.assertIn('**FAIL: 7 of 8 checks passed.**', result)

    def test_the_saved_project_is_recognised_through_a_link(self):
        base = pathlib.Path(self.directory.name)
        project = base / 'saved.rpp'
        project.write_text('x')
        link = base / 'link'
        link.symlink_to(base, target_is_directory=True)
        self.assertTrue(reaper_check.same_file(str(link / 'saved.rpp'), project))
        self.assertFalse(reaper_check.same_file(str(base / 'missing.rpp'), project))
        self.assertFalse(reaper_check.same_file('', project))

    def test_a_render_that_differs(self):
        os.environ['FAKE_RENDER'] = str(self.package / reaper_check.INPUT)
        status, result = self.run_check()
        self.assertEqual(status, 1)
        self.assertIn('| 4 | The render matches expected.wav | **FAIL** | Different: another level |', result)

    def test_only_what_the_log_gained_is_reported(self):
        original = reaper_check.run_stage

        def stage(*args, **kwargs):
            with self.log.open('a') as log:
                log.write('during the run\n')
            return original(*args, **kwargs)

        with mock.patch.object(reaper_check, 'run_stage', stage):
            _, result = self.run_check()
        self.assertIn('during the run', result)
        self.assertNotIn('before the run', result)

    def run_soak(self, mode='pass', *extra):
        executable(self.package / reaper_check.PRESET_TOOL, FAKE_PRESET_TOOL)
        reverbs = tuple((name, 0.02, layout) for name, _, layout in reaper_check.SOAK_REVERBS)
        patches = (
            mock.patch.object(reaper_check, 'SOAK_REVERBS', reverbs),
            mock.patch.object(reaper_check, 'SOAK_REPLACEMENT', ('true-stereo-replacement', 0.02, 'true stereo')),
            mock.patch.dict(os.environ, {'FAKE_REAPER': mode}),
            mock.patch('sys.stdout'),
        )
        with contextlib.ExitStack() as stack:
            for patch in patches:
                stack.enter_context(patch)
            status = reaper_check.main(
                ['--package', str(self.package), '--reaper', str(self.reaper), '--soak', '--soak-minutes', '2', *extra]
            )
        folders = sorted(self.package.glob('reaper-soak-*'))
        self.assertEqual(len(folders), 1)
        result = (folders[0] / 'result.md').read_text()
        if os.environ.get('KEEP_SOAK'):
            print(result, file=sys.stderr)
        shutil.rmtree(folders[0])
        return status, result, folders[0]

    def test_a_soak_with_no_new_underrun(self):
        status, result, _ = self.run_soak()
        self.assertEqual(status, 0, result)
        self.assertIn('**Result: no new underrun.** 6 of 6 checks passed.', result)
        self.assertNotIn('FAIL', result)
        for wanted in (
            '- REAPER: 7.99/macOS-arm64',
            '- Package SHA-256: `',
            'FFT: test',
            'block size 64',
            '- Channels: 4 stereo source tracks',
            'Reverb 1: stereo-10s',
            'true-stereo-30s',
            'All sources were silent from 30 to 42 s, 78 to 90 s',
            'At 60.0 s, while playing, it loaded true-stereo-replacement',
            "within 10% of the replacement's size",
            'Underruns while the filters loaded: none reported.',
            'REAPER reported no audio underrun at any time in this session',
            "REAPER's anticipative FX settings",
            '; its state after the soak is ',
            '78 to 90 s, longer than the longest tail.',
            '## Convolver log',
        ):
            self.assertIn(wanted, result)

    def test_each_soak_result_and_failure(self):
        cases = {
            'underrun': ('at least one new underrun', 6, 'Seen at: 20.0 s: audio 3000, media 0; 80.0 s'),
            'noapi': ('underrun reporting unavailable', 6, 'This REAPER has no GetUnderrunTime.'),
            'frozen': ('underrun reporting unavailable', 6, 'did not advance'),
            'big_block': (None, 5, 'Set the block size to 128 or fewer'),
            'stopped': (None, 4, 'transport stopped after 40.0 s of 120 s'),
            'exit': (None, 0, 'No underrun times were read after it.'),
            'old_state': (
                'no new underrun',
                5,
                'filter while playing loaded the new one | **FAIL** | at 60.0 s TrackFX_SetPreset returned true',
            ),
            'nostate': ('no new underrun', 5, 'so the new filter cannot be told from the old'),
            'huge_state': ('no new underrun', 5, "not within 10% of the replacement's size"),
            'start_underrun': ('at least one new underrun', 6, 'The only change came as playback started'),
            'fault': ('no new underrun', 5, 'changes: 30.0 s, Reverb 2: Fault: output too large.'),
            'refused': ('no new underrun', 5, 'TrackFX_SetPreset returned false'),
        }
        # No result (None) for a soak that did not run as specified.
        for mode, (wanted, passed, evidence) in cases.items():
            with self.subTest(mode=mode):
                status, result, _ = self.run_soak(mode)
                self.assertEqual(status, 1, result)
                # The steps say the replacement loaded only when check 6 passed (jrp's review).
                if mode in ('refused', 'old_state', 'nostate', 'huge_state'):
                    self.assertIn('it tried to load (check 6 says what happened) true-stereo-replacement', result)
                    self.assertNotIn('it loaded true-stereo-replacement', result)
                headline = '**No result.**' if wanted is None else f'**Result: {wanted}.**'
                self.assertIn(f'{headline} {passed} of 6 checks passed.', result)
                self.assertIn(evidence, result)

    def test_a_soak_whose_presets_cannot_be_made(self):
        reverbs = (('refuse-me', 0.01, 'stereo'),)
        executable(self.package / reaper_check.PRESET_TOOL, FAKE_PRESET_TOOL)
        with mock.patch.object(reaper_check, 'SOAK_REVERBS', reverbs), mock.patch('sys.stdout') as out:
            status = reaper_check.main(['--package', str(self.package), '--reaper', str(self.reaper), '--soak'])
        self.assertEqual(status, 2)
        self.assertIn('the filter is damaged', ''.join(str(call) for call in out.mock_calls))

    def test_a_soak_needs_the_preset_tool_not_the_example(self):
        with mock.patch('sys.stdout'):
            self.assertEqual(
                reaper_check.main(['--package', str(self.package), '--reaper', str(self.reaper), '--soak']), 2
            )
        self.assertEqual(list(self.package.glob('reaper-soak-*')), [])

    def test_nothing_starts_while_reaper_runs_or_outside_a_package(self):
        with mock.patch.object(reaper_check, 'reaper_running', lambda: True), mock.patch('sys.stdout'):
            self.assertEqual(reaper_check.main(['--package', str(self.package), '--reaper', str(self.reaper)]), 2)
        with mock.patch('sys.stdout'):
            self.assertEqual(reaper_check.main(['--package', self.directory.name, '--reaper', str(self.reaper)]), 2)
        self.assertEqual(list(self.package.glob('reaper-check-*')), [])


if __name__ == '__main__':
    unittest.main()
