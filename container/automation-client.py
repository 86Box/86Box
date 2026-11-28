#!/usr/bin/env python3
"""Minimal client for the 86Box automation socket (doc/specifications/automation.md).

Sends each command given on the command line, or each line read from stdin,
and prints the one-line JSON reply. Exits non-zero if a reply has "ok": false.

    automation-client.py unix:/path/86box.sock status "step 1000" "screenshot /run/86box/s.png"
    automation-client.py tcp:127.0.0.1:7086 --token-file ./run/automation.token info
    automation-client.py unix:./run/86box.sock --wait 30 status    # wait for the socket first
"""
import argparse
import json
import os
import socket
import sys
import time


def connect(endpoint, wait):
    deadline = time.monotonic() + wait
    while True:
        try:
            if endpoint.startswith("unix:"):
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                path = endpoint[5:]
                try:
                    s.connect(path)
                except OSError as e:
                    if "too long" not in str(e):
                        raise
                    # A socket path is limited to about 100 bytes: connect from its directory.
                    cwd = os.getcwd()
                    os.chdir(os.path.dirname(os.path.abspath(path)))
                    try:
                        s.connect(os.path.basename(path))
                    finally:
                        os.chdir(cwd)
            elif endpoint.startswith("tcp:"):
                host, _, port = endpoint[4:].rpartition(":")
                s = socket.create_connection((host or "127.0.0.1", int(port)))
            else:
                sys.exit("endpoint must be unix:<path> or tcp:[host:]port")
            return s
        except (FileNotFoundError, ConnectionRefusedError) as e:
            if time.monotonic() >= deadline:
                sys.exit(f"cannot connect to {endpoint}: {e.strerror or e} (is 86Box running? --wait gives it time to start)")
            time.sleep(0.2)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("endpoint")
    ap.add_argument("commands", nargs="*", help="commands; read from stdin if none")
    ap.add_argument("--token-file", help="TCP: file holding the token (sent as 'auth <token>')")
    ap.add_argument("--wait", type=float, default=0, help="seconds to wait for the socket to appear")
    args = ap.parse_args()

    deadline = time.monotonic() + args.wait
    token = None
    if args.token_file:
        # The token is written at startup: wait for it with the socket.
        while True:
            try:
                token = open(args.token_file).read().strip()
                if token:
                    break
            except FileNotFoundError:
                pass
            if time.monotonic() >= deadline:
                sys.exit("no token in " + args.token_file)
            time.sleep(0.2)

    failed = False
    f = None

    def roundtrip(line):
        f.write(line.encode() + b"\n")
        f.flush()
        return f.readline()

    def show(reply):
        nonlocal failed
        print(reply.decode().rstrip("\n"), flush=True)
        if not json.loads(reply).get("ok", False):
            failed = True

    # The first exchange: auth with a token, otherwise "status" (not shown).
    # A container's published port accepts connections before 86Box listens
    # and then drops them, so with --wait, a connection closed before this
    # reply is tried again.
    while True:
        f = connect(args.endpoint, max(0.0, deadline - time.monotonic())).makefile("rwb")
        reply = roundtrip(("auth " + token) if token else "status")
        if reply:
            break
        if time.monotonic() >= deadline:
            sys.exit("connection closed by 86Box")
        time.sleep(0.2)
    if token:
        show(reply)

    def send(line):
        reply = roundtrip(line)
        if not reply:
            sys.exit("connection closed by 86Box")
        show(reply)

    commands = args.commands or (line.rstrip("\n") for line in sys.stdin)
    for cmd in commands:
        if cmd.strip():
            send(cmd)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
