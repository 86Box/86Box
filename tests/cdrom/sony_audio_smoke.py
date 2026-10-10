#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Verify Sony BIN/CUE data and CD-DA through real DOS drivers and OpenAL.

Requires Linux, mtools, genisoimage, GNU as/ld, a C compiler and OpenAL headers.
Supply your own PB430 ROMs, prepared CMOS, DOS disk and Sony drivers. The disk
must contain C:\DOS\MSCDEX.EXE. All writes go to copies in a new output folder.
See doc/hardware/sony-cdrom.md for setup and the limits of this test.
"""

import argparse
import array
from concurrent.futures import ThreadPoolExecutor
import ctypes
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from sony_slcd_smoke import assemble, run, SCREEN_SOURCE, DONE_SOURCE

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
    run(["genisoimage", "-quiet", "-V", "SONY_AUDIO", "-o", iso, content])
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
    run(["cc", "-shared", "-fPIC", "-O2", HERE / "sony_audio_capture.c",
         "-ldl", "-pthread", "-o", root / "capture.so"])
    for name, source in [("PROBE", (HERE / "sony_audio_probe.s").read_text()),
                         ("DONE", DONE_SOURCE), ("SCREEN", SCREEN_SOURCE)]:
        assemble(root, name, source)


def validate_probe(data, kind):
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
    checks["completion_position"] = absolute(16) == start + 75
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


def testcase(a, root, case):
    kind, model, mode, version, driver = case
    dest = root / f"{kind}-{model}-{mode}-{version}"
    (dest / "nvr").mkdir(parents=True)
    disk = dest / "dos.img"
    shutil.copyfile(a.dos_image, disk)
    shutil.copyfile(a.nvr, dest / "nvr/pb430.nvr")
    image = f"{disk}@@{a.partition_offset}"
    run(["mdir", "-i", image, "::DOS/MSCDEX.EXE"])
    for name in OUTPUTS + ["DONE.TAG"]:
        subprocess.run(["mdel", "-i", image, "::" + name], capture_output=True)
    base = a.creative_base if mode == "creative" else "340"
    options = " /M:D /T:3" if mode == "dma" else ""
    (dest / "CONFIG.SYS").write_text(f"DEVICE=C:\\SLCD.SYS /D:SONYCD /B:{base} /V{options}\r\n"
                                     "FILES=30\r\nBUFFERS=20\r\nLASTDRIVE=Z\r\n", newline="")
    lines = ["@ECHO OFF", r"C:\SCREEN.COM", r"C:\DOS\MSCDEX.EXE /D:SONYCD /L:R /V >C:\MSCDEX.LOG"]
    if kind == "mixed":
        lines += [r"DIR R:\ >C:\CDDIR.LOG", r"COPY /B R:\CHECK.BIN C:\CHECK.OUT >C:\COPY1.LOG",
                  r"COPY /B R:\ODD.BIN C:\ODD.OUT >C:\COPY2.LOG"]
    lines += [r"C:\PROBE.COM", r"C:\DONE.COM"]
    (dest / "AUTOEXEC.BAT").write_text("\r\n".join(lines) + "\r\n", newline="")
    for name in ["CONFIG.SYS", "AUTOEXEC.BAT"]:
        run(["mcopy", "-o", "-i", image, dest / name, "::" + name])
    for name in ["PROBE.COM", "DONE.COM", "SCREEN.COM"]:
        run(["mcopy", "-o", "-i", image, root / name, "::" + name])
    run(["mcopy", "-o", "-i", image, driver, "::SLCD.SYS"])
    interface = "sony_creative" if mode == "creative" else "sony_cdu31a"
    (dest / "86box.cfg").write_text(f"""[General]
