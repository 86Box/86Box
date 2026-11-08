---
name: 86box-automation
description: Drive a running 86Box machine headlessly from a script through its automation socket — boot a VM, step emulated time, capture screen/audio/serial, read and write memory, I/O ports, PCI config and CPU registers, and pull the emulator's log. Use to reproduce and observe emulator behaviour, verify a device model or machine fix against a real guest, or capture evidence for any claim about what the emulated hardware does.
---

# 86Box automation

The protocol — every request, every reply field, transports, tokens, memory
views, the GDB stub — is specified in doc/specifications/automation.md
(read it there; do not re-derive it here). This skill covers only getting a
run going, the traps, and how to use the socket as the evidence loop.

Needs a build with `-D AUTOMATION=ON` (`cmake -DAUTOMATION=ON -S . -B build`).

`automation.py` next to this file is a Python 3 client using only the standard
library: `run()`, `screen()`, `memory()`, `log()`. Copy it beside your work.
It supports UNIX sockets on Linux/macOS and TCP on all three platforms.

## Launch

`-P <dir>` is required — without it 86Box opens the VM manager instead of the
machine, and no socket ever appears. Launch detached; the socket is the
interface, never wait on the process.

### Linux and macOS

Use a UNIX socket. The shell commands below work in both bash and zsh and do
not require the Linux `setsid` utility. For a macOS app bundle, invoke the
executable inside it rather than `open`, so the arguments reach 86Box directly.

```sh
exe="$PWD/build/src/86Box"
# macOS bundle build:
# exe="$PWD/build/src/86Box.app/Contents/MacOS/86Box"
rm -f /tmp/86box.sock                     # only after the previous VM has exited
nohup "$exe" -P "/path/to/VM" \
    --automation unix:/tmp/86box.sock \
    --automation-hold --automation-seed 1 \
    > /tmp/vm.log 2>&1 < /dev/null &
vm_pid=$!
```

### Windows (PowerShell)

86Box's Windows server uses TCP, not UNIX sockets. Keep the listener on
loopback and use its token file; the client authenticates each new connection.
Quote paths in `-ArgumentList` as shown, including paths containing spaces.

```powershell
$vm = 'C:\path\to\VM'
$env:AUTOMATION_ENDPOINT = 'tcp:127.0.0.1:7086'
$env:AUTOMATION_TOKEN_FILE = Join-Path $vm 'automation.token'
$vmProcess = Start-Process -FilePath 'C:\path\to\86Box.exe' -ArgumentList @(
    '-P', "`"$vm`"",
    '--automation', $env:AUTOMATION_ENDPOINT,
    '--automation-token-file', "`"$env:AUTOMATION_TOKEN_FILE`"",
    '--automation-hold', '--automation-seed', '1'
) -WindowStyle Hidden -PassThru
```

### Connect

Startup may take several seconds. With `automation.py` beside your script,
wait for a successful request with a deadline rather than waiting indefinitely
for a socket or token file to appear:

```python
import time
import automation as a

deadline = time.monotonic() + 30
while True:
    try:
        status = a.run(["status"], timeout=1)[0][1]
        if not status.get("ok"):
            raise RuntimeError(status.get("error"))
        break
    except (OSError, ValueError):
        if time.monotonic() >= deadline:
            raise
        time.sleep(0.2)
```

The default endpoint is `/tmp/86box.sock` on Linux/macOS and `tcp:7086` on
Windows. Set `AUTOMATION_ENDPOINT` before importing the client to override it;
`AUTOMATION_TOKEN_FILE` names the TCP token file. The Windows launch example
sets both for the recipes below. For explicit settings, all four helpers also
accept `sock_path=` and `token_file=`:

```python
a.run(["status"], sock_path="tcp:127.0.0.1:7086",
      token_file=r"C:\path\to\VM\automation.token")
```

The command-line client accepts the same settings with `--endpoint` and
`--token-file`, or reads them from the environment. From the repository root:

```sh
python3 doc/skills/automation/automation.py --endpoint unix:/tmp/86box.sock status
```

