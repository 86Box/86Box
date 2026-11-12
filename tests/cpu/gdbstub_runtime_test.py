#!/usr/bin/env python3
"""Launch isolated VMs to check GDB configuration, CLI precedence and failures.

Requires a UNIX host and an AUTOMATION-enabled Qt binary.
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
import unittest

from gdbstub_test import Remote, configuration


class RuntimeTests(unittest.TestCase):
    def run_vm(self, configured, cli, expected, diagnostic=None, occupied=False):
        with tempfile.TemporaryDirectory(prefix="86box-gdb-runtime-") as directory:
            root = Path(directory)
            with socket.socket() as reservation:
                reservation.bind(("0.0.0.0", 0))
                port = reservation.getsockname()[1]
                if occupied:
                    reservation.listen()
                else:
                    reservation.close()
                cfg_port = 12349 if "--gdbstub-port" in cli else port
                if configured == "invalid-port":
                    cfg_port = 65536
                config = root / "86box.cfg"
                contents = configuration(cfg_port)
                if configured is not None and configured != "invalid-port":
                    contents = contents.replace("[General]\n",
                                                f"[General]\ngdbstub_enabled = {int(configured)}\n")
                config.write_text(contents)
                control_path = root / "automation.sock"
                command = [str(BINARY), "--automation", f"unix:{control_path}",
                           "--noconfirm", "--rompath", str(ROMPATH), "--vmpath", str(root)]
                command += [str(port) if arg == "PORT" else arg for arg in cli]
                environment = dict(os.environ, QT_QPA_PLATFORM="offscreen",
                                   XDG_CONFIG_HOME=str(root / "config"),
                                   XDG_CACHE_HOME=str(root / "cache"),
                                   XDG_DATA_HOME=str(root / "data"))
                with (root / "process.log").open("w+") as log:
                    process = subprocess.Popen(command, env=environment, stdout=log,
                                               stderr=log, cwd=root)
                    remote = None
                    try:
                        deadline = time.monotonic() + 15
                        while True:
                            output = (root / "process.log").read_text(errors="replace")
                            if diagnostic and diagnostic in output:
                                # fatal() may be waiting for its GUI dialog to be dismissed.
                                self.assertNotIn("GDB Stub: Listening", output)
                                return
                            if not diagnostic and control_path.exists():
                                break
                            self.assertIsNone(process.poll(), output)
                            self.assertLess(time.monotonic(), deadline, output)
                            time.sleep(0.05)
                        with socket.socket(socket.AF_UNIX) as control:
                            control.settimeout(10)
                            control.connect(str(control_path))
                            with control.makefile("rwb", buffering=0) as stream:
                                stream.write(b"info\n")
                                status = json.loads(stream.readline())
                                self.assertTrue(status["ok"], status)
                                data = status.get("data", status)
                                self.assertEqual(data["gdbstub_active"], expected, status)
                                self.assertEqual(data["gdbstub_port"], port, status)
                                if expected:
                                    remote = Remote(port)
                                    self.assertTrue(remote.command("?").startswith("T02"))
                                    self.assertEqual(remote.read_reg(8), 0xfffffff0)
                                    if configured is True and not cli:
                                        self.check_hold_and_reset(remote, stream)
                                        remote.send("c")
                                else:
                                    with self.assertRaises(ConnectionRefusedError):
                                        socket.create_connection(("127.0.0.1", port), timeout=1)
                                stream.write(b"quit\n")
                                self.assertTrue(json.loads(stream.readline())["ok"])
                        self.assertEqual(process.wait(timeout=10), 0)
                        saved = config.read_text()
                        self.assertEqual("gdbstub_enabled = 1" in saved, bool(configured), saved)
                        self.assertIn(f"gdbstub_port = {cfg_port}", saved)
                    finally:
                        if remote is not None:
                            remote.socket.close()
                        if process.poll() is None:
                            process.terminate()
                            try:
                                process.wait(timeout=5)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait()

    def check_hold_and_reset(self, remote, stream):
        stream.write(b"hold\n")
        self.assertTrue(json.loads(stream.readline())["ok"])
        remote.write_reg(0x10, 0x10)
        remote.write_reg(0xa, 0)
        remote.write_reg(9, 2)
        remote.write_reg(8, 0x7000)
        remote.write_mem(0x7000, bytes.fromhex("90ebfe"))
        remote.write_reg(0, 0x12345678)
        self.assertEqual(remote.read_reg(0), 0x12345678)
        remote.send("s")
        remote.socket.settimeout(0.2)
        with self.assertRaises(TimeoutError):
            remote.receive()
        remote.socket.settimeout(10)
        stream.write(b"release\n")
        self.assertTrue(json.loads(stream.readline())["ok"])
        self.assertTrue(remote.receive().startswith("T05"))
        self.assertEqual(remote.read_reg(8), 0x7001)
        self.assertEqual(remote.command("qRcmd," + b"r".hex()), "OK")
        deadline = time.monotonic() + 10
        while remote.read_reg(8) == 0x7001:
            self.assertLess(time.monotonic(), deadline, "Hard reset did not take effect")
            time.sleep(0.05)
        self.assertEqual(remote.read_reg(8), 0xfffffff0)
        stream.write(b"info\n")
        self.assertTrue(json.loads(stream.readline())["gdbstub_active"])

    def test_activation_and_nonpersistent_overrides(self):
        for configured, cli, enabled in (
            (None, [], False),
            (True, [], True),
            (True, ["--no-gdbstub"], False),
            (False, ["--gdbstub"], True),
            (False, ["--gdbstub", "--gdbstub-port", "PORT"], True),
        ):
            with self.subTest(configured=configured, cli=cli):
                self.run_vm(configured, cli, enabled)

    def test_startup_errors(self):
        for port in ("0", "65536", "bad", "1234junk", "999999999999999999999"):
            with self.subTest(port=port):
                self.run_vm(None, ["--gdbstub-port", port], False,
                            diagnostic="Invalid GDB stub port:")
        self.run_vm(None, ["--gdbstub-port"], False,
                    diagnostic="--gdbstub-port requires")
        self.run_vm("invalid-port", ["--gdbstub"], False,
                    diagnostic="GDB Stub: Invalid port")
        self.run_vm(True, [], False, occupied=True,
                    diagnostic="GDB Stub: Failed to bind on port")

    def test_isolated_launcher_rejects_occupied_port(self):
        with socket.socket() as listener:
            listener.bind(("0.0.0.0", 0))
            listener.listen()
            listener.settimeout(0.2)
            run = subprocess.run(
                [sys.executable, "-B", str(Path(__file__).with_name("gdbstub_test.py")),
                 "--binary", str(BINARY), "--rompath", str(ROMPATH), "--runtime",
                 "--port", str(listener.getsockname()[1]), "GuestTests.test_watchpoints"],
                capture_output=True, text=True, timeout=30)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("Test VM did not start its own listener", run.stderr)
            with self.assertRaises(TimeoutError):
                listener.accept()


def packet(payload):
    data = payload if isinstance(payload, bytes) else payload.encode("ascii")
    return b"$" + data + b"#" + f"{sum(data) & 255:02x}".encode()


class ProtocolTests(unittest.TestCase):
    """Remote serial protocol framing, sizing and lifecycle on a disposable VM."""

    next_port = None

    def setUp(self):
        port = ProtocolTests.next_port
        ProtocolTests.next_port += 1
        directory = tempfile.TemporaryDirectory(prefix="86box-gdb-rsp-")
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        (root / "86box.cfg").write_text(configuration(port).replace(
            "[General]\n", "[General]\ngdbstub_enabled = 1\n"))
        control_path = root / "automation.sock"
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen",
                           XDG_CONFIG_HOME=str(root / "config"),
                           XDG_CACHE_HOME=str(root / "cache"),
                           XDG_DATA_HOME=str(root / "data"))
        self.log_path = root / "process.log"
        log = self.log_path.open("w+")
        self.addCleanup(log.close)
        self.process = subprocess.Popen(
            [str(BINARY), "--automation", f"unix:{control_path}", "--noconfirm",
             "--rompath", str(ROMPATH), "--vmpath", str(root)],
            env=environment, stdout=log, stderr=log, cwd=root)
        self.addCleanup(self.stop_process)
        deadline = time.monotonic() + 15
        while not control_path.exists():
            self.assertIsNone(self.process.poll(), self.log())
            self.assertLess(time.monotonic(), deadline, self.log())
            time.sleep(0.05)
        self.control = socket.socket(socket.AF_UNIX)
        self.addCleanup(self.control.close)
        self.control.settimeout(10)
        self.control.connect(str(control_path))
        self.stream = self.control.makefile("rwb", buffering=0)
        self.addCleanup(self.stream.close)
        self.remote = Remote(port)
        self.addCleanup(self.remote.socket.close)
        self.assertTrue(self.remote.command("?").startswith("T"))

    def log(self):
        return self.log_path.read_text(errors="replace")

    def stop_process(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()

    def quit(self):
        self.stream.write(b"quit\n")
        self.assertTrue(json.loads(self.stream.readline())["ok"])
        self.assertEqual(self.process.wait(timeout=15), 0, self.log())

    def raw_ack(self, data):
        self.remote.socket.sendall(data)
        return self.remote.socket.recv(1)

    def packet_size(self):
        features = self.remote.command("qSupported:swbreak+").split(";")
        sizes = [f for f in features if f.startswith("PacketSize=")]
        self.assertEqual(len(sizes), 1, features)
        return int(sizes[0].split("=")[1], 16)

    def test_checksum_mismatch_is_rejected_and_recovers(self):
        for bad in (b"$?#00", b"$?#zz", b"$g#00"):
            with self.subTest(packet=bad):
                self.assertEqual(self.raw_ack(bad), b"-")
                self.assertTrue(self.remote.command("?").startswith("T"))
        # A NAK leaves the command unexecuted; the retransmission is executed once.
        self.remote.write_reg(0, 0x11111111)
        bad = bytearray(packet("P0=78563412"))
        bad[-1] ^= 1
        self.assertEqual(self.raw_ack(bytes(bad)), b"-")
        self.assertEqual(self.remote.read_reg(0), 0x11111111)
        self.assertEqual(self.remote.command("P0=78563412"), "OK")
        self.assertEqual(self.remote.read_reg(0), 0x12345678)

    def test_nak_retransmits_last_response(self):
        self.assertEqual(self.raw_ack(packet("qC")), b"+")
        sock = self.remote.socket
        first = b""
        while not first.endswith(b"#") and len(first) < 64:
            first += sock.recv(1)
        first += sock.recv(1) + sock.recv(1)
        self.assertEqual(first, packet("QC1"))
        sock.sendall(b"-")
        again = b""
        while len(again) < len(first):
            again += sock.recv(len(first) - len(again))
        self.assertEqual(again, first)
        sock.sendall(b"+")
        self.assertEqual(self.remote.command("qC"), "QC1")
        self.assertTrue(self.remote.command("?").startswith("T"))

    def test_qsupported_response_is_bounded_near_capacity(self):
        size = self.packet_size()
        # Each one-byte unsupported feature is echoed as three bytes ("a-;").
        payload = "qSupported:" + ";".join("a" * ((size - 11 + 1) // 2))
        payload = payload[:min(size, 16380)]
        response = self.remote.command(payload)
        self.assertLessEqual(len(response), 16383)
        suffix = f"PacketSize={size:x};swbreak+;hwbreak+;qXfer:features:read+"
        self.assertEqual(response[-len(suffix):].lower(), suffix.lower(), response[-80:])
        echoed = response[:-len(suffix)].split(";")
        self.assertEqual(echoed[-1], "")
        self.assertEqual(set(echoed[:-1]), {"a-"})
        self.assertTrue(self.remote.command("?").startswith("T"))
        self.assertEqual(self.remote.command("qC"), "QC1")

    def test_packet_size_limit_is_accurate(self):
        size = self.packet_size()
        # Pad the address so the hex data ends exactly at the advertised size.
        count = (size - len("M7000,") - 4 - 1) // 2
        header = f"M7000,{count:x}:"
        header = header.replace("M7000", "M" + "0" * (size - len(header) - 2 * count) + "7000")
        data = bytes((i * 7 + 3) & 255 for i in range(count))
        payload = header + data.hex()
        self.assertEqual(len(payload), size)
        self.assertEqual(self.remote.command(payload), "OK")
        read = b""
        while len(read) < count:
            chunk = min(0x1000, count - len(read))
            read += bytes.fromhex(self.remote.command(f"m{0x7000 + len(read):x},{chunk:x}"))
        self.assertEqual(read, data)
        # One byte more is acknowledged (its checksum is valid) but refused.
        self.assertEqual(self.raw_ack(packet(payload + "0")), b"+")
        self.assertEqual(self.remote.receive(), "E22")
        self.assertTrue(self.remote.command("?").startswith("T"))

    WIDTHS = [4] * 10 + [2] * 6 + [4] * 4 + [8] + [10] * 8 + [2, 2, 2, 2, 4, 2, 4, 2] + [8] * 8

    def fields(self, registers):
        offsets, offset = [], 0
        for width in self.WIDTHS:
            offsets.append((offset, offset + 2 * width))
            offset += 2 * width
        self.assertEqual(offset, len(registers))
        return offsets

    def test_g_packet_validates_widths_and_stops_at_rejected_segment(self):
        # Leave the reset vector, whose CS base alias a CS reload would drop.
        self.remote.write_reg(0x10, 0x10)
        self.remote.write_reg(0xa, 0)
        self.remote.write_reg(8, 0x7000)
        registers = self.remote.command("g")
        fields = self.fields(registers)
        self.assertEqual(self.remote.command("G" + registers), "OK")
        self.assertEqual(self.remote.command("g"), registers)
        for malformed in (registers[:-1], registers + "00", registers[:-2] + "zz"):
            self.assertEqual(self.remote.command("G" + malformed), "E22")
            self.assertEqual(self.remote.command("g"), registers)

        self.remote.write_reg(0x10, 1)  # protected mode: a null CS is refused
        before = self.remote.command("g")
        changed = list(before)

        def put(index, value):
            start, end = fields[index]
            changed[start:end] = value.to_bytes((end - start) // 2, "little").hex()

        put(10, 0)              # CS = null selector
        put(17, 0x13572468)     # CR2
        put(37, 0x0123456789abcdef)  # MM0
        self.assertEqual(self.remote.command("G" + "".join(changed)), "E22")
        after = self.remote.command("g")
        for index in range(10, len(fields)):
            start, end = fields[index]
            self.assertEqual(after[start:end], before[start:end], f"register {index} written")

    def test_unacknowledged_pipelined_commands_do_not_block_shutdown(self):
        # Requests sent without acknowledging the responses to earlier ones.
        sock = self.remote.socket
        sock.sendall(packet("?") + packet("qC") + packet("g") + packet("qC"))
        # Quit once the stub is waiting for the first response's acknowledgement.
        received = b""
        while received.count(b"+") < 2:
            received += sock.recv(1)
        self.assertTrue(received.startswith(b"+$T"), received)
        self.quit()

    def test_responses_wait_for_acknowledgement(self):
        sock = self.remote.socket
        sock.sendall(packet("qC"))
        self.assertEqual(sock.recv(1), b"+")
        response = b""
        while len(response) < len(packet("QC1")):
            response += sock.recv(len(packet("QC1")) - len(response))
        self.assertEqual(response, packet("QC1"))
        # Until it is acknowledged, a further request (including IDA's Hc1,
        # once answered by the reader thread) gets only its own '+'.
        sock.sendall(packet("Hc1"))
        self.assertEqual(sock.recv(1), b"+")
        sock.settimeout(0.3)
        with self.assertRaises(TimeoutError):
            sock.recv(1)
        sock.settimeout(10)
        sock.sendall(b"+")
        self.assertEqual(self.remote.receive(), "OK")
        self.assertEqual(self.remote.command("qC"), "QC1")

    def test_buffered_packets_do_not_block_shutdown(self):
        self.remote.socket.sendall(b"".join(b"+" + packet("?") for _ in range(3000)))
        self.quit()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--rompath", type=Path, required=True)
    parser.add_argument("--protocol-port", type=int, default=12450,
                        help="first fixed port for protocol tests (one per test)")
    args, remaining = parser.parse_known_args()
    BINARY, ROMPATH = args.binary.resolve(), args.rompath.resolve()
    ProtocolTests.next_port = args.protocol_port
    unittest.main(argv=[__file__] + remaining)
