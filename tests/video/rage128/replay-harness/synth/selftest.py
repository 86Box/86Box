#!/usr/bin/env python3
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          ATI Rage 128 Pro -- self-test of the capture replay harness on
#          the synthetic captures.
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

"""Replay the synthetic captures and check their pixels.

usage: selftest.py <replay binary> <work directory>

Generates the gen_agptex.py capture in both modes (agp, gart) and the
gen_ckfn.py capture, replays each with --dump-vram, and checks the image.
Exit 0 when every check passes. CTest runs this as Rage128.ReplaySynth.

Both streams draw into an RGB565 surface at 0x100000 (pitch 1024 bytes)
over a background of 0x1111, from an 8x8 ARGB8888 texture whose columns
0-3 are green (the color-compare key) and columns 4-7 red.

gen_ckfn.py draws one right triangle per 40-line band, each under its own
CLR_CMP_FCN_3D code and CLR_CMP_MSK_3D. The expected colors follow the
device's model of the legacy 3D color compare (vid_ati_rage128_3d.c): code
0 discards nothing; code 3 discards the texel that matches the key; code 2
discards every texel that does not; code 1 discards every texel whatever
the mask; a zero mask turns codes 2 and 3 off.
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASE, PITCH = 0x100000, 1024
GREEN, RED, BG = 0x07E0, 0xF800, 0x1111

# (CLR_CMP_FCN_3D, CLR_CMP_MSK_3D) per band, as gen_ckfn.py draws them, and
# the texel colors that survive the compare.
CKFN_BANDS = (
    ((0, 0xFFFFFFFF), {GREEN, RED}),
    ((3, 0xFFFFFFFF), {RED}),
    ((1, 0xFFFFFFFF), set()),
    ((2, 0xFFFFFFFF), {GREEN}),
    ((1, 0x00000000), set()),
    ((3, 0x00000000), {GREEN, RED}),
)

failures = []


def check(ok, what):
    print("%-4s %s" % ("ok" if ok else "FAIL", what))
    if not ok:
        failures.append(what)


def pixel(vram, x, y):
    return struct.unpack_from("<H", vram, BASE + y * PITCH + x * 2)[0]


def replay(binary, work, name, gen_args):
    cap = os.path.join(work, name + ".cap")
    img = os.path.join(work, name + ".vram")
    log = os.path.join(work, name + ".log")
    with open(cap, "w") as f:
        subprocess.run([sys.executable] + gen_args, stdout=f, check=True)
    with open(log, "w") as f:
        rc = subprocess.run([binary, "--dump-vram", img, cap], stdout=f,
                            stderr=subprocess.STDOUT).returncode
    check(rc == 0, "%s: replay exit status 0 (got %d, see %s)" % (name, rc, log))
    if not os.path.exists(img):
        check(False, "%s: VRAM image written" % name)
        return None
    with open(img, "rb") as f:
        return f.read()


def agptex(binary, work, mode):
    vram = replay(binary, work, "agptex-" + mode,
                  [os.path.join(HERE, "gen_agptex.py"), mode])
    if vram is None:
        return
    for x, want, what in ((4, GREEN, "green texel"), (24, RED, "red texel"),
                          (40, BG, "background")):
        got = pixel(vram, x, 4)
        check(got == want, "agptex %s: (%d, 4) %s %04x (got %04x)"
              % (mode, x, what, want, got))


def ckfn(binary, work):
    vram = replay(binary, work, "ckfn", [os.path.join(HERE, "gen_ckfn.py")])
    if vram is None:
        return
    for i, ((fcn, msk), keep) in enumerate(CKFN_BANDS):
        y0 = 40 * i
        seen = set()
        stray = 0
        for y in range(y0, y0 + 32):
            for x in range(32):
                if x + y - y0 >= 31:        # outside the right triangle
                    continue
                p = pixel(vram, x, y)
                if p in (GREEN, RED):
                    seen.add(p)
                elif p != BG:
                    stray += 1
        tag = "ckfn band %d (code %d, mask %08x)" % (i, fcn, msk)
        check(seen == keep, "%s: texel colors %s (got %s)"
              % (tag, sorted("%04x" % c for c in keep), sorted("%04x" % c for c in seen)))
        check(stray == 0, "%s: no pixel other than texel or background (got %d)"
              % (tag, stray))
        # The sample points: (4, y0+4) is a green texel, (24, y0+4) a red one.
        for x, color in ((4, GREEN), (24, RED)):
            want = color if color in keep else BG
            got = pixel(vram, x, y0 + 4)
            check(got == want, "%s: (%d, %d) %04x (got %04x)" % (tag, x, y0 + 4, want, got))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    binary, work = os.path.abspath(sys.argv[1]), sys.argv[2]
    os.makedirs(work, exist_ok=True)
    agptex(binary, work, "agp")
    agptex(binary, work, "gart")
    ckfn(binary, work)
    print("%s: %d failed" % ("FAIL" if failures else "PASS", len(failures)))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
