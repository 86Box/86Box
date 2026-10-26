# Guest GDB debugging

The GDB remote stub is available in ordinary 86Box builds. It is disabled by
default and does not require an interpreter-only build. The old `GDBSTUB`
CMake option is no longer used; `DYNAREC` remains an independent build option.

## Activation

In the Qt GUI, open **Settings → Machine → Debugging**, enable **GDB remote
debugger**, and select a TCP port (12345 by default). Leave **Dynamic
recompiler** enabled under the Performance tab if you want to debug dynarec
execution.

Saving applies the changes through the normal settings hard reset, restarting
the **guest**, not the application. Enabling the listener starts the guest
paused. Disabling it closes the listener and disconnects clients; changing
ports disconnects clients and starts a new, paused listener on the new port.
Hard resets with unchanged effective debugger settings preserve the listener
and debugger connection. The page displays saved settings and a notice when
command-line options override them.

Alternatively, enable the stub when launching a machine:

```sh
86Box --gdbstub --gdbstub-port 12345 --vmpath /path/to/vm
```

Alternatively, edit the machine's `86box.cfg`:

```ini
[General]
gdbstub_enabled = 1
gdbstub_port = 12345
```

The default port is 12345. Ports must be between 1 and 65535. An enabled
listener that cannot start produces a fatal error rather than silently
continuing without debugging.

`--gdbstub` and `--no-gdbstub` override the configured enable setting.
`--gdbstub-port PORT` overrides the configured port but does not itself enable
the listener. Command-line overrides apply only to that launch and are not
saved to the machine configuration. If an option is repeated, the last value
wins.

Enabling the stub starts the guest paused, even before a debugger connects.
The ordinary disabled state opens no debugger socket and does not wrap CPU
execution. The GUI does not hot-toggle debugging while the guest is executing;
settings changes use the hard-reset boundary described above.

> [!CAUTION]
> The stub listens on **all IPv4 interfaces**, not just localhost. It has no
> authentication or encryption. Anyone who can reach it can control the
> guest, inspect or change memory and registers, perform guest port I/O, and
> reset the machine. Use a trusted host/network and a firewall; never expose
> or forward the port to the internet. An SSH tunnel does not by itself
> restrict access to this all-interface listener.

## Connecting

For example, from GDB on the same host:

```gdb
target remote 127.0.0.1:12345
info registers
x/16bx $eip
stepi
continue
```

Load suitable guest symbols separately with GDB's `file`, `symbol-file` or
`add-symbol-file` commands. This is a debugger for the **emulated CPU**, not
the host emulator or its generated host instructions. Use a separate host
debugger to investigate 86Box itself.

The stub retains its existing linear-address convention: reported EIP and
breakpoint/memory addresses include the CS base. In real mode, account for
segment bases when interpreting addresses.

Supported operations include register and memory access, continue,
instruction stepping, software and hardware execution breakpoints, and
read/write/access watchpoints through the existing checked memory paths.
Software execution breakpoints are checked at instruction boundaries rather
than repeatedly replacing guest instructions with INT3. Their existing
writable-memory check is retained.

Guest debug registers are not used to implement remote breakpoints. Their
normal emulation and configuration remain independent of the remote stub.
Watchpoints retain the existing memory-helper coverage; paths that bypass
these helpers, including the 808x/NEC memory engines and some device/DMA
accesses, are not newly covered.

The existing monitor commands remain available:

| GDB command | Operation |
|---|---|
| `monitor help` | List commands. |
| `monitor ib/iw/il [port [length]]` | Read byte/word/dword guest I/O ports. |
| `monitor ob/ow/ol [[port] value]` | Write byte/word/dword guest I/O ports. |
| `monitor r` | Hard-reset the guest machine. |

Port numbers and values use the stub's existing hexadecimal syntax. Monitor
I/O has real guest-device side effects.

Detaching or disconnecting clears remote breakpoints/watchpoints and resumes
execution. A new connection pauses the guest; replacing the current client
does not inherit its breakpoints. Normal CPU selection is preserved across
resets.

If 86Box is paused in its UI or held by automation, register/memory commands
still work. A remote continue or step takes effect when that outer pause/hold
is released. AUTOMATION-enabled builds report the listener's effective
`gdbstub_active` and `gdbstub_port` values in `info`.

## Dynarec behavior

Attaching does not turn off the dynarec. Debugger-aware translated blocks
check guest instruction boundaries during continue and single-step, including
inside previously warmed loops. Connecting/disconnecting changes code-cache
mode on the emulation thread. Debugger memory/control-state edits invalidate
cached assumptions before execution resumes.

Direct block links and full-loop unrolling are disabled while a client is
connected. The dynarec's ordinary discovery/compilation interpretation and
unsupported-instruction helpers still apply. Debugging changes execution
overhead and timing; it is not a performance-equivalent view of optimized,
unattached execution.

For 386+ execution, a single step covers one instruction, including a
completed REP instruction, unless a guest exception/interrupt redirects
execution or the instruction is restarted by the CPU engine. A watchpoint
reports the matching instruction's completed access and takes precedence over
the single-step stop reason.

## Regression tests

The Python tests use only the standard library. They **overwrite guest
registers, RAM, interrupt vectors and page tables**; never attach them to a
machine with valuable state.

Launch an isolated, diskless Pentium MMX fixture using locally installed ROMs:

```sh
python tests/cpu/gdbstub_test.py --binary build/src/86Box \
    --rompath /path/to/roms --runtime --port 12346 -v
```

Use `--interpreter` to select interpreter execution. `--trace-translated`
requires host GDB and asserts calls from generated code during both continue
and single-step. `--guest-gdb` adds a real GDB compatibility check.
`--graceful` requires an AUTOMATION-enabled binary and checks shutdown through
the automation socket.

On a UNIX host with an AUTOMATION-enabled Qt binary:

```sh
python tests/cpu/gdbstub_runtime_test.py --binary build/src/86Box \
    --rompath /path/to/roms -v
```

This checks default-disabled behavior, activation, nonpersistent CLI
overrides, invalid ports, and an occupied listener port. The launchers isolate
configuration/cache/data directories but preserve `XDG_RUNTIME_DIR`, so the
host's audio sockets remain available.

With `BUILD_TESTING=ON`, the `gdbstub_watch_tests` target provides focused
watchpoint macro tests. To opt into isolated CTest integration, configure
`GDBSTUB_TEST_BINARY` and `GDBSTUB_TEST_ROMPATH`. Alternatively,
`GDBSTUB_TEST_PORT` targets an already running disposable, paused 386+ guest.
The guest tests run serially; firmware is not downloaded.
