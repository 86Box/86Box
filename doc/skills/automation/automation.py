#!/usr/bin/env python3
"""Minimal 86Box automation client.

    from automation import run
    run(["step 15000"])[0][1]["ms"]

Each command is one line; each reply is one JSON line. `run` returns
[(command, reply_dict)]. State lives in the machine, not the connection, so a
fresh connection per command is fine and keeps scripts simple.
"""
import json
import socket
import sys

SOCK = "/tmp/86box.sock"


def run(cmds, sock_path=SOCK, timeout=600):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(sock_path)
    f = s.makefile("rwb")
    out = []
    for c in cmds:
        c = c.strip()
        if not c:
            continue
        f.write((c + "\n").encode())
        f.flush()
        line = f.readline()
        if not line:
            raise SystemExit("connection closed after: " + c)
        out.append((c, json.loads(line)))
    f.close()
    s.close()
    return out


def screen(sock_path=SOCK):
    """The text screen as a list of str, or None in graphics modes."""
    r = run(["text"], sock_path)[0][1]
    if not r.get("ok"):
        return None
    return [bytes.fromhex(row).decode("cp437").rstrip() for row in r["chars"]]


def memory(addr, length, sock_path=SOCK):
    """Physical memory as bytes. Lengths are decimal on the wire: pass decimal."""
    r = run([f"memr {addr} {length}"], sock_path)[0][1]
    if not r.get("ok"):
        raise RuntimeError(r.get("error"))
    return bytes.fromhex(r["data"])


def log(sock_path=SOCK):
    return run(["log 0 65536"], sock_path)[0][1].get("text", "")


if __name__ == "__main__":
    cmds = sys.argv[1:] if len(sys.argv) > 1 else list(sys.stdin)
    for c, r in run(cmds):
        print(c)
        print("  ", json.dumps(r))
