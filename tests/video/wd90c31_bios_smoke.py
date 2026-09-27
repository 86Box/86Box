#!/usr/bin/env python3
"""Run WD90C31 BIOS tests in an SDL build of 86Box (requires NASM and user ROMs)."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", required=True, type=Path)
    parser.add_argument("--rom-dir", required=True, type=Path,
                        help="Directory containing BIOS.BIN and wd90c31alrdiamondspeedstar24x1.BIN")
    args = parser.parse_args()
    emulator = args.emulator.resolve()
    source = Path(__file__).with_suffix(".asm")
    modes = [(0x5f, 640, 480, 8), (0x5c, 800, 600, 8), (0x60, 1024, 768, 8)]
    failures = []
    with tempfile.TemporaryDirectory(prefix="wd90c31-smoke-") as temporary:
        root = Path(temporary)
        bios_dir = root / "roms/machines/ibmat"
        bios_dir.mkdir(parents=True)
        shutil.copytree(args.rom_dir, root / "roms/video/wd90c31a")
        profiles = (("wd90c31", 256), ("wd90c31", 512), ("wd90c31", 1024), ("speedstar24x", 1024))
        for card, memory in profiles:
            extra = [(0x62, 640, 480, 15)] if card == "wd90c31" else [
                (0x62, 640, 480, 15), (0x63, 800, 600, 15), (0x72, 640, 480, 24)]
            for mode in [None] + modes + extra:
                if mode and mode[1] * mode[2] * ((mode[3] + 7) // 8) > memory * 1024:
                    continue
                description = (f"mode {mode[0]:02x}, {mode[1]}x{mode[2]}x{mode[3]}"
                               if mode else "POST/text scroll/mode 13h")
                label = f"{card} ({memory} KB): {description}"
                definitions = [] if mode is None else [f"-D{k}={v}" for k, v in zip(
                    ("TEST_MODE", "TEST_WIDTH", "TEST_HEIGHT", "TEST_BPP"), mode)]
                subprocess.run(["nasm", *definitions, "-f", "bin", str(source),
                                "-o", str(root / "bios.bin")], check=True)
                bios = (root / "bios.bin").read_bytes()
                for lane, socket in enumerate(("U27", "U47")):
                    (bios_dir / f"BIOS_5170_15NOV85_{socket}.BIN").write_bytes(bios[lane::2])
                config = root / "86box.cfg"
                config.write_text(f"""[General]
confirm_exit = 0
[Machine]
machine = ibmat
cpu_family = 286
cpu_speed = 8000000
mem_size = 512
[Video]
gfxcard = {card}
[Other peripherals]
unittester_enabled = 1
[86Box Unit Tester]
exit_enabled = 1
[Paradise WD90C31A-LR]
memory = {memory}
""")
                env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy",
                           SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
                try:
                    command = [str(emulator), "-N", "-P", str(root),
                               "-O", str(root / "global.cfg"), "-R", str(root / "roms"),
                               "-C", str(config), "-L", str(root / "86box.log")]
                    result = subprocess.run(command, env=env, capture_output=True,
                                            text=True, timeout=30)
                    code = result.returncode
                except subprocess.TimeoutExpired:
                    code = "timeout"
                print(f"{'PASS' if code == 0 else 'FAIL'} {label}" + (f" (exit {code})" if code else ""), flush=True)
                if code:
                    failures.append(label)
                    if code != "timeout":
                        print(result.stdout + result.stderr)
    if failures:
        print("Exit codes: 1=text scroll, 2=VGA memory, 3=width, 4=height, 5=pixel color")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
