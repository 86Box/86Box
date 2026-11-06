#!/usr/bin/env python3
"""Inspect PowerVR PCX frame dumps written by 86Box (powervr_dumps/frame_NNNNNN.bin).

  pcx_dump.py FILE                 summary: header and the named registers
  pcx_dump.py FILE --extract DIR   also write isp.bin, object.bin, texmem.bin

Format (little endian): 64-byte header, 1024 register dwords, then the ISP
pages read through the TLB, the PCX1 object buffer and texture memory.
"""
import os
import struct
import sys

REGS = {
    0x00: 'ID', 0x01: 'REVISION', 0x04: 'INTMASK', 0x06: 'FOGAMOUNT', 0x07: 'OBJECT_OFFSET',
    0x08: 'PAGE_CTRL', 0x0A: 'ISP_BASE', 0x0B: 'PREC_BASE', 0x0C: 'TMEM_SETUP', 0x0D: 'TMEM_REFRESH',
    0x0E: 'FOGCOL', 0x0F: 'CAMERA', 0x10: 'PACKMODE', 0x11: 'ARBMODE', 0x12: 'LSTRIDE',
    0x13: 'SOFADDR', 0x14: 'XCLIP', 0x18: 'IEEEFP', 0x19: 'BILINEAR', 0x1D: 'FASTFOG', 0x1E: 'POWERDOWN',
}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    d = open(argv[1], 'rb').read()
    magic = d[:8]
    if magic != b'PCXDUMP1':
        sys.exit('not a PCX dump')
    (version, frame, chip, page_bytes, tlb_entries, object_bytes, texmem_bytes,
     pci_command) = struct.unpack_from('<8I', d, 8)
    regs = struct.unpack_from('<1024I', d, 64)
    pos = 64 + 4096
    isp = d[pos:pos + tlb_entries * page_bytes]
    pos += len(isp)
    obj = d[pos:pos + object_bytes]
    pos += len(obj)
    tex = d[pos:pos + texmem_bytes]

    print('frame %u  chip PCX%u  PCI command %04x' % (frame, chip, pci_command))
    print('ISP buffer: %u TLB pages x %u bytes = %u bytes' % (tlb_entries, page_bytes, len(isp)))
    print('object buffer: %u bytes   texture memory: %u bytes' % (len(obj), len(tex)))
    for i, name in sorted(REGS.items()):
        print('  %-13s [%02x] %08x' % (name, i, regs[i]))
    print('  TLB          ' + ' '.join('%05x' % regs[0x100 + i] for i in range(min(tlb_entries, 16))) +
          (' ...' if tlb_entries > 16 else ''))
    xclip = regs[0x14]
    if xclip & (1 << 28):
        print('  XCLIP right edge %u (frame width)' % ((xclip >> 16) & 0x3FF))
    print('  output %s, stride %u bytes' % (
        {0: '32 bpp', 1: '24 bpp', 2: '16 bpp 565', 3: '15 bpp 555'}[regs[0x10] & 3], regs[0x12]))

    if '--extract' in argv:
        out = argv[argv.index('--extract') + 1]
        os.makedirs(out, exist_ok=True)
        for name, blob in (('isp.bin', isp), ('object.bin', obj), ('texmem.bin', tex)):
            open(os.path.join(out, name), 'wb').write(blob)
        print('extracted to', out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
