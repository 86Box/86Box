# Optional VX timer POST bypass

The VX machine has an optional `skip_timer_checks` configuration setting, disabled
by default. When enabled, it changes the supported VX's loaded ITF buffer so timer
POST failures continue into initialization. Firmware files remain unchanged.
Disabling the setting restores normal POST on the next machine initialization.
Enable it in **Settings → Machine → Configure → Skip BIOS timer checks (CPU
upgrades)**, then select the CPU and save the settings. Its INI value is
`skip_timer_checks = 1` in the `[NEC PC-9801VX]` section.
This is a requested firmware check bypass, not a change to the PIT frequency or
a claim that accelerated CPU timing matches physical VX hardware.

## Why the check is sensitive to CPU timing

The supplied VX ITF's `TIMER ERROR` test is at F800:08D4–091C. It programs each
8253 channel in mode zero with an LSB-only count, first FFh and then 00h. After a
few instructions and two short jumps, it issues a counter latch and reads back
the count. It expects that value already to differ from the programmed byte. An
unchanged value selects the fatal return address 090Bh and prints `TIMER ERROR`.
In the measured firmware, the error routine disables interrupts at 18EAh and
loops at 18EBh before reaching that HLT. This is a test of elapsed
time between I/O operations as well as of the timer's operation.

The native motherboard drives all PIT channels at 1.9968 MHz. Changing the CPU
frequency or memory wait states changes how much time those intervening
instructions consume, while the PIT reload and counting sequence still runs in
its clock domain. This explains why the failure can depend on CPU/wait-state
settings; it does not by itself prove that the PIT frequency is wrong. The
instruction evidence is preserved in `tmp/vx-timer-audit/timer-post.disasm.txt`.

`TIMER INTERRUPT ERROR` is a separate test at 0960h–09DDh. It verifies master and
slave PIC mask readback, checks that IRQ0 does not arrive prematurely, programs
channel zero with 001Ah, unmasks IRQ0, and polls at most 32 times for its handler
to set AH. That handler masks the master PIC and sends EOI. The bounded wait is
also sensitive to how quickly the guest executes its polling instructions.

## Version validation and exact changes

The helper accepts only a complete 32 KiB ITF buffer. Its full FNV-1a fingerprint
must match the supported original image, `40EC270E008F53E9`, or the exact patched
image, `C9958255F5A6E707`. These fingerprints identify a firmware revision; they
are not authentication mechanisms. It also verifies each short patch signature
and the checksum padding before any mutation. Unknown revisions and invalid
lengths/pointers are left unchanged. A repeated call on the exact patched buffer
returns `ALREADY_APPLIED` without changing it.
If the setting is enabled for an unsupported firmware revision, the loader shows
an error and stops initialization; it does not silently start unpatched firmware.
The original firmware can still be used by disabling the setting.

| ITF offset | Original bytes | Replacement | Effect |
| --- | --- | --- | --- |
| 0900h | `75 0A` | `EB 0A` | Continue past the unchanged-count fatal exit |
| 096Ch | `74 0A` | `EB 0A` | Continue past master PIC mask readback failure |
| 0980h | `74 0A` | `EB 0A` | Continue past slave PIC mask readback failure |
| 09ACh | `74 0A` | `EB 0A` | Continue past premature IRQ0 failure |
| 09D4h–09DDh | `BE CC 17 BC DD 09 E9 86 0E F4` | `B0 FF E6 02 EB 04 90 90 90 90` | On IRQ timeout, mask master PIC and continue at 09DEh |

The final replacement is `MOV AL,FFh; OUT 02h,AL; JMP 09DEh;` followed by four
NOPs. It retains the original bounded polling loop at 09D0h–09D3h. A working
IRQ0 handler still follows the original successful path; a timeout now masks
the master PIC as that handler would, then reaches the original PIT reload.
The PIT programming, all three channel tests, IRQ setup/teardown, reset vector,
and unrelated fatal checks remain intact.

ITF's checksum routine at 04BCh independently sums the even and odd byte lanes
into DL and DH. It hangs at 04D2h if either sum is nonzero. The patch compensates
both sums in the validated zero-filled padding at 7FECh/7FEDh, producing bytes
2Dh/E6h. This preserves the self-check without disabling it. No checksum bytes
in the reset-vector metadata are changed.

## Verification

The standalone CTest target has a normal synthetic test that runs without ROM
files. It checks invalid arguments, unknown buffers, and an unknown synthetic
image containing every valid patch-site signature. All are rejected without
mutation, proving the full-image version guard is required.

