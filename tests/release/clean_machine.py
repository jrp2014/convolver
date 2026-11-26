# SPDX-License-Identifier: GPL-2.0-or-later
"""Run one platform's exact preview files on a machine that never built
Convolver (REL-9, REL-11).

The "Draft preview release" workflow runs this on fresh Linux containers and
fresh Windows and macOS runners, with only Python added, before it makes a
draft. It takes the candidate run's `complete-preview-candidate` files and,
for the platform named:

- checks the platform's ZIP (and installer) against SHA256SUMS;
- unpacks the ZIP into a folder whose name has spaces and a non-ASCII letter,
  then runs package_smoke.py there: the tools render the first-use examples
  to their expected output, remake every shipped preset and export to
  Equalizer APO, and the guides' examples run (on macOS it also checks the
  bundles' signatures and the Audio Unit's bundle);
- loads the VST3 plug-in's library as a host does and asks its factory for
  its classes, which must be the processor and controller with the class IDs
  in plugins/identities.json (no instance is created and no audio runs);
- on Windows, with --install, runs the installer silently, checks that it
  installs exactly the ZIP's files, with their bytes, where it owns them,
  plus its uninstaller; renders a first-use example with the installed CLI,
  loads the installed plug-in's library, and runs the uninstaller silently,
  which must remove every file it installed and its registry keys;
- on macOS, with --install, installs the .pkg and checks it as the candidate
  run does on its build machine (macos_installer.py: exactly the ZIP's files
  in place, signatures, the installed Audio Unit through `auval -strict`,
  the installed CLI), then loads the installed VST3 library, renders a
  first-use example with the installed CLI, and runs the shipped uninstall.sh.

The build runners already check the packages (release.yml); this shows the
same files run on an image other than the build's, without the libraries,
SDKs and build tree the build used. The Linux images are stock distribution
images. GitHub's Windows images do include Visual Studio and the Visual C++
runtime, so a computer without that runtime is not tried here (the installer
refuses to run without it); its macOS images include Xcode. GitHub has no
macOS 14 image (the oldest system the macOS packages are for) and no Windows
11 image for x86-64, so the workflow uses the oldest macOS it has and the
Windows 11 on Arm image, where the x86-64 files run under Windows' own x64
emulation (docs/develop/releasing.md#publishing-an-unsigned-preview). It is
not a host test: nothing here plays audio through a DAW.
"""

import argparse
import ctypes
import json
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
sys.path.insert(0, str(ROOT / 'tests/release'))
from candidate import extract_zip  # noqa: E402
from contract import sha256  # noqa: E402

PLUGIN_BINARIES = {
    'Darwin': 'Contents/MacOS/Convolver',
    'Linux': 'Contents/x86_64-linux/Convolver.so',
    'Windows': 'Contents/x86_64-win/Convolver.vst3',
}
SYSTEMS = {'linux': 'Linux', 'macos': 'Darwin', 'windows': 'Windows'}
AUDIO_MODULE_CLASS = b'Audio Module Class'
CONTROLLER_CLASS = b'Component Controller Class'


def class_id_bytes(hex_id, system):
    """A VST3 class ID as the factory reports it. Windows builds use COM's
    layout (the SDK's COM_COMPATIBLE): the first three fields little-endian."""
    plain = bytes.fromhex(hex_id)
    if system == 'Windows':
        return plain[3::-1] + plain[5:3:-1] + plain[7:5:-1] + plain[8:]
    return plain


class ClassInfo(ctypes.Structure):
    # Steinberg::PClassInfo: TUID cid, int32 cardinality, char8 category[32], char8 name[64].
    _fields_ = [
        ('cid', ctypes.c_ubyte * 16),
        ('cardinality', ctypes.c_int32),
        ('category', ctypes.c_char * 32),
        ('name', ctypes.c_char * 64),
    ]


