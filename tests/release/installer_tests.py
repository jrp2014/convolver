# SPDX-License-Identifier: GPL-2.0-or-later
"""Installer ownership is an exact mapping, never a recursive removal scope."""

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))


class InstallerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        assert (ROOT / 'scripts/release/installers.py').is_file(), 'installer ownership generator missing'
        import installers

        cls.installers = installers

    def test_owned_paths(self):
        owned = self.installers.destinations(
            [
                'VST3/Convolver.vst3/Contents/x86_64-win/Convolver.vst3',
                'presets/identity-48000-stereo/identity-48000-stereo.vstpreset',
                'bin/convolver-preset.exe',
            ],
            'Windows',
        )
        self.assertEqual(owned['bin/convolver-preset.exe'], '$INSTDIR\\bin\\convolver-preset.exe')
        self.assertEqual(
            owned['VST3/Convolver.vst3/Contents/x86_64-win/Convolver.vst3'],
            '$COMMONFILES64\\VST3\\Convolver.vst3\\Contents\\x86_64-win\\Convolver.vst3',
        )
        user_files = {'$INSTDIR\\presets\\my-own.vstpreset', '$COMMONFILES64\\VST3\\Other.vst3'}
        self.assertTrue(user_files.isdisjoint(owned.values()))

    def test_outside_owned_tree(self):
        for filename in [
            'VST3/Other.vst3/plugin',
            '../user',
            '/Library/user',
            'bin/../../user',
            'bin/evil$INSTDIR',
            'bin/a"b',
        ]:
            with self.subTest(filename=filename), self.assertRaises(ValueError):
                self.installers.destinations([filename], 'Windows')

    def test_audio_unit_owned_on_macos_only(self):
        # AU-5: the component goes beside other Audio Units, for all users.
        name = 'Components/Convolver.component/Contents/MacOS/Convolver'
        owned = self.installers.destinations([name], 'Darwin')
        self.assertEqual(owned[name], '/Library/Audio/Plug-Ins/Components/Convolver.component/Contents/MacOS/Convolver')
        for filename, system in (
            (name, 'Windows'),
            ('Components/Other.component/Contents/MacOS/Other', 'Darwin'),
            ('Components/Convolver.component', 'Darwin'),
        ):
            with self.subTest(filename=filename, system=system), self.assertRaises(ValueError):
                self.installers.destinations([filename], system)

    def test_macos_payloads(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage, output = Path(temporary) / 'stage', Path(temporary) / 'installer'
            for name in (
                'VST3/Convolver.vst3/Contents/MacOS/Convolver',
                'Components/Convolver.component/Contents/MacOS/Convolver',
                'bin/convolver',
            ):
                (stage / name).parent.mkdir(parents=True, exist_ok=True)
                (stage / name).write_bytes(b'owned')
            self.installers.prepare(stage, output, 'Darwin', {'version': '1.2.3'})
            self.assertTrue((output / 'plugin-payload/Convolver.vst3/Contents/MacOS/Convolver').is_file())
            self.assertTrue((output / 'au-payload/Convolver.component/Contents/MacOS/Convolver').is_file())
            self.assertTrue((output / 'tools-payload/bin/convolver').is_file())
            import plistlib

            for plist, bundle in (('component.plist', 'Convolver.vst3'), ('au-component.plist', 'Convolver.component')):
                with (output / plist).open('rb') as stream:
                    self.assertEqual(plistlib.load(stream)[0]['RootRelativeBundlePath'], bundle)
            script = (output / 'uninstall.sh').read_text(encoding='utf-8')
            self.assertIn('rmdir -- /Library/Audio/Plug-Ins/Components/Convolver.component 2>/dev/null', script)
            self.assertNotIn('rmdir -- /Library/Audio/Plug-Ins/Components 2>', script)

    def test_macos_uninstall_preserves_custom_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            paths = [
                'VST3/Convolver.vst3/Contents/MacOS/Convolver',
                'Components/Convolver.component/Contents/MacOS/Convolver',
                'presets/identity.vstpreset',
            ]
            # Use a test prefix: exercise the actual generated uninstall script.
            mapping = self.installers.destinations(paths, 'Darwin', prefix=root)
            for dest in mapping.values():
                path = Path(dest)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'owned')
            custom = root / 'Library/Application Support/Convolver/presets/custom.vstpreset'
            custom.write_bytes(b'user preset')
            script = root / 'uninstall.sh'
            self.installers.write_uninstaller(mapping, script)
            import subprocess

            subprocess.run(['sh', str(script)], check=True)
            self.assertEqual(custom.read_bytes(), b'user preset')
            self.assertTrue(all(not Path(p).exists() for p in mapping.values()))


if __name__ == '__main__':
    unittest.main()
