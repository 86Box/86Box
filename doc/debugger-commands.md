# Qt debugger commands

Open **Memory viewer** and use the **Debugger commands** pane at the bottom.
Drag the splitter to resize it; **Commands** or **Ctrl+D** toggles it. Enter
executes a command; Up/Down recalls the last 200 commands and restores your
unfinished input. Output retains the last 4096 lines.

The console uses the viewer's debugger attachment, memory selection, address
space, breakpoints, and refresh timer. Closing the viewer detaches the debugger.
Command input is kept out of the guest keyboard stream. Stop notifications
appear in the transcript, including stops caused by toolbar actions.

Numbers are hexadecimal, optionally prefixed with `0x` or suffixed with `h`.
Only instruction counts for `step` are decimal. Addresses use the same parser
as the viewer: numbers, register names, or `segment:offset`, including `cs:ip`.
`.` means the selected hex address. Segment:offset addresses are translated to
physical addresses when the physical view is selected. Disassembly and
breakpoints always use linear addresses; `.` is rejected for these commands
when the selection is physical.

| Command | Effect |
| --- | --- |
| `help`, `help registers` | Show syntax or available register names |
| `clear` | Clear output, retaining history |
| `refresh` | Rebuild mappings and refresh all viewer panes |
| `regs`, `r` | Print the processor snapshot |
| `r eax`, `r eax 12345678` | Read or edit a register |
| `run`, `pause`, `step [count]` | Control the same debugger as the toolbar |
| `space physical`, `space linear` | Change the hex viewer's address space |
| `goto cs:ip` | Navigate the hex viewer |
| `db [address [length]]` | Print bytes and ASCII and navigate the hex viewer; defaults to selection and 80h bytes |
| `write address byte [byte ...]` | Write RAM/video bytes and refresh the viewer |
| `dump` | Open the viewer's binary export dialog for the selection |
| `u address` | Navigate the disassembly pane and disable following CS:IP |
| `follow on`, `follow off` | Toggle disassembly following CS:IP |
| `bp address` | Set a linear execution breakpoint (idempotent) |
| `bc address`, `bc all` | Clear a breakpoint or all breakpoints |
| `bl` | List linear breakpoints, including those set by double-clicking disassembly |
| `peek port [b\|w\|d]` | Read a byte, word, or dword from an I/O port (default byte) |
| `poke port value [b\|w\|d]` | Write a byte, word, or dword to an I/O port |

Examples:

```text
pause
r eax 12345678
r ah ff
space linear
goto cs:ip
db . 40
bp cs:ip
peek 3da
poke 3c4 02
poke 3c5 0f
step 10
```

Memory reads use the viewer's side-effect-free debugger access: `--` denotes
unmapped bytes and `??` inaccessible bytes. Reads and writes are limited to
1000h bytes per command and cannot wrap the 32-bit address space. Writes require
a paused processor; inaccessible/ROM bytes are skipped and the actual written
count is reported. Every byte argument is validated before any write occurs.

I/O requires a paused processor and ports in `0000..FFFF`. **Both peek and poke
invoke real device handlers**, so reads can consume FIFO data or clear status
bits, and writes can reconfigure devices or reset the machine. Width and value
are validated before access. This differs from side-effect-free memory reads.

Register editing supports the integer/system registers displayed in the
processor pane, plus byte aliases and hidden segment-cache fields:

- General registers: EAX–EDI, AX–DI, AL/CL/DL/BL/AH/CH/DH/BH.
- Execution state: IP/EIP, FLAGS/EFLAGS, CS/SS/DS/ES/FS/GS.
- System registers: MSW, CR0/CR2/CR3/CR4, DR0–DR7 (DR4/DR5 alias DR6/DR7).
- Descriptor tables: GDTR.base/limit, IDTR.base/limit, LDTR, TR.
- Hidden caches: `.base`, `.limit`, `.access`, `.flags` on segment registers,
  LDTR and TR. `.flags` is the descriptor's high attribute byte, including D/B
  and G. `.limit` is the effective byte limit, already expanded for granularity.

Registers unavailable on the selected CPU and values too wide for the register
are rejected. Register edits require a complete instruction boundary; paused
REP/HLT partial execution and NEC 8080 emulation mode are rejected. x87/MMX
registers and MSRs are not exposed by this command interface.

Selector edits read the descriptor without device-handler accesses, guest
exceptions, or task switches. They validate the descriptor's presence and type;
they are debugger edits, not privilege-checked guest MOV/far-jump instructions.
Real-mode selector edits update the base and preserve hidden limits. Cache-field
edits are literal, allowing repair of unreal-mode state. Derived decode/stack
sizes, lazy flags, MMU caches, and DR7 breakpoint activation are updated as
appropriate. Changes to memory, registers, or I/O rebuild the viewer mappings.

For deliberate memory-mapped device access, use dbio [address [length]] and writeio address byte [byte ...] with the same syntax and bounds as db/write. Both require pause and invoke CPU-view physical memory handlers once per byte. Linear addresses use the debugger's side-effect-free page walk (no guest page faults or accessed/dirty updates). Writes report transactions issued, since devices can ignore them. Automatic viewer refreshes remain side-effect-free.