def mac_bundle_ref(bundle):
    """A CFBundleRef for the bundle, as a macOS host passes to bundleEntry.
    The caller releases it with CFRelease after bundleExit."""
    core = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
    core.CFURLCreateFromFileSystemRepresentation.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_long,
        ctypes.c_bool,
    ]
    core.CFURLCreateFromFileSystemRepresentation.restype = ctypes.c_void_p
    core.CFBundleCreate.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    core.CFBundleCreate.restype = ctypes.c_void_p
    core.CFRelease.argtypes = [ctypes.c_void_p]
    path = os.fsencode(bundle)
    is_directory = True
    url = core.CFURLCreateFromFileSystemRepresentation(None, path, len(path), is_directory)
    assert url, f'{bundle}: no CFURL'
    try:
        reference = core.CFBundleCreate(None, url)
    finally:
        core.CFRelease(url)
    assert reference, f'{bundle}: CFBundleCreate failed'
    return core, reference


def factory_classes(binary, system):
    """Loads a VST3 module as a host does (module entry, then GetPluginFactory)
    and returns its classes as (class ID bytes, category, name)."""
    library = ctypes.CDLL(str(binary))
    core = reference = None
    if system == 'Darwin':
        # Contents/MacOS/Convolver: the bundle is three levels up.
        core, reference = mac_bundle_ref(binary.parents[2])
        entry, leave = library.bundleEntry, library.bundleExit
        entry.argtypes, entry.restype = [ctypes.c_void_p], ctypes.c_bool
        assert entry(reference), f'{binary.name}: bundleEntry failed'
    elif system == 'Linux':
        entry, leave = library.ModuleEntry, library.ModuleExit
        entry.argtypes, entry.restype = [ctypes.c_void_p], ctypes.c_bool
        # The module's own handle, as a host passes the one dlopen gave it.
        handle = ctypes.c_void_p(getattr(library, '_handle'))  # noqa: B009
        assert entry(handle), f'{binary.name}: ModuleEntry failed'
    else:
        entry, leave = library.InitDll, library.ExitDll
        entry.restype = ctypes.c_bool
        assert entry(), f'{binary.name}: InitDll failed'
    leave.restype = ctypes.c_bool
    try:
        get_factory = library.GetPluginFactory
        get_factory.restype = ctypes.c_void_p
        factory = get_factory()
        assert factory, f'{binary.name}: GetPluginFactory returned no factory'
        # IPluginFactory's table: FUnknown (queryInterface, addRef, release),
        # then getFactoryInfo, countClasses, getClassInfo, createInstance.
        table = ctypes.cast(factory, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p)))[0]
        release = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_void_p)(table[2])
        count_classes = ctypes.CFUNCTYPE(ctypes.c_int32, ctypes.c_void_p)(table[4])
        get_class_info = ctypes.CFUNCTYPE(ctypes.c_int32, ctypes.c_void_p, ctypes.c_int32, ctypes.POINTER(ClassInfo))(
            table[5]
        )
        classes = []
        for index in range(count_classes(factory)):
            info = ClassInfo()
            result = get_class_info(factory, index, ctypes.byref(info))
            assert result == 0, f'{binary.name}: getClassInfo({index}) returned {result}'
            classes.append((bytes(info.cid), info.category, info.name))
        release(factory)
        return classes
    finally:
        assert leave(), f'{binary.name}: module exit failed'
        if reference:
            core.CFRelease(reference)


def check_plugin(bundle, system):
    """Loads the plug-in in a separate process, so that nothing here keeps its
    file open (Windows would refuse to uninstall it) or loaded (a later load
    of the installed copy must be the installed file)."""
    subprocess.run([sys.executable, str(Path(__file__).resolve()), '--plugin', str(bundle)], check=True)


def load_plugin(bundle, system):
    binary = bundle / PLUGIN_BINARIES[system]
    assert binary.is_file(), f'missing plug-in binary: {binary}'
    identities = json.loads((ROOT / 'plugins/identities.json').read_text(encoding='utf-8'))['vst3']
    expected = {
        (class_id_bytes(identities['processor_class_id'], system), AUDIO_MODULE_CLASS, b'Convolver'),
        (class_id_bytes(identities['controller_class_id'], system), CONTROLLER_CLASS, b'Convolver Controller'),
    }
    classes = factory_classes(binary, system)
    assert set(classes) == expected and len(classes) == 2, f'{binary}: unexpected classes {classes}'
    print(f'Loaded {binary}: processor and controller classes as in plugins/identities.json', flush=True)


