#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild a System DBCS font file (family A) or an IBM DOS-K / HANZI DOS font
into a 5550 font-card image (512 KB = 16 pages x 32 KB).

Two source layouts are accepted:

* **DOS/V "family A"** -- carries its own tables: T1 at 0x00C0 (264 B) and T2
  at 0x0200 (0x800 B), followed by the glyph record stream. Used as-is.
* **Raw (DOS-K 3.44 / HANZI DOS P2.61)** -- T2 at 0x0000 (0x800 B) and the
  record stream from the first 0x200-aligned block, but **no T1 table**. The
  OS supplies T1 from its IBMBIO, so building from a raw font requires
  ``--ibmbio`` (see ``--t1-offset`` if auto-detection fails).

Recipe (reverse-engineered 2026-10-10 by byte-for-byte md5 comparison against
the deployed cards):
  1. Base = the real 5535-M card ROM (roms/machines/ibm5535/65X1460.BIN).
  2. Zero the pointer table 0x0000..0x5820 and the attribute table
     0x5820..0x6E28 (11280 cells each).
  3. Copy T2 to 0x6E32 (0x800 B) and T1 to 0x763C (264 B); both must belong to
     the same source as the records.
  4. Records are placed only into pages 1,2,3,4,6 (16-dot) / 1..8,10 (24-dot):
     the ANK page for that dot size is skipped, each page starts at offset 0,
     and page 0 is never written.
  5. Pointer = page-internal offset (2 B big-endian); attribute nibble = the
     page number itself (4 bits/cell, even high / odd low).
  6. Self-check on decode: address every cell through stage1/stage2 and compare
     byte-for-byte against the source record.

Usage:
  python rebuild_card.py probe <src> [<src> ...]
  python rebuild_card.py build <src> <16|24> <start_hex> <out> <base_rom>
                                [--ibmbio IBMBIO] [--t1-offset HEX] [--patch-7ff7 HEX]

