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
#          draws one triangle whose texture the device reads from system
#          memory over the bus, through the AGP aperture or the on-chip
#          GART.
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

"""Synthetic capture: one textured triangle whose 8x8 ARGB8888 texture
lives in the AGP half, so the device gathers it through
rage128_pm4_bus_read_block and the replay must serve the texels from a B
record. Same surface and geometry as gen_ckfn.py (RGB565 dst at 0x100000,
pitch 512 px, background 0x11 bytes; cols 0-3 green 0xFF00FF00, cols 4-7
red 0xFFFF0000), chroma key off.

Modes (argv[1]):
  agp   (default) AGP_BASE 0xd0000000, PCI_GART_PAGE bit0 set: the block
        goes through the AGP branch as one dma_bm_read; the B record is
        served whole.
  gart  PCI_GART_PAGE = table at 0x01000000, bit0 clear: the block goes
        dword by dword through the on-chip PTE walk; the replay plants an
        out-of-RAM table at that base and the B record is served per dword.

Sample: (4, 4) = texel col 1 (green 0x07e0), (24, 4) = col 6 (red 0xf800),
(40, 4) = background 0x1111. See sample_agptex.py.
"""
import struct
import sys

mode = sys.argv[1] if len(sys.argv) > 1 else "agp"
out = []


def R(off, val, mask=0xffffffff):
    out.append("R %04x %08x %08x" % (off, val & 0xffffffff, mask))


def J(v):
    out.append("J %08x" % (v & 0xffffffff))


def f2u(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


AGP_BASE = 0xd0000000
TEX_OFF = 0x60000                 # offset inside the AGP half
TEX_VM = 0x02000000 | TEX_OFF     # bit25 = AGP half of card space

if mode == "agp":
    R(0x0170, AGP_BASE)           # AGP_BASE
    R(0x017c, 0x00000001)         # PCI_GART_PAGE: on-chip GART disabled
    tex_bus = AGP_BASE + TEX_OFF  # what the AGP branch records
elif mode == "gart":
    R(0x0170, 0x00000000)         # no aperture: the on-chip table resolves
    R(0x017c, 0x01000000)         # PCI_GART_PAGE: table at 16 MB, bit0 clear
    tex_bus = TEX_VM              # the walk records the GART-relative address
else:
    sys.exit("mode: agp | gart")

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

# --- 3D context with the texture in the AGP half
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
R(0x1c9c, 0x00000010)   # TEX_CNTL_C: TEXMAP_EN only
R(0x1cb0, 0x00060080)   # PRIM_TEX_CNTL_C: dt=6 ARGB8888, MIP_MAP_DISABLE, nearest
R(0x1cb4, 0x04185041)   # PRIM_TEX_COMBINE_CNTL_C: COPY(Ct)
R(0x1cb8, 0x00003333)   # TEX_SIZE_PITCH_C: 8x8
R(0x1cbc, TEX_VM)       # PRIM_TEX_0_OFFSET_C: AGP half
R(0x1d44, 0xffffffff)   # PLANE_3D_MASK_C
R(0x1d34, 0x00ffffff)   # CONSTANT_COLOR_C
R(0x1ca0, 0x00000000)   # MISC_3D_STATE_CNTL_REG: CLR_CMP_FCN_3D = 0 (off)
R(0x1ca8, 0x00000000)   # TEX_CLR_CMP_MSK_C = 0

# --- the texture bytes, as the tap records the gather: B <bus> <len> <hex>
texels = bytearray()
for row in range(8):
    for col in range(8):
        texels += struct.pack("<I", 0xff00ff00 if col < 4 else 0xffff0000)
out.append("B %08x %x %s" % (tex_bus, len(texels), texels.hex()))

# --- one right triangle at (0,0)-(32,0)-(0,32)
J(0xc0132500)           # type3 | 19<<16 | op 0x25
J(0x00000081)           # VC_FORMAT xyz + rhw + st
J(0x00030034)           # TRI_LIST | WALK_RING | 3 verts
for (x, y, s, t) in ((0, 0, 0.0, 0.0), (32, 0, 1.0, 0.0), (0, 32, 0.0, 1.0)):
    for v in (float(x), float(y), 0.0, 1.0, s, t):
        J(f2u(v))

R(0x147c, 0x11111111)   # brush re-poke: CCE drain + raster flush
R(0x0000, 0x80000000 | (0x100000 + 28 * 1024))  # MM_INDEX aperture store into
R(0x0004, 0x11111111)                            # the drawn band: flush trigger
R(0x147c, 0x11111111)
sys.stdout.write("\n".join(out) + "\n")
