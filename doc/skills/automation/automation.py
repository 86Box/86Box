#!/usr/bin/env python3
"""Minimal 86Box automation client.

    from automation import run
    run(["step 15000"])[0][1]["ms"]

Each command is one line; each reply is one JSON line. `run` returns
[(command, reply_dict)]. State lives in the machine, not the connection, so a
fresh connection per command is fine and keeps scripts simple.
"""
import argparse
import json
import os
import socket
import sys

SOCK = os.environ.get("AUTOMATION_ENDPOINT",
                      "tcp:7086" if sys.platform == "win32" else "/tmp/86box.sock")


def _endpoint(spec):
    """Use the same endpoint syntax and platform defaults as 86Box."""
    if spec.startswith("unix:"):
        return False, spec[5:]
    if spec.startswith("tcp:") or sys.platform == "win32":
        target = spec[4:] if spec.startswith("tcp:") else spec
        host, sep, port = target.rpartition(":")
        if not sep:
            host, port = "127.0.0.1", target
        port = int(port)
        if not host or not 0 < port < 65536:
            raise ValueError("expected tcp:[host:]port with a port from 1 to 65535")
        return True, (host, port)
    return False, spec


def _request(f, command):
    f.write((command + "\n").encode())
    f.flush()
    line = f.readline()
    if not line:
        raise ConnectionError("automation connection closed")
    return json.loads(line)


def run(cmds, sock_path=SOCK, timeout=600, token_file=None):
    """Run commands over UNIX or TCP; authenticate TCP using a token file.

    sock_path accepts the endpoint syntax from --automation, as well as the
    original bare UNIX socket path. AUTOMATION_TOKEN_FILE supplies the token
    file when it is not passed explicitly. Omit it only for an insecure TCP
    server; UNIX sockets do not need a token.
    """
    tcp, address = _endpoint(sock_path)
    token = None
    if tcp:
        if token_file is None:
            token_file = os.environ.get("AUTOMATION_TOKEN_FILE")
        if token_file is not None:
            with open(token_file, encoding="ascii") as f:
                token = f.read().strip()
            if not token or any(c.isspace() for c in token):
                raise ValueError("token file must contain one non-empty token")
        s = socket.create_connection(address, timeout)
    else:
        if not hasattr(socket, "AF_UNIX"):
            raise ValueError("UNIX sockets are unavailable; use tcp:[host:]port")
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

    with s:
        s.settimeout(timeout)
        if not tcp:
            s.connect(address)
        with s.makefile("rwb") as f:
            if token is not None:
                reply = _request(f, "auth " + token)
                if not reply.get("ok") or not reply.get("auth"):
                    raise PermissionError(reply.get("error", "authentication failed"))
            out = []
            for c in cmds:
                c = c.strip()
                if c:
                    out.append((c, _request(f, c)))
            return out


def screen(sock_path=SOCK, token_file=None):
    """The text screen as a list of str, or None in graphics modes."""
    r = run(["text"], sock_path, token_file=token_file)[0][1]
    if not r.get("ok"):
        return None
    return [bytes.fromhex(row).decode("cp437").rstrip() for row in r["chars"]]


def memory(addr, length, sock_path=SOCK, token_file=None):
    """Physical memory as bytes. Lengths are decimal on the wire: pass decimal."""
    r = run([f"memr {addr} {length}"], sock_path, token_file=token_file)[0][1]
    if not r.get("ok"):
        raise RuntimeError(r.get("error"))
    return bytes.fromhex(r["data"])


def log(sock_path=SOCK, token_file=None):
    return run(["log 0 65536"], sock_path, token_file=token_file)[0][1].get("text", "")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", default=SOCK,
                        help="86Box endpoint (default: %(default)s)")
    parser.add_argument("--token-file", help="TCP token file (or AUTOMATION_TOKEN_FILE)")
    parser.add_argument("--timeout", type=float, default=600,
                        help="socket timeout in seconds (default: %(default)s)")
    parser.add_argument("commands", nargs="*", help="commands, one per argument; default: stdin")
    args = parser.parse_args()
    cmds = args.commands if args.commands else list(sys.stdin)
    for c, r in run(cmds, args.endpoint, args.timeout, args.token_file):
        print(c)
        print("  ", json.dumps(r))
