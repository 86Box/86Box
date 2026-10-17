# Automation socket

Build with `-D AUTOMATION=ON`, then start 86Box with:

```
86Box --automation <endpoint> [--automation-hold] ...
```

86Box listens on the endpoint and accepts one client at a time.
`--automation-hold` starts the machine **held**: no emulated time passes until
a client asks for it. That makes runs repeatable and lets a slow client (a
test harness, a script, an AI agent) take as long as it needs between
moves.

## Transports and security

| Endpoint | Transport |
|---|---|
| `unix:/path/to/socket` | UNIX domain socket (Linux, macOS) |
| `tcp:port` | TCP on `127.0.0.1` |
| `tcp:host:port` | TCP on `host` |
| bare value | A socket path on Linux/macOS; a TCP port (or `host:port`) on Windows |

Prefer the UNIX socket where you can. It is created owner-only (mode 0600), so
only your user can connect, and it can't be reached over the network at all.

Any process on the machine can connect to a TCP port, so TCP requires a
token:

- At startup 86Box generates 32 random bytes from the OS random source and
  writes them, hex-encoded, to the token file. That's `--automation-token-file
  <path>`, or `automation.token` in the VM directory by default. On Linux and
  macOS the file is owner-only; on Windows it inherits the folder's
  permissions.
- A client's first line must be `auth <token>`. Anything else, or a wrong
  token, gets `{"ok":false,"error":"authentication required"}` and the
  connection is closed. The comparison is constant-time, and the log shows
  only the first 8 characters of the token. Authentication can't be turned
  off.
- 86Box refuses to listen on a non-loopback address unless you also pass
  `--automation-allow-remote`, and then logs a warning. Think hard before
  using it. The protocol is unencrypted, and anyone who can reach the port
  and has the token fully controls the machine, including reading and writing
  its memory and changing its media.

The token file is deleted when 86Box exits.

## Protocol (version 1)

Each request is one line of space-separated words. Each response is one line
of JSON with `"ok": true` or `"ok": false, "error": "..."`. Requests run on
the emulation thread between 1 ms quanta (`pc_run()` calls), so each request
sees the machine at a consistent point. Requests are handled one at a time,
in order.

Responses to most commands include `ms` (emulated milliseconds since
startup), `frames` (frames rendered) and `held`.

| Request | Effect |
|---|---|
| `status` | Protocol and emulator version, `ms`, `frames`, `held`, `paused` (UI pause), `ff` (fast forward), last frame size `w`/`h`, `bios_mode` (BIOS data area 0x449), `quantum_ms`. |
| `hold` | Stop running the CPU between requests. |
| `release` | Resume normal real-time execution. |
| `ff [on\|off]` | Fast forward on or off, as the UI's (no pacing to real time when the machine runs); with no argument, what it is. Reply has `ff`. |
| `step <ms>` | Run exactly `ms` of emulated time, then reply. If held, runs without real-time pacing (as fast as the host allows), then holds again. |
| `wait text <timeout_ms> <text>` | Run until `<text>` (rest of line, matched against raw screen bytes) appears in a single row of the text screen, or until the timeout. Reply has `matched`. |
| `wait mem <timeout_ms> <addr> <hex>` | Run until physical memory at `addr` equals the hex bytes, or until the timeout. |
| `key down\|up <scancode>...` | Press or release keys. Scancodes are set-1 make codes; add `0x100` for E0-prefixed keys (e.g. `0x148` = Up). Events are delivered 2 ms of emulated time apart (a burst would overflow the keyboard FIFO); the reply comes after the last event plus its 2 ms, so a request consumes 2 ms per key. |
| `mouse <dx> <dy> <buttons>` | Relative motion; buttons bitmask 1=left, 2=right, 4=middle. |
| `screenshot <path>` | Write the most recent frame of monitor 0 as a PNG file. Reply has `w`, `h`. |
| `record start <interval_ms> <dir>` / `record stop` | Save the displayed frame as `<dir>/<emulated ms>.png` every `interval_ms` of emulated time, whatever advances the clock (steps, waits, real time). The directory must exist. `stop` replies with the number of frames written. |
| `info` | Version, machine, CPU and clock, RAM, video and sound cards, mounted hard disk/floppy/CD images, `hard_resets`/`soft_resets` (every CPU reset, including a guest triple fault) and `last_reset_ms`, and `speed_percent` (100 = real time). |
| `text` | Text-mode screen via the BIOS data area: `mode`, `cols`, `rows`, `cursor` [row, col], and `chars`/`attrs` as one hex string per row (CP437). Fails in graphics modes. |
| `memr <addr> <len>` | Read up to 65536 bytes of physical memory; `data` is hex. |
| `memw <addr> <hex>` | Write bytes to physical memory. |
| `regs` | General registers, `eip`, `eflags`, segment selectors, `cs_base`, `cr0`. |
| `fdd load <drive> <path>` / `fdd eject <drive>` | Change floppy media (drive 0 = A:). |
| `cd load <drive> <path>` / `cd eject <drive>` | Change CD-ROM media. |
| `reset hard` / `reset cad` | Hard reset, or send Ctrl+Alt+Del. |
| `quit soft` | Soft power-off, like the GUI "Power off (soft)" button. Machine must not be held. |
| `quit` | Power off and exit 86Box. |

Numbers accept C syntax (`0x` hex, decimal).

`wait`, `memr` and `text` read memory through the normal physical memory map,
so a read in a memory-mapped device range may have side effects (for example,
VGA latches).

Try it by hand:

```
$ 86Box --automation unix:/tmp/86box.sock --automation-hold &
$ socat - UNIX-CONNECT:/tmp/86box.sock
status
step 1000
screenshot /tmp/frame.png
```

Over TCP, send the token first:

```
$ 86Box --automation tcp:7086 --automation-token-file /tmp/86box.token &
$ (echo "auth $(cat /tmp/86box.token)"; echo status) | socat - TCP:127.0.0.1:7086
```

## Determinism notes

- While held, emulated time is fully under the client's control. Disable
  "Synchronize time with host" (`time_sync = disabled`) so the guest RTC does
  not follow the host clock.
- Devices that run their own host threads or use host timers (some sound
  devices, networking, passthrough) can still introduce variation.
