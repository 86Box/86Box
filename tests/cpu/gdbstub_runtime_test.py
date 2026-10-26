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


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--rompath", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    BINARY, ROMPATH = args.binary.resolve(), args.rompath.resolve()
    unittest.main(argv=[__file__] + remaining)
