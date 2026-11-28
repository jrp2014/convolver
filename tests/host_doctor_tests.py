#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Behaviour tests for scripts/maintenance/host_doctor.py (HOST-14).

The contract under test: the doctor finds each installed Convolver plug-in in
the folders hosts scan, with its format, version and scope, from the bundle
files the build writes (the Info.plist templates and the LV2 Turtle files);
it names duplicates, mixed versions and installs older than the download or
--expect; only Convolver's identity counts as installed; it tells "loaded"
(a process has the current executable mapped) from "opened" (files read)
and from "found"; and it changes nothing on disk.
"""

import contextlib
import importlib.util
import io
import json
import mmap
import os
import re
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
        # Every path goes through a symbolic link, as macOS's /var does to
        # /private/var, so that aliases in a parent folder are covered.
        (Path(self.tmp.name) / 'real').mkdir()
        (Path(self.tmp.name) / 'alias').symlink_to('real')
        self.base = Path(self.tmp.name) / 'alias'
        # What lsof prints on the macOS side: one unrelated process.
        self.lsof = 'p1\nclaunchd\nftxt\ni1\nn/sbin/launchd\n'
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
        # On the macOS side the doctor runs lsof; it gets self.lsof instead, so
        # the result does not depend on this machine's tools or processes.
        lsof = unittest.mock.Mock(return_value=subprocess.CompletedProcess([], 0, self.lsof or '', ''))
        with contextlib.ExitStack() as stack:
            stack.enter_context(contextlib.redirect_stdout(out))
            stack.enter_context(unittest.mock.patch.dict(os.environ, environment, clear=True))
            found = '/usr/sbin/lsof' if self.lsof is not None else None
            stack.enter_context(unittest.mock.patch.object(doctor, 'find_lsof', return_value=found))
            stack.enter_context(unittest.mock.patch.object(doctor.subprocess, 'run', lsof))
            status = doctor.main(argv)
        self.assertEqual(before, self.snapshot(), 'the doctor must change nothing')
        return status, out.getvalue()

    def test_nothing_installed_is_a_problem(self):
        status, text = self.run_doctor()
        self.assertEqual(status, 1, text)
        self.assertIn("No plug-in with Convolver's identity is installed", text)

    def test_one_clean_install_passes(self):
        self.vst3('0.19.21')
        self.au('0.19.21')
        (self.downloads / 'Convolver-0.19.21-macos-arm64.zip').write_bytes(b'')
        status, text = self.run_doctor()
        self.assertEqual(status, 0, text)
        self.assertRegex(text, r'VST3\s+0\.19\.21\s+user\s+\S*Library/Audio/Plug-Ins/VST3/Convolver\.vst3')
        self.assertRegex(text, r'AU\s+0\.19\.21\s+user\s+\S*Components/Convolver\.component')
        self.assertIn('not seen loaded in the processes checked', text)
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

    def test_only_convolvers_identity_counts_as_installed(self):
        # jrp's review: a similarly named third-party bundle alone must not
        # pass, even with --expect.
        other = self.vst3('1.0.0', name='Convolver Pro.vst3')
        plist = other / 'Contents' / 'Info.plist'
        plist.write_text(plist.read_text().replace(IDENTITIES['vst3']['macos_bundle_id'], 'com.example.pro'))
        self.mac_bundle(self.home / 'Library/Audio/Plug-Ins/VST3', 'Reverb.vst3', 'vst3', '1.0.0')
        reverb = self.home / 'Library/Audio/Plug-Ins/VST3/Reverb.vst3/Contents/Info.plist'
        reverb.write_text(reverb.read_text().replace(IDENTITIES['vst3']['macos_bundle_id'], 'com.example.reverb'))
        status, text = self.run_doctor('--expect', '1.0.0')
        self.assertEqual(status, 1, text)
        self.assertIn("No plug-in with Convolver's identity is installed", text)
        self.assertIn('named Convolver, but carries another identity: not counted', text)
        self.assertNotIn('Reverb', text)
        # A bundle named Convolver whose identity cannot be read is a problem,
        # not an installation.
        broken = self.vst3('0.19.21')
        (broken / 'Contents' / 'Info.plist').write_text('not a plist')
        status, text = self.run_doctor('--expect', '1.0.0')
        self.assertEqual(status, 1, text)
        self.assertIn('Convolver.vst3: named Convolver, but its identity cannot be read', text)
        self.assertIn("No plug-in with Convolver's identity is installed", text)

    def test_another_bundle_still_loaded_is_not_a_problem(self):
        # HOST-16 (second audit of 0.22.0): a host still running an earlier
        # copy of another plug-in named Convolver is not Convolver's problem;
        # the listing leaves that bundle out of the checks.
        self.vst3('0.19.21')
        other = self.vst3('1.0.0', name='Convolver Pro.vst3')
        plist = other / 'Contents' / 'Info.plist'
        plist.write_text(plist.read_text().replace(IDENTITIES['vst3']['macos_bundle_id'], 'com.example.pro'))
        executable = other / 'Contents' / 'MacOS' / 'Convolver'
        self.lsof = f'p812\ncREAPER\nftxt\ni{executable.stat().st_ino + 1}\nn{executable}\n'
        status, text = self.run_doctor('--expect', '0.19.21')
        self.assertEqual(status, 0, text)
        self.assertNotIn('still has an earlier copy', text)
        self.assertIn('Nothing found that would spoil a host test', text)
        # The same for Convolver's own bundle is a problem.
        executable = self.home / 'Library/Audio/Plug-Ins/VST3/Convolver.vst3/Contents/MacOS/Convolver'
        self.lsof = f'p812\ncREAPER\nftxt\ni{executable.stat().st_ino + 1}\nn{executable}\n'
        status, text = self.run_doctor('--expect', '0.19.21')
        self.assertEqual(status, 1, text)
        self.assertIn('REAPER (pid 812) still has an earlier copy', text)

    def test_lv2_with_no_readable_turtle_is_unknown(self):
        # Codex's review: two empty or missing Turtle files must not read as
        # another plug-in's identity, which would leave the bundle unchecked.
        bundle = self.lv2('0.19.21')
        (bundle / 'manifest.ttl').write_text('')
        (bundle / 'convolver.ttl').unlink()
        status, text = self.run_doctor(platform='linux')
        self.assertEqual(status, 1, text)
        self.assertIn('convolver.lv2: named Convolver, but its identity cannot be read', text)

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

    def test_loaded_means_the_executable_is_mapped(self):
        bundle = self.lv2('0.19.21')
        library = bundle / 'convolver.so'
        inode = library.stat().st_ino
        maps = {
            '4242': ('ardour', f'7f00-7f01 r-xp 00000000 08:01 {inode} {library}\n'),
            # Mapping another file of the bundle is reading it, not loading.
            '77': ('indexer', f'7f00-7f01 r--p 00000000 08:01 9 {bundle}/convolver.ttl\n'),
            # So is mapping the library without execute permission (Codex's review).
            '78': ('scanner', f'7f00-7f01 r--p 00000000 08:01 {inode} {library}\n'),
            '5': ('other', '7f01-7f02 r--p 00000000 08:01 13 /usr/lib/libc.so.6\n'),
        }
        for pid, (command, lines) in maps.items():
            (self.proc / pid).mkdir()
            (self.proc / pid / 'comm').write_text(command + '\n')
            (self.proc / pid / 'maps').write_text(lines)
        status, text = self.run_doctor(platform='linux')
        self.assertEqual(status, 0, text)
        self.assertIn('loaded by ardour (pid 4242): its executable is mapped', text)
        self.assertIn(
            'opened by indexer (pid 77), scanner (pid 78): files read, executable not mapped, so not loaded', text
        )
        self.assertNotIn('other', text)

    def test_an_earlier_copy_still_loaded_is_named(self):
        # A host started before the new package was installed keeps running
        # the old, replaced file: that is not the installed copy loaded.
        library = self.lv2('0.19.21') / 'convolver.so'
        for pid, path, inode in (
            ('100', f'{library} (deleted)', str(library.stat().st_ino)),
            ('200', str(library), str(library.stat().st_ino + 1)),
        ):
            process = self.proc / pid
            process.mkdir()
            (process / 'comm').write_text(f'host{pid}\n')
            (process / 'maps').write_text(f'7f00-7f01 r-xp 00000000 08:01 {inode} {path}\n')
        status, text = self.run_doctor(platform='linux')
        self.assertEqual(status, 1, text)
        self.assertIn('an earlier copy, since replaced, is loaded by host100 (pid 100), host200 (pid 200)', text)
        self.assertIn('host100 (pid 100) still has an earlier copy', text)
        self.assertNotIn(': its executable is mapped', text)

    def test_loaded_from_a_real_process(self):
        if not Path('/proc/self/maps').exists():
            self.skipTest('no /proc')
        bundle = self.lv2('0.19.21')
        library = bundle / 'convolver.so'
        plugin = {'path': bundle, 'links': []}
        mine = str(os.getpid())

        def pids(state):
            return {pid for pid, _ in doctor.loaded_by(plugin, doctor.open_files('linux')[0])[state]}

        # Mapped read-only, as a scanner reading it would: opened, not loaded.
        with library.open('rb') as handle, mmap.mmap(handle.fileno(), 0, flags=mmap.MAP_PRIVATE, prot=mmap.PROT_READ):
            self.assertEqual(doctor.open_files('linux')[1], '/proc, the processes you can read')
            self.assertNotIn(mine, pids('loaded'))
            self.assertIn(mine, pids('opened'))
        with library.open('rb') as handle, mmap.mmap(
            handle.fileno(), 0, flags=mmap.MAP_PRIVATE, prot=mmap.PROT_READ | mmap.PROT_EXEC
        ):
            # Mapped as code, as a host loading it does.
            self.assertIn(mine, pids('loaded'))
            # Installing a new package replaces the file under the running host.
            replacement = bundle / 'new.so'
            replacement.write_bytes(b'\1' * 64)
            replacement.replace(library)
            self.assertNotIn(mine, pids('loaded'))
            self.assertIn(mine, pids('stale'))
        # Once the file is closed and unmapped, nothing has it loaded.
        self.assertNotIn(mine, pids('loaded') | pids('stale'))

    def test_no_lsof_or_an_empty_listing_is_unknown(self):
        self.vst3('0.19.21')
        for output, reason in ((None, 'no lsof or /proc'), ('', 'lsof listed nothing')):
            with self.subTest(reason=reason):
                self.lsof = output
                status, text = self.run_doctor('--expect', '0.19.21')
                self.assertEqual(status, 0, text)
                self.assertIn(f'whether a program has it loaded is unknown ({reason})', text)
                self.assertNotIn('not seen loaded', text)

    def test_lsof_output_is_read(self):
        # The form `lsof -F pcfin` prints: p and c per process, then f, i and
        # n per file (i is absent for some kinds of file). txt is mapped code;
        # a numbered descriptor is a file that is only open.
        bundle = self.vst3('0.19.21')
        executable = bundle / 'Contents' / 'MacOS' / 'Convolver'
        inode = executable.stat().st_ino
        component = self.au('0.19.21')
        text = (
            'p812\ncREAPER\nftxt\ni1\nn/Applications/REAPER.app/Contents/MacOS/REAPER\n'
            f'ftxt\ni{inode}\nn{executable}\n'
            'p900\ncAUHostingServic\n'
            f'ftxt\ni{inode + 1}\nn{component}/Contents/MacOS/Convolver\n'
            f'p901\ncmds\nf12\nn{bundle}/Contents/Info.plist\n'
            f'p902\ncLogic Pro X\nf13\ni{inode}\nn{executable}\n'
        )
        opened = doctor.parse_lsof(text)
        self.assertEqual(
            doctor.loaded_by({'path': bundle}, opened),
            {'loaded': [('812', 'REAPER')], 'opened': [('902', 'Logic Pro X'), ('901', 'mds')], 'stale': []},
        )
        self.assertEqual(
            doctor.loaded_by({'path': component}, opened),
            {'loaded': [], 'opened': [], 'stale': [('900', 'AUHostingServic')]},
        )
        # A neighbour whose name starts the same is not the bundle.
        self.assertEqual(
            doctor.loaded_by({'path': bundle.with_name('Convolver')}, opened),
            {'loaded': [], 'opened': [], 'stale': []},
        )

    def test_links_name_only_bundles_and_downloads(self):
        # The system bundle is read through the alias folder, the user link
        # points at its real path: the bundle is still the entry, whichever
        # folder is read first (jrp saw the reverse through macOS's /var).
        real = self.vst3('0.19.21', scope='system')
        user_folder = self.home / 'Library/Audio/Plug-Ins/VST3'
        user_folder.mkdir(parents=True)
        (user_folder / 'Convolver.vst3').symlink_to(real.resolve())
        # Another platform's newer package does not apply here.
        (self.downloads / 'Convolver-0.19.30-linux-x86_64.zip').write_bytes(b'')
        (self.downloads / 'Convolver-0.19.21-macos-arm64.zip').write_bytes(b'')
        status, text = self.run_doctor()
        self.assertEqual(status, 0, text)
        self.assertRegex(text, rf'VST3\s+0\.19\.21\s+system\s+{re.escape(str(real))}\n')
        self.assertIn(f'also reached through the link {user_folder / "Convolver.vst3"}', text)
        self.assertIn('Newest Convolver download: Convolver-0.19.21-macos-arm64.zip', text)

    def test_no_download_to_compare_is_said(self):
        self.vst3('0.18.23')
        (self.downloads).rmdir()
        status, text = self.run_doctor()
        self.assertEqual(status, 0, text)
        self.assertIn('Versions were not compared with a download', text)
        self.assertIn('Give --expect VERSION', text)

    def test_lv2_path_replaces_the_default_folders(self):
        self.lv2('0.19.21')
        elsewhere = self.base / 'lv2'
        elsewhere.mkdir()
        folders = doctor.plugin_folders('linux', self.home, self.system, str(elsewhere))
        self.assertIn(('.lv2', elsewhere, 'system'), folders)
        self.assertNotIn(('.lv2', self.home / '.lv2', 'user'), folders)


if __name__ == '__main__':
    unittest.main()
