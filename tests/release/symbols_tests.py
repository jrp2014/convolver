# SPDX-License-Identifier: GPL-2.0-or-later
"""Kept debugging symbols must hold debugging information and match the packaged binary.

Synthetic ELF, Mach-O, PE and PDB files cover every platform's checks on any
machine; where a compiler and the platform's symbol tools are present, real
binaries are checked too (Linux: objcopy; macOS: dsymutil). DIAG-1, from jrp's
review of 0.4.43.
"""

from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/release'))
import symbols  # noqa: E402


# Synthetic files -----------------------------------------------------------


def elf(build_id=None, debuglink=None, debug_info=None, nobits_debug_info=False):
    """A minimal 64-bit little-endian ELF file with the given sections."""
    sections = []
    if build_id is not None:
        note = struct.pack('<III', 4, len(build_id), 3) + b'GNU\0' + build_id
        sections.append(('.note.gnu.build-id', 7, note + b'\0' * (-len(note) % 4)))
    if debuglink is not None:
        name, crc = debuglink
        raw = name.encode() + b'\0'
        sections.append(('.gnu_debuglink', 1, raw + b'\0' * (-len(raw) % 4) + struct.pack('<I', crc)))
    if debug_info is not None:
        sections.append(('.debug_info', 8 if nobits_debug_info else 1, debug_info))
    names = b'\0' + b''.join(n.encode() + b'\0' for n, _, _ in sections) + b'.shstrtab\0'
    sections.append(('.shstrtab', 3, names))
    body, headers = b'', [b'\0' * 64]
    for name, kind, contents in sections:
        offset = 64 + len(body)
        if kind != 8:
            body += contents
        headers.append(
            struct.pack(
                '<IIQQQQIIQQ', names.index(name.encode() + b'\0'), kind, 0, 0, offset, len(contents), 0, 0, 1, 0
            )
        )
    shoff = 64 + len(body)
    header = (
        b'\x7fELF\x02\x01\x01'
        + b'\0' * 9
        + struct.pack('<HHIQQQIHHHHHH', 3, 62, 1, 0, 0, shoff, 0, 64, 0, 0, 64, len(headers), len(headers) - 1)
    )
    return header + body + b''.join(headers)


def macho(uuid, dwarf_size=0, cputype=0x0100000C):
    """A thin 64-bit Mach-O file with an LC_UUID and, optionally, __DWARF,__debug_info."""
    commands = struct.pack('<II16s', 0x1B, 24, uuid)
    if dwarf_size:
        data_offset = 32 + len(commands) + 72 + 80
        section = struct.pack(
            '<16s16sQQIIIIIIII', b'__debug_info', b'__DWARF', 0, dwarf_size, data_offset, 0, 0, 0, 0, 0, 0, 0
        )
        commands += (
            struct.pack('<II16sQQQQiiII', 0x19, 72 + 80, b'__DWARF', 0, 0, data_offset, dwarf_size, 7, 3, 1, 0)
            + section
        )
    header = struct.pack(
        '<IiiIIIII', 0xFEEDFACF, cputype, 0, 10 if dwarf_size else 2, 2 if dwarf_size else 1, len(commands), 0, 0
    )
    return header + commands + b'\xab' * dwarf_size


def universal(*slices):
    out = struct.pack('>II', 0xCAFEBABE, len(slices))
    offset = 8 + 20 * len(slices)
    offset += -offset % 4096
    body = b''
    for data, cputype in slices:
        start = offset + len(body)
        out += struct.pack('>iiIII', cputype, 0, start, len(data), 12)
        body += data + b'\0' * (-len(data) % 4096)
    return out + b'\0' * (offset - len(out)) + body


def pe(guid, age, pdb_path):
    """A minimal PE32+ file whose debug directory holds a CodeView (RSDS) record."""
    optional_size = 112 + 16 * 8
    sections_at = 0x40 + 24 + optional_size
    raw = 0x200
    record = b'RSDS' + guid + struct.pack('<I', age) + pdb_path.encode() + b'\0'
    debug = struct.pack('<IIHHIIII', 0, 0, 0, 0, 2, len(record), 0x1000 + 28, raw + 28)
    section_data = debug + record
    directories = [(0, 0)] * 16
    directories[6] = (0x1000, 28)
    optional = (
        struct.pack('<H', 0x20B)
        + b'\0' * 106
        + struct.pack('<I', 16)
        + b''.join(struct.pack('<II', *d) for d in directories)
    )
    head = bytearray(raw)
    head[0:2] = b'MZ'
    head[0x3C:0x40] = struct.pack('<I', 0x40)
    head[0x40:0x44] = b'PE\0\0'
    head[0x44:0x58] = struct.pack('<HHIIIHH', 0x8664, 1, 0, 0, 0, optional_size, 0x22)
    head[0x58 : 0x58 + optional_size] = optional
    head[sections_at : sections_at + 40] = struct.pack(
        '<8sIIIIIIHHI', b'.rdata', len(section_data), 0x1000, len(section_data), raw, 0, 0, 0, 0, 0x40000040
    )
    return bytes(head) + section_data