A separate test is registered when the local user-supplied YLL02/YLL04 physical
ROM files exist. It reads them without writing and reconstructs ITF in memory.
It checks original and patched lane checksums, all changed locations, preservation
of the bounded IRQ loop and PIT setup/teardown, idempotence, and rejection of
modified original/patched revisions. Debug and Release pass both CTest cases:
**7 synthetic checks** and **29 checks** when supplied-ROM validation is included.
No full ROM image is included in the tests or source; only short instruction
signatures needed for validation are present.

### 386DX-33 interpreter runtime

Three fresh disposable VX instances used the executable copied from
`build/src/86Box.exe`, built on 2026-10-03 at 15:06:23 local time. Its SHA-256 was
`cb819228e4f9b7dcf46d7b5994e3abfe3419202df21ad31016faab509798a1ca`.
All used Intel 386DX at 33,333,333 Hz, interpreter execution, 4 MiB RAM, and no
floppy drives or storage controllers. Each ran for 25 seconds; only these fresh
processes and their private configuration directories were used.

| Configured wait-state value | Bypass | Observed result at 25 seconds |
| --- | --- | --- |
| 1 (UI: zero waits) | Off | Red `TIMER ERROR`; CPU loops at F800:18EB |
| 6 (UI: five waits) | Off | Red `TIMER ERROR`; CPU loops at F800:18EB |
| 1 (UI: zero waits) | On | POST completed into ROM BASIC's `How many files(0–15)?` prompt; CPU at F320:035C |

The higher-wait run does **not** establish that five effective waits fail:
read-only process snapshots show configured values 1 and 6, but both have
`cpu_prefetch_cycles=2` and `cpu_cycles_read=2`. A separate fresh five-second
probe confirms both cache-enable flags are zero while the effective timing
remains two cycles. The CPU initialization selects external-cache timing for
386DX, while the global reset later clears its cache flags; this ordering is a
candidate explanation for the stale timing. No wait-state timing change is part
of this firmware-bypass work. The observed CPU clock stayed 33.333333 MHz.

Configurations, copied executables, read-only snapshots, and rendered guest
frames are in `tmp/vx-timer-runtime/wait1-off`, `wait6-off`, `wait1-on`, and
`wait6-cache-probe`. `frame-25s.png` records each primary outcome.

### 40 MHz 386DX and 486 interpreter runtime

Fresh instances with the same executable, 4 MiB RAM, no drives, and interpreter
execution verified the following results. For the original-ITF counterfactual,
a private diagnostic executable replaced only the patch helper with one that
returns success without changing the buffer. This allowed the selected CPU
while retaining the exact original firmware. That executable is not distributed.

| Actual CPU | Loaded ITF | Result |
| --- | --- | --- |
| Intel 486DX, 33,333,333 Hz | Original | `TIMER ERROR`, loops at F800:18EB |
| Intel 486DX, 33,333,333 Hz | Patched | Visible ROM BASIC prompt by 5 seconds |
| Intel 386DX, 40 MHz | Original | `TIMER ERROR`, loops at F800:18EB |
| Intel 386DX, 40 MHz | Patched | Visible ROM BASIC prompt by 5 seconds |
| AMD 386DX, 40 MHz | Patched | Visible ROM BASIC prompt by 5 seconds |

Read-only snapshots verified each actual CPU type, speed, interpreter mode, and
loaded ITF fingerprint. The original cases fail the first counter test: the
BIOS programs FFh and reads FFh back before a decrement, leaving AH equal to AL
at 08FEh. SP=090Bh identifies the selected fatal exit. The 486 failure reproduced
here is therefore the same timer check, rather than an initial reset fetch or
unsupported instruction. Evidence is in `tmp/vx486-runtime/verification.txt` and
its named case directories.

The final production rebuild (SHA-256
`ae08cb42ecfed87df8480828c9adde6c1c105d1c430fe521b9fb328be0651898`)
was separately checked with Intel 486DX-33 interpreter and the option enabled.
It reached BASIC by five seconds and returned to BASIC after a hard reset.
The loaded patch persisted across reset. Frames and state snapshots are in
`tmp/vx486-runtime/final486-reset`.

All four original physical ROM files retained their SHA-256 hashes after these
tests. This establishes POST and ROM BASIC entrance for the specified CPUs. It
does not establish full OS/game compatibility, correct accelerated timing, boot
success on every exposed CPU, or support for other firmware revisions.
