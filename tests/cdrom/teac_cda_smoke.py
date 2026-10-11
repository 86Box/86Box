#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Boot external TEAC_CDA.SYS drivers and verify data and optional CD-DA.

Requires Linux, mtools, genisoimage, GNU as/ld, a C compiler, OpenAL headers,
PB430 ROMs, prepared PB430 CMOS and a DOS raw disk with C:\DOS\MSCDEX.EXE.
All writes go to disposable copies in a new output directory. No vendor
software or firmware is distributed. See doc/hardware/teac-cdrom.md.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from mscdex_smoke import fixtures, validate_probe, validate_capture, OUTPUTS, run

CARDS = {
    "creative": ("mkecd", "MKE/Panasonic interface (Creative)", "230", "/P:220 /T:0"),
    "panasonic": ("mkecd_normal", "MKE/Panasonic interface", "340", "/P:340 /T:2"),
    "teac8": ("teac_8bit", "TEAC CD-55A interface (8-bit)", "2c0", "/P:2C0 /T:1"),
    "teac16": ("teac_16bit", "TEAC CD-55A interface (16-bit)", "2c0", "/P:2C0 /T:1"),
}


def testcase(a, root, case):
    media, mode, (version, driver) = case
    dest = root / f"{media}-{mode}-{version}"
    (dest / "nvr").mkdir(parents=True)
    disk = dest / "dos.img"
    # DOS images often contain hundreds of MiB of unused zero-filled space.
    run(["cp", "--sparse=always", a.dos_image, disk])
    shutil.copyfile(a.nvr, dest / "nvr/pb430.nvr")
    image = f"{disk}@@{a.partition_offset}"
    run(["mdir", "-i", image, "::DOS/MSCDEX.EXE"])
    for name in OUTPUTS + ["DONE.TAG"]:
        subprocess.run(["mdel", "-i", image, "::" + name], capture_output=True)
    interface, section, base, options = CARDS[mode]
    (dest / "CONFIG.SYS").write_text(
        f"DEVICE=C:\\{a.driver_filename} /D:{a.device_name} {options}\r\n"
        "FILES=30\r\nBUFFERS=20\r\nLASTDRIVE=Z\r\n", newline="")
    lines = ["@ECHO OFF", r"C:\SCREEN.COM",
             rf"C:\DOS\MSCDEX.EXE /D:{a.device_name} /L:R /V >C:\MSCDEX.LOG"]
    if media != "audio":
        lines += [r"DIR R:\ >C:\CDDIR.LOG", r"COPY /B R:\CHECK.BIN C:\CHECK.OUT >C:\COPY1.LOG",
                  r"COPY /B R:\ODD.BIN C:\ODD.OUT >C:\COPY2.LOG"]
    if media != "iso":
        lines += [r"C:\PROBE.COM"]
    lines += [r"C:\DONE.COM"]
    (dest / "AUTOEXEC.BAT").write_text("\r\n".join(lines) + "\r\n", newline="")
    for name in ["CONFIG.SYS", "AUTOEXEC.BAT"]:
        run(["mcopy", "-o", "-i", image, dest / name, "::" + name])
    for name in ["PROBE.COM", "DONE.COM", "SCREEN.COM"]:
        run(["mcopy", "-o", "-i", image, root / name, "::" + name])
    run(["mcopy", "-o", "-i", image, driver, "::" + a.driver_filename])
    medium = root / ("test.iso" if media == "iso" else media + ".cue")
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
cdrom_01_image_path = {medium}
cdrom_01_parameters = 1, mke
cdrom_01_type = {a.drive_model}
cdrom_01_speed = {4 if a.drive_model == 'teac_cd55a' else 2}
cdrom_01_mke_channel = 0
[{section}]
base = 0{base}
""")
    global_cfg = dest / "global.cfg"
    global_cfg.write_text("[Emulator]\nvmm_path = " + str(dest) + "/\n")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", ALSOFT_DRIVERS="null")
    if media != "iso":
        env.update(LD_PRELOAD=str(root / "capture.so"), CDROM_AUDIO_CAPTURE=str(dest / "capture.dat"))
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
        if media != "audio" and name.endswith(".OUT"):
            checks[name] = data.returncode == 0 and data.stdout == (
                root / "iso-content" / name.replace(".OUT", ".BIN")).read_bytes()
    records, capture = [], {}
    if media != "iso":
        # TEAC sub-Q reports the last sector read: start + 74 for 75 frames.
        records, probe_checks = validate_probe((dest / "PROBE.BIN").read_bytes(), media,
                                               final_frame_offset=74)
        capture, capture_checks = validate_capture(dest / "capture.dat", (root / "tone.bin").read_bytes())
        checks.update(probe_checks)
        checks.update(capture_checks)
    result = dict(case=dest.name, passed=all(checks.values()), checks=checks,
                  records=records, capture=capture, returncode=proc.returncode,
                  seconds=round(time.monotonic() - start, 1),
                  driver_sha256=hashlib.sha256(driver.read_bytes()).hexdigest())
    (dest / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ["case", "passed", "checks"]}), flush=True)
    if result["passed"]:
        disk.unlink()
    return result


def main(drive_models=("teac_cd55a", "cr563"), description=__doc__,
         driver_filename="TEAC_CDA.SYS", device_name="TEACCD",
         media_modes=("iso", "mixed", "audio")):
    p = argparse.ArgumentParser(description=description)
    for name in ["emulator", "dos-image", "nvr", "roms", "output"]:
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--driver", action="append", required=True, metavar="VERSION=PATH")
    p.add_argument("--geometry", default="63,16,854")
    p.add_argument("--partition-offset", type=int, default=32256)
    p.add_argument("--timeout", type=float, default=90)
    p.add_argument("--workers", type=int, default=1)
    p.add_argument("--modes", nargs="+", choices=CARDS, default=list(CARDS))
    p.add_argument("--media", nargs="+", choices=media_modes, default=["iso"])
    p.add_argument("--drive-model", choices=drive_models, default=drive_models[0])
    a = p.parse_args()
    a.driver_filename = driver_filename
    a.device_name = device_name
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
    for name in ["cp", "mcopy", "mdel", "mtype", "mdir", "genisoimage", "as", "ld", "cc"]:
        if not shutil.which(name):
            p.error("Missing executable: " + name)
    root = a.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    fixtures(root)
    cases = list(itertools.product(dict.fromkeys(a.media), dict.fromkeys(a.modes), drivers))
    results = []
    with ThreadPoolExecutor(max_workers=a.workers) as pool:
        for result in pool.map(lambda case: testcase(a, root, case), cases):
            results.append(result)
            (root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
