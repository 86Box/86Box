#!/usr/bin/env python3
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          ATI Rage 128 Pro -- writes a synthetic replay capture that
#          sweeps the legacy 3D color-compare function over textured
#          triangles.
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

"""Synthetic capture: legacy CLR_CMP_FCN_3D code sweep, six bands
(fcn 0, 3, 1, 2, then fcn 1 with CLR_CMP_MSK_3D = 0, fcn 3 with mask 0).

An 8x8 ARGB8888 texture lives at VRAM
0x200000 (cols 0-3 green 0xFF00FF00 = the key, cols 4-7 red 0xFFFF0000),
RGB565 dst at 0x100000 pitch 512 px, background 0x11 bytes via 2D PATCOPY.
Six right triangles (x 0..32) at y bands 0/40/../200, each with its own
MISC_3D_STATE_CNTL_REG[31:30] value and CLR_CMP_MSK_3D; CLR_CMP_CLR_3D =
green throughout. Band args: "fcn" or "fcn:mask" (hex mask).

Sample pixels per band: (4, y0+4) = texel col 1 (green), (24, y0+4) = col 6 (red).
"""
import struct
import sys

out = []


def R(off, val, mask=0xffffffff):
    out.append("R %04x %08x %08x" % (off, val & 0xffffffff, mask))


def J(v):
    out.append("J %08x" % (v & 0xffffffff))


def f2u(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def aper(addr, val):
    R(0x0000, 0x80000000 | addr)
    R(0x0004, val)


# --- 2D background fill: 0x11 bytes, rows 0..239 (8bpp PATCOPY, 1024 B rows)
R(0x16e8, 0x1fff1fff)   # DEFAULT_SC_BOTTOM_RIGHT
R(0x146c, 0x70f002d3)   # DP_GUI_MASTER_CNTL: 8bpp PATCOPY
R(0x16ec, 0x00000000)   # SC_TOP_LEFT
R(0x16f0, 0x1fff1fff)   # SC_BOTTOM_RIGHT
R(0x16c0, 0x00000003)   # DP_CNTL L->R, T->B
R(0x147c, 0x11111111)   # DP_BRUSH_FRGD_CLR
R(0x142c, 0x10008000)   # DST_PITCH_OFFSET: 1024 8bpp px, offset 0x100000
R(0x1594, 0x00000000)   # DST_X_Y
R(0x1598, 0x040000f0)   # DST_WIDTH_HEIGHT 1024x240: fires

# --- texture upload: 8x8 ARGB8888 at 0x200000
for row in range(8):
    for col in range(8):
        aper(0x200000 + (row * 8 + col) * 4,
             0xff00ff00 if col < 4 else 0xffff0000)

# --- 3D context for the textured triangles
R(0x142c, 0x08008000)   # DST_PITCH_OFFSET: 512 px (565), offset 0x100000
R(0x1c80, 0x08008000)   # DST_PITCH_OFFSET_C twin
R(0x16c4, 0x00000004)   # DP_DATATYPE: dst datatype [3:0] = 4, RGB565
R(0x071c, 0x0000005e)   # PM4_VC_FPU_SETUP: both faces solid, Gouraud
                        # (the reset value 0 culls both faces)
R(0x16cc, 0xffffffff)   # DP_WRITE_MASK
R(0x1660, 0x00000000)   # AUX_SC_CNTL
R(0x1bc4, 0x00000020)   # SETUP_CNTL
R(0x1bcc, 0x00000000)   # WINDOW_XY_OFFSET
R(0x1a00, 0x00000000)   # SCALE_3D_CNTL
R(0x1c9c, 0x00000010)   # TEX_CNTL_C: TEXMAP_EN only (bit 12 chroma OFF)
R(0x1cb0, 0x00060080)   # PRIM_TEX_CNTL_C: dt=6 ARGB8888, MIP_MAP_DISABLE, nearest
R(0x1cb4, 0x04185041)   # PRIM_TEX_COMBINE_CNTL_C: COPY(Ct)
R(0x1cb8, 0x00003333)   # TEX_SIZE_PITCH_C: 8x8
R(0x1cbc, 0x00200000)   # PRIM_TEX_0_OFFSET_C
R(0x1d44, 0xffffffff)   # PLANE_3D_MASK_C
R(0x1d34, 0x00ffffff)   # CONSTANT_COLOR_C
R(0x1a24, 0xff00ff00)   # CLR_CMP_CLR_3D = green (legacy key)
R(0x1a28, 0xffffffff)   # CLR_CMP_MSK_3D = full mask
R(0x1ca4, 0x00000000)   # TEX_CLR_CMP_CLR_C (GL path, unused)
R(0x1ca8, 0x00000000)   # TEX_CLR_CMP_MSK_C = 0 (GL path off)

def band(a):
    f, _, m = a.partition(":")
    return (int(f), int(m, 16) if m else 0xffffffff)


bands = [band(a) for a in sys.argv[1:]] or [(0, 0xffffffff), (3, 0xffffffff),
                                              (1, 0xffffffff), (2, 0xffffffff),
                                              (1, 0), (3, 0)]


def tri(y0):
    J(0xc0132500)           # type3 | 19<<16 | op 0x25
    J(0x00000081)           # VC_FORMAT xyz + rhw + st
    J(0x00030034)           # TRI_LIST | WALK_RING | 3 verts
    for (x, y, s, t) in ((0, y0, 0.0, 0.0), (32, y0, 1.0, 0.0), (0, y0 + 32, 0.0, 1.0)):
        for v in (float(x), float(y), 0.0, 1.0, s, t):
            J(f2u(v))


for i, (fcn, msk) in enumerate(bands):
    y0 = 40 * i
    R(0x1a28, msk)               # CLR_CMP_MSK_3D per band
    R(0x1ca0, (fcn & 3) << 30)   # MISC_3D_STATE_CNTL_REG: CLR_CMP_FCN_3D
    tri(y0)
    R(0x147c, 0x11111111)        # brush re-poke: CCE drain + raster flush
    # MM_DATA store into the drawn band (row y0+28, x 0..1): flush trigger
    aper(0x100000 + (y0 + 28) * 1024, 0x11111111)

R(0x147c, 0x11111111)
sys.stdout.write("\n".join(out) + "\n")
