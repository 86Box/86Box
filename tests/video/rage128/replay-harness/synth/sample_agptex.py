#!/usr/bin/env python3
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          ATI Rage 128 Pro -- checks the pixels the replay harness drew
#          from the gen_agptex.py capture.
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

"""Verdict for gen_agptex.py: read three RGB565 pixels out of the replay's
VRAM dump (dst 0x100000, pitch 512 px) and compare with the texels the B
record carried. Exit 0 = PASS."""
import struct
import sys

vram = open(sys.argv[1], "rb").read()
want = ((4, 4, 0x07e0, "texel col 1 green"),
        (24, 4, 0xf800, "texel col 6 red"),
        (40, 4, 0x1111, "background"))
ok = True
for (x, y, exp, what) in want:
    off = 0x100000 + y * 1024 + x * 2
    got = struct.unpack_from("<H", vram, off)[0]
    verdict = "ok" if got == exp else "WRONG"
    ok &= got == exp
    print("(%2d,%2d) %-18s want %04x got %04x %s" % (x, y, what, exp, got, verdict))
print("PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