def pdb(guid, age, modules=64, dbi=True):
    """A minimal MSF 7.00 file: streams 0-3, with the information and DBI streams.

    Blocks: 0 superblock, 1 free-block map, 2 block map, 3 directory, then streams.
    """
    block = 512
    info = struct.pack('<III16s', 20000404, 0, age, guid)
    dbi_stream = struct.pack('<iIIHHHHHHi', -1, 19990903, age, 0, 0, 0, 0, 0, 0, modules).ljust(64, b'\0')
    streams = [b'', info, b'', dbi_stream if dbi else None]
    lists, data, next_block = [], b'', 4
    for stream in streams:
        count = (len(stream) + block - 1) // block if stream else 0
        lists.append(list(range(next_block, next_block + count)))
        data += b''.join(stream[i * block : (i + 1) * block].ljust(block, b'\0') for i in range(count))
        next_block += count
    directory = struct.pack('<I', len(streams))
    directory += b''.join(struct.pack('<I', 0xFFFFFFFF if s is None else len(s)) for s in streams)
    directory += b''.join(struct.pack(f'<{len(b)}I', *b) for b in lists)
    superblock = symbols.MSF_MAGIC + struct.pack('<IIIIII', block, 1, next_block, len(directory), 0, 2)
    return (
        superblock.ljust(block, b'\0')
        + b'\0' * block
        + struct.pack('<I', 3).ljust(block, b'\0')
        + directory.ljust(block, b'\0')
        + data
    )


class SymbolTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.kept = self.root / 'symbols'
        self.stage = self.root / 'stage'

    def write(self, path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def fails(self, system, *fragments):
        with self.assertRaises(symbols.SymbolError) as caught:
            symbols.check(self.kept, self.stage, system)
        for fragment in fragments:
            self.assertIn(fragment, str(caught.exception))
        return str(caught.exception)

    # Linux

    def linux(self, ids=(b'\x11' * 20, b'\x22' * 20, b'\x33' * 20)):
        for (name, pattern), build_id in zip(symbols.TARGETS['Linux'], ids):
            binary_path = pattern.replace('*', 'x86_64-linux')
            debug = elf(build_id, debug_info=b'\1' * 32)
            self.write(self.kept / name, debug)
            self.write(self.stage / binary_path, elf(build_id, (name, zlib.crc32(debug))))

    def test_linux_matching(self):
        self.linux()
        found = symbols.check(self.kept, self.stage, 'Linux')
        self.assertEqual(found['Convolver.so.debug'], 'GNU build ID ' + '11' * 20)

    def test_linux_empty_file(self):
        self.linux()
        (self.kept / 'convolver-preset.debug').write_bytes(b'')
        self.fails('Linux', 'convolver-preset.debug', 'empty')

    def test_linux_missing(self):
        self.linux()
        (self.kept / 'Convolver.so.debug').unlink()
        self.fails('Linux', 'Convolver.so.debug: missing')

    def test_linux_mismatched_build_id(self):
        self.linux()
        debug = elf(b'\x99' * 20, debug_info=b'\1' * 32)
        self.write(self.kept / 'convolver-preset.debug', debug)
        self.write(
            self.stage / 'bin/convolver-preset', elf(b'\x22' * 20, ('convolver-preset.debug', zlib.crc32(debug)))
        )
        self.fails('Linux', 'another build')

    def test_linux_no_debugging_information(self):
        self.linux()
        for nobits in (False, True):
            debug = elf(b'\x22' * 20, debug_info=b'' if not nobits else b'\0' * 8, nobits_debug_info=nobits)
            self.write(self.kept / 'convolver-preset.debug', debug)
            self.write(
                self.stage / 'bin/convolver-preset', elf(b'\x22' * 20, ('convolver-preset.debug', zlib.crc32(debug)))
            )
            self.fails('Linux', 'no debugging information')

    def test_linux_debuglink(self):
        self.linux()
        self.write(self.stage / 'bin/convolver-preset', elf(b'\x22' * 20, ('convolver-preset.debug', 1)))
        self.fails('Linux', 'CRC-32')
        self.write(self.stage / 'bin/convolver-preset', elf(b'\x22' * 20))
        self.fails('Linux', 'no .gnu_debuglink')

    def test_linux_malformed(self):
        self.linux()
        for data in (b'\x7fELF\x02', elf(b'\x22' * 20)[:100], b'\x7fELF' + b'\xff' * 200):
            self.write(self.kept / 'convolver-preset.debug', data)
            self.fails('Linux', 'convolver-preset.debug')

    def test_missing_binary(self):
        self.linux()
        (self.stage / 'bin/convolver-equalizer-apo').unlink()
        self.fails('Linux', 'expected one packaged binary, found 0')

    # macOS

    def darwin(self):
        for i, (name, pattern) in enumerate(symbols.TARGETS['Darwin']):
            uuid = bytes([i + 1]) * 16
            binary = self.write(self.stage / pattern, macho(uuid))
            self.write(self.kept / name / 'Contents/Info.plist', b'<plist/>')
            self.write(self.kept / name / 'Contents/Resources/DWARF' / binary.name, macho(uuid, 64))

    def test_darwin_matching(self):
        self.darwin()
        found = symbols.check(self.kept, self.stage, 'Darwin')
        self.assertEqual(found['Convolver.dSYM'], 'Mach-O UUID 01010101-0101-0101-0101-010101010101 (arm64)')

    def test_darwin_only_info_plist(self):
        self.darwin()
        shutil.rmtree(self.kept / 'Convolver.dSYM/Contents/Resources')
        self.fails('Darwin', 'Convolver.dSYM', 'no Contents/Resources/DWARF/Convolver')

    def test_darwin_empty_and_missing(self):
        self.darwin()
        (self.kept / 'convolver-preset.dSYM/Contents/Resources/DWARF/convolver-preset').write_bytes(b'')
        shutil.rmtree(self.kept / 'convolver-equalizer-apo.dSYM')
        self.fails('Darwin', 'empty', 'convolver-equalizer-apo.dSYM: missing')

    def test_darwin_mismatched_uuid_and_payload(self):
        self.darwin()
        dwarf = self.kept / 'convolver-preset.dSYM/Contents/Resources/DWARF/convolver-preset'
        dwarf.write_bytes(macho(b'\x77' * 16, 64))
        self.fails('Darwin', 'another build')
        dwarf.write_bytes(macho(b'\x02' * 16))
        self.fails('Darwin', 'no debugging information')

    def test_darwin_universal(self):
        self.darwin()
        a, b = b'\x0a' * 16, b'\x0b' * 16
        self.write(
            self.stage / 'bin/convolver-preset',
            universal((macho(a, cputype=0x01000007), 0x01000007), (macho(b), 0x0100000C)),
        )
        dwarf = self.kept / 'convolver-preset.dSYM/Contents/Resources/DWARF/convolver-preset'
        dwarf.write_bytes(universal((macho(a, 64, 0x01000007), 0x01000007), (macho(b, 64), 0x0100000C)))
        found = symbols.check(self.kept, self.stage, 'Darwin')
        self.assertIn('(arm64)', found['convolver-preset.dSYM'])
        self.assertIn('(x86_64)', found['convolver-preset.dSYM'])
        dwarf.write_bytes(macho(b, 64))
        self.fails('Darwin', 'covers arm64', 'x86_64')

    # Windows

    def windows(self):
        for i, (name, pattern) in enumerate(symbols.TARGETS['Windows']):
            guid = bytes([0x40 + i]) * 16
            self.write(self.stage / pattern.replace('*', 'x86_64-win'), pe(guid, 1, 'D:\\a\\build\\symbols\\' + name))
            self.write(self.kept / 'Release' / name, pdb(guid, 1))

    def test_windows_matching(self):
        self.windows()
        found = symbols.check(self.kept, self.stage, 'Windows')
        self.assertEqual(found['Convolver.pdb'], 'PDB signature {40404040-4040-4040-4040-404040404040}, age 1')

    def test_windows_empty_missing_mismatched(self):
        self.windows()
        (self.kept / 'Release/Convolver.pdb').write_bytes(b'')
        self.fails('Windows', 'Convolver.pdb is empty')
        (self.kept / 'Release/Convolver.pdb').unlink()
        self.fails('Windows', 'Convolver.pdb: missing')
        self.write(self.kept / 'Release/Convolver.pdb', pdb(b'\x41' * 16, 1))
        self.fails('Windows', 'another link')
        self.write(self.kept / 'Release/Convolver.pdb', pdb(b'\x40' * 16, 2))
        self.fails('Windows', 'another link')
        self.write(self.kept / 'Release/Convolver.pdb', b'not a PDB' * 100)
        self.fails('Windows', 'not a PDB')

    def test_windows_no_debugging_information(self):
        self.windows()
        self.write(self.kept / 'Release/Convolver.pdb', pdb(b'\x40' * 16, 1, modules=0))
        self.fails('Windows', 'no compiled modules')
        self.write(self.kept / 'Release/Convolver.pdb', pdb(b'\x40' * 16, 1, dbi=False))
        self.fails('Windows', 'no DBI stream')

    def test_windows_pdb_name(self):
        self.windows()
        self.write(self.stage / 'bin/convolver-preset.exe', pe(b'\x41' * 16, 1, 'C:\\other.pdb'))
        self.fails('Windows', 'names other.pdb')

    # Real binaries, where the tools are present.

    def compile(self, source, output, *flags):
        compiler = shutil.which('cc') or shutil.which('clang') or shutil.which('gcc')
        if compiler is None:
            self.skipTest('no C compiler')
        # Compile and link separately and keep the object file: on macOS the
        # linker only records where the debugging information is (the object
        # files) and dsymutil reads it from there, so a one-step build, whose
        # temporary object is deleted, gives a dSYM without any (which the
        # check rightly refuses).
        c = self.root / f'{output.name}.c'
        obj = self.root / f'{output.name}.o'
        c.write_text(source)
        subprocess.run([compiler, '-g', '-c', str(c), '-o', str(obj)], check=True)
        subprocess.run([compiler, *flags, str(obj), '-o', str(output)], check=True)

    @unittest.skipUnless(platform.system() == 'Linux' and shutil.which('objcopy'), 'needs Linux binutils')
    def test_real_elf(self):
        built = {}
        for (name, pattern), value in zip(symbols.TARGETS['Linux'], (1, 2, 3)):
            binary = self.stage / pattern.replace('*', 'x86_64-linux')
            binary.parent.mkdir(parents=True, exist_ok=True)
            self.kept.mkdir(exist_ok=True)
            self.compile(f'int main(void) {{ return {value}; }}\n', binary, '-Wl,--build-id')
            debug = self.kept / name
            # As cmake/ConvolverDebugSymbols.cmake does.
            subprocess.run(['objcopy', '--only-keep-debug', str(binary), str(debug)], check=True)
            subprocess.run(['objcopy', '--strip-debug', f'--add-gnu-debuglink={debug}', str(binary)], check=True)
            built[name] = binary
        found = symbols.check(self.kept, self.stage, 'Linux')
        self.assertRegex(found['convolver-preset.debug'], r'^GNU build ID [0-9a-f]{40}$')
        # The symbols of another binary are refused.
        shutil.copyfile(self.kept / 'Convolver.so.debug', self.kept / 'convolver-preset.debug')
        self.fails('Linux', 'convolver-preset.debug', 'another build')

    @unittest.skipUnless(platform.system() == 'Darwin' and shutil.which('dsymutil'), 'needs dsymutil')
    def test_real_macho(self):
        for (name, pattern), value in zip(symbols.TARGETS['Darwin'], (1, 2, 3)):
            binary = self.stage / pattern
            binary.parent.mkdir(parents=True, exist_ok=True)
            self.kept.mkdir(exist_ok=True)
            self.compile(f'int main(void) {{ return {value}; }}\n', binary)
            subprocess.run(['dsymutil', str(binary), '-o', str(self.kept / name)], check=True)
            subprocess.run(['strip', '-S', str(binary)], check=True)
        found = symbols.check(self.kept, self.stage, 'Darwin')
        self.assertIn('Mach-O UUID', found['Convolver.dSYM'])
        dwarf = self.kept / 'convolver-preset.dSYM/Contents/Resources/DWARF/convolver-preset'
        shutil.copyfile(self.kept / 'Convolver.dSYM/Contents/Resources/DWARF/Convolver', dwarf)
        self.fails('Darwin', 'another build')


if __name__ == '__main__':
    unittest.main()
