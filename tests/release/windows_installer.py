# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise actual NSIS upgrade and locked-file recovery on a disposable CI runner."""

import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import subprocess
import sys
import tempfile
import winreg

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
from contract import load_policy  # noqa: E402
from installers import prepare  # noqa: E402


def run(command, success=True):
    arguments = [str(arg) for arg in command]
    # NSIS parses these final tail arguments itself, including unquoted spaces.
    invocation = (
        subprocess.list2cmdline(arguments[:-1]) + ' ' + arguments[-1]
        if arguments[-1].startswith(('/D=', '_?='))
        else arguments
    )
    result = subprocess.run(invocation, timeout=90)
    if (result.returncode == 0) != success:
        raise AssertionError(f'unexpected installer status {result.returncode}: {command}')


def check(nsis):
    # Never target a user's existing installation. CI runners are disposable.
    try:
        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE, r'Software\Convolver', 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY
        ):
            raise RuntimeError('refusing installer tests over an existing installation')
    except FileNotFoundError:
        pass
    with tempfile.TemporaryDirectory(prefix='convolver-installer-') as temporary:
        root = Path(temporary)
        stage = root / 'stage'
        (stage / 'bin').mkdir(parents=True)
        (stage / 'bin/tool.exe').write_bytes(b'owned fixture')
        shared = root / 'Common Files'
        plugin = shared / 'VST3/Convolver.vst3/Contents/x86_64-win/Convolver.vst3'
        source = stage / 'VST3/Convolver.vst3/Contents/x86_64-win/Convolver.vst3'
        source.parent.mkdir(parents=True)
        source.write_bytes(b'owned plugin fixture')
        generated = root / 'generated'
        prepare(stage, generated, 'Windows', load_policy(ROOT / 'packaging/release-policy.json'))
        # Keep the fixed shared plug-in location inside this test's private tree.
        for file in ('install-files.nsh', 'uninstall-files.nsh'):
            path = generated / file
            path.write_text(path.read_text().replace('$COMMONFILES64', str(shared)), encoding='utf-8')
        setup = root / 'setup.exe'
        run([nsis, '/DGENERATED=' + str(generated), '/DOUTPUT=' + str(setup), ROOT / 'packaging/windows/installer.nsi'])
        old, new = root / 'Install A', root / 'Install B'
        run([setup, '/S', '/D=' + str(old)])
        assert (old / 'bin/tool.exe').is_file(), 'installer did not create the requested destination'
        custom = old / 'custom.vstpreset'
        custom.write_bytes(b'user')
        run([setup, '/S', '/D=' + str(new)])
        assert not (old / 'Uninstall.exe').exists(), 'obsolete uninstaller survived upgrade'
        assert not (old / 'bin/tool.exe').exists(), 'old owned tool survived upgrade'
        assert custom.read_bytes() == b'user' and plugin.exists()
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.HANDLE,
        ]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.CreateFileW(str(plugin), 0x80000000, 0, None, 3, 0, None)
        if handle == wintypes.HANDLE(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            run([new / 'Uninstall.exe', '/S', '_?=' + str(new)], success=False)
            assert plugin.exists() and (new / 'Uninstall.exe').exists()
            with winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE, r'Software\Convolver', 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY
            ):
                pass
        finally:
            kernel.CloseHandle(handle)
        run([new / 'Uninstall.exe', '/S', '_?=' + str(new)])
        assert not plugin.exists() and custom.read_bytes() == b'user'
    print('NSIS changed-directory upgrade and locked-file recovery passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nsis', type=Path, required=True)
    args = parser.parse_args()
    check(args.nsis.resolve())
