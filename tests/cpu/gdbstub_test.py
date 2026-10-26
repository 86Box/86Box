#!/usr/bin/env python3
"""Destructive guest-state tests: connect only to a disposable, paused 386+ VM."""

import argparse
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET


CPU_MODELS = {
    "8088": ("ibmxt", "8088", 4772728, 1, "none", 640, "cga"),
    "v20": ("ibmxt", "necv20", 4772728, 1, "none", 640, "cga"),
    "286": ("ibmat", "286", 6000000, 1, "none", 1024, "et4000ax"),
    "386": ("acc386", "i386dx", 16000000, 1, "none", 4096, "et4000ax"),
    "pentium-mmx": ("p55t2p4", "pentium_p55c", 233333333, 3.5,
                    "internal", 16384, "et4000ax"),
}


def configuration(port, interpreter=False, cpu="pentium-mmx", guest_debug_regs=True):
    machine, family, speed, multiplier, fpu, memory, video = CPU_MODELS[cpu]
    return (
        f"[Machine]\nmachine = {machine}\ncpu_family = {family}\n"
        f"cpu_speed = {speed}\ncpu_multi = {multiplier}\nfpu_type = {fpu}\n"
        f"cpu_use_dynarec = {int(not interpreter)}\nmem_size = {memory}\n"
        f"cpu_debug_regs_486 = {int(guest_debug_regs)}\n"
        "time_sync = disabled\n[General]\nvid_renderer = qt_software\n"
        f"gdbstub_port = {port}\n[Video]\ngfxcard = {video}\n"
        "[Sound]\nsndcard = none\n[Input devices]\nmouse_type = none\n"
        "[Storage controllers]\nhdc = none\n"
    )


class Remote:
    def __init__(self, port):
        deadline = time.monotonic() + 15
        while True:
            try:
                self.socket = socket.create_connection(("127.0.0.1", port), timeout=1)
                break
            except ConnectionRefusedError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)
        self.socket.settimeout(10)
        self.socket.sendall(b"+")

    def receive(self):
        while True:
            byte = self.socket.recv(1)
            if not byte:
                raise EOFError("GDB connection closed")
            if byte == b"$":
                break
        data = bytearray()
        while True:
            byte = self.socket.recv(1)
            if not byte:
                raise EOFError("GDB connection closed")
            if byte == b"#":
                break
            data.extend(byte)
        checksum = self.socket.recv(1) + self.socket.recv(1)
        if int(checksum, 16) != sum(data) & 255:
            raise AssertionError("Invalid response checksum")
        self.socket.sendall(b"+")
        return data.decode("ascii")

    def send(self, payload):
        data = payload if isinstance(payload, bytes) else payload.encode("ascii")
        self.socket.sendall(b"$" + data + b"#" + f"{sum(data) & 255:02x}".encode())
        ack = self.socket.recv(1)
        if ack != b"+":
            raise AssertionError(f"Expected acknowledgement, got {ack!r}")

    def command(self, payload):
        self.send(payload)
        return self.receive()

    def write_reg(self, index, value):
        response = self.command(f"P{index:x}={struct.pack('<I', value).hex()}")
        if response != "OK":
            raise AssertionError(f"Register write: {response}")

    def read_reg(self, index):
        return int.from_bytes(bytes.fromhex(self.command(f"p{index:x}")), "little")

    def write_mem(self, address, data):
        response = self.command(f"M{address:x},{len(data):x}:{data.hex()}")
        if response != "OK":
            raise AssertionError(f"Memory write: {response}")


