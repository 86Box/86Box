#!/usr/bin/env python3
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          Removes the code page conversion tables from ICU's data
#          library (a Linux shared object or a macOS dynamic library), as
#          nothing converts legacy code pages through ICU; its algorithmic
#          converters (UTF-*, ISO-8859-1, US-ASCII and others) are code and
#          keep working.
#
#          Usage: trim_icu_data.py [--cc compiler] library
#
# Authors: RichardG, <richardg867@gmail.com>
#
#          Copyright 2026 RichardG.
#
import os, re, shutil, struct, subprocess, sys, tempfile


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def sections(path, arch):
    """(segment, section, file offset, size) of every section."""
    out, cur = [], {}
    for line in run('otool', '-arch', arch, '-l', path).splitlines():
        words = line.split()
        if len(words) == 2 and words[0] in ('sectname', 'segname', 'size', 'offset'):
            cur[words[0]] = words[1]
            if 'offset' in cur and 'size' in cur:
                out.append((cur['segname'], cur['sectname'], int(cur['offset']), int(cur['size'], 16)))
                cur = {}
    return out


def find_package(data):
    """The offset of ICU's common data package: a data header whose format is CmnD."""
    pos = 0
    while True:
        pos = data.find(b'\xda\x27', pos + 1)
        if pos < 2:
            raise ValueError('no ICU data package found')
        start = pos - 2
        if data[start + 12:start + 16] == b'CmnD':
            return start


def trim_package(data, start, end):
    """The package without its .cnv items, and how many were removed."""
    header_size = struct.unpack_from('<H', data, start)[0]
    toc = start + header_size
    count = struct.unpack_from('<I', data, toc)[0]
    items = []
    for i in range(count):
        name_offset, data_offset = struct.unpack_from('<II', data, toc + 4 + 8 * i)
        name_end = data.index(b'\0', toc + name_offset)
        items.append([data[toc + name_offset:name_end], data_offset])
    # Items are sorted by name for lookups; their data is in the same order.
    by_offset = sorted(items, key=lambda item: item[1])
    for i, item in enumerate(by_offset):
        item_end = by_offset[i + 1][1] if (i + 1) < len(by_offset) else (end - toc)
        item.append(data[toc + item[1]:toc + item_end])
    kept = [item for item in items if not item[0].endswith(b'.cnv')]

    out = bytearray(data[start:toc])
    names = b''.join(item[0] + b'\0' for item in kept)
    table_size = 4 + 8 * len(kept)
    data_offset = (table_size + len(names) + 15) & ~15
    name_offsets, data_offsets = [], []
    name_pos = table_size
    for item in kept:
        name_offsets.append(name_pos)
        name_pos += len(item[0]) + 1
        data_offsets.append(data_offset)
        data_offset = (data_offset + len(item[2]) + 15) & ~15
    out += struct.pack('<I', len(kept))
    for name_offset, item_offset in zip(name_offsets, data_offsets):
        out += struct.pack('<II', name_offset, item_offset)
    out += names
    for item, item_offset in zip(kept, data_offsets):
        out += b'\0' * (header_size + item_offset - len(out))
        out += item[2]
    return bytes(out), count - len(kept)


def replace(path, new):
    shutil.copymode(path, new)
    shutil.move(new, path + '.trimmed')
    os.replace(path + '.trimmed', path)


def trim_elf(path, cc, workdir):
    data = open(path, 'rb').read()
    symbol, value, size, index = next((m.group(4), int(m.group(1), 16), int(m.group(2), 0), int(m.group(3)))
                                      for m in re.finditer(r'^\s*\d+: ([0-9a-f]+)\s+(\S+)\s+OBJECT\s+GLOBAL\s+\S+\s+(\d+)\s+(icudt\d+_dat)$',
                                                           run('readelf', '-sW', '--dyn-syms', path), re.M))
    for line in run('readelf', '-SW', path).splitlines():
        m = re.match(r'^\s*\[\s*%d\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)' % index, line)
        if m:
            section, address, offset = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
    start = offset + (value - address)
    package, removed = trim_package(data, start, start + size)
    soname = re.search(r'Library soname: \[(.*)\]', run('readelf', '-d', path)).group(1)

    dat = os.path.join(workdir, 'icudata.dat')
    open(dat, 'wb').write(package)
    asm = os.path.join(workdir, 'icudata.s')
    open(asm, 'w').write('.section %s,"a"\n.balign 16\n.globl %s\n.type %s, @object\n%s:\n.incbin "%s"\n'
                         '.size %s, . - %s\n.section .note.GNU-stack,"",@progbits\n' %
                         (section, symbol, symbol, symbol, dat, symbol, symbol))
    out = os.path.join(workdir, 'icudata.so')
    run(cc, '-shared', '-nostdlib', '-o', out, asm, '-Wl,-soname,' + soname, '-Wl,-Bsymbolic', '-Wl,-z,now')
    print('[-] ICU data: %d code page tables removed, %d -> %d bytes' % (removed, len(data), os.path.getsize(out)))
    replace(path, out)


def trim_slice(path, arch, workdir):
    thin = os.path.join(workdir, arch + '.in.dylib')
    if len(run('lipo', '-archs', path).split()) > 1:
        run('lipo', path, '-thin', arch, '-output', thin)
    else:
        shutil.copyfile(path, thin)
    data = open(thin, 'rb').read()
    start = find_package(data)
    segment, section, offset, size = next(s for s in sections(thin, arch) if s[2] <= start < (s[2] + s[3]))
    package, removed = trim_package(data, start, offset + size)

    symbol = next(word for line in run('nm', '-arch', arch, '-gU', thin).splitlines()
                  for word in line.split()[-1:] if re.fullmatch(r'_icudt\d+_dat', word))
    install_name = run('otool', '-arch', arch, '-D', thin).splitlines()[-1].strip()
    versions = re.search(r'compatibility version ([\d.]+), current version ([\d.]+)',
                         run('otool', '-arch', arch, '-L', thin).splitlines()[1])
    minos = re.search(r'\bminos ([\d.]+)', run('otool', '-arch', arch, '-l', thin))

    dat = os.path.join(workdir, arch + '.dat')
    open(dat, 'wb').write(package)
    asm = os.path.join(workdir, arch + '.s')
    open(asm, 'w').write('.section %s,%s\n.globl %s\n.p2align 4\n%s:\n.incbin "%s"\n' %
                         (segment, section, symbol, symbol, dat))
    out = os.path.join(workdir, arch + '.out.dylib')
    args = ['clang', '-arch', arch, '-dynamiclib', '-o', out, asm, '-install_name', install_name,
            '-compatibility_version', versions.group(1), '-current_version', versions.group(2)]
    if minos:
        args.append('-mmacosx-version-min=' + minos.group(1))
    run(*args)
    print('[-] ICU data [%s]: %d code page tables removed, %d -> %d bytes' %
          (arch, removed, len(data), os.path.getsize(out)))
    return out


def main():
    args = sys.argv[1:]
    cc = 'cc'
    if args[:1] == ['--cc']:
        cc, args = args[1], args[2:]
    path = args[0]
    with tempfile.TemporaryDirectory() as workdir:
        if open(path, 'rb').read(4) == b'\x7fELF':
            trim_elf(path, cc, workdir)
        else:
            slices = [trim_slice(path, arch, workdir) for arch in run('lipo', '-archs', path).split()]
            merged = os.path.join(workdir, 'merged.dylib')
            run('lipo', '-create', *slices, '-output', merged)
            replace(path, merged)


if __name__ == '__main__':
    main()
