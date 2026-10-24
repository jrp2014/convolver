# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that kept debugging symbols are usable and belong to the packaged binaries.

A symbol file only helps with a crash report if it holds debugging information
and was made from the very binary that crashed. For each packaged binary this
reads, without platform tools, the identity a debugger matches on and the
debugging payload:

- Linux: the GNU build ID note in the binary and in `<file>.debug`, the
  binary's `.gnu_debuglink` (file name and CRC-32 of the `.debug` file), and a
  non-empty `.debug_info` section in the `.debug` file.
- macOS: the Mach-O UUID of each architecture in the binary and in
  `<file>.dSYM/Contents/Resources/DWARF/<file>`, and a non-empty
  `__DWARF,__debug_info` section there.
- Windows: the CodeView (RSDS) GUID, age and PDB name in the binary's debug
  directory and in the PDB's own information stream, and module information in
  the PDB's DBI stream.

DIAG-1, from jrp's review of 0.4.43: before this, only the names were checked,
so an empty `.debug` file or a `.dSYM` holding only `Info.plist` passed.
"""
from pathlib import Path
import struct
import zlib

# Kept symbol file and the packaged binary (a glob under the staged package).
TARGETS = {
    'Linux': (('Convolver.so.debug', 'VST3/Convolver.vst3/Contents/*/Convolver.so'),
              ('convolver-preset.debug', 'bin/convolver-preset'),
              ('convolver-equalizer-apo.debug', 'bin/convolver-equalizer-apo')),
    'Darwin': (('Convolver.dSYM', 'VST3/Convolver.vst3/Contents/MacOS/Convolver'),
               ('convolver-preset.dSYM', 'bin/convolver-preset'),
               ('convolver-equalizer-apo.dSYM', 'bin/convolver-equalizer-apo')),
    'Windows': (('Convolver.pdb', 'VST3/Convolver.vst3/Contents/*/Convolver.vst3'),
                ('convolver-preset.pdb', 'bin/convolver-preset.exe'),
                ('convolver-equalizer-apo.pdb', 'bin/convolver-equalizer-apo.exe')),
}


class SymbolError(ValueError):
    """Symbols that a debugger could not use for the binary they were kept for."""


def _read(path):
    data = path.read_bytes()
    if not data:
        raise SymbolError(f'{path.name} is empty (0 bytes)')
    return data


def _unpack(fmt, data, offset, what):
    try:
        return struct.unpack_from(fmt, data, offset)
    except struct.error:
        raise SymbolError(f'{what} is truncated') from None


# ELF ----------------------------------------------------------------------

SHT_NOTE, SHT_NOBITS = 7, 8
NT_GNU_BUILD_ID = 3


def elf_sections(data, what):
    """{name: (type, contents or None for NOBITS)} of a little-endian ELF file."""
    if data[:4] != b'\x7fELF':
        raise SymbolError(f'{what} is not an ELF file')
    if data[5] != 1:
        raise SymbolError(f'{what} is not little-endian ELF')
    if data[4] == 2:
        shoff, = _unpack('<Q', data, 0x28, what)
        shentsize, shnum, shstrndx = _unpack('<HHH', data, 0x3a, what)
        entry = '<IIQQQQIIQQ'
    elif data[4] == 1:
        shoff, = _unpack('<I', data, 0x20, what)
        shentsize, shnum, shstrndx = _unpack('<HHH', data, 0x2e, what)
        entry = '<IIIIIIIIII'
    else:
        raise SymbolError(f'{what} has an unknown ELF class')
    if shnum == 0 or shstrndx >= shnum:
        raise SymbolError(f'{what} has no section table')
    headers = [_unpack(entry, data, shoff + i * shentsize, what) for i in range(shnum)]
    names_offset, names_size = headers[shstrndx][4], headers[shstrndx][5]
    names = data[names_offset:names_offset + names_size]
    sections = {}
    for name_index, kind, _flags, _addr, offset, size, *_ in headers:
        name = names[name_index:names.index(b'\0', name_index)].decode('ascii', 'replace')
        if kind == SHT_NOBITS:
            sections[name] = (kind, None)
        else:
            if offset + size > len(data):
                raise SymbolError(f'{what}: section {name} lies outside the file')
            sections[name] = (kind, data[offset:offset + size])
    return sections


def elf_build_id(sections, what):
    for kind, contents in sections.values():
        if kind != SHT_NOTE or contents is None:
            continue
        offset = 0
        while offset + 12 <= len(contents):
            namesz, descsz, note = struct.unpack_from('<III', contents, offset)
            name = contents[offset + 12:offset + 12 + namesz]
            desc_offset = offset + 12 + (namesz + 3) // 4 * 4
            if note == NT_GNU_BUILD_ID and name == b'GNU\0':
                return contents[desc_offset:desc_offset + descsz].hex()
            offset = desc_offset + (descsz + 3) // 4 * 4
    raise SymbolError(f'{what} has no GNU build ID (link with --build-id)')


def check_elf(binary, debug):
    binary_sections = elf_sections(_read(binary), binary.name)
    debug_data = _read(debug)
    debug_sections = elf_sections(debug_data, debug.name)
    expected = elf_build_id(binary_sections, binary.name)
    actual = elf_build_id(debug_sections, debug.name)
    if actual != expected:
        raise SymbolError(f'{debug.name} has GNU build ID {actual}, but the packaged '
                          f'{binary.name} has {expected}: the symbols come from another build')
    link = binary_sections.get('.gnu_debuglink')
    if link is None or link[1] is None:
        raise SymbolError(f'the packaged {binary.name} has no .gnu_debuglink naming its symbols')
    contents = link[1]
    name = contents[:contents.find(b'\0')].decode('utf-8', 'replace')
    crc_offset = (len(name) + 1 + 3) // 4 * 4
    crc, = _unpack('<I', contents, crc_offset, f'{binary.name} .gnu_debuglink')
    if name != debug.name:
        raise SymbolError(f'the packaged {binary.name} names {name} as its symbols, not {debug.name}')
    if crc != zlib.crc32(debug_data):
        raise SymbolError(f'{debug.name} is not the file the packaged {binary.name} was linked to '
                          '(.gnu_debuglink CRC-32 differs)')
    info = debug_sections.get('.debug_info') or debug_sections.get('.zdebug_info')
    if info is None or not info[1]:
        raise SymbolError(f'{debug.name} holds no debugging information (.debug_info is '
                          'missing or empty): was the binary compiled with -g?')
    return f'GNU build ID {expected}'


# Mach-O -------------------------------------------------------------------

LC_SEGMENT, LC_SEGMENT_64, LC_UUID = 0x1, 0x19, 0x1b
CPU_NAMES = {7: 'i386', 0x01000007: 'x86_64', 12: 'arm', 0x0100000c: 'arm64'}


def _macho_slice(data, what):
    """(cputype, UUID hex or None, size of __DWARF,__debug_info) of a thin file."""
    magic, = _unpack('<I', data, 0, what)
    if magic == 0xfeedfacf:
        header, segment, section = 32, ('<16sQQQQiiII', LC_SEGMENT_64), ('<16s16sQQI', 80)
    elif magic == 0xfeedface:
        header, segment, section = 28, ('<16sIIIIiiII', LC_SEGMENT), ('<16s16sIII', 68)
    else:
        raise SymbolError(f'{what} is not a little-endian Mach-O file')
    cputype, _subtype, _filetype, ncmds = _unpack('<iiII', data, 4, what)
    offset, uuid, dwarf = header, None, 0
    for _ in range(ncmds):
        cmd, size = _unpack('<II', data, offset, what)
        if size < 8:
            raise SymbolError(f'{what} has a malformed load command')
        if cmd == LC_UUID:
            uuid = bytes(_unpack('<16s', data, offset + 8, what)[0]).hex()
        elif cmd == segment[1]:
            fields = _unpack(segment[0], data, offset + 8, what)
            first = offset + 8 + struct.calcsize(segment[0])
            for i in range(fields[7]):
                sect, seg, _addr, sect_size, sect_offset = _unpack(
                    section[0], data, first + i * section[1], what)
                if (seg.rstrip(b'\0'), sect.rstrip(b'\0')) == (b'__DWARF', b'__debug_info') \
                        and sect_offset and sect_offset + sect_size <= len(data):
                    dwarf = sect_size
        offset += size
    return cputype, uuid, dwarf


def macho_slices(data, what):
    """{architecture: (UUID, debug_info size)} of a thin or universal Mach-O file."""
    magic, = _unpack('>I', data, 0, what)
    if magic in (0xcafebabe, 0xcafebabf):
        count, = _unpack('>I', data, 4, what)
        entry = ('>iiIII', 20) if magic == 0xcafebabe else ('>iiQQII', 32)
        slices = {}
        for i in range(count):
            _cpu, _sub, offset, size, *_ = _unpack(entry[0], data, 8 + i * entry[1], what)
            cpu, uuid, dwarf = _macho_slice(data[offset:offset + size], what)
            slices[CPU_NAMES.get(cpu, hex(cpu))] = (uuid, dwarf)
        return slices
    cpu, uuid, dwarf = _macho_slice(data, what)
    return {CPU_NAMES.get(cpu, hex(cpu)): (uuid, dwarf)}


def check_macho(binary, dsym):
    if not dsym.is_dir():
        raise SymbolError(f'{dsym.name} is not a .dSYM bundle (folder)')
    dwarf = dsym / 'Contents/Resources/DWARF' / binary.name
    if not dwarf.is_file():
        raise SymbolError(f'{dsym.name} has no Contents/Resources/DWARF/{binary.name}, so it holds '
                          'no debugging information (dsymutil writes it)')
    expected = macho_slices(_read(binary), binary.name)
    actual = macho_slices(_read(dwarf), f'{dsym.name} DWARF file')
    if set(actual) != set(expected):
        raise SymbolError(f'{dsym.name} covers {", ".join(sorted(actual))}, but the packaged '
                          f'{binary.name} is built for {", ".join(sorted(expected))}')
    for arch, (uuid, _) in sorted(expected.items()):
        if uuid is None:
            raise SymbolError(f'the packaged {binary.name} ({arch}) has no LC_UUID')
        if actual[arch][0] != uuid:
            raise SymbolError(f'{dsym.name} ({arch}) has UUID {actual[arch][0]}, but the packaged '
                              f'{binary.name} has {uuid}: the symbols come from another build')
        if not actual[arch][1]:
            raise SymbolError(f'{dsym.name} ({arch}) holds no debugging information '
                              '(__DWARF,__debug_info is missing or empty)')
    return 'Mach-O UUID ' + ', '.join(f'{uuid} ({arch})' for arch, (uuid, _) in sorted(expected.items()))


# PE and PDB ---------------------------------------------------------------

IMAGE_DEBUG_TYPE_CODEVIEW = 2
MSF_MAGIC = b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0'


def pe_codeview(data, what):
    """(GUID bytes, age, PDB path) from a PE file's CodeView debug record."""
    if data[:2] != b'MZ':
        raise SymbolError(f'{what} is not a PE file')
    pe, = _unpack('<I', data, 0x3c, what)
    if data[pe:pe + 4] != b'PE\0\0':
        raise SymbolError(f'{what} is not a PE file')
    _machine, nsections, _, _, _, optional_size, _ = _unpack('<HHIIIHH', data, pe + 4, what)
    optional = pe + 24
    magic, = _unpack('<H', data, optional, what)
    directories = optional + {0x10b: 96, 0x20b: 112}.get(magic, -1)
    if directories < optional:
        raise SymbolError(f'{what} has an unknown optional header')
    count, = _unpack('<I', data, directories - 4, what)
    if count <= 6:
        raise SymbolError(f'{what} has no debug directory (link with /DEBUG)')
    debug_rva, debug_size = _unpack('<II', data, directories + 6 * 8, what)
    sections = [_unpack('<8sIIII', data, optional + optional_size + i * 40, what)
                for i in range(nsections)]

    def file_offset(rva):
        for _name, vsize, vaddr, raw_size, raw in sections:
            if vaddr <= rva < vaddr + max(vsize, raw_size):
                return rva - vaddr + raw
        raise SymbolError(f'{what}: debug directory lies outside its sections')

    start = file_offset(debug_rva) if debug_size else 0
    for i in range(debug_size // 28):
        *_, kind, size, _address, pointer = _unpack('<IIHHIIII', data, start + i * 28, what)
        if kind == IMAGE_DEBUG_TYPE_CODEVIEW and data[pointer:pointer + 4] == b'RSDS':
            guid, age = _unpack('<16sI', data, pointer + 4, what)
            path = data[pointer + 24:pointer + size].split(b'\0', 1)[0].decode('utf-8', 'replace')
            return bytes(guid), age, path
    raise SymbolError(f'{what} has no CodeView (RSDS) record naming a PDB (link with /DEBUG)')


def pdb_streams(data, what):
    """The streams of an MSF 7.00 (PDB) file, as a list of bytes (None if absent)."""
    if not data.startswith(MSF_MAGIC):
        raise SymbolError(f'{what} is not a PDB (MSF 7.00) file')
    block, _free, nblocks, directory_bytes, _, map_block = _unpack('<IIIIII', data, 32, what)
    if block not in (512, 1024, 2048, 4096) or len(data) < nblocks * block:
        raise SymbolError(f'{what} is truncated or malformed')

    def blocks(indices, size):
        if any(i >= nblocks for i in indices):
            raise SymbolError(f'{what} refers to blocks beyond its end')
        return b''.join(data[i * block:(i + 1) * block] for i in indices)[:size]

    def count(size):
        return (size + block - 1) // block

    directory_blocks = _unpack(f'<{count(directory_bytes)}I', data, map_block * block, what)
    directory = blocks(directory_blocks, directory_bytes)
    nstreams, = _unpack('<I', directory, 0, what)
    sizes = _unpack(f'<{nstreams}I', directory, 4, what)
    offset, streams = 4 + 4 * nstreams, []
    for size in sizes:
        if size == 0xffffffff:
            streams.append(None)
            continue
        indices = _unpack(f'<{count(size)}I', directory, offset, what)
        offset += 4 * count(size)
        streams.append(blocks(indices, size))
    return streams


def check_pe(binary, pdb):
    guid, age, path = pe_codeview(_read(binary), binary.name)
    named = path.replace('\\', '/').rsplit('/', 1)[-1]
    if named.lower() != pdb.name.lower():
        raise SymbolError(f'the packaged {binary.name} names {named} as its PDB, not {pdb.name}')
    streams = pdb_streams(_read(pdb), pdb.name)
    if len(streams) < 4 or not streams[1] or len(streams[1]) < 28:
        raise SymbolError(f'{pdb.name} has no PDB information stream')
    _version, _signature, pdb_age, pdb_guid = struct.unpack_from('<III16s', streams[1], 0)
    dbi = streams[3]
    if not dbi or len(dbi) < 64:
        raise SymbolError(f'{pdb.name} has no DBI stream, so it holds no debugging information')
    _signature, _version, dbi_age = struct.unpack_from('<iII', dbi, 0)
    modules, = struct.unpack_from('<i', dbi, 24)
    identity = f'{guid.hex()}, age {age}'
    if bytes(pdb_guid) != guid or age not in (pdb_age, dbi_age):
        raise SymbolError(f'{pdb.name} has signature {bytes(pdb_guid).hex()}, age {pdb_age}, but the '
                          f'packaged {binary.name} expects {identity}: the symbols come from another link')
    if modules <= 0:
        raise SymbolError(f'{pdb.name} lists no compiled modules, so it holds no debugging information')
    return 'PDB signature ' + identity


CHECKS = {'Linux': check_elf, 'Darwin': check_macho, 'Windows': check_pe}


def check(kept, stage, system):
    """Check each packaged binary's kept symbols; return {symbol name: identity}.

    kept: the build's symbols/ folder; stage: the installed package. Raises
    SymbolError naming every binary whose symbols are missing or unusable.
    """
    problems, identities = [], {}
    for name, pattern in TARGETS[system]:
        binaries = sorted(stage.glob(pattern))
        found = sorted(p for p in kept.rglob(name) if p.name == name) if kept.is_dir() else []
        if len(binaries) != 1:
            problems.append(f'{pattern}: expected one packaged binary, found {len(binaries)}')
        elif not found:
            problems.append(f'{name}: missing (no debugging symbols for {binaries[0].name})')
        elif len(found) > 1:
            problems.append(f'{name}: more than one copy in {kept}')
        else:
            try:
                identities[name] = CHECKS[system](binaries[0], found[0])
            except SymbolError as error:
                problems.append(f'{name}: {error}')
    if problems:
        raise SymbolError('the kept debugging symbols cannot be used for the packaged binaries:\n  '
                          + '\n  '.join(problems))
    return identities