class GuestTests(unittest.TestCase):
    native_flags = 2
    guest_debug_regs = True

    @classmethod
    def setUpClass(cls):
        cls.remote = Remote(PORT)
        response = cls.remote.command("?")
        if not response.startswith("T"):
            raise AssertionError(f"Expected a paused guest, got {response!r}")

    @classmethod
    def tearDownClass(cls):
        cls.remote.socket.close()

    def setUp(self):
        r = self.remote
        # Reset-mode, 16-bit RAM program: no BIOS or guest OS dependencies.
        r.write_reg(0x10, 0x10)  # CR0: real mode, paging off
        for reg in range(0xa, 0x10):
            r.write_reg(reg, 0)
        r.write_reg(9, self.native_flags)  # IF and TF clear; retain NEC native mode
        r.write_reg(4, 0x9000)
        r.write_reg(0, 0)
        r.write_reg(8, 0x7000)

    def test_step_and_execution_breakpoints(self):
        r = self.remote
        # inc ax; inc ax; jmp 7000
        r.write_mem(0x7000, bytes.fromhex("4040ebfc"))
        for kind in (0, 1):
            with self.subTest(kind=kind):
                r.write_reg(0, 0)
                r.write_reg(8, 0x7000)
                self.assertTrue(r.command("s").startswith("T05"))
                self.assertEqual(r.read_reg(8), 0x7001)
                self.assertEqual(r.read_reg(0), 1)
                self.assertEqual(r.command(f"Z{kind},7001,1"), "OK")
                r.write_reg(8, 0x7000)
                stop = r.command("c")
                self.assertIn(("swbreak" if kind == 0 else "hwbreak") + ":;", stop)
                self.assertEqual(r.read_reg(8), 0x7001)
                self.assertEqual(r.command(f"z{kind},7001,1"), "OK")

    def test_watchpoints(self):
        r = self.remote
        # mov ax,[8000]; mov [8002],ax; jmp 7000
        r.write_mem(0x7000, bytes.fromhex("a10080a30280ebf8"))
        r.write_mem(0x8000, bytes.fromhex("34120000"))
        for kind, address, reason, pc in (
            (2, 0x8002, "watch", 0x7006),
            (3, 0x8000, "rwatch", 0x7003),
            (4, 0x8000, "awatch", 0x7003),
        ):
            with self.subTest(kind=kind):
                r.write_reg(8, 0x7000)
                self.assertEqual(r.command(f"Z{kind},{address:x},2"), "OK")
                self.assertIn(f"{reason}:{address:X};", r.command("c"))
                self.assertEqual(r.read_reg(8), pc)
                self.assertEqual(r.read_reg(0) & 0xffff, 0x1234)
                r.write_reg(8, 0x7000)
                if kind == 2:
                    self.assertTrue(r.command("s").startswith("T05"))
                self.assertIn(f"{reason}:{address:X};", r.command("s"))
                self.assertEqual(r.command(f"z{kind},{address:x},2"), "OK")

    def test_warmed_continue_and_step(self):
        r = self.remote
        # inc ax; cmp ax,1000; jne 7000; nop; jmp 7000
        r.write_mem(0x7000, bytes.fromhex("403de80375fa90ebf7"))
        self.assertEqual(r.command("Z1,7006,1"), "OK")
        self.assertIn("hwbreak:;", r.command("c"))
        self.assertEqual(r.read_reg(8), 0x7006)
        self.assertEqual(r.read_reg(0) & 0xffff, 1000)
        self.assertTrue(r.read_reg(9) & 0x40)
        self.assertEqual(r.command("z1,7006,1"), "OK")
        r.write_reg(8, 0x7000)
        r.write_reg(0, 0)
        self.assertTrue(r.command("s").startswith("T05"))
        self.assertEqual(r.read_reg(8), 0x7001)
        self.assertEqual(r.read_reg(0) & 0xffff, 1)
        for kind, reason in ((0, "swbreak"), (1, "hwbreak")):
            r.write_reg(8, 0x7000)
            r.write_reg(0, 0)
            self.assertEqual(r.command(f"Z{kind},7001,1"), "OK")
            self.assertIn(f"{reason}:;", r.command("c"))
            self.assertEqual(r.read_reg(8), 0x7001)
            self.assertEqual(r.read_reg(0) & 0xffff, 1)
            self.assertEqual(r.command(f"z{kind},7001,1"), "OK")

    def test_step_calls_returns_and_flags(self):
        r = self.remote
        program = bytearray(b"\x90" * 0x15)
        program[:10] = bytes.fromhex("b83412505be80800eb08")
        program[0x10:0x12] = bytes.fromhex("40c3")
        program[0x12:0x15] = bytes.fromhex("90ebeb")
        r.write_mem(0x7000, program)
        for pc in (0x7003, 0x7004, 0x7005, 0x7010, 0x7011, 0x7008, 0x7012):
            self.assertTrue(r.command("s").startswith("T05"))
            self.assertEqual(r.read_reg(8), pc)
        self.assertEqual(r.read_reg(0) & 0xffff, 0x1235)
        self.assertEqual(r.read_reg(3) & 0xffff, 0x1234)
        self.assertEqual(r.read_reg(4) & 0xffff, 0x9000)

    def test_interrupt_and_reconnect(self):
        r = self.remote
        r.write_mem(0x7000, bytes.fromhex("ebfe"))
        r.send("c")
        time.sleep(0.05)
        r.socket.sendall(b"\x03")
        self.assertTrue(r.receive().startswith("T02"))
        self.assertEqual(r.read_reg(8), 0x7000)
        self.assertEqual(r.command("Z1,7100,1"), "OK")
        r.socket.close()
        time.sleep(0.05)
        type(self).remote = Remote(PORT)
        r = self.remote
        self.assertTrue(r.command("?").startswith("T"))
        # A disconnected client's breakpoint must not survive into a new session.
        self.assertEqual(r.command("Z1,7100,1"), "OK")
        self.assertEqual(r.command("z1,7100,1"), "OK")
        self.assertEqual(r.command("Z1,7100,1"), "OK")
        self.assertEqual(r.command("Z2,8000,1"), "OK")
        replacement = Remote(PORT)
        self.assertTrue(replacement.command("?").startswith("T"))
        r.socket.close()
        type(self).remote = r = replacement
        for kind, address in ((1, "7100"), (2, "8000")):
            self.assertEqual(r.command(f"Z{kind},{address},1"), "OK")
            self.assertEqual(r.command(f"z{kind},{address},1"), "OK")

    def test_warmed_code_edits(self):
        r = self.remote
        # Warm the inc loop, then have the guest replace its inc with dec.
        r.write_mem(0x7000, bytes.fromhex("403de80375fac606007048ebf3"))
        self.assertEqual(r.command("Z1,7006,1"), "OK")
        self.assertIn("hwbreak:;", r.command("c"))
        self.assertEqual(r.command("z1,7006,1"), "OK")
        self.assertEqual(r.command("Z1,700b,1"), "OK")
        self.assertIn("hwbreak:;", r.command("c"))
        self.assertEqual(r.command("z1,700b,1"), "OK")
        self.assertEqual(r.command("m7000,1"), "48")
        r.write_reg(8, 0x7000)
        r.write_reg(0, 10)
        self.assertTrue(r.command("s").startswith("T05"))
        self.assertEqual(r.read_reg(0) & 0xffff, 9)
        # A debugger edit must invalidate the warmed instruction too.
        r.write_mem(0x7000, b"\x40")
        r.write_reg(8, 0x7000)
        self.assertTrue(r.command("s").startswith("T05"))
        self.assertEqual(r.read_reg(0) & 0xffff, 10)
        self.assertEqual(r.command(b"X7000,1:\x48"), "OK")
        r.write_reg(8, 0x7000)
        self.assertTrue(r.command("s").startswith("T05"))
        self.assertEqual(r.read_reg(0) & 0xffff, 9)

    def test_debugger_memory_accesses_and_binary_packets(self):
        r = self.remote
        r.write_mem(0x7000, bytes.fromhex("90ebfd"))
        payload = bytes((0, 3, 35, 36, 125, 42, 255))
        escaped = b"".join(bytes((125, value ^ 0x20)) if value in (35, 36, 125, 42)
                           else bytes((value,)) for value in payload)
        self.assertEqual(r.command("Z4,9000,7"), "OK")
        self.assertEqual(r.command(b"X9000,7:" + escaped), "OK")
        self.assertEqual(r.command("m9000,7"), payload.hex())
        stop = r.command("s")
        self.assertTrue(stop.startswith("T05"))
        self.assertNotIn("watch:", stop)
        self.assertEqual(r.command("z4,9000,7"), "OK")
        for address, length in (("ffffffff", 2), ("8000", 0)):
            self.assertEqual(r.command(f"Z2,{address},{length:x}"), "E22")

    def test_guest_tf_and_debug_registers(self):
        r = self.remote
        r.write_mem(4, struct.pack("<HH", 0x7100, 0))
        r.write_mem(0x7000, bytes.fromhex("90ebfd"))
        # Save DR6, disable guest execution breakpoints, then stay in the handler.
        r.write_mem(0x7100, bytes.fromhex("0f21f066a3009366b8000400000f23f8ebfe"))
        self.assertEqual(r.command("Z1,7100,1"), "OK")
        r.write_reg(9, 0x102)
        self.assertIn("hwbreak:;", r.command("c"))
        self.assertEqual(r.read_reg(8), 0x7100)
        self.assertFalse(r.read_reg(9) & 0x100)
        r.write_reg(9, 2)
        if not self.guest_debug_regs:
            self.assertEqual(r.command("z1,7100,1"), "OK")
            return
        # Set DR0=7000 and DR7=401, then jump to the guest breakpoint.
        r.write_mem(0x7200, bytes.fromhex(
            "66b8007000000f23c066b8010400000f23f8e9ebfd"))
        r.write_reg(8, 0x7200)
        self.assertIn("hwbreak:;", r.command("c"))
        self.assertEqual(r.read_reg(8), 0x7100)
        self.assertEqual(r.command("z1,7100,1"), "OK")
        self.assertEqual(r.command("Z1,7110,1"), "OK")
        self.assertIn("hwbreak:;", r.command("c"))
        self.assertEqual(r.read_reg(8), 0x7110)
        self.assertTrue(int.from_bytes(bytes.fromhex(r.command("m9300,4")), "little") & 1)
        self.assertEqual(r.command("z1,7110,1"), "OK")

    def test_fault_step(self):
        r = self.remote
        r.write_mem(6 * 4, struct.pack("<HH", 0x7100, 0))
        r.write_mem(0x7100, bytes.fromhex("90ebfe"))
        r.write_mem(0x7000, bytes.fromhex("0f0b"))  # #UD
        self.assertTrue(r.command("s").startswith("T05"))
        self.assertEqual(r.read_reg(8), 0x7100)
        self.assertEqual(r.read_reg(4) & 0xffff, 0x8ffa)

    def test_register_packet_layout_and_roundtrip(self):
        r = self.remote
        xml = ""
        offset = 0
        while True:
            response = r.command(f"qXfer:features:read:target.xml:{offset:x},200")
            self.assertIn(response[0], "ml")
            xml += response[1:]
            if response[0] == "l":
                break
            offset = len(xml)
        widths = [int(reg.attrib["bitsize"]) // 8 for reg in ET.fromstring(xml).iter("reg")]
        registers = r.command("g")
        self.assertEqual(len(registers), sum(widths) * 2)
        self.assertEqual(r.command("G" + registers), "OK")
        self.assertEqual(r.command("g"), registers)

    def test_rep_step(self):
        r = self.remote
        r.write_mem(0x7000, bytes.fromhex("f3a490ebfe"))
        r.write_mem(0x9000, bytes(range(16)))
        for _ in range(4):
            r.write_reg(8, 0x7000)
            r.write_reg(1, 16)
            r.write_reg(6, 0x9000)
            r.write_reg(7, 0x9100)
            self.assertTrue(r.command("s").startswith("T05"))
            self.assertEqual(r.read_reg(8), 0x7002)
            self.assertEqual(r.read_reg(1) & 0xffff, 0)
            self.assertEqual(bytes.fromhex(r.command("m9100,10")), bytes(range(16)))

    def test_protected_mode_and_paging(self):
        r = self.remote
        r.write_mem(0x7400, bytes.fromhex(
            "0000000000000000ffff0000009acf00ffff00000092cf00"))
        r.write_mem(0x7500, struct.pack("<HI", 23, 0x7400))
        r.write_mem(0x7000, bytes.fromhex(
            "fa0f011600750f20c06683c8010f22c066ea008000000800"))
        # Enter flat 32-bit segments, then warm a translated loop.
        r.write_mem(0x8000, bytes.fromhex(
            "66b810008ed88ec08ed0bc00a0000031c0403de803000075f890ebfe"))
        self.assertEqual(r.command("Z1,8019,1"), "OK")
        self.assertIn("hwbreak:;", r.command("c"))
        self.assertEqual(r.read_reg(8), 0x8019)
        self.assertEqual(r.read_reg(0), 1000)
        self.assertEqual(r.read_reg(0xa), 8)
        self.assertEqual(r.command("z1,8019,1"), "OK")
        for index, selector in ((0xa, 8), (0xb, 0x10)):
            self.assertEqual(r.command(f"P{index:x}=ffff"), "E14")
            self.assertEqual(r.read_reg(index), selector)

        # Identity-map low memory; linear 400000 aliases physical 9000.
        directory = bytearray(4096)
        struct.pack_into("<II", directory, 0, 0x11003, 0x12003)
        for offset in range(0, 4096, 256):
            r.write_mem(0x10000 + offset, directory[offset:offset + 256])
            table = b"".join(struct.pack("<I", (i << 12) | 3)
                             for i in range(offset // 4, (offset + 256) // 4))
            r.write_mem(0x11000 + offset, table)
            r.write_mem(0x12000 + offset, bytes(256))
        r.write_mem(0x12000, struct.pack("<I", 0x9003))
        r.write_mem(0x8200, bytes.fromhex(
            "b8000001000f22d80f20c00d000000800f22c090b878563412a30000400090ebfe"))
        r.write_reg(8, 0x8200)
        self.assertEqual(r.command("Z2,400000,4"), "OK")
        self.assertIn("watch:400000;", r.command("c"))
        self.assertEqual(r.read_reg(8), 0x821e)
        self.assertEqual(r.command("m9000,4"), "78563412")
        self.assertEqual(r.command("m400000,4"), "78563412")
        self.assertEqual(r.command("z2,400000,4"), "OK")

    def test_warmed_cross_page_watchpoints(self):
        r = self.remote
        # mov ax,[8fff]; mov [9fff],ax; inc cx; cmp cx,1000; jne 7000
        r.write_mem(0x7000, bytes.fromhex("a1ff8fa3ff9f4181f9e80375f390ebf0"))
        r.write_mem(0x8fff, bytes.fromhex("3412"))
        r.write_reg(1, 0)
        self.assertEqual(r.command("Z1,700d,1"), "OK")
        self.assertIn("hwbreak:;", r.command("c"))
        self.assertEqual(r.read_reg(8), 0x700d)
        self.assertEqual(r.command("z1,700d,1"), "OK")
        for kind, address, reason, pc in (
            (2, 0xa000, "watch", 0x7006),
            (3, 0x9000, "rwatch", 0x7003),
            (4, 0x9000, "awatch", 0x7003),
        ):
            with self.subTest(kind=kind):
                r.write_reg(8, 0x7000)
                self.assertEqual(r.command(f"Z{kind},{address:x},1"), "OK")
                self.assertIn(f"{reason}:{address:X};", r.command("c"))
                self.assertEqual(r.read_reg(8), pc)
                self.assertEqual(r.command(f"z{kind},{address:x},1"), "OK")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=12345)
    parser.add_argument("--binary", type=Path, help="Launch an isolated diskless test VM")
    parser.add_argument("--rompath", type=Path)
    parser.add_argument("--runtime", action="store_true", help="Use the new --gdbstub CLI")
    parser.add_argument("--interpreter", action="store_true")
    parser.add_argument("--guest-debug-regs", choices=("on", "off"), default="on",
                        help="Exercise execution with guest debug-register emulation on or off")
    parser.add_argument("--cpu", choices=CPU_MODELS, default="pentium-mmx",
                        help="Older CPUs support only the real-mode instruction tests")
    parser.add_argument("--trace-translated", action="store_true",
                        help="Use host GDB to assert generated continue and step boundaries")
    parser.add_argument("--guest-gdb", action="store_true", help="Also connect a real guest GDB")
    parser.add_argument("--graceful", action="store_true",
                        help="Use automation quit to check clean server shutdown")
    args, remaining = parser.parse_known_args()
    PORT = args.port
    GuestTests.native_flags = 0x8002 if args.cpu == "v20" else 2
    GuestTests.guest_debug_regs = args.guest_debug_regs == "on"
    if args.binary:
        if not args.rompath:
            parser.error("--binary requires --rompath")
        with tempfile.TemporaryDirectory(prefix="86box-gdb-") as directory:
            root = Path(directory)
            (root / "86box.cfg").write_text(configuration(
                PORT, args.interpreter, args.cpu, GuestTests.guest_debug_regs))
            command = [str(args.binary.resolve()), "--noconfirm",
                       "--rompath", str(args.rompath.resolve()), "--vmpath", str(root)]
            if args.runtime:
                command += ["--gdbstub"]
            if args.graceful:
                command += ["--automation", f"unix:{root / 'automation.sock'}"]
            if args.trace_translated:
                trace = root / "trace.gdb"
                trace.write_text(
                    "set pagination off\nset confirm off\nset debuginfod enabled off\n"
                    "set $seen_continue = 0\nset $seen_step = 0\n"
                    "break gdbstub_instruction_start\ncommands\nsilent\n"
                    "if *(int*)&gdbstub_step == 0 && !$seen_continue\n"
                    'printf "GENERATED_CONTINUE\\n"\nset $seen_continue = 1\nend\n'
                    "if *(int*)&gdbstub_step == 1 && !$seen_step\n"
                    'printf "GENERATED_STEP\\n"\nset $seen_step = 1\nend\n'
                    "if $seen_continue && $seen_step\ndisable 1\nend\n"
                    "continue\nend\nrun\n"
                )
                command = ["gdb", "--nx", "--batch", "-x", str(trace), "--args"] + command
            # Keep XDG_RUNTIME_DIR: replacing it hides the host's PipeWire socket.
            environment = dict(os.environ, QT_QPA_PLATFORM="offscreen",
                               XDG_CONFIG_HOME=str(root / "config"),
                               XDG_CACHE_HOME=str(root / "cache"),
                               XDG_DATA_HOME=str(root / "data"))
            with (root / "stderr.log").open("w+") as log:
                process = subprocess.Popen(command, env=environment, cwd=root,
                                           stdout=log, stderr=log)
                result = None
                shutdown_ok = not args.graceful
                try:
                    # Never connect to a different VM if our launch failed to bind.
                    deadline = time.monotonic() + 15
                    while True:
                        startup = (root / "stderr.log").read_text(errors="replace")
                        if f"GDB Stub: Listening on port {PORT}\n" in startup:
                            break
                        if (process.poll() is not None or "GDB Stub: Failed" in startup or
                                time.monotonic() >= deadline):
                            raise AssertionError(f"Test VM did not start its own listener\n{startup}")
                        time.sleep(0.05)
                    result = unittest.main(argv=[__file__] + remaining, exit=False).result
                    if result.wasSuccessful() and args.guest_gdb:
                        remote_gdb = subprocess.run(
                            ["gdb", "--nx", "--batch",
                             "-iex", "set debuginfod enabled off",
                             "-ex", f"target remote 127.0.0.1:{PORT}",
                             "-ex", "info registers eax eip",
                             "-ex", 'printf "GUEST_GDB_REGS:%x,%x\\n", $eax, $eip',
                             "-ex", "set $eax = 0",
                             "-ex", "set $eip = 0x7000",
                             "-ex", "set {unsigned int}0x7000 = 0xfceb4040",
                             "-ex", "hbreak *0x7001",
                             "-ex", "continue",
                             "-ex", 'printf "GUEST_GDB_HW:%x,%x\\n", $eax, $eip',
                             "-ex", "stepi",
                             "-ex", 'printf "GUEST_GDB_STEP:%x,%x\\n", $eax, $eip',
                             "-ex", "continue",
                             "-ex", 'printf "GUEST_GDB_RESUME:%x,%x\\n", $eax, $eip',
                             "-ex", "delete breakpoints",
                             "-ex", "set $eax = 0",
                             "-ex", "set $eip = 0x7000",
                             "-ex", "break *0x7001",
                             "-ex", "continue",
                             "-ex", 'printf "GUEST_GDB_SW:%x,%x\\n", $eax, $eip',
                             "-ex", "delete breakpoints",
                             "-ex", "monitor help", "-ex", "detach"],
                            capture_output=True, text=True, timeout=20, check=True)
                        if (any(marker not in remote_gdb.stdout for marker in (
                                "GUEST_GDB_HW:1,7001", "GUEST_GDB_STEP:2,7002",
                                "GUEST_GDB_RESUME:3,7001", "GUEST_GDB_SW:1,7001")) or
                            "Commands:" not in remote_gdb.stdout + remote_gdb.stderr or
                            "GUEST_GDB_REGS:" not in remote_gdb.stdout or
                            "rejected" in remote_gdb.stderr or "Truncated" in remote_gdb.stderr):
                            raise AssertionError(remote_gdb.stdout + remote_gdb.stderr)
                    if args.graceful and result.wasSuccessful():
                        with socket.socket(socket.AF_UNIX) as control:
                            control.settimeout(10)
                            control.connect(str(root / "automation.sock"))
                            control.sendall(b"quit\n")
                            control.recv(4096)
                        process.wait(timeout=10)
                        shutdown_ok = process.returncode == 0
                    elif not result.wasSuccessful():
                        shutdown_ok = True
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                    log.seek(0)
                    output = log.read()
                    if result is None or not result.wasSuccessful():
                        print(output)
                if args.trace_translated:
                    for marker in ("GENERATED_CONTINUE", "GENERATED_STEP"):
                        if marker not in output:
                            print(output)
                            raise AssertionError(f"Host GDB did not observe {marker}")
                if not shutdown_ok:
                    raise AssertionError(f"Unclean server shutdown\n{output}")
                raise SystemExit(result is None or not result.wasSuccessful())
    else:
        unittest.main(argv=[__file__] + remaining)
