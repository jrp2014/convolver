#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for scripts/reaper_check.py (HOST-11) without REAPER.

The script runs only on jrp's Mac, and each change to it costs him a run,
so these check in pre-commit what can be checked anywhere: the Lua it
writes, how it reads what the ReaScripts record, and a whole run against a
stand-in REAPER and a stand-in `convolver --compare`.
"""

import importlib.util
import os
import pathlib
import re
import stat
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
FAKE_REAPER = textwrap.dedent(
    r"""
    #!/usr/bin/env python3
    import os, re, shutil, sys
    script = next(a for a in sys.argv[1:] if a.endswith('.lua'))
    text = open(script, encoding='utf-8').read()
    config = dict(re.findall(r'^  (\w+) = "((?:[^"\\]|\\.)*)",$', text, re.M))
    mode = os.environ.get('FAKE_REAPER', 'pass')
    if mode == 'exit':
        sys.exit(3)
    reopen = 'record("stage", "reopen")' in text
    loaded = 'Filter loaded: 48000 Hz, 2 in, 2 out.'
    status = 'Silent: no filter' if reopen and mode == 'silent' else loaded
    lines = [('stage', 'reopen' if reopen else 'first'), ('reaper_version', '7.99/macOS-arm64'),
             ('fx_name', 'VST3: Convolver (Convolver)'), ('status_playing', status),
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
        self.assertNotIn('TrackFX_SetPreset', reaper_check.lua_script('reopen', {}))


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
        loaded = reaper_check.LOADED + '.'
        self.assertTrue(reaper_check.loaded_while_playing({'play_state': '1', 'status_playing': loaded}))
        self.assertTrue(reaper_check.loaded_while_playing({'play_state': '5', 'status_playing': loaded}))
        for state in ('0', '2', '', 'x'):
            self.assertFalse(reaper_check.loaded_while_playing({'play_state': state, 'status_playing': loaded}))
        self.assertFalse(reaper_check.loaded_while_playing({'status_playing': loaded}))
        self.assertIn('did not start within 10 s', reaper_check.playing_text({'play_state': '0', 'play_started': 'no'}))

    def test_a_missing_file_records_nothing(self):
        self.assertEqual(reaper_check.read_results(pathlib.Path('/nonexistent/first.txt')), {})


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

    def test_nothing_starts_while_reaper_runs_or_outside_a_package(self):
        with mock.patch.object(reaper_check, 'reaper_running', lambda: True), mock.patch('sys.stdout'):
            self.assertEqual(reaper_check.main(['--package', str(self.package), '--reaper', str(self.reaper)]), 2)
        with mock.patch('sys.stdout'):
            self.assertEqual(reaper_check.main(['--package', self.directory.name, '--reaper', str(self.reaper)]), 2)
        self.assertEqual(list(self.package.glob('reaper-check-*')), [])


if __name__ == '__main__':
    unittest.main()
