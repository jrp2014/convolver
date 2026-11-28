#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Behaviour tests for scripts/maintenance/host_doctor.py (HOST-14).

The contract under test: the doctor finds each installed Convolver plug-in in
the folders hosts scan, with its format, version and scope, from the bundle
files the build writes (the Info.plist templates and the LV2 Turtle files);
it names duplicates, mixed versions and installs older than the download or
--expect; it tells "loaded now" (a process has the bundle mapped) from
"installed"; and it changes nothing on disk.
"""

import contextlib
import importlib.util
import io
import json
import mmap
import os
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts' / 'maintenance' / 'host_doctor.py'
IDENTITIES = json.loads((ROOT / 'plugins' / 'identities.json').read_text(encoding='utf-8'))

spec = importlib.util.spec_from_file_location('host_doctor', SCRIPT)
doctor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(doctor)


def info_plist(template, version):
    """The bundle's Info.plist, from the template the build configures."""
    text = (ROOT / 'plugins' / template / 'Info.plist.in').read_text(encoding='utf-8')
    values = {
        'Convolver_VERSION': version,
        'CONVOLVER_ID_VST3_BUNDLE': IDENTITIES['vst3']['macos_bundle_id'],
        'CONVOLVER_ID_AU_BUNDLE': IDENTITIES['au']['macos_bundle_id'],
        'CONVOLVER_ID_AU_TYPE': IDENTITIES['au']['type'],
        'CONVOLVER_ID_AU_SUBTYPE': IDENTITIES['au']['subtype'],
        'CONVOLVER_ID_AU_MANUFACTURER': IDENTITIES['au']['manufacturer'],
        'CONVOLVER_ID_VENDOR': IDENTITIES['vendor']['name'],
        'CONVOLVER_AU_VERSION': '1',
    }
    for key, value in values.items():
        text = text.replace('${' + key + '}', value)
    return text


