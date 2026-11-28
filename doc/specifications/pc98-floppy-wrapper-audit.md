# PC-98 floppy motherboard wrapper audit

The 640 KB and 1 MB interfaces share one physical uPD765 controller. The
motherboard must preserve the command's IRQ/DMA route through execution and
honor the controller's actual command, data, result and RESET phases.

| Trigger | Previous behavior | Corrected behavior | Checks failing before the focused fix |
| --- | --- | --- | --- |
| SPECIFY with ND=1, then another command or board control write | Board gate overwrote the controller's programmed-I/O selection | Effective DMA also respects SPECIFY ND | 4 |
| Read a PIO data byte or probe an execution FIFO | Route unlocked before execution ended | Only draining an actual core result releases the route | 6 |
| Write PIO data containing command-like bytes | Payload became wrapper commands or parameters | MSR B0 data is forwarded; parameter framing requires MSR 90 | 26 |
| Write a new command while the shared controller is busy | Wrapper metadata changed even though the core rejected the write | New command framing requires command-ready MSR 80 | 14 |
| Assert RESET through one interface, release through the other | Per-interface latch comparison missed the physical RESET transition | Compare the shared controller's DOR reset level | 22 |
| Reset after moving a head to cylinder 16, then RECALIBRATE | Reset zeroed PCN; a zero PCN delta left the physical head on cylinder 16 while reporting success | RECALIBRATE finds physical TRACK0 independently of PCN; SEEK retains PCN-relative stepping | 9 |

The last failure reproduced the runtime DOS 6.20 reset stall. The copied guest
cold-booted its installer, but a frontend hard reset left READ ID retrying on
physical cylinder 16 through 60 seconds. With the recalibration fix, physical
track and PCN were zero at 32 seconds, the DOS banner returned at 35 seconds,
and the installer returned at 40 seconds. This runtime test used copied VM
configuration and media; user VM files were not modified.

The motherboard suite passes **2,316 checks**. It uses the production board,
765, FDD and IMG/86F code with adapted host scheduling, IRQ observation and
DMA transport. Focused PIO framing tests invoke the production media callbacks
directly before deferred command dispatch; they isolate register framing and
do not establish a complete media transfer. Separate mounted-image tests read
sector zero after reset and physical RECALIBRATE without a corrective test
seek. The integrated production 8237/image suite passes **58 checks**; its
physical-track reset regression failed 21 checks before the recalibration fix.
Failure counts above are from successive focused pre-fix runs, not a combined
old-source run.

READY-low lifecycle tests dispatch public commands and actual controller
timers without the focused PIO callback injection. Their empty/absent-drive
coverage therefore independently exercises the core READY check. Eject and
controller-lifetime changes are described in
[the lifecycle audit](pc98-floppy-lifecycle-audit.md).

External mechanisms remain incomplete. NEC's hardware manual describes two
internal dual-mode mechanisms plus two external 1 MB mechanisms on the VX;
BX/BA external mechanisms instead require the PC-9801-87 adapter. The current
wrapper's motor reconciliation and density polarity handle only physical
units zero and one. Configuring units two or three may yield seek/equipment
identification, but does not provide reliable media access. This audit does
not claim working external-drive support. [NEC / ASCII, PC-9800 Technical Data
Book, Hardware (1993), printed pp.213–214 and 219](https://vtda.org/docs/computing/NEC/PC-9800TechnicalDataBookHARDWARE%2BOCR_1993.pdf).

Later-model motor control and inactive-interface decode also need a separate
hardware audit. The BX4 profile remains experimental; this VX reset work is
not validation of every later-model floppy control register.
