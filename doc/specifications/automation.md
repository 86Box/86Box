# Automation socket

Build with `-D AUTOMATION=ON`, then start 86Box with:

```
86Box --automation <endpoint> [--automation-hold] [--automation-seed <n>] ...
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
  only the first 8 characters of the token.
- `--automation-insecure` turns the token off, for convenience on a machine
  you trust, as with the GDB stub. TCP clients are then accepted without
  one, no token file is written, and an `auth` line from a client that sends
  one anyway gets `{"ok":true,"auth":true}`. Any local process can then
  control the machine, and with `--automation-allow-remote` anyone who can
  reach the port. It has no effect on a UNIX socket.
- 86Box refuses to listen on a non-loopback address unless you also pass
  `--automation-allow-remote`, and then logs a warning. Think hard before
  using it. The protocol is unencrypted, and anyone who can reach the port
  and has the token fully controls the machine, including reading and writing
  its memory and changing its media, and can write files into the VM
  directory's `automation-media` folder (`media put`). Loading media also
  lets a client read, through the guest, any file 86Box can open.

The token file is deleted when 86Box exits.

> [!CAUTION]
> **Never connect a machine running 86Box with `--automation-allow-remote` to
> the internet**, nor to any network you don't fully trust, and never forward
> the automation port (or the GDB stub's) through a router. The protocol is
> unencrypted, so the token can be read by anyone on the path, and with
> `--automation-insecure` there is no token at all. Whoever gets in can read
> files on the host through the guest and fill its disk with `media put`. To
> reach 86Box from another machine, keep it on `127.0.0.1` and use an SSH
> tunnel or a VPN instead.

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
| `wait sound <timeout_ms> <dBFS>` | Run until any sound stream reaches the level (peak, dBFS), or until the timeout. Adds `stream` and `onset_ms`, the emulated time of the first sample at the level (sound arrives in blocks of about 20 ms, so the run stops up to a block later). |
| `wait silence <timeout_ms> <dBFS> <for_ms> [quiet_since_ms]` | Run until every stream has stayed below the level for `for_ms`, counting from `quiet_since_ms` (default now), or until the timeout. Adds `quiet_since_ms`, the time of the last sample at the level, also on a timeout, so a client can go on waiting from there. |
| `wait serial <timeout_ms> <port> <from> <text>` | Run until `<text>` (rest of line, raw bytes) is in what the guest has sent on COM`<port>` from position `<from>` on, or until the timeout. Adds `next`, and with a match `at`, the position where it starts. See [Serial ports](#serial-ports). |
| `key down\|up <scancode>...` | Press or release keys. Scancodes are set-1 make codes; add `0x100` for E0-prefixed keys (e.g. `0x148` = Up). Events are delivered 2 ms of emulated time apart (a burst would overflow the keyboard FIFO); the reply comes after the last event plus its 2 ms, so a request consumes 2 ms per key. |
| `mouse <dx> <dy> <buttons>` | Relative motion; buttons bitmask 1=left, 2=right, 4=middle. |
| `tablet <x> <y> <buttons>` / `tablet out` | For the configured tablet, touch screen or light pen: the pen at pixel `x`,`y` of the last frame (the coordinates of `screenshot`), with the buttons pressed (1 = tip or touch); `out` lifts it out of range. With a mouse configured too, `mouse` and `tablet` each switch input to their own device, so both can be used. Fails if no tablet is configured. |
| `screenshot <path>` | Write the most recent frame of monitor 0 as a PNG file. Reply has `w`, `h`. |
| `record start <interval_ms> [<dir>]` / `record stop` | Save the displayed frame every `interval_ms` of emulated time, whatever advances the clock (steps, waits, real time): with `<dir>` (which must exist) as `<dir>/<emulated ms>.png`; without, in memory for `record pull`, for a client on another machine. `stop` replies with the number of frames written. Each frame saved also gets a line in `<dir>/frames.jsonl`: `ms`, `cycles` (CPU cycles since the recording started, the time base the timers run on), `us` (the same in microseconds), `frame` (frames rendered since startup) and `audio`, each sample-exact sound stream's position in samples since `audio start`, when one is recording. |
| `record pull [<max_frames>]` | The frames recorded in memory since the last pull, oldest first, at most `max_frames` (default 32): each with the same fields as a `frames.jsonl` line, `w`, `h`, `key`, and `zstd`, base64 zstd of the frame as RGB24 (`w` × `h` × 3 bytes), XORed byte by byte with the previous frame in the reply sequence unless `key`. Key frames come first and after a size change or a gap. With `lost` (frames skipped because more than 256 MB were waiting; the next frame after them is a key frame), `pending` (frames still waiting) and `recording`. Works after `record stop`, for the last frames. |
| `audio start [<dir>]` / `audio stop` | Record every sound stream: with `<dir>`, as `<dir>/<stream>.wav`; without, in memory only, for a client on another machine to fetch with `audio pull`. The WAVs are (16-bit stereo, each at its own rate): `main` (the sound cards' digital audio, at the configured sample rate), `music` (OPL FM and the PC speaker, 49716 Hz), `wavetable`, `ym2151`, `cd`, `fdd`, `hdd`, `midi`. The streams polled on emulated timers (`main`, `music`, `wavetable`, `ym2151`) are sample-exact: frame n is at `start_ms + n * 1000 / rate`. The others are rendered on their own threads and are dated by when their blocks arrive. A stream whose rate changes goes on in a new segment (`<stream>-2.wav` and so on). `stop` replies with `start_ms` and each segment's `name`, `segment`, `rate`, `frames`, `start_ms`, `exact` and `file` (with a directory), and for a sample-exact stream `drift_frames`: frames written and pending, against the emulated time elapsed (0 means no gap). Capture runs whether or not the host has a sound device, and while fast-forwarding. |
| `audio level <ms>` | Peak and RMS level (dBFS) of each stream over its last `ms` (up to 30 s), with `rate` and `last_ms`, when its last block arrived. |
| `audio read <stream> <ms>` | The stream's last `ms` (up to 30 s) as base64 16-bit stereo `data`, with `rate`, `frames` and `end_ms`. |
| `audio pull <stream> <from> [<max_frames>]` | The recording's samples for one stream, from position `from` (frames since the recording, or the segment, started) up to now, or up to where it stopped: base64 16-bit stereo `data`, with `segment`, `rate`, `from`, `frames`, `next` (the `from` for the next pull, so successive pulls join exactly), `recording`, and `lost`: frames that left the 30 s ring before they were pulled. Works over TCP, and after `audio stop` for the last samples. After a rate change, pull the new `segment` from 0. |
| `serial read <port> [<from> [<max>]]` | What the guest has sent on COM`<port>` from position `from` (default 0) on, at most `max` bytes (default and limit 65536), as hex `data`. With `from` and `next` (the `from` for the next read), `pos` (bytes sent so far), `lost` (bytes asked for that were no longer kept), `last_ms` (emulated time of the last byte), `pending` (bytes queued for the guest that it hasn't taken), `taken` (bytes it has taken), `attached`, and the lines: `dtr`, `rts`, `break` (set by the guest), `cts`, `dsr`, `dcd`, `ri` (seen by it). Fails if the port doesn't have the `automation` backend. |
| `serial write <port> <hex>` | Queue bytes for the guest to receive on COM`<port>` (up to 65536 waiting). Reply has `queued` and `pending`; fails with `input queue full`, queueing nothing, if they don't fit. |
| `serial lines <port> <cts> <dsr> <dcd> <ri>` | Set the modem status lines the guest sees, each 0 or 1 (CTS, DSR and DCD start on). Reply has the lines, as for `serial read`. |
| `info` | Version, machine, CPU and clock, RAM, video and sound cards, mounted hard disk/floppy/CD images, `hard_resets`/`soft_resets` (every CPU reset, including a guest triple fault) and `last_reset_ms`, `speed_percent` (100 = real time), and `serial`: each enabled COM port's `port` and `device` (the backend's internal name, or `none`), the line settings the guest has programmed when a backend is attached (`baud`, `data_bits`, `parity`, `stop_bits`), and for an `automation` port `pos`, `oldest` (the first position still kept), `last_ms`, `pending` and the lines. In a build with the GDB stub, `gdbstub_port`. |
| `text` | Text-mode screen via the BIOS data area: `mode`, `cols`, `rows`, `cursor` [row, col], and `chars`/`attrs` as one hex string per row (CP437). Fails in graphics modes. |
| `memr <addr> <len>` | Read up to 65536 bytes of physical memory; `data` is hex. |
| `memw <addr> <hex>` | Write bytes to physical memory. |
| `regs` | General registers, `eip`, `eflags`, segment selectors, `cs_base`, `cr0`. From the 286: each segment's descriptor cache (`ds_base`, `ds_limit`, `ds_ar` and so on, `ar` being descriptor bytes 5 and 6: the access byte, then G, D/B, AVL and limit 19:16), `gdtr_base`/`gdtr_limit`, `idtr_base`/`idtr_limit`, `ldtr` and `tr` with their base, limit and `ar`, and `cpl`. From the 386: `cr2` (the last page-fault address), `cr3` (the page directory base), `dr0`–`dr3`, `dr6`, `dr7` and `smm`. `cr4` and `tsc` on CPUs that have them. No x86 CPU has a CR1 (`MOV` to or from it is an invalid opcode, see `src/cpu/x86_ops_mov_ctrl.h`), so there is none. |
| `fpu` | The x87: `fcw`, `fsw` (with TOP), `ftw` (the full tag word, two bits per register), `top`, and `st0`–`st7` as 20 hex digits each (sign and exponent, then the significand), as FSAVE would store them. With MMX, `mm0`–`mm7` as 16 hex digits. Fails without an FPU. |
| `fdd load <drive> <path>` / `fdd eject <drive>` | Change floppy media (drive 0 = A:). |
| `cd load <drive> <path>` / `cd eject <drive>` | Change CD-ROM media. |
| `media put <name> <bytes>`, then the bytes | Receive a file from the client, for a client on another machine: the line is followed on the connection by exactly `<bytes>` raw bytes, saved as `automation-media/<name>` in the VM directory (created if needed), replacing a file of that name once all of it has arrived. `<name>` must stay in that folder: no `/`, `\` or `:`, and not `.` or `..`; any other name the OS refuses gets its error. Reply has `path`, to use with `fdd load` or `cd load`, and `bytes`. The machine runs on (or stays held) while the bytes arrive. If the name is bad or the file can't be written, the bytes are still read, and the reply is an error with the OS's reason; if `<bytes>` isn't a number, the connection is closed, since what follows can't be told from the next request. See [Media from a client](#media-from-a-client). |
| `media remove <name>` | Delete a file received with `media put`. |
| `log [<from> [<max>]]` | What 86Box has written to its log, from position `from` (bytes logged since startup; default 0) on, at most `max` bytes (default and limit 65536), as `text`: the last 64 KB are kept. With `from`, `next` (the `from` for the next read), `pos` (bytes logged so far) and `lost` (bytes asked for that were no longer kept). Includes warnings, which 86Box logs instead of showing them (see [Warnings](#warnings)); a release build logs little else. |
| `reset hard` / `reset cad` | Hard reset, or send Ctrl+Alt+Del. |
| `quit soft` | Soft power-off, like the GUI "Power off (soft)" button. Machine must not be held. |
| `quit` | Power off and exit 86Box. |

Numbers accept C syntax (`0x` hex, decimal).

`wait`, `memr` and `text` read memory through the normal physical memory map,
so a read in a memory-mapped device range may have side effects (for example,
VGA latches). They don't cost the emulated CPU time, though: a device's wait
states are not charged for reads and writes made for a client.

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

## Serial ports

A COM port can be connected to the automation socket instead of a host
device or file. In `86box.cfg`, under `[Ports (COM & LPT)]`:

```
serial1_device = automation
```

The guest's UART then works as usual, and a client reads what it sends with
`serial read`, sends it bytes with `serial write`, and sets its modem status
lines with `serial lines`. Everything happens on the emulated clock:

- **Output** goes into a 64 KiB buffer per port. Each byte has a position,
  counting from 0 at startup. A read doesn't remove anything: a client passes
  the `next` from its last reply as `from`, so a lost reply can be read again
  and several readers can follow one port. When more than 64 KiB are waiting,
  the oldest bytes go, and a read that asks for them gets `lost`.
- **Input** waits in a queue of up to 64 KiB and reaches the guest no faster
  than its line would carry it: one character per frame (start bit, data bits,
  parity and stop bits) at the baud rate the guest has set, in emulated time.
  Sent all at once, it would overrun a guest that reads one byte per
  interrupt.
- **Lines:** the guest sees CTS, DSR and DCD on and RI off until a client sets
  them. Its DTR, RTS and break are in every reply. A client can use them to
  act as a modem or a null-modem cable. BIOS INT 14h, for one, won't send
  without DSR and CTS.
- **Resets:** the buffers belong to the socket, not the port, so a hard
  reset, which closes and reopens serial devices, loses nothing.
  Positions run on across it.

A held run is repeatable here as elsewhere: the same input at the same
emulated times gives the same bytes at the same positions and `last_ms`.

The backend is only built with `-D AUTOMATION=ON`.

## Media from a client

`fdd load` and `cd load` take a path on the machine running 86Box. A client
on another machine sends the image first, then loads it from the path in the
reply:

```
media put game.img 1474560
<1474560 bytes>
{"ok":true,"bytes":1474560,"path":"/vms/dos/automation-media/game.img"}
fdd load 0 /vms/dos/automation-media/game.img
```

A CD image in several files (a `.cue` and its `.bin`) goes as several files
under the names the `.cue` uses. The files stay in the VM directory, where
the configuration may refer to them, until `media remove` deletes them.
Replacing a file that a drive has open works on Linux and macOS (the drive
reads the old one until the next load); on Windows, eject it first.

## Warnings

With automation on, 86Box writes warnings (such as a media image that can't
be loaded) to its log and carries on, without the warning dialog it otherwise
shows. A client with no window couldn't dismiss it, and the machine and the
socket would be stuck until someone did.

## When 86Box stops

On a fatal error, 86Box writes the message to its log and closes the
automation socket before it shows the error dialog, so a client sees its
connection close rather than waiting for a reply. Before it closes, a
connected client gets `{"ok":false,"fatal":true,"error":"<message>"}`: the
reply to its request, if one was waiting, or a line of its own.

## With the GDB stub

In a build with both `-D AUTOMATION=ON` and `-D GDBSTUB=ON`, a GDB client
and an automation client can share a machine:

- While the GDB stub has the CPU stopped (the stub starts it stopped, and a
  breakpoint, a watchpoint or a GDB interrupt stops it), no emulated time
  passes, and `ms` doesn't advance. A `step`, `wait` or `key` command ends
  as soon as the CPU stops, with `"stopped":"debugger"` in its reply. A
  `wait` that ends this way has `"matched":false`.
- While the machine is held, the stub still answers GDB: registers, memory,
  breakpoints and monitor commands work. A GDB `continue` or `step` takes
  effect when the CPU next runs, for example during an automation `step`.

## Determinism notes

- While held, emulated time is fully under the client's control. Disable
  "Synchronize time with host" (`time_sync = disabled`) so the guest RTC does
  not follow the host clock.
- Devices that run their own host threads or use host timers (some sound
  devices, networking, passthrough) can still introduce variation.
- 86Box's random numbers mix in the host CPU's timestamp counter on every
  call. They give a network card its MAC when the configuration has none,
  and the weak bits and unformatted areas of 86F floppy images, among other
  things. Software can depend on these: Warcraft II's game setup changes with
  the NE2000's MAC, for example. Pass `--automation-seed <n>` to draw them
  from a fixed seed instead (decimal, or hex with `0x`), so the same seed
  gives the same numbers. Copies of one VM that are networked together then
  need different seeds or their own `mac` lines, or they get the same
  generated MAC.