```powershell
py -3 doc/skills/automation/automation.py status
```

`--automation-hold` makes runs repeatable: no emulated time passes until a
request asks for it, and `step` then runs unpaced (a 15 s boot costs seconds of
wall clock). For Qt builds with the offscreen platform plugin available, set
`QT_QPA_PLATFORM=offscreen` before launching to run without a display (`export
QT_QPA_PLATFORM=offscreen` in bash/zsh; `$env:QT_QPA_PLATFORM = 'offscreen'` in
PowerShell).

State belongs to the machine, not the connection, so a fresh connection per
command is fine — keep the script that way unless you need ordering *within* a
wait.

## Traps

- **`memr`/`memw` lengths are decimal only** (`atoi`), so `0x1000` parses as 0
  and the reply is `length out of range, or not a multiple of the width`.
  Addresses do take hex. Length ≤ 65536, multiple of the width.
- **The VM directory is stateful.** Devices persist NVR/EEPROM (`nvr/*.nvr`)
  and the CMOS. A run where the guest *repaired* something masks that bug on
  the next run — delete the specific `.nvr` when testing NVR code, and step a
  fresh boot rather than trusting a second run.
- **`screenshot` fails before the first frame**; `step` a few ms first.
- Reads through a device's memory window can have side effects (latches,
  FIFOs) and `text`/`memr` do not charge emulated CPU time, so they are not
  invisible to the guest's timing.
- Set `time_sync = disabled` in the VM config so the guest RTC does not follow
  the host clock.

## Observe

Pick the narrowest channel that answers the question; a screen dump is the
slowest and least precise.

| Question | Command |
|---|---|
| Is the guest at the point I expect? | `wait text` / `wait mem` / `wait serial` — run until the condition, not a guessed `step` |
| What does the guest show? | `text` (CP437 rows), `screenshot`, `record start` for a timeline |
| What does the guest say? | `serial read` (needs an `automation` backend on the port) |
| What is in memory / an I/O port? | `memr`, `ior` |
| Is a card present, and where? | `pcir` — read straight from the function, bypassing 0CF8h |
| Where is the CPU? | `regs`, `fpu` |
| What did the model do? | `log` — most device models log in-tree |
| Any reset, fault, triple fault? | `info` (`hard_resets`, `soft_resets`) |

`wait` is the workhorse: it advances only until its condition matches and
returns `matched`, so scripts need no polling loop and no magic delay.

## Recipes

Boot, then assert the guest reached a known state:

```python
import automation as a
assert a.run(["wait text 30000 Award"])[0][1]["matched"]
print("\n".join(r for r in a.screen() if r.strip()))
```

Enumerate the PCI bus and compare against what the machine's firmware expects:

```python
for dev in range(32):
    r = a.run([f"pcir 0 {dev} 0 0 8"])[0][1]
    if r.get("present"):
        print(dev, r["data"])     # 05901f00... = 9005:001F
```

A card in the wrong slot — because it was registered with the wrong PCI card
type — is invisible to firmware that scans a fixed device number, and both the
POST listing and `pcir` show it at once.

Dump a region and compare two builds by what the guest ended up with, not by
reading logs:

```python
mem = b"".join(a.memory(base, 65536) for base in range(0xC0000, 0x100000, 0x10000))
```

For a device model, the log is the highest-signal evidence: grep it for the
model's own lines rather than instrumenting a rebuild. A register-level trace
floods the log and costs an order of magnitude in speed — enable it for the one
question, then turn it back off.

## Iterate

`quit`, confirm the process is gone, rebuild, relaunch (removing the socket
first), re-run the same scripted scenario. Never trust a build without it.

End every run with `run(["quit"])`; `quit` also works while held. Wait for the
process to exit (`wait "$vm_pid"` on Linux/macOS or `$vmProcess.WaitForExit()`
on Windows). On Linux/macOS, remove a leftover socket with
`rm -f /tmp/86box.sock` only after the process has exited. TCP has no socket
file to remove; 86Box deletes its token file when it exits.