class Doctor(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='convolver-host-doctor-')
        self.base = Path(self.tmp.name)
        self.home = self.base / 'home'
        self.system = self.base / 'system'
        self.downloads = self.base / 'downloads'
        self.proc = self.base / 'proc'
        for folder in (self.home, self.system, self.downloads, self.proc):
            folder.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def mac_bundle(self, folder, name, template, version):
        bundle = folder / name
        (bundle / 'Contents' / 'MacOS').mkdir(parents=True)
        (bundle / 'Contents' / 'Info.plist').write_text(info_plist(template, version), encoding='utf-8')
        (bundle / 'Contents' / 'MacOS' / 'Convolver').write_bytes(b'\0' * 64)
        return bundle

    def vst3(self, version, scope='user', name='Convolver.vst3'):
        root = self.home if scope == 'user' else self.system
        return self.mac_bundle(root / 'Library/Audio/Plug-Ins/VST3', name, 'vst3', version)

    def au(self, version, scope='user'):
        root = self.home if scope == 'user' else self.system
        return self.mac_bundle(root / 'Library/Audio/Plug-Ins/Components', 'Convolver.component', 'au', version)

    def lv2(self, version):
        bundle = self.home / '.lv2' / 'convolver.lv2'
        version_file = self.base / 'VERSION'
        version_file.write_text(version + '\n', encoding='utf-8')
        subprocess.run(
            [
                sys.executable,
                str(ROOT / 'plugins' / 'lv2' / 'generate_ttl.py'),
                str(ROOT / 'plugins' / 'identities.json'),
                str(version_file),
                'convolver.so',
                str(bundle),
            ],
            check=True,
            capture_output=True,
        )
        (bundle / 'convolver.so').write_bytes(b'\0' * 64)
        return bundle

    def snapshot(self):
        return sorted(
            (str(path.relative_to(self.base)), path.stat().st_mtime_ns, path.stat().st_size)
            for path in self.base.rglob('*')
        )

    def run_doctor(self, *args, platform='mac'):
        out = io.StringIO()
        argv = [
            '--platform',
            platform,
            '--home',
            str(self.home),
            '--system-root',
            str(self.system),
            '--downloads',
            str(self.downloads),
            '--proc',
            str(self.proc),
            *args,
        ]
        before = self.snapshot()
        environment = {key: value for key, value in os.environ.items() if key != 'LV2_PATH'}
        with contextlib.redirect_stdout(out), unittest.mock.patch.dict(os.environ, environment, clear=True):
            status = doctor.main(argv)
        self.assertEqual(before, self.snapshot(), 'the doctor must change nothing')
        return status, out.getvalue()

    def test_nothing_installed_is_a_problem(self):
        status, text = self.run_doctor()
        self.assertEqual(status, 1, text)
        self.assertIn('No Convolver plug-in is installed', text)

    def test_one_clean_install_passes(self):
        self.vst3('0.19.21')
        self.au('0.19.21')
        (self.downloads / 'Convolver-0.19.21-macos-arm64.zip').write_bytes(b'')
        status, text = self.run_doctor()
        self.assertEqual(status, 0, text)
        self.assertRegex(text, r'VST3\s+0\.19\.21\s+user\s+\S*Library/Audio/Plug-Ins/VST3/Convolver\.vst3')
        self.assertRegex(text, r'AU\s+0\.19\.21\s+user\s+\S*Components/Convolver\.component')
        self.assertIn('not loaded by any running program', text)
        self.assertIn('Nothing found that would spoil a host test', text)

    def test_older_install_than_the_download_is_named(self):
        # The Audacity test of 27 November 2026: 0.19.21 downloaded, 0.18.23 installed.
        self.vst3('0.18.23')
        (self.downloads / 'Convolver-0.19.21-macos-arm64.zip').write_bytes(b'')
        (self.downloads / 'Convolver-0.18.29-preview-macos-arm64').mkdir()
        status, text = self.run_doctor()
        self.assertEqual(status, 1, text)
        self.assertIn('is 0.18.23, older than the download Convolver-0.19.21-macos-arm64.zip', text)

    def test_duplicates_and_mixed_versions_are_named(self):
        self.vst3('0.19.21')
        self.vst3('0.18.23', scope='system')
        self.vst3('0.17.0', name='Convolver old.vst3')
        self.au('0.19.21')
        status, text = self.run_doctor()
        self.assertEqual(status, 1, text)
        self.assertIn('VST3 is installed 3 times; a host may load any of them', text)
        self.assertRegex(text, r'0\.18\.23\s+system\s+\S*/system/Library/Audio/Plug-Ins/VST3/Convolver\.vst3')
        self.assertIn('this tool removes nothing', text)
        self.assertIn('not all the same version', text)
        self.assertNotIn('AU is installed', text)

    def test_expect_overrides_the_download(self):
        self.vst3('0.19.21')
        (self.downloads / 'Convolver-0.19.30-macos-arm64.zip').write_bytes(b'')
        status, text = self.run_doctor('--expect', 'v0.19.21')
        self.assertEqual(status, 0, text)
        status, text = self.run_doctor('--expect', '0.19.22')
        self.assertEqual(status, 1, text)
        self.assertIn('is 0.19.21, not the expected 0.19.22', text)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            self.run_doctor('--expect', 'latest')

    def test_other_plugins_and_unreadable_versions(self):
        self.mac_bundle(self.home / 'Library/Audio/Plug-Ins/VST3', 'Reverb.vst3', 'vst3', '1.0.0')
        plist = self.home / 'Library/Audio/Plug-Ins/VST3/Reverb.vst3/Contents/Info.plist'
        plist.write_text(plist.read_text().replace(IDENTITIES['vst3']['macos_bundle_id'], 'com.example.reverb'))
        broken = self.vst3('0.19.21')
        (broken / 'Contents' / 'Info.plist').write_text('not a plist')
        status, text = self.run_doctor()
        self.assertEqual(status, 1, text)
        self.assertNotIn('Reverb', text)
        self.assertIn('its version cannot be read', text)

    def test_linux_lv2_version_from_the_generated_turtle(self):
        self.lv2('0.19.21')
        status, text = self.run_doctor(platform='linux')
        self.assertEqual(status, 0, text)
        self.assertRegex(text, r'LV2\s+0\.19\.21\s+user\s+\S*/\.lv2/convolver\.lv2')
        self.assertEqual(doctor.version_tuple('1.0.0'), (1, 0, 0))

    def test_linux_vst3_version_from_moduleinfo(self):
        bundle = self.home / '.vst3' / 'Convolver.vst3' / 'Contents' / 'Resources'
        bundle.mkdir(parents=True)
        (bundle / 'moduleinfo.json').write_text(
            '{\n  "Name": "Convolver",\n  "Version": "0.19.21",\n  "Classes": [\n'
            f'    {{ "CID": "{IDENTITIES["vst3"]["processor_class_id"]}", }},\n  ],\n}}\n',
            encoding='utf-8',
        )
        status, text = self.run_doctor(platform='linux')
        self.assertEqual(status, 0, text)
        self.assertRegex(text, r'VST3\s+0\.19\.21\s+user')

    def test_loaded_now_comes_from_mapped_files(self):
        bundle = self.lv2('0.19.21')
        library = bundle / 'convolver.so'
        process = self.proc / '4242'
        process.mkdir()
        (process / 'comm').write_text('ardour\n')
        (process / 'maps').write_text(
            f'7f00-7f01 r-xp 00000000 08:01 12 {library}\n7f01-7f02 r--p 00000000 08:01 13 /usr/lib/libc.so.6\n'
        )
        status, text = self.run_doctor(platform='linux')
        self.assertEqual(status, 0, text)
        self.assertIn('loaded now by ardour (pid 4242)', text)

    def test_loaded_now_from_a_real_process(self):
        if not Path('/proc/self/maps').exists():
            self.skipTest('no /proc')
        library = self.lv2('0.19.21') / 'convolver.so'
        with library.open('rb') as handle, mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ):
            opened, how = doctor.open_files('linux')
            self.assertEqual(how, '/proc')
            users = doctor.loaded_by(library.parent, opened)
            self.assertIn(str(os.getpid()), {pid for pid, _ in users})
        # Once the file is closed and unmapped, nothing has it loaded.
        self.assertEqual(doctor.loaded_by(library.parent, doctor.open_files('linux')[0]), [])

    def test_lsof_output_is_read(self):
        # The form `lsof -F pcn` prints on macOS: p, c, then one n per file.
        text = (
            'p812\ncREAPER\nn/Applications/REAPER.app/Contents/MacOS/REAPER\n'
            'n/Users/jrp/Library/Audio/Plug-Ins/VST3/Convolver.vst3/Contents/MacOS/Convolver\n'
            'p900\ncAUHostingServiceXPC_arrow\n'
            'n/Library/Audio/Plug-Ins/Components/Convolver.component/Contents/MacOS/Convolver\n'
        )
        opened = doctor.parse_lsof(text)
        bundle = Path('/Users/jrp/Library/Audio/Plug-Ins/VST3/Convolver.vst3')
        self.assertEqual(doctor.loaded_by(bundle, opened), [('812', 'REAPER')])
        component = Path('/Library/Audio/Plug-Ins/Components/Convolver.component')
        self.assertEqual(doctor.loaded_by(component, opened), [('900', 'AUHostingServiceXPC_arrow')])
        # A neighbour whose name starts the same is not the bundle.
        self.assertEqual(doctor.loaded_by(Path('/Users/jrp/Library/Audio/Plug-Ins/VST3/Convolver'), opened), [])


if __name__ == '__main__':
    unittest.main()
