#!/usr/bin/env python3
r"""Boot external Sony DOS drivers and verify ISO file copies in an isolated VM.

Requires Linux, mtools, genisoimage, GNU as/ld, PB430 ROMs, a prepared PB430
NVR, and a bootable DOS raw disk with C:\DOS\MSCDEX.EXE. See
doc/hardware/sony-cdrom.md. No firmware, DOS, or vendor driver is distributed.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


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


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--emulator", required=True, type=Path)
    p.add_argument("--dos-image", required=True, type=Path)
    p.add_argument("--nvr", required=True, type=Path, help="Prepared PB430 CMOS file")
    p.add_argument("--roms", required=True, type=Path)
    p.add_argument("--driver", action="append", required=True, metavar="VERSION=PATH")
    p.add_argument("--output", required=True, type=Path, help="New directory; must not exist")
    p.add_argument("--geometry", default="63,16,854", help="DOS disk sectors,heads,cylinders")
    p.add_argument("--partition-offset", type=int, default=32256)
    p.add_argument("--timeout", type=float, default=60)
    p.add_argument("--modes", nargs="+", choices=["sony", "creative", "dma"],
                   default=["sony", "creative", "dma"])
    p.add_argument("--base", choices=["320", "330", "340", "360"], default="340")
    p.add_argument("--creative-base", choices=["230", "250", "270", "290"], default="230")
    p.add_argument("--dma", type=int, choices=[1, 2, 3], default=3)
    a = p.parse_args()
    if not re.fullmatch(r"[1-9][0-9]*,[1-9][0-9]*,[1-9][0-9]*", a.geometry):
        p.error("--geometry must be sectors,heads,cylinders")
    if a.partition_offset < 0 or a.timeout <= 0:
        p.error("Offset must be nonnegative and timeout positive")
    drivers = []
    for entry in a.driver:
        version, sep, filename = entry.partition("=")
        if not sep or not re.fullmatch(r"[A-Za-z0-9_.-]+", version):
            p.error("--driver requires a simple version label followed by =PATH")
        path = Path(filename).resolve(strict=True)
        drivers.append((version, path))
    if len({v for v, _ in drivers}) != len(drivers):
        p.error("Driver version labels must be unique")
    for name in ["emulator", "dos_image", "nvr", "roms"]:
        setattr(a, name, getattr(a, name).resolve(strict=True))
    for name in ["mcopy", "mdel", "mtype", "mdir", "genisoimage", "as", "ld"]:
        if not shutil.which(name):
            p.error("Missing executable: " + name)
    root = a.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    disk = root / "dos.img"
    shutil.copyfile(a.dos_image, disk)
    image = f"{disk}@@{a.partition_offset}"
    run(["mdir", "-i", image, "::DOS/MSCDEX.EXE"])
    content = root / "iso-content"
    content.mkdir()
    for name, size in [("CHECK.BIN", 131072), ("ODD.BIN", 100003)]:
        (content / name).write_bytes(bytes((i * 37 + (i >> 8) * 17 + 11) & 255
                                          for i in range(size)))
    (content / "README.TXT").write_text("Sony CD-ROM read test passed.\r\n", newline="")
    iso = root / "test.iso"
    run(["genisoimage", "-quiet", "-V", "SONY_TEST", "-o", iso, content])
    for name, source in [("SCREEN", SCREEN_SOURCE), ("DONE", DONE_SOURCE)]:
        run(["mcopy", "-o", "-i", image, assemble(root, name, source), "::" + name + ".COM"])
    auto = root / "AUTOEXEC.BAT"
    auto.write_text("\r\n".join([
        "@ECHO OFF", r"C:\SCREEN.COM", r"PATH C:\DOS",
        r"VER >C:\CDTEST.LOG",
        r"C:\DOS\MSCDEX.EXE /D:SONYCD /L:R /V >C:\MSCDEX.LOG",
        r"DIR R:\ >C:\CDDIR.LOG",
        r"COPY /B R:\CHECK.BIN C:\CHECK.OUT >C:\COPY1.LOG",
        r"COPY /B R:\ODD.BIN C:\ODD.OUT >C:\COPY2.LOG",
        r"TYPE R:\README.TXT >>C:\CDTEST.LOG", r"C:\DONE.COM", ""
    ]), newline="")
    run(["mcopy", "-o", "-i", image, auto, "::AUTOEXEC.BAT"])
    global_cfg = root / "global.cfg"
    global_cfg.write_text("[Emulator]\nvmm_path = " + str(root) + "/\n")
    results = []
    outputs = ["CHECK.OUT", "ODD.OUT", "SCREEN.TXT", "MSCDEX.LOG", "CDDIR.LOG",
               "CDTEST.LOG", "COPY1.LOG", "COPY2.LOG"]
    for mode in a.modes:
        for model in ["cdu31a", "cdu33a"]:
            for version, driver in drivers:
                tag = f"{version}-{model}-{mode}"
                dest = root / tag
                (dest / "nvr").mkdir(parents=True)
                shutil.copyfile(a.nvr, dest / "nvr/pb430.nvr")
                base = a.creative_base if mode == "creative" else a.base
                options = f" /M:D /T:{a.dma}" if mode == "dma" else ""
                config = dest / "CONFIG.SYS"
                config.write_text(f"DEVICE=C:\\SLCD.SYS /D:SONYCD /B:{base} /V{options}\r\n"
                                  "FILES=30\r\nBUFFERS=20\r\nLASTDRIVE=Z\r\n", newline="")
                run(["mcopy", "-o", "-i", image, config, "::CONFIG.SYS"])
                run(["mcopy", "-o", "-i", image, driver, "::SLCD.SYS"])
                for name in outputs + ["DONE.TAG"]:
                    subprocess.run(["mdel", "-i", image, "::" + name], capture_output=True)
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
cdrom_01_image_path = {iso}
cdrom_01_parameters = 1, sony
cdrom_01_type = sony_{model}
cdrom_01_speed = {2 if model == 'cdu33a' else 1}
[SONY CDU31A interface]
base = 0{a.base}
irq = -1
dma = {a.dma if mode == 'dma' else -1}
[SONY/Creative interface]
base = 0{a.creative_base}
""")
                start = time.monotonic()
                complete = False
                env = dict(os.environ, QT_QPA_PLATFORM="offscreen", ALSOFT_DRIVERS="null")
                with (dest / "console.log").open("wb") as log:
                    proc = subprocess.Popen([str(a.emulator), "-N", "-O", str(global_cfg),
                                             "-P", str(dest), "-R", str(a.roms),
                                             "-L", str(dest / "86box.log")],
                                            env=env, stdout=log, stderr=log)
                    try:
                        while proc.poll() is None and time.monotonic() - start < a.timeout:
                            found = subprocess.run(["mdir", "-i", image, "::DONE.TAG"],
                                                   capture_output=True)
                            if found.returncode == 0:
                                # The helper flushes DOS before and after creating this marker.
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
                checks = {}
                for name in outputs:
                    data = subprocess.run(["mtype", "-i", image, "::" + name], capture_output=True)
                    (dest / name).write_bytes(data.stdout)
                    if name.endswith(".OUT"):
                        checks[name] = data.returncode == 0 and data.stdout == (
                            content / name.replace(".OUT", ".BIN")).read_bytes()
                results.append(dict(case=tag, driver_sha256=hashlib.sha256(driver.read_bytes()).hexdigest(),
                                    completed=complete, checks=checks,
                                    seconds=round(time.monotonic() - start, 1)))
                (root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
                print(json.dumps(results[-1]), flush=True)
    return 0 if all(r["completed"] and all(r["checks"].values()) for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
