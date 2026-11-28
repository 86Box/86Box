# PC-98 CRTV interrupt request latch

The video wrapper raised IRQ2 at every vertical sync. The generic PIC's
`picint` API latches a request on each call, so acknowledging the preceding
interrupt and sending EOI allowed a new request on every subsequent frame,
even when guest software never reset the CRT interrupt circuit.

The NEC hardware manual describes port 64h as CRT interrupt reset, with all
data bits ignored. CRTV interrupts once after power-on. Further interrupts
require another reset write. The wrapper now arms one initial request,
consumes it at vertical sync, and rearms only through the motherboard's 64h
write. Repeated writes before the next vertical sync still request one
interrupt. Machine reset clears an old request and restores the initial
one-shot state. [NEC / ASCII, PC-9800 Technical Data Book, Hardware (1993),
printed p.182, note 2](https://vtda.org/docs/computing/NEC/PC-9800TechnicalDataBookHARDWARE%2BOCR_1993.pdf).

NP2's 64h write handler sets `vsyncint`; its vertical-sync callback consumes
that flag before raising IRQ2. MAME expresses the same hardware circuit as
a level held high at vertical sync until a 64h write lowers it. Its edge PIC
does not receive another rising edge while that level stays high. This last
comparison is an inference from the two callbacks, rather than a claim that
MAME has an explicit one-shot flag. [NP2 GDC port handler](https://github.com/AZO234/NP2kai/blob/master/io/gdc.c),
[NP2 vertical-sync callback](https://github.com/AZO234/NP2kai/blob/master/pccore.c),
[MAME CRTV callbacks](https://github.com/mamedev/mame/blob/master/src/mame/nec/pc9801.cpp).

The production video-wrapper test uses the actual PIC IRR, acknowledge and
EOI code. It verifies one initial request while masked, no repeats after
acknowledge/EOI, rearming through the native reset helper, collapsing two
rearms to one request, and restoring the initial state after machine reset.
Both VX and BX4 wrappers pass **130 checks**, including existing ROM/mapping
checks. Before the gate change, the minimal repeated-frame regression failed
once for each wrapper. This establishes the interrupt protocol correction;
the Skirmish boot outcome requires separate guest-runtime validation.
