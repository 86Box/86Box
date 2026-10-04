# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared DOS/MSCDEX fixtures and CD audio validation for smoke tests."""

import array
import ctypes
import math
from pathlib import Path
import struct
import subprocess


SCREEN_SOURCE = r'''
.intel_syntax noprefix
.code16
.text
.global _start
_start:
    mov ah,0x3c
    xor cx,cx
    mov dx,offset filename
    int 0x21
    jc done
    mov bx,ax
    push ds
    pop es
    mov di,offset buffer
    mov ax,0xb800
    mov ds,ax
    xor si,si
    mov dx,25
row:
    mov cx,80
col:
    lodsw
    stosb
    loop col
    mov ax,0x0a0d
    stosw
    dec dx
    jnz row
    push es
    pop ds
    mov ah,0x40
    mov cx,2050
    mov dx,offset buffer
    int 0x21
    mov ah,0x3e
    int 0x21
done:
    mov ax,0x4c00
    int 0x21
filename: .asciz "C:\\SCREEN.TXT"
buffer: .space 2050
'''

DONE_SOURCE = r'''
.intel_syntax noprefix
.code16
.text
.global _start
_start:
    mov ah,0x0d
    int 0x21
    xor cx,cx
    mov dx,offset filename
    mov ah,0x3c
    int 0x21
    jc halt
    mov bx,ax
    mov ah,0x3e
    int 0x21
    mov ah,0x0d
    int 0x21
halt:
    sti
    hlt
    jmp halt
filename: .asciz "C:\\DONE.TAG"
'''


def run(argv, **kwargs):
    return subprocess.run([str(x) for x in argv], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)


def assemble(root, name, source):
    asm = root / (name + ".s")
    obj = root / (name + ".o")
    com = root / (name + ".COM")
    asm.write_text(source)
    run(["as", "--32", "-o", obj, asm])
    run(["ld", "-m", "elf_i386", "-Ttext", "0x100", "--oformat", "binary",
         "-o", com, obj])
    return com


HERE = Path(__file__).resolve().parent
OUTPUTS = ["PROBE.BIN", "SCREEN.TXT", "MSCDEX.LOG", "CDDIR.LOG",
           "COPY1.LOG", "COPY2.LOG", "CHECK.OUT", "ODD.OUT"]


