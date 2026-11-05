#!/usr/bin/env python3
"""Drive the HD44780 character LCD through each of its interfaces in a running 86Box.

Needs an 86Box built with -D AUTOMATION=ON, on Linux or macOS (the automation
socket is a UNIX socket here), and the machine ROMs for the IBM AT and, for the
TS-3x00 case, the TS-3200. See doc/hardware/hd44780-lcd.md.

Each case sets the LCD up for 8 bits and 2 lines, turns the display on with a
blinking cursor, clears it, sets the entry mode, and writes "Hi", with the same
port writes as the DEBUG scripts in the documentation. It then reads back the
address counter and the two characters in DDRAM. The parallel port's reads are
not wired, so its case reads them through the same module's ISA port. A control
case does the parallel port's writes with nothing attached to the port, and
must not find "Hi".
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

CASES = {
    # name: (machine config, how the LCD is driven, whether "Hi" should land)
    "lpt": ("""[Machine]
machine = ibmat

[Ports (COM & LPT)]
lpt1_enabled = 1
lpt1_device = lpt_char_lcd

[Character LCD (HD44780)]
base = 0300
""", "lpt", True),
    "lpt-control": ("""[Machine]
machine = ibmat

[Ports (COM & LPT)]
lpt1_enabled = 1

[Other peripherals]
char_lcd_enabled = 1

[Character LCD (HD44780)]
base = 0300
""", "lpt", False),
    "isa": ("""[Machine]
machine = ibmat

[Other peripherals]
char_lcd_enabled = 1

[Character LCD (HD44780)]
base = 0300
""", "isa", True),
    "ts3x00": ("""[Machine]
machine = ts3200

[Video]
gfxcard = none

[Other peripherals]
char_lcd_enabled = 1
""", "ts3x00", True),
}


class Automation:
    def __init__(self, path, timeout):
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(str(path))
                break
            except OSError:
                self.sock.close()
                if time.monotonic() > deadline:
                    raise RuntimeError("could not connect to the automation socket")
                time.sleep(0.1)
        self.file = self.sock.makefile("rwb", buffering=0)

    def request(self, line):
        self.file.write((line + "\n").encode())
        reply = self.file.readline()
        if not reply:
            raise RuntimeError(f"86Box closed the connection after {line!r}")
        reply = json.loads(reply)
        if not reply.get("ok"):
            raise RuntimeError(f"{line!r}: {reply.get('error')}")
        return reply

    def out(self, port, val):
        self.request(f"iow {port:#x} 1 {val:#x}")

    def inp(self, port):
        return self.request(f"ior {port:#x} 1")["values"][0]

    def settle(self):
        # Longer than a clear's 1.52 ms, so that the busy flag is down again.
        self.request("step 3")


def drive(auto, how):
    """The interface's instruction and data writes, its reads, and an address set."""
    if how == "lpt":
        data_port, ctrl_port = 0x378, 0x37A

        def lcd(rs, val):
            # The byte on D0-D7, then EN up and down again with R/W low.
            auto.out(data_port, val)
            auto.out(ctrl_port, 0x06 if rs else 0x02)
            auto.out(ctrl_port, 0x07 if rs else 0x03)
            auto.settle()

        inst = lambda v: lcd(0, v)
        data = lambda v: lcd(1, v)
        base = 0x300
    elif how == "isa":
        base = 0x300
        inst = lambda v: (auto.out(base, v), auto.settle())
        data = lambda v: (auto.out(base + 1, v), auto.settle())
    else:
        # The board's register block is at 70H, where its BIOS puts CS5; DIO
        # control bit 4 makes the LCD port an LCD bus.
        auto.out(0x7D, 0x10)
        base = 0x72
        inst = lambda v: (auto.out(base, v), auto.settle())
        data = lambda v: (auto.out(base + 1, v), auto.settle())

    rd_inst = lambda: auto.inp(base)
    rd_data = lambda: auto.inp(base + 1)
    set_addr = lambda v: (auto.out(base, v), auto.settle())
    return inst, data, rd_inst, rd_data, set_addr


def run_case(name, emulator, rom_path, timeout):
    config, how, should_land = CASES[name]
    with tempfile.TemporaryDirectory(prefix="char_lcd_smoke_") as tmp:
        root = Path(tmp)
        (root / "86box.cfg").write_text(config)
        sock = root / "auto.sock"
        command = [str(emulator), "-N", "-P", str(root), "-O", str(root / "global.cfg"),
                   "-L", str(root / "86box.log"), "--automation", str(sock), "--automation-hold"]
        if rom_path:
            command += ["-R", str(rom_path)]
        env = dict(os.environ)
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
        proc = subprocess.Popen(command, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            auto = Automation(sock, timeout)
            auto.request("step 4000")  # Through POST.
            inst, data, rd_inst, rd_data, set_addr = drive(auto, how)

            for v in (0x38, 0x0F, 0x01, 0x06):
                inst(v)
            for c in b"Hi":
                data(c)

            ac = rd_inst()
            set_addr(0x80)
            first = rd_data()
            auto.settle()
            second = rd_data()
            got = (ac, first, second)
            want = (0x02, ord("H"), ord("i"))
            landed = got == want

            auto.file.write(b"quit\n")
            proc.wait(timeout=timeout)
        except Exception as e:
            print(f"FAIL {name}: {e}", flush=True)
            return False
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()

    ok = landed == should_land
    detail = "AC %02X, DDRAM %02X %02X" % got
    expect = "expected \"Hi\" at 00H" if should_land else "expected nothing to land"
    print(f"{'PASS' if ok else 'FAIL'} {name}: {detail} ({expect})", flush=True)
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--emulator", required=True, type=Path)
    parser.add_argument("--rom-path", type=Path, help="ROM directory, if not 86Box's usual one")
    parser.add_argument("--case", action="append", choices=sorted(CASES),
                        help="run only this case (repeatable; default all)")
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()

    failures = [name for name in (args.case or CASES)
                if not run_case(name, args.emulator, args.rom_path, args.timeout)]
    if failures:
        sys.exit(f"{len(failures)} failed: {', '.join(failures)}")


if __name__ == "__main__":
    main()