def check_sums(candidate_dir, names):
    sums = {}
    for line in (candidate_dir / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        digest, _, name = line.partition('  ')
        sums[name] = digest
    for name in names:
        assert name in sums, f'SHA256SUMS does not list {name}'
        assert sha256(candidate_dir / name) == sums[name], f'{name} does not match its SHA256SUMS line'
        print(f'{name}: SHA-256 matches', flush=True)


def render_first_use(cli, examples):
    """The installed CLI renders the stereo first-use example to its expected output."""
    folder = examples / 'stereo'
    with tempfile.TemporaryDirectory(prefix='convolver-clean-') as temporary:
        render = Path(temporary) / 'render ü.wav'
        subprocess.run(
            [
                str(cli),
                '--filter',
                str(folder / 'filter.cfg'),
                '--input',
                str(folder / 'input.wav'),
                '--output',
                str(render),
            ],
            check=True,
            capture_output=True,
        )
        compared = subprocess.run(
            [str(cli), '--compare', str(render), str(folder / 'expected.wav')],
            capture_output=True,
            text=True,
            check=False,
        )
        assert compared.returncode == 0 and 'Match' in compared.stdout, compared.stdout + compared.stderr


def registry_key_exists(winreg, key):
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY):
            return True
    except FileNotFoundError:
        return False


def check_windows_installer(setup, unpacked):
    import winreg

    from installers import destinations

    identities = json.loads((ROOT / 'plugins/identities.json').read_text(encoding='utf-8'))['windows_installer']
    key = identities['registry_key']
    uninstall_key = 'Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\' + identities['uninstall_key']
    if registry_key_exists(winreg, key):
        raise RuntimeError('refusing to install over an existing Convolver installation')
    files = sorted(p.relative_to(unpacked).as_posix() for p in unpacked.rglob('*') if p.is_file())
    # Windows environment names ignore case.
    install_dir = Path(os.environ['PROGRAMW6432']) / 'Convolver'
    common = Path(os.environ['COMMONPROGRAMW6432'])
    installed = {}
    for name, target in destinations(files, 'Windows').items():
        base, _, rest = target.partition('\\')
        installed[name] = (install_dir if base == '$INSTDIR' else common) / rest
    # The installer's default folder, as a user who accepts it gets.
    result = subprocess.run([str(setup), '/S'], timeout=300, check=False)
    assert result.returncode == 0, f'installer exited with {result.returncode}'
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as opened:
        assert Path(winreg.QueryValueEx(opened, 'InstallDir')[0]) == install_dir
    for name, path in installed.items():
        assert path.is_file(), f'installer did not install {name} at {path}'
        assert path.read_bytes() == (unpacked / name).read_bytes(), f'{path} differs from the ZIP'
    uninstaller = install_dir / 'Uninstall.exe'
    bundle = common / 'VST3/Convolver.vst3'
    present = {p for folder in (install_dir, bundle) for p in folder.rglob('*') if p.is_file()}
    extra = sorted(str(p) for p in present - set(installed.values()) - {uninstaller})
    assert uninstaller.is_file() and not extra, f'installer added files the ZIP does not hold: {extra}'
    print(f'Installer put {len(installed)} files in place with the ZIP bytes', flush=True)
    render_first_use(install_dir / 'bin/convolver.exe', install_dir / 'examples/first-use')
    check_plugin(common / 'VST3/Convolver.vst3', 'Windows')
    # _?= keeps the uninstaller in the foreground, so its status is the result;
    # it then leaves only itself, which a user's uninstall removes too.
    result = subprocess.run(
        subprocess.list2cmdline([str(install_dir / 'Uninstall.exe'), '/S']) + f' _?={install_dir}',
        timeout=300,
        check=False,
    )
    assert result.returncode == 0, f'uninstaller exited with {result.returncode}'
    left = [str(path) for path in installed.values() if path.exists()]
    assert not left, f'uninstaller left installed files: {left}'
    # Run with _?=, the uninstaller cannot delete itself; nothing else may stay.
    remaining = sorted(str(p) for p in install_dir.rglob('*') if p != uninstaller) if install_dir.exists() else []
    assert not remaining, f'uninstaller left files or folders: {remaining}'
    for registered in (key, uninstall_key):
        assert not registry_key_exists(winreg, registered), f'uninstaller left HKLM\\{registered}'
    assert not bundle.exists(), 'uninstaller left the plug-in bundle'
    print('Uninstaller removed every installed file and registry key')