No firmware is distributed. See doc/hardware/ibm5550-fontcard.md.
"""
import argparse
import hashlib
import os
import sys

sys.dont_write_bytecode = True

PAGE = 32 * 1024
PTR0, PTRN   = 0x0000, 0x5820      # pointer table: 2 B/cell, 0..0x5820 = 11280 cells (= attr start / 2)
ATTR0, ATTRN = 0x5820, 0x1608      # attribute table: 4 bits/cell, 0x5820..0x6E28 = 11280 cells
T2OFF, T1OFF = 0x6E32, 0x763C
T1_SIZE, T2_SIZE = 264, 0x800
SRC_T1, SRC_T2   = 0x00C0, 0x0200  # family-A (DOS/V) in-file offsets
PAGES = {16: [1, 2, 3, 4, 6], 24: [1, 2, 3, 4, 5, 6, 7, 8, 10]}
DIMS  = {16: (32, 4, 2), 24: (72, 6, 3)}   # dl, d6, d3
PROBE_STARTS = (0x800, 0xC00, 0x1000, 0x1400, 0x1800, 0x1C00, 0x2000,
                0x2400, 0x2800, 0x2C00)
# Leading bytes of the 5550 T1 bit-length tables as shipped in IBM DOS-K 3.44
# and HANZI DOS P2.61 IBMBIO; used only to locate T1 inside --ibmbio.
T1_ANCHOR = {16: bytes.fromhex("008a8c8b"), 24: bytes.fromhex("008b7a7a")}


def load(path):
    return open(path, "rb").read()


def grab(b, bs):
    si, recs = bs, []; lim = bs + 0x200
    while si + 3 <= min(lim, len(b)):
        key = b[si] | (b[si+1] << 8)
        if key == 0xFFFF: break
        ln = b[si+2]
        if ln == 0 or ln > 0x7F or si + 3 + ln > len(b): break
        k = key & 0x3FFF
        if not (0x100 <= k <= 0x2D0F): break
        recs.append((si, k, ln)); si += 3 + ln
    return recs


def collect(b, start):
    seen = {}; bs = start
    while bs < len(b):
        r = grab(b, bs)
        if not r:
            if not (bs + 0x200 < len(b) and grab(b, bs + 0x200)): break
            bs += 0x200; continue
        for si, k, ln in r:
            if k not in seen: seen[k] = (si, ln)
        bs += 0x200
    return seen


def stage1(p, t1, dl):
    buf = p + bytes(512); si = cl = ch = al = 0; out = bytearray()
    while len(out) < dl:
        if si + 3 >= len(buf): break
        w = buf[si] | (buf[si+1] << 8); c = cl & 15
        r = (((w << c) | (w >> (16-c))) & 0xFFFF) if c else w
        if (r & 0xFF) == 0:
            if cl <= 5: bx2, n = r, 3
            else:
                bx2 = buf[si+1] | (buf[si+2] << 8); n = 2 if cl == 7 else 1
            for _ in range(n): bx2 = ((bx2 << 1) | (bx2 >> 15)) & 0xFFFF
            bx = 0x100 | (bx2 & 7); si += 1
        else: bx = r & 0xFF
        ah = t1[bx]; ch ^= 4
        if ch != 0: al = (ah << 4) & 0xFF
        else:
            al |= ah & 0x0F; out.append(al)
            if len(out) >= dl: break
        cl += ah >> 4
        if cl >= 8: si += 1; cl -= 8
    return bytes(out)


def stage2(buf, T2, d6, d3):
    n = len(buf); work = bytearray(b"\0"*6) + bytearray(buf)
    for i in range(n):
        di = 6 + i
        ch, ah, al = work[di-d6], work[di-d3], work[di]
        bh = (ah << 4) & 0xFF; bl = ((al & 0x0F) | bh) & 0xFF
        bp = (((ch & 0x0F) << 8) | bl) & 0xFFFF
        bl2 = ((al >> 4) | (ah & 0xF0)) & 0xFF
        bx2 = (((ch >> 4) & 0x0F) << 8) | bl2
        cf = bx2 & 1; bx2 >>= 1
        hi = T2[bx2 & 0x7FF]
        if cf: hi = (hi << 4) & 0xFF
        hi &= 0xF0
        cf2 = bp & 1; bp >>= 1
        lo = T2[bp & 0x7FF]
        if cf2 == 0: lo >>= 4
        lo &= 0x0F
        work[di] = (hi | lo) & 0xFF
    return bytes(work[6:6+n])


def has_own_tables(src):
    """True for DOS/V family-A fonts (T1 at 0xC0, T2 at 0x200)."""
    if len(src) < SRC_T2 + T2_SIZE:
        return False
    if src[SRC_T1] != 0x00:
        return False
    return all(((src[SRC_T1 + i] >> 4) <= 8) for i in range(T1_SIZE))


def locate_t1(ibmbio, dots, offset=None):
    if offset is not None:
        if offset + T1_SIZE > len(ibmbio):
            raise SystemExit("--t1-offset 0x%X is past the end of the IBMBIO" % offset)
        return offset
    anchor = T1_ANCHOR[dots]
    hits = [i for i in range(len(ibmbio) - (T1_SIZE - 1)) if ibmbio[i:i+len(anchor)] == anchor]
    if len(hits) == 1:
        return hits[0]
    if not hits:
        raise SystemExit("no %d-dot T1 table found in the IBMBIO; pass --t1-offset" % dots)
    raise SystemExit("ambiguous %d-dot T1 in the IBMBIO at %s; pass --t1-offset"
                     % (dots, ", ".join("0x%X" % h for h in hits[:8])))


def build(src_path, dots, start, out_path, base_path, ibmbio_path=None, t1_offset=None,
          patch_7ff7=None):
    dl, d6, d3 = DIMS[dots]
    pages = PAGES[dots]
    SRC = load(src_path); img = bytearray(load(base_path))
    if has_own_tables(SRC):
        t2_off, T1 = SRC_T2, SRC[SRC_T1:SRC_T1+T1_SIZE]
        mode = "family A (DOS/V)"
    else:
        if ibmbio_path is None:
            raise SystemExit("source has no T1 table (raw DOS-K/HANZI layout); "
                             "pass --ibmbio so T1 can be taken from the OS")
        T1 = load(ibmbio_path)
        T1 = T1[locate_t1(T1, dots, t1_offset):][:T1_SIZE]
        t2_off = 0x0000
        mode = "raw (T1 from IBMBIO)"
    img[PTR0:PTR0+PTRN]   = bytes(PTRN)
    img[ATTR0:ATTR0+ATTRN] = bytes(ATTRN)
    img[T2OFF:T2OFF+T2_SIZE] = SRC[t2_off:t2_off+T2_SIZE]
    img[T1OFF:T1OFF+T1_SIZE] = T1
    rec = collect(SRC, start); cells = sorted(rec)
    ptr = {}; pi = 0; cur = 0
    for k in cells:
        si, ln = rec[k]; pl = SRC[si+3:si+3+ln]
        if cur + len(pl) > PAGE:
            pi += 1; cur = 0
            if pi >= len(pages): raise SystemExit("records do not fit: need more pages")
        pg = pages[pi]
        img[pg*PAGE+cur: pg*PAGE+cur+len(pl)] = pl
        ptr[k] = (pg, cur); cur += len(pl)
    for k in cells:
        pg, off = ptr[k]; cell = k - 0x100
        img[cell*2] = (off >> 8) & 0xFF; img[cell*2+1] = off & 0xFF
        nib = min(15, pg); byi = ATTR0 + cell // 2
        img[byi] = ((img[byi] & 0x0F) | (nib << 4)) if cell % 2 == 0 else ((img[byi] & 0xF0) | nib)
    if patch_7ff7 is not None:
        img[0x7FF7:0x7FFB] = bytes.fromhex(patch_7ff7)
    T1x = bytes(img[T1OFF:T1OFF+T1_SIZE]); T2x = bytes(img[T2OFF:T2OFF+T2_SIZE])
    ok = 0
    for k in cells:
        cell = k - 0x100; si, ln = rec[k]
        by = img[ATTR0 + cell//2]; nib = (by >> 4) if cell % 2 == 0 else (by & 0x0F)
        off = (img[cell*2] << 8) | img[cell*2+1]
        g = stage2(stage1(bytes(img[nib*PAGE+off: nib*PAGE+off+ln]), T1x, dl), T2x, d6, d3)
        if g == stage2(stage1(SRC[si+3:si+3+ln], T1x, dl), T2x, d6, d3): ok += 1
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    open(out_path, "wb").write(bytes(img))
    print("  %-28s %2d-dot  %-20s records %4d  keys 0x%X..0x%X  pages %s  decode %d/%d  md5=%s"
          % (os.path.basename(out_path), dots, mode, len(cells), min(cells), max(cells),
             ",".join(str(x) for x in pages[:pi+1]), ok, len(cells),
             hashlib.md5(bytes(img)).hexdigest()[:12]))
    return bytes(img)


def cmd_probe(a):
    for f in a.src:
        b = load(f)
        print("%s (%d B)  %s" % (f, len(b), "family A" if has_own_tables(b) else "raw"))
        for st in PROBE_STARTS:
            r = collect(b, st)
            print("   0x%04X -> %4d records  payload %6d  %s"
                  % (st, len(r), sum(l for _, l in r.values()),
                     ("0x%X..0x%X" % (min(r), max(r))) if r else "-"))
    return 0


def cmd_build(a):
    build(a.src, a.dots, a.start, a.out, a.base_rom, a.ibmbio, a.t1_offset, a.patch_7ff7)
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command")

    q = sub.add_parser("probe", help="probe the record-stream start of a font file")
    q.add_argument("src", nargs="+", help="System DBCS font file(s) to probe")

    b = sub.add_parser("build", help="build a font-card image from a font file")
    b.add_argument("src", help="System DBCS font file")
    b.add_argument("dots", type=int, choices=[16, 24], help="dot size (16 or 24)")
    b.add_argument("start", type=lambda s: int(s, 16), help="record-stream start offset (hex)")
    b.add_argument("out", help="output card image path")
    b.add_argument("base_rom", help="real 5535-M card ROM used as the base image")
    b.add_argument("--ibmbio", help="IBMBIO file supplying T1 for raw fonts (required if src has no T1)")
    b.add_argument("--t1-offset", type=lambda s: int(s, 16),
                   help="offset of T1 inside --ibmbio (hex); default is auto-detect")
    b.add_argument("--patch-7ff7", metavar="HEX",
                   help="write this 4-byte value at 0x7FF7 in the output (e.g. 0AA00031 for the Chinese card)")

    a = p.parse_args(argv)
    if a.command == "probe":
        return cmd_probe(a)
    if a.command == "build":
        return cmd_build(a)
    p.error("choose a subcommand: probe or build")


if __name__ == "__main__":
    raise SystemExit(main())