vid_renderer = qt_software
[Machine]
machine = pb430
cpu_family = i486dx2
cpu_speed = 66666666
cpu_multi = 2
cpu_use_dynarec = 0
fpu_type = internal
mem_size = 8192
[Video]
gfxcard = internal
[Input devices]
mouse_type = none
[Storage controllers]
cdrom_interface = {interface}
[Hard disks]
hdd_01_fn = {disk}
hdd_01_parameters = {a.geometry}, 0, ide
hdd_01_ide_channel = 0:0
hdd_01_speed = ramdisk
[Floppy and CD-ROM drives]
fdd_01_type = 35_2hd
fdd_02_type = none
cdrom_01_image_path = {root / (kind + '.cue')}
cdrom_01_parameters = 1, sony
cdrom_01_type = sony_{model}
cdrom_01_speed = {2 if model == 'cdu33a' else 1}
[SONY CDU31A interface]
base = 0340
irq = -1
dma = {3 if mode == 'dma' else -1}
[SONY/Creative interface]
base = 0{a.creative_base}
""")
    global_cfg = dest / "global.cfg"
    global_cfg.write_text("[Emulator]\nvmm_path = " + str(dest) + "/\n")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", ALSOFT_DRIVERS="null",
               LD_PRELOAD=str(root / "capture.so"), SONY_AUDIO_CAPTURE=str(dest / "capture.dat"))
    start = time.monotonic()
    complete = False
    with (dest / "console.log").open("wb") as log:
        proc = subprocess.Popen([str(a.emulator), "-N", "-O", str(global_cfg), "-P", str(dest),
                                 "-R", str(a.roms), "-L", str(dest / "86box.log")],
                                env=env, stdout=log, stderr=log)
        try:
            while proc.poll() is None and time.monotonic() - start < a.timeout:
                if subprocess.run(["mdir", "-i", image, "::DONE.TAG"], capture_output=True).returncode == 0:
                    time.sleep(0.25)
                    complete = True
                    break
                time.sleep(0.25)
        finally:
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    checks = {"completed": complete}
    for name in OUTPUTS:
        data = subprocess.run(["mtype", "-i", image, "::" + name], capture_output=True)
        (dest / name).write_bytes(data.stdout)
        if kind == "mixed" and name.endswith(".OUT"):
            checks[name] = data.returncode == 0 and data.stdout == (
                root / "iso-content" / name.replace(".OUT", ".BIN")).read_bytes()
    records, probe_checks = validate_probe((dest / "PROBE.BIN").read_bytes(), kind)
    capture, capture_checks = validate_capture(dest / "capture.dat", (root / "tone.bin").read_bytes())
    checks.update(probe_checks)
    checks.update(capture_checks)
    result = dict(case=dest.name, passed=all(checks.values()), checks=checks, records=records,
                  capture=capture, seconds=round(time.monotonic() - start, 1),
                  driver_sha256=hashlib.sha256(driver.read_bytes()).hexdigest())
    (dest / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ["case", "passed", "checks", "capture"]}), flush=True)
    if result["passed"]:
        disk.unlink()  # Keep failing guests for investigation; bound matrix disk usage.
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ["emulator", "dos-image", "nvr", "roms", "output"]:
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--driver", action="append", required=True, metavar="VERSION=PATH")
    p.add_argument("--geometry", default="63,16,854")
    p.add_argument("--partition-offset", type=int, default=32256)
    p.add_argument("--timeout", type=float, default=90)
    p.add_argument("--workers", type=int, default=1)
    p.add_argument("--modes", nargs="+", choices=["sony", "creative", "dma"], default=["sony", "creative"])
    p.add_argument("--media", nargs="+", choices=["mixed", "audio"], default=["mixed", "audio"])
    p.add_argument("--creative-base", choices=["230", "250", "270", "290"], default="230")
    a = p.parse_args()
    if (not re.fullmatch(r"[1-9][0-9]*,[1-9][0-9]*,[1-9][0-9]*", a.geometry)
            or a.partition_offset < 0 or a.timeout <= 0 or a.workers < 1):
        p.error("Invalid geometry, partition offset, timeout or worker count")
    if sys.byteorder != "little":
        p.error("The capture helper requires a little-endian Linux host")
    drivers = []
    for entry in a.driver:
        version, sep, filename = entry.partition("=")
        if not sep or not re.fullmatch(r"[A-Za-z0-9_.-]+", version):
            p.error("--driver requires a simple version label followed by =PATH")
        drivers.append((version, Path(filename).resolve(strict=True)))
    if len({v for v, _ in drivers}) != len(drivers):
        p.error("Driver version labels must be unique")
    for name in ["emulator", "dos_image", "nvr", "roms"]:
        setattr(a, name, getattr(a, name).resolve(strict=True))
    for name in ["mcopy", "mdel", "mtype", "mdir", "genisoimage", "as", "ld", "cc"]:
        if not shutil.which(name):
            p.error("Missing executable: " + name)
    root = a.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    fixtures(root)
    cases = [(kind, model, mode, version, driver)
             for kind, model, mode, (version, driver) in itertools.product(
                 dict.fromkeys(a.media), ["cdu31a", "cdu33a"], dict.fromkeys(a.modes), drivers)]
    results = []
    with ThreadPoolExecutor(max_workers=a.workers) as pool:
        for result in pool.map(lambda case: testcase(a, root, case), cases):
            results.append(result)
            (root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