def package_version(unpacked):
    """The version the package was built as, from its VERSION.txt
    ("Convolver X.Y.Z"): a dry run's scripts come from a later commit than
    the files, so the checkout's VERSION may differ."""
    first = (unpacked / 'VERSION.txt').read_text(encoding='utf-8').splitlines()[0]
    name, _, version = first.partition(' ')
    assert name == 'Convolver' and version, f'unexpected VERSION.txt: {first!r}'
    return version


def check_macos_installer(installer, archive, unpacked):
    """Installs the .pkg, checks it as the candidate run does on its build
    machine, and uses the installed files before the uninstaller runs."""
    from macos_installer import SUPPORT, VST3
    from macos_installer import check as check_installed

    def use_installed():
        check_plugin(VST3 / 'Convolver.vst3', 'Darwin')
        render_first_use(SUPPORT / 'bin/convolver', SUPPORT / 'examples/first-use')
        print('The installed plug-in loaded and the installed CLI rendered a first-use example', flush=True)

    check_installed(installer, archive, version=package_version(unpacked), also=use_installed)


def describe_machine(system):
    """The system the files ran on, for the run's log."""
    if system == 'Darwin':
        detail = f'macOS {platform.mac_ver()[0]}'
    elif system == 'Windows':
        # Under x64 emulation Python reports AMD64; the native processor is in
        # PROCESSOR_IDENTIFIER.
        release, build = platform.win32_ver()[:2]
        detail = (
            f'Windows {release} {platform.win32_edition()} build {build}, {os.environ.get("PROCESSOR_IDENTIFIER", "")}'
        )
    else:
        detail = platform.platform()
    print(f'Machine: {detail}; Python {platform.python_version()} for {platform.machine()}', flush=True)


def check(candidate_dir, platform_id, *, install=False):
    system = platform.system()
    describe_machine(system)
    candidate = json.loads((candidate_dir / 'candidate.json').read_text(encoding='utf-8'))
    records = {record['id']: record for record in candidate['platforms']}
    assert platform_id in records, f'the candidate has no {platform_id} package'
    record = records[platform_id]
    expected_system = SYSTEMS.get(platform_id.split('-')[0])
    assert expected_system == system, f'{platform_id} needs a {expected_system} machine, not {system}'
    installers = record.get('installers', [])
    check_sums(candidate_dir, [record['artifact'], *installers])
    from package_smoke import check as smoke

    with tempfile.TemporaryDirectory(prefix='convolver-clean-') as temporary:
        unpacked = Path(temporary) / 'Convolver ü download'
        extract_zip(candidate_dir / record['artifact'], unpacked)
        smoke(unpacked, version=package_version(unpacked))
        check_plugin(unpacked / 'VST3/Convolver.vst3', system)
        if install:
            assert system in ('Windows', 'Darwin') and len(installers) == 1, '--install needs the package installer'
            if system == 'Windows':
                check_windows_installer(candidate_dir / installers[0], unpacked)
            else:
                check_macos_installer(candidate_dir / installers[0], candidate_dir / record['artifact'], unpacked)
    print(f'{platform_id}: the published files ran on this clean machine')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--candidate', type=Path, help="the run's complete-preview-candidate files")
    parser.add_argument('--platform', help='linux-x86_64, windows-x86_64, macos-arm64 or macos-x86_64')
    parser.add_argument('--install', action='store_true', help='also install and uninstall (Windows and macOS)')
    parser.add_argument('--plugin', type=Path, help='only load this VST3 bundle and check its classes')
    args = parser.parse_args()
    if args.plugin:
        load_plugin(args.plugin.resolve(), platform.system())
    elif args.candidate and args.platform:
        check(args.candidate.resolve(), args.platform, install=args.install)
    else:
        parser.error('give --candidate and --platform, or --plugin')