def fixtures(root):
    content = root / "iso-content"
    content.mkdir()
    for name, size in [("CHECK.BIN", 131072), ("ODD.BIN", 100003)]:
        (content / name).write_bytes(bytes((i * 37 + (i >> 8) * 17 + 11) & 255
                                          for i in range(size)))
    iso = root / "test.iso"
    run(["genisoimage", "-quiet", "-V", "CDROM_TEST", "-o", iso, content])
    # Use the repository's ECC generator to produce valid MODE1/2352 sectors.
    ecc_so = root / "ecc.so"
    run(["cc", "-shared", "-fPIC", "-O2",
         HERE.parents[1] / "src/cdrom/libchdr/src/libchdr_cdrom.c", "-o", ecc_so])
    ecc = ctypes.CDLL(str(ecc_so))
    ecc.ecc_generate.argtypes = [ctypes.c_void_p]
    ecc.ecc_verify.argtypes = [ctypes.c_void_p]
    ecc.ecc_verify.restype = ctypes.c_int
    table = []
    for value in range(256):
        for _ in range(8):
            value = (value >> 1) ^ (0xd8018001 if value & 1 else 0)
        table.append(value)
    bcd = lambda value: ((value // 10) << 4) | (value % 10)
    data = iso.read_bytes()
    raw = bytearray()
    for lba in range(len(data) // 2048):
        minute, remainder = divmod(lba + 150, 4500)
        second, frame = divmod(remainder, 75)
        sector = bytearray(b"\0" + b"\xff" * 10 + b"\0")
        sector += bytes([bcd(minute), bcd(second), bcd(frame), 1])
        sector += data[lba * 2048:(lba + 1) * 2048] + bytes(288)
        edc = 0
        for value in sector[:2064]:
            edc = (edc >> 8) ^ table[(edc ^ value) & 255]
        struct.pack_into("<I", sector, 2064, edc)
        buf = (ctypes.c_ubyte * 2352).from_buffer(sector)
        ecc.ecc_generate(buf)
        if not ecc.ecc_verify(buf):
            raise RuntimeError("Generated sector failed ECC verification")
        raw += sector
    tone = b"".join(struct.pack("<hh",
                               round(12000 * math.sin(2 * math.pi * (i % 100) / 100)),
                               round(7000 * math.sin(2 * math.pi * (i % 137) / 137)))
                    for i in range(441000))
    (root / "tone.bin").write_bytes(tone)
    for kind, prefix in [("mixed", raw), ("audio", b"")]:
        (root / (kind + ".bin")).write_bytes(prefix + tone + tone)
        lines = [f'FILE "{kind}.bin" BINARY']
        if prefix:
            lines += ["  TRACK 01 MODE1/2352", "    INDEX 01 00:00:00"]
        for n in range(2):
            minute, remainder = divmod(len(prefix) // 2352 + n * 750, 4500)
            second, frame = divmod(remainder, 75)
            lines += [f"  TRACK {n + 1 + bool(prefix):02d} AUDIO",
                      f"    INDEX 01 {minute:02d}:{second:02d}:{frame:02d}"]
        (root / (kind + ".cue")).write_text("\n".join(lines) + "\n")
    run(["cc", "-shared", "-fPIC", "-O2", HERE / "cdrom_audio_capture.c",
         "-ldl", "-pthread", "-o", root / "capture.so"])
    for name, source in [("PROBE", (HERE / "mscdex_audio_probe.s").read_text()),
                         ("DONE", DONE_SOURCE), ("SCREEN", SCREEN_SOURCE)]:
        assemble(root, name, source)


def validate_probe(data, kind, final_frame_offset=75):
    records = []
    for offset in range(0, len(data) - 19, 20):
        seq, status = struct.unpack_from("<HH", data, offset)
        records.append(dict(id=seq, status=status, data=data[offset + 4:offset + 20].hex()))
    checks = {"complete_probe": len(data) == 320 and [r["id"] for r in records] == list(range(1, 17))}
    if not checks["complete_probe"]:
        return records, checks
    payload = {r["id"]: bytes.fromhex(r["data"]) for r in records}
    busy = {4, 5, 6, 11, 12, 14}
    checks["request_status"] = all(r["status"] == (0x300 if r["id"] in busy else 0x100) for r in records)
    last = 3 if kind == "mixed" else 2
    checks["toc"] = (payload[1][1:3] == bytes([1, last]) and payload[2][1] == 1
                     and payload[3][1] == last and bool(payload[2][6] & 0x40) == (kind == "mixed")
                     and not payload[3][6] & 0x40)
    checks["pause_status"] = payload[8][1:3] == b"\x01\0" and payload[15][1:3] == b"\0\0"
    # MSCDEX Q-channel IOCTL uses binary M:S:F; track/index are packed BCD.
    absolute = lambda record: (payload[record][8] * 60 + payload[record][9]) * 75 + payload[record][10]
    start = (payload[3][4] * 60 + payload[3][3]) * 75 + payload[3][2]
    leadout = (payload[1][5] * 60 + payload[1][4]) * 75 + payload[1][3]
    checks["toc_positions"] = payload[2][2:6] == b"\0\x02\0\0" and leadout == start + 750
    checks["q_track"] = all(payload[n][2:4] == bytes([last, 1]) for n in [5, 6, 9, 10, 12, 16])
    checks["playing_advances"] = start < absolute(5) < absolute(6)
    checks["pause_holds"] = absolute(9) == absolute(10)
    checks["resume_advances"] = absolute(12) > absolute(10)
    checks["completion_position"] = absolute(16) == start + final_frame_offset
    return records, checks


def validate_capture(path, tone):
    capture = path.read_bytes() if path.exists() else b""
    offset = nonzero = matched = blocks = 0
    valid = True
    while offset + 12 <= len(capture):
        fmt, size, rate = struct.unpack_from("<III", capture, offset)
        offset += 12
        samples = capture[offset:offset + size]
        offset += size
        if rate != 44100 or len(samples) != size:
            valid = False
            break
        if fmt == 0x10011:  # AL_FORMAT_STEREO_FLOAT32
            values = array.array("f", samples)
            pcm = b"".join(struct.pack("<h", max(-32768, min(32767, round(x * 32768)))) for x in values)
        elif fmt == 0x1103:  # AL_FORMAT_STEREO16
            pcm = samples
        else:
            valid = False
            break
        blocks += 1
        if any(pcm):
            nonzero += 1
            matched += pcm in tone
    return dict(blocks=blocks, nonzero=nonzero, matched=matched), {
        "capture_valid": valid and offset == len(capture),
        "audio_pcm": nonzero >= 20 and matched == nonzero,
    }
