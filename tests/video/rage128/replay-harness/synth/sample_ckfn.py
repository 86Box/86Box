#!/usr/bin/env python3
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          ATI Rage 128 Pro -- prints, band by band, the pixels the replay
#          harness drew from the gen_ckfn.py capture.
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

"""Sample the replay VRAM dump: RGB565 dst at 0x100000, pitch 1024 B.
Prints the dword at (4,y0+4) and (24,y0+4) for each band, plus a count
of non-background pixels in the band's triangle bbox (x 0..31, y0..y0+31)."""
import struct
import sys

raw = open(sys.argv[1], "rb").read()
def band(a):
    f, _, m = a.partition(":")
    return (int(f), int(m, 16) if m else 0xffffffff)


bands = [band(a) for a in sys.argv[2:]] or [(0, 0xffffffff), (3, 0xffffffff),
                                              (1, 0xffffffff), (2, 0xffffffff),
                                              (1, 0), (3, 0)]
BASE, PITCH = 0x100000, 1024
names = {0: "FALSE", 1: "TRUE", 2: "NEQ", 3: "EQ"}
for i, (fcn, msk) in enumerate(bands):
    y0 = 40 * i
    a = struct.unpack_from("<I", raw, BASE + (y0 + 4) * PITCH + 4 * 2)[0]
    b = struct.unpack_from("<I", raw, BASE + (y0 + 4) * PITCH + 24 * 2)[0]
    green = red = bg = other = 0
    for y in range(y0, y0 + 32):
        for x in range(32):
            if x + y - y0 >= 31:      # outside the right triangle
                continue
            p = struct.unpack_from("<H", raw, BASE + y * PITCH + x * 2)[0]
            if p == 0x07e0:
                green += 1
            elif p == 0xf800:
                red += 1
            elif p == 0x1111:
                bg += 1
            else:
                other += 1
    print("band %d fcn=%d(%-5s) msk=%08x (4,%3d)=%08x (24,%3d)=%08x  tri px: green=%d red=%d bg=%d other=%d"
          % (i, fcn, names[fcn], msk, y0 + 4, a, y0 + 4, b, green, red, bg, other))
