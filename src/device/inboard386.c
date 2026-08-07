/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Intel Inboard 386/PC accelerator card - an
 *          8088-socket-compatible 386SX/486-class CPU upgrade for the
 *          IBM PC/XT (5150/5160), with software-controlled memory wait
 *          states, A20 gating (no 8042 present on a stock XT), and BIOS
 *          ROM shadow/cache control.
 *
 *          Ported to 86Box from UniPCemu's hardware/inboard.c, itself
 *          derived from disassembly of the real INBRDPC.SYS driver
 *          (Intel's own driver for the card).
 *
 * Authors: SuperFury, <https://github.com/superfury> (original
 *              UniPCemu implementation, 2019-2022)
 *          Mike1978uk, <mike.w.lycett@gmail.com> (86Box port, 2026)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/machine.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/plat_unused.h>

/* Crystal choices for the real card: stock Inboard 386/PC shipped with
   either a 16MHz or 20MHz crystal; this project's actual board has been
   modded to 40MHz (from a stock 32MHz part) - see project notes. This
   only affects the display/documentation value here; the wait-state
   levels below are the same 4 software-selectable levels regardless of
   the physical crystal, matching the real INBRDPC.SYS-observed protocol. */
#define INBOARD_CRYSTAL_16MHZ 0
#define INBOARD_CRYSTAL_20MHZ 1
#define INBOARD_CRYSTAL_CUSTOM 2

typedef struct inboard386_t {
    uint8_t is_xt;             /* XT-style (port 0xA0/0x60) vs AT-style (port 0x674) card */
    uint8_t speed;             /* Raw value written to port 0x670 (bits 4-1 = waitstates/2, bit 0 = cache enable) */
    uint8_t port_a0;           /* XT-only port 0xA0 shadow (memory-size/remap related) */
    uint8_t port_674;          /* AT-only port 0x674 shadow (bit 7 = upper speed states) */
    uint8_t a20_enabled;       /* Current A20 gate state, XT-only path (no real 8042 on a stock XT) */
    uint8_t rom_shadow_enabled;/* BIOS ROM shadowed into fast RAM vs read directly from slow ROM */

    mem_mapping_t bios_shadow_mapping;
    mem_mapping_t bios_shadow_alias_mapping; /* High alias at (0xF0000 + mem_size*1KB) - see
                                           inboard386_init() for why this exists: real hardware's
                                           shadow-RAM self-patch writes through this alias, not
                                           through 0xF0000 directly. */
    uint8_t      *bios_shadow_ram;     /* Writable shadow buffer - ALWAYS receives writes,
                                           regardless of rom_shadow_enabled (matches real
                                           hardware / UniPCemu's mapmemoryROM(): reads are
                                           steered by the enable bit, writes are not - see
                                           inboard386_bios_shadow_read/write below). */
    uint8_t      *bios_rom_snapshot;   /* Read-only snapshot of the real ROM, captured once
                                           at init - serves reads while shadowing is disabled,
                                           standing in for "pass-through to the real ROM chip"
                                           since our mapping stays enabled unconditionally now. */
} inboard386_t;

#ifdef USE_WAITSTATES
static const uint8_t inboard_xt_waitstates[16] = {
    /* Indexed by (speed & 0x1E) >> 1, i.e. the 4-waitstate-step field.
       Confirmed from real hardware / INBRDPC.SYS disassembly (see
       UniPCemu hardware/inboard.c and WIN95_PLAN.md project notes):
       0=Level1(30WS) .. 0xF=Level16(0WS) in steps of 2. */
    30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0
};
#endif

static const uint8_t inboard_at_waitstates[4] = {
    /* AT variant: measured against real BIOS ROM DMA test validation,
       roughly a x0.5-ish relationship to the XT table except the final
       (zero-waitstate) entry - see UniPCemu inboard.c comment history. */
    94, 47, 24, 0
};

static void
inboard386_apply_waitstates(inboard386_t *dev)
{
    uint8_t value;

    if (dev->is_xt) {
        /* Confirmed values: 0=Level1(30WS) steps of 2 up to 1Eh=0WS. */
        value = 30 - (dev->speed & 0x1E);
    } else {
        /* AT: 2 bits of level selection (bit0 of speed, bit7 of 674) -> 4-entry table. */
        uint8_t idx = ((dev->speed & 1) << 1) | ((dev->port_674 >> 7) & 1);
        value       = inboard_at_waitstates[idx & 3];
    }

    /* This is 86Box's existing, already-wired-up global CPU wait-state
       knob - the same one sanyo.c/scat.c/sl82c461.c use for their own
       chipset-specific memory timing. Covers memory bus cycles only -
       see inboard386_apply_io_waitstates() below for the I/O-port side,
       which real ISA-bus hardware paces independently of CPU speed. */
    cpu_waitstates = value ? (value + 1) : 0;

    /* CONFIRMED DEAD FOR OUR REAL TARGET CPU (2026-07-24): cpu_waitstates above is 86Box's
       existing knob, but cpu_update_waitstates() (cpu.c ~line 4523) only consults it when
       "cpu_s->cpu_type >= CPU_286 && cpu_s->cpu_type <= CPU_386DX" - and CPU_IBM486BL sits
       AFTER CPU_386DX in the enum (cpu.h ~line 47-48), so this assignment is silently ignored
       for our real target CPU regardless of value (already independently confirmed by the
       200-waitstate extreme test earlier this session having zero effect). Also: even where
       this mechanism DOES apply, it was never scaled by busspeed - a genuine "wait state" on
       real hardware is one extra cycle of the FIXED ~4.77MHz XT bus clock (confirmed by
       PC-MOS's own INBRDPC.SYS-equivalent driver source, MINBRDPC.ASM's intrboot routine,
       which forces exactly 30 wait states + ram_enable=0 before every reboot's BIOS POST),
       not one extra cycle of whatever speed the accelerator's own CPU happens to run at - so
       even the existing mechanism's un-scaled raw count was never going to be enough on its
       own for a CPU fast enough to reach this gated-out territory in the first place.
       inboard386_apply_mem_timing() below overrides the actual consumed variables directly,
       bypassing cpu_waitstates/cpu_update_waitstates() entirely, and applies real
       busspeed-ratio scaling - the fix this project actually needs for general memory access,
       parallel to what inboard386_apply_rom_prefetch() already does for ROM-flagged fetches
       specifically. */
}

/* Real, previously-un-scaled general memory timing override - parallel to
   inboard386_apply_rom_prefetch() below but for ALL memory access, not just
   MEM_MAPPING_ROM_WS-flagged ROM fetches. Per MINBRDPC.ASM's ram_enable flag (default 1,
   forced to 0 by intrboot before every BIOS POST): while the Inboard hasn't taken over with
   its own fast 32-bit RAM (rom_shadow_enabled off, matching ram_enable=0), ALL memory access
   - not just ROM - still goes through the original, real, XT-bus-speed-limited system board,
   so general RAM should be exactly as bus-paced as ROM is during that phase. Bypasses
   cpu_waitstates/cpu_update_waitstates() entirely (confirmed dead for CPU_IBM486BL above) and
   sets the actual consumed variables (cpu.c ~line 4515-4536) directly, scaled by the same
   cpu_busspeed/4772728 ratio used everywhere else in this file, using the CPU table's own
   mem_read_cycles/mem_write_cycles as the baseline (rather than a fresh guessed constant) so
   the scaling is relative to whatever 86Box already assumes for this specific chip. */
static void
inboard386_apply_mem_timing(const inboard386_t *dev)
{
    double ratio;
    int    native_read_baseline;
    int    native_write_baseline;
    int    read_baseline;
    int    write_baseline;
    int    read_cycles;
    int    write_cycles;

    native_read_baseline  = (cpu_s != NULL) ? cpu_s->mem_read_cycles : 1;
    native_write_baseline = (cpu_s != NULL) ? cpu_s->mem_write_cycles : 1;
    if (native_read_baseline < 1)
        native_read_baseline = 1;
    if (native_write_baseline < 1)
        native_write_baseline = 1;

    if (dev->rom_shadow_enabled || (cpu_busspeed <= 4772728.0)) {
        /* Shadowed into the Inboard's own fast RAM (or genuinely at/below XT bus speed
           already) - let 86Box's own CPU-table defaults stand, unscaled. */
        cpu_prefetch_cycles     = native_read_baseline;
        cpu_mem_prefetch_cycles = native_read_baseline;
        cpu_cycles_read         = native_read_baseline;
        cpu_cycles_read_l       = (cpu_16bitbus ? 2 : 1) * native_read_baseline;
        cpu_cycles_write        = native_write_baseline;
        cpu_cycles_write_l      = (cpu_16bitbus ? 2 : 1) * native_write_baseline;
        return;
    }

    /* 2026-07-26: baseline for THIS (unaccelerated/scaled) branch changed from
       cpu_s->mem_read_cycles/mem_write_cycles (the CPU table's own NATIVE-speed memory-wait
       figures, e.g. 12-18 for the 486BL3 grades) to a small, near-zero reference matching the
       genuine 8088 @ 4.77MHz CPU table entry itself (mem_read_cycles=0/mem_write_cycles=0 -
       see cpu_table.c). Using the 486BL3's own native baseline was the wrong reference frame
       here: those figures represent normal wait-state cost for a fast 486-class chip running
       at its OWN speed, not "how many extra cycles does a genuine 4.77MHz 8088 need" (answer:
       none beyond the core's own built-in timing, hence 0). Multiplying the wrong (much
       larger) baseline by the ~17.5x busspeed ratio (at 83.5MHz) compounded the error,
       producing wait-state counts an order of magnitude too high. Confirmed via a real-
       hardware cross-check: the conventional-memory POST count (both real and emulated boot
       from the same architectural point, before INBRDPC.SYS has loaded and while the Inboard
       is still in its default/unaccelerated state, so this is a clean, isolated timing
       comparison) measured 23.87s on the real 5160 (user-timed from video) versus 209s in the
       emulator at this project's current cpu_speed - an 8.76x slowdown, entirely within this
       one uniform phase (confirmed via vram_dump.txt timestamp analysis: zero gaps, one
       snapshot per second throughout, ruling out a stall and pointing to a genuine per-access
       timing miscalibration). Back-solving from the measured ratio implied a baseline near
       1.6, well outside a 12-18 native-486 reference and much closer to the real 8088's own 0
       - using a small fixed constant (2) instead of 0 outright to stay conservative until a
       from-scratch re-timing confirms exact real-hardware parity. Deliberately NOT applied to
       the shadowed/native branch above - that path represents the CPU running at its own,
       correct, native 486 speed post-acceleration, where the original native baseline is the
       right reference frame. */
    read_baseline  = 2;
    write_baseline = 2;

    ratio        = cpu_busspeed / 4772728.0;
    read_cycles  = (int) (((double) read_baseline * ratio) + 0.5);
    write_cycles = (int) (((double) write_baseline * ratio) + 0.5);
    if (read_cycles < read_baseline)
        read_cycles = read_baseline;
    if (write_cycles < write_baseline)
        write_cycles = write_baseline;

    cpu_prefetch_cycles     = read_cycles;
    cpu_mem_prefetch_cycles = read_cycles;
    cpu_cycles_read         = read_cycles;
    cpu_cycles_read_l       = (cpu_16bitbus ? 2 : 1) * read_cycles;
    cpu_cycles_write        = write_cycles;
    cpu_cycles_write_l      = (cpu_16bitbus ? 2 : 1) * write_cycles;
}

/* Real ISA-bus I/O port cycles are paced by the bus's own fixed clock,
   independent of an accelerator card's CPU speed - that's an electrical
   property of the bus itself on real hardware, not something software
   normally has to configure. Neither this device's memory-side wait-state
   register nor 86Box's core CLOCK_CYCLES() model that on their own (see
   INBOARD_86BOX_PORT_PLAN.md: confirmed by testing up to 200 wait-states,
   with zero effect, since cpu_waitstates never touches x86_ops_io.h's
   CLOCK_CYCLES() calls at all). This scales io_waitstates (added to every
   IN/OUT's cycle count in x86_ops_io.h) so that, regardless of how fast
   the Inboard's CPU is actually configured to run, an I/O port access
   still takes roughly the same real wall-clock time as it would on a
   genuine 4.77MHz 8088 - the baseline ~11-cycle cost in x86_ops_io.h is
   scaled up proportionally to the CPU/bus speed ratio. */
static void
inboard386_apply_io_waitstates(void)
{
    double ratio;
    int    extra;

    if (cpu_busspeed <= 4772728.0) {
        io_waitstates = 0; /* At or below genuine XT bus speed - no compensation needed. */
        return;
    }

    ratio = cpu_busspeed / 4772728.0;
    extra = (int) ((11.0 * ratio) - 11.0 + 0.5); /* Round to nearest. */
    if (extra < 0)
        extra = 0;
    io_waitstates = extra;
}

/* Found 2026-07-24 (very late), via live-traced instruction tracing of the ACTUAL failing
   routine (a PIC IMR self-test at F000:E32A, not the PIT self-test this project spent most of
   the night chasing): the routine's final step, after the IMR readback checks pass, is a pure
   ~65536-iteration LOOP-only delay (F000:E349, "sub cx,cx" then "loop $"), with NO memory or
   I/O access at all - so neither io_waitstates, cpu_rom_prefetch_cycles, mem_timing, nor
   isa_cycles touch it. LOOP's own cost in x86_ops_jump.h is a flat 11 cycles (7 if is486,
   which CPU_IBM486BL is not per the is486 gating found earlier tonight) - completely unscaled
   by CPU speed. At our 486BL3/75 test's 25MHz busspeed, 65536 iterations costs ~28.8ms; at
   genuine 4.77MHz (what the ROM's delay was presumably calibrated against) the same loop
   would cost ~151ms - almost exactly our 5.24x busspeed ratio, confirmed by direct
   calculation. This scales LOOP's cost the same way as io_waitstates, so a pure
   register/branch delay loop takes roughly the same real wall-clock time regardless of
   configured CPU speed, matching what all the other mechanisms in this file already do for
   memory/IO/ROM access. */
static void
inboard386_apply_reg_op_waitstates(void)
{
    double ratio;
    int    extra;

    if (cpu_busspeed <= 4772728.0) {
        reg_op_waitstates = 0; /* At or below genuine XT bus speed - no compensation needed. */
        return;
    }

    ratio = cpu_busspeed / 4772728.0;
    extra = (int) ((11.0 * ratio) - 11.0 + 0.5); /* Round to nearest. */
    if (extra < 0)
        extra = 0;
    reg_op_waitstates = extra;
}

/* CONFIRMED INERT FOR CPU_IBM486BL SPECIFICALLY (2026-07-24) - kept in place because it's still real
   and correct for any CPU/config where the underlying mechanism actually applies, but do not expect it
   to affect this project's real target CPU. Original reasoning: 86Box's fast PIT device (pit_fast.c)
   charges an extra "cycles -= ISA_CYCLES(8)" (ISA_CYCLES(x) = x * isa_cycles) in its port handlers,
   decoupled from the generic opcode-level io_waitstates/cpu_waitstates path entirely - a second,
   independent lever. But pit_fast.c is only selected when machine_common_init()'s
   cpu_requires_fast_pit is true, which requires is486 (cpu.c ~line 547: is486 = cpu_type >=
   CPU_RAPIDCAD). CPU_IBM486BL sits BEFORE CPU_RAPIDCAD in the enum (cpu.h ~line 46-49), so despite
   the "486" in its name, 86Box classifies it as NOT is486 - meaning our real target CPU uses the
   classic pit.c instead, which (confirmed by grep - no ISA_CYCLES or explicit "cycles -=" anywhere in
   it) charges zero device-specific cost at all, relying purely on the generic IN/OUT opcode cost that
   io_waitstates already targets. Verified directly: added a temporary diagnostic to pitf_write() -
   zero hits across a full cold-boot cycle with cpu_family=ibm486bl3, confirming this code path is
   never reached for our actual test configuration. This is the third mechanism this project found
   that is real and mechanically sound, but turns out to be a dead path for the specific CPU type this
   project actually needs (after the earlier cpu_rom_prefetch_cycles/i386SX finding, which was later
   corrected once the *right* CPU was tested - this one has been checked against the right CPU from
   the start and is still inert). See INBOARD_86BOX_PORT_PLAN.md for the full test log. */
static void
inboard386_apply_isa_speed(void)
{
    /* 2026-07-26: CONFIRMED DOMINANT CONTRIBUTOR to a real, precisely-measured emulator-vs-
       real-hardware slowdown, found via bisection against a real timed benchmark, not
       guesswork. The original version of this function (see git history / plan doc for the
       exact prior code) scaled cpu_s->atclk_div - ALREADY a correct, non-zero reference for
       this CPU's stock (PS/2 Model 80-oriented) ISA timing at its own speed grade (confirmed
       to give a consistent 2.88us per ISA_CYCLES(8) across the 75/100MHz grades, pre-scaling)
       - by the SAME ~17.5x busspeed ratio used elsewhere in this file, compounding an already-
       correct baseline into a value roughly an order of magnitude too high. ISA_CYCLES() is
       used pervasively (DMA, keyboard controller, PIC, PIT, and evidently per-character text-
       mode video output too, given how directly this affected the conventional-memory POST
       count's own screen-update-driven timing).

       Calibrated empirically against a real-hardware-measured reference: the conventional-
       memory POST count (both real and emulated boot from the same architectural point,
       before INBRDPC.SYS has loaded and while the Inboard is still in its default/
       unaccelerated state - a clean, isolated timing comparison) takes 23.87s on the real
       5160 (user-timed from video, this project's cpu_speed=83.5MHz grade). Bisected against
       this target: isa_cycles=175 (original formula's output) -> ~209s; =20 -> ~71s; =10 ->
       ~71s (plateaus with 20, some other factor caps it in that range); =4 -> ~51s; =2 -> ~44s;
       =1 -> ~1.5s (drastic overshoot the other way). The isa_cycles=2 value below is the best
       point found this session - roughly 1.85x the real-hardware target, a ~4.75x improvement
       over the original ~8.75x-too-slow baseline, but NOT yet exact. The 1->2 and 2->4 deltas
       don't fit a single smooth curve, suggesting either real run-to-run timing variance in
       this manual, one-sample-per-rebuild measurement method, or a genuine secondary
       contributor still uncounted for. Next session: consider an in-emulator instruction/
       cycle counter (the user's own suggestion) for a more precise, repeatable calibration
       than manually timing full boot cycles; this value is an empirical calibration for THIS
       project's specific 83.5MHz speed grade specifically, not a theoretically-derived
       constant, and should be re-checked if the speed grade ever changes. */
    isa_cycles = 2;
}

/* The real, previously-unaccounted-for mechanism: per Intel's own Appendix D
   (APPENDD.DOC/DOX1.TXT, NOCACHE parameter), "By default, the system BIOS is
   executed from the Inboard 386/PC 32-bit RAM" - but that shadow-copy is a
   software-enabled feature; INBRDPC.SYS hasn't loaded yet during cold POST,
   so the BIOS necessarily executes from the real, slow ROM chip until then.
   86Box's own CPU core (cpu.c cpu_set(), ~line 589) already has a distinct
   cpu_rom_prefetch_cycles variable, separate from cpu_waitstates/
   cpu_prefetch_cycles, that getpccache() (mem.c ~line 655) swaps in whenever
   code execution is fetching from a MEM_MAPPING_ROM_WS-flagged region - but
   86Box computes it from the raw configured CPU clock (rspeed/1000000,
   e.g. 16 at 16MHz) with no way for a card like ours to say "I'm still
   running uncached/slow regardless of the configured clock." That's why
   every previous cpu_waitstates/io_waitstates experiment had no effect on
   the self-test loop: none of them touch this variable at all.
   This overrides it directly: while rom_shadow_enabled is off (the correct
   default at reset, matching real cold-boot state), scale it the same way
   as io_waitstates so a ROM fetch costs roughly the same real wall-clock
   time as it would on a genuine 4.77MHz 8088, regardless of configured
   clock. Once rom_shadow_enabled flips on, bios_shadow_mapping (RAM,
   not ROM_WS-flagged) is what actually services reads instead, so this
   value stops mattering for BIOS fetches at that point anyway - but we
   still restore it to 86Box's own default so nothing else regresses.

   TESTED AGAINST BOTH CPU PATHS (2026-07-24): first tested against the
   i386SX convenience-test CPU - getpccache() never fired at all, because
   cpu_set() (cpu.c ~line 1863-1884) routes plain 286/386SX/386DX-class
   CPUs through a *different* interpreter core (exec386_2386) that never
   calls getbyte()/fastreadb()/getpccache() from 386_common.h at all. The
   real target CPU, CPU_IBM486BL, is explicitly routed through exec386
   instead (the path that DOES use getpccache() and this variable) -
   confirmed directly via a temporary fastreadb() diagnostic showing real
   hits once cpu_family was switched to ibm486bl3.
   Re-tested properly against ibm486bl3 with a 3-point bisection (8x
   baseline / 60x / 500x): the calculated and 60x values leave the
   self-test's 1801->640 KB OK->101 sequence completely unchanged (just a
   visibly slower memory count); 500x skips straight to 101 with no 1801
   or memory count at all - the same three-shape response io_waitstates
   showed independently. Also tested combined with io_waitstates at its
   own calculated value simultaneously, both active, real CPU: still no
   change. This mechanism is real, reachable, and correctly wired - it
   just isn't sufficient at any tested magnitude to make this self-test
   pass. Left in place (harmless, conceptually correct, matches Intel's
   documented NOCACHE behavior) but do not assume it fixes the 101 POST
   error - it doesn't, per direct, now-thorough testing against the
   correct CPU. See INBOARD_86BOX_PORT_PLAN.md. */
static void
inboard386_apply_rom_prefetch(const inboard386_t *dev)
{
    double ratio;
    int    extra;

    if (dev->rom_shadow_enabled) {
        /* Shadowed into fast RAM - let 86Box's own default stand. */
        cpu_rom_prefetch_cycles = (cpu_busspeed > 8000000.0) ? (int) (cpu_busspeed / 1000000.0) : cpu_mem_prefetch_cycles;
        return;
    }

    if (cpu_busspeed <= 4772728.0) {
        cpu_rom_prefetch_cycles = (int) (cpu_busspeed / 1000000.0);
        if (cpu_rom_prefetch_cycles < 1)
            cpu_rom_prefetch_cycles = 1;
        return;
    }

    ratio = cpu_busspeed / 4772728.0;
    extra = (int) ((8.0 * ratio) + 0.5); /* Round to nearest; ~8 cycles baseline at 4.77MHz. */
    if (extra < 1)
        extra = 1;
    cpu_rom_prefetch_cycles = extra;
}

/* The mapping itself now stays permanently enabled (see inboard386_init()) - this only
   exists so callers that flip rom_shadow_enabled don't need to know that detail; kept as
   a no-op hook in case a future revision needs to do more here. Read steering happens in
   inboard386_bios_shadow_read() below, keyed directly off dev->rom_shadow_enabled. */
static void
inboard386_apply_rom_shadow(UNUSED(inboard386_t *dev))
{
}

/* Real hardware (confirmed against UniPCemu's inboard.c, the reference implementation
   this whole port is based on - mapmemoryROM()'s own comment literally says "Write RAM,
   Read=PCI/ROM") steers READS based on the shadow-enable bit, but WRITES always land in
   the underlying RAM regardless of that bit. This matters because INBRDPC.SYS's own
   init sequence needs to be able to copy the real ROM content into the shadow buffer via
   ordinary writes *before* it flips shadowing on for reads - if writes were gated the
   same way reads are (our earlier, wrong implementation - toggling one combined
   mem_mapping_enable/disable for both directions), any software-driven copy-in the
   driver performs while shadowing is still "off" would be silently discarded, and only
   whatever this device pre-populated at init time would ever be visible. That earlier
   version's fix (pre-populating bios_shadow_ram with a real ROM copy at init) was
   necessary but insufficient - the shadow RAM self-test still failed, because it's not
   simply asking "does a read return ROM content" - see the read/write split below. */
static uint8_t
inboard386_bios_shadow_read(uint32_t addr, void *priv)
{
    const inboard386_t *dev = (inboard386_t *) priv;
    if (dev->rom_shadow_enabled)
        return dev->bios_shadow_ram[addr & 0xffff];
    return dev->bios_rom_snapshot[addr & 0xffff];
}

static void
inboard386_bios_shadow_write(uint32_t addr, uint8_t val, void *priv)
{
    inboard386_t *dev = (inboard386_t *) priv;
    dev->bios_shadow_ram[addr & 0xffff] = val;
}

/* Port 0x60 - XT-only. A stock XT has no 8042, so the Inboard card
   itself provides A20 gating via this port (commands 0xDD disable /
   0xDF enable), unlike an AT where the keyboard controller does it. */
static void
inboard386_write_60(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    inboard386_t *dev = (inboard386_t *) priv;

    if (!dev->is_xt)
        return; /* AT has a real 8042 for this - not our port. */

    switch (val) {
        case 0xDD: /* Disable A20 */
            dev->a20_enabled = 0;
            mem_a20_alt      = 0;
            mem_a20_recalc();
            break;
        case 0xDF: /* Enable A20 */
            dev->a20_enabled = 1;
            mem_a20_alt      = 1;
            mem_a20_recalc();
            break;
        default:
            break; /* Unknown command - ignored, matching stock behavior. */
    }
}

/* Port 0x64 - a stock XT has no 8042, so nothing should be electrically present
   here at all. Left unclaimed, 86Box's default open-bus read is 0xFF (io.c
   inb()) - but empirical measurement on the real 5160+Inboard hardware this
   project models (via COMrade's io_in, see memory win95-on-5160 /
   WIN95_PLAN.md 2026-07-23 "baseline io_in reads") found port 0x64
   consistently reads back 0x00, not floating/0xFF. This matters in practice:
   MS-DOS HIMEM.SYS's default AT-method A20 auto-detect polls port 0x64 bit 1
   (input-buffer-full) in a bounded wait loop before each command byte - with
   the real 0x00 value that bit is already clear, so the wait exits
   immediately and (per this project's own prior disassembly of HIMEM's A20
   routine) execution falls through to writing the correct literal 0xDF/0xDD
   to port 0x60 "by a fortunate accident of the flag logic," matching what
   Inboard hardware actually requires. Without this, HIMEM's default
   auto-detect fails differently in emulation (0xFF keeps the loop's condition
   satisfied every iteration in a way that doesn't reach the same fallthrough)
   even though the real machine's plain, switchless `HIMEM.SYS` line works. */
static uint8_t
inboard386_read_64(UNUSED(uint16_t port), void *priv)
{
    return 0x00;
}

/* Port 0xA0 - XT-only (an AT has a PIC at this address instead). */
static void
inboard386_write_a0(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    inboard386_t *dev = (inboard386_t *) priv;

    if (!dev->is_xt)
        return;

    dev->port_a0 = val;
    inboard386_apply_waitstates(dev);
}

/* Port 0xA1 - fully unclaimed on this machine (no pic2_init(), no other
   device registers it), so reads float to 86Box's generic 0xFF and writes
   vanish. Claimed here only so behavior is explicit rather than incidental
   (return 0xFF on read, do nothing on write - identical to leaving it
   unclaimed). */
static uint8_t
inboard386_read_a1(UNUSED(uint16_t port), UNUSED(void *priv))
{
    return 0xFF;
}

static void
inboard386_write_a1(UNUSED(uint16_t port), UNUSED(uint8_t val), UNUSED(void *priv))
{
}

/* Port 0x670 - both XT and AT variants use this for the speed/cache
   setting (bits 4-1 = waitstates/2, bit 0 = cache/shadow enable). */
static void
inboard386_write_670(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    inboard386_t *dev = (inboard386_t *) priv;

    dev->speed              = val & 0x1F;
    dev->rom_shadow_enabled = dev->speed & 1;

    inboard386_apply_rom_shadow(dev);
    inboard386_apply_waitstates(dev);
    inboard386_apply_rom_prefetch(dev);
    inboard386_apply_mem_timing(dev);
}

/* Port 0x674 - AT variant only, additional speed-level bit. */
static void
inboard386_write_674(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    inboard386_t *dev = (inboard386_t *) priv;

    if (dev->is_xt)
        return;

    dev->port_674 = val;
    inboard386_apply_waitstates(dev);
}

static void
inboard386_reset(void *priv)
{
    inboard386_t *dev = (inboard386_t *) priv;

    dev->speed              = 0; /* Default: slowest (30WS XT / 94WS AT), matching real cold-boot state. */
    dev->port_674           = 0;
    dev->port_a0            = 0x80;
    dev->a20_enabled        = 0;
    dev->rom_shadow_enabled = 0;

    /* 2026-07-26: re-assert both shadow mappings on every reset, not just once at device
       construction. Live-traced discovery: the LOW mapping (0xF0000) only kept working across
       hard resets by accident - machine_ibmxt_inboard386_init() re-runs bios_load_linear() on
       every reset, and that call's own mem_mapping recalc happens to re-walk and re-include our
       still-"enabled" mapping for that exact page range. Nothing else in the system ever
       triggers a recalc covering the high alias range (0xF0000 + mem_size*1KB), so once some
       later, broader reset pass clears its page-table entries, they were never restored -
       confirmed via a live comrade capture showing the real card's actual shadow-RAM self-patch
       write lands there, and a direct 86Box readback showing 0xFF (no mapping at all) at that
       address after the first reset. Explicit re-enable here, every reset, fixes it for good
       instead of relying on another device's incidental side effect. */
    mem_mapping_enable(&dev->bios_shadow_mapping);
    mem_mapping_enable(&dev->bios_shadow_alias_mapping);

    inboard386_apply_rom_shadow(dev);
    inboard386_apply_waitstates(dev);
    inboard386_apply_io_waitstates();
    inboard386_apply_rom_prefetch(dev);
    inboard386_apply_isa_speed();
    inboard386_apply_mem_timing(dev);
    inboard386_apply_reg_op_waitstates();
}

static void
inboard386_speed_changed(void *priv)
{
    /* CPU speed changed at runtime (user changed it via the UI, or a
       machine-level speed_changed cascade) - recompute the I/O and ROM-
       fetch wait-state compensation, and the ISA-bus-speed decoupling,
       for the new cpu_busspeed. */
    inboard386_apply_io_waitstates();
    inboard386_apply_rom_prefetch((const inboard386_t *) priv);
    inboard386_apply_isa_speed();
    inboard386_apply_mem_timing((const inboard386_t *) priv);
    inboard386_apply_reg_op_waitstates();
}

static void *
inboard386_init(const device_t *info)
{
    inboard386_t *dev = (inboard386_t *) calloc(1, sizeof(inboard386_t));

    dev->is_xt = (info->local & 1) ? 0 : 1; /* local=0 -> XT variant, local=1 -> AT variant */

    dev->bios_shadow_ram   = (uint8_t *) calloc(1, 0x10000); /* 64KB - system BIOS shadow window only */
    dev->bios_rom_snapshot = (uint8_t *) calloc(1, 0x10000);

    /* 2026-07-26: corrected to cover ONLY the system BIOS window (F0000-FFFFF, 64KB), not the
       previous C0000-FFFFF (256KB) span - confirmed against Intel's own primary-source manual
       (APPENDD.DOC, Table D-2): system BIOS caching is the DEFAULT-ON behavior this whole file
       models, but caching the video/EGA ROM is a SEPARATE, OPT-IN feature ("EGACACHE... By
       default [without it] ... "), gated behind a driver command-line parameter the user's real
       CONFIG.SYS does not use (confirmed: `DEVICE=c:\INBRDPC.SYS NODIAGS NOPAUSE`, no EGACACHE).
       UniPCemu's own reference model (`inboard_setROMcache(enabledBIOS, enabledVideo)`) matches
       this exactly - every call site passes `enabledVideo=0` unconditionally, i.e. video ROM is
       never cached in the reference implementation either. The previous, over-broad mapping had
       a second, compounding bug: `inboard386_init()` runs before the video card's own ROM gets
       mapped (`video_post_reset()` is a post-machine-init fallback, confirmed via `vid_table.c`),
       so the old snapshot loop captured pre-video-card garbage for the C0000-C7FFF portion,
       serving stale data instead of the real, live Mach8 ROM for as long as this device's
       (permanently-enabled) mapping intercepted reads there. Scoping to F0000-FFFFF only removes
       both problems at once: the video ROM is no longer touched by this device at all (matching
       documented, real, "NOCACHE unless EGACACHE" behavior), and the timing hazard doesn't apply
       since the system BIOS is already fully loaded via `bios_load_linear()`/`bios_load_aux_linear()`
       earlier in `machine_ibmxt_inboard386_init()`, well before this device's own init runs. */
    for (uint32_t a = 0; a < 0x10000; a++) {
        uint8_t b                  = mem_readb_phys(0xf0000 + a);
        dev->bios_rom_snapshot[a]  = b;
        dev->bios_shadow_ram[a]    = b;
    }

    /* "ROM BIOS shadow RAM failed" - SUPERSEDED (2026-07-26, later same day): the "ROM is
       stock, message is cosmetic/expected on real hardware too" conclusion below this comment
       was WRONG. Live comrade capture of INBRDPC.SYS's own iSTATPC.EXE status utility on the
       real 5160 shows "system BIOS: 32-bit RAM (default setting)" with NO caveat tied to the
       real CONFIG.SYS's `NODIAGS` flag (that flag only suppresses the separate "extended
       memory diagnostics" line) - i.e. the shadow-RAM self-check genuinely runs on real
       hardware too, and it silently PASSES there. A live [int1587]/[int1587post] CPU trace of
       the actual self-test (INBRDPC.SYS file offset 0xA5E9's `int 15h AH=87h` call, then the
       `repe cmpsb` at file offset 0xB11) caught the real mechanism: the driver copies its 3
       hardcoded reference bytes (file offset 0x2C6, `EA F5 0B`) via AH=87h's GDT-descriptor
       block-move to destination physical `0xF0000+0xE05B + (mem_size_KB*1024)` - in this
       machine's `mem_size=5120` config, physical `0x5FE05B`, exactly `0xFE05B + 0x500000`
       (0x500000 = 5120KB, byte for byte). It then re-reads plain `F000:E05B` (physical
       `0xFE05B`) and compares. This means the *real* card's shadow-write path is a HIGH
       linear alias at (target + total configured RAM) that its own memory controller bank-
       aliases back into the F0000-FFFFF window for real-mode reads - not a same-address
       read/write split the way this device modeled it below. This device never implemented
       that alias, so the AH=87h copy landed in ordinary (disconnected) extended RAM at
       0x5FE05B, the low-side shadow buffer was never touched by it, and the readback
       correctly-per-this-bug saw the stale ROM snapshot (0xFA) instead of the expected patched
       byte (0xEA) - a genuine, now-understood emulation gap, not stock-ROM-content cosmetic
       behavior. Fixed below by adding a second mem_mapping at the same high alias, backed by
       the SAME bios_shadow_ram buffer (the read/write callbacks already mask `addr & 0xffff`,
       so they work unmodified at either base address). */

    /* 2026-07-26: flags changed from MEM_MAPPING_INTERNAL to 0 - live comrade proof this same
       date: the real 5160, right now, through its NORMAL (shadow-enabled) read path, shows
       F000:E05B = EA F5 0B (the patched/expected value), not the raw ROM's FA B4 D5 - so this
       mapping's read side genuinely has to win over the base system BIOS ROM's own read
       mapping for this range. It never actually did: mem_mapping_access_allowed() (mem.c) only
       honors MEM_MAPPING_INTERNAL-flagged mappings on pages whose _mem_state was already marked
       ACCESS_INTERNAL by someone else, which this range apparently never was either - meaning
       reads here were being served by the base ROM's own mapping the entire time, coincidentally
       indistinguishable because bios_shadow_ram started byte-identical to the ROM. Only
       diverged, and only detectably broke, once the alias mapping below (also fixed to flags=0
       the same way) started actually writing a patched byte into bios_shadow_ram - the low
       mapping kept reading stale ROM content instead of picking up the patch. flags=0 (like the
       alias) is unconditionally allowed per the same function's logic. */
    mem_mapping_add(&dev->bios_shadow_mapping, 0xf0000, 0x10000,
                     inboard386_bios_shadow_read, NULL, NULL,
                     inboard386_bios_shadow_write, NULL, NULL,
                     dev->bios_shadow_ram, 0, dev);
    /* Stays enabled permanently now - inboard386_bios_shadow_read() itself steers
       between the ROM snapshot and the live shadow RAM based on rom_shadow_enabled,
       so writes (which always target bios_shadow_ram) are never gated off. */
    mem_mapping_enable(&dev->bios_shadow_mapping);

    /* High alias: real hardware's own shadow-write mechanism targets (0xF0000 + total
       configured RAM), bank-aliased by the card's memory controller back into 0xF0000-FFFFF
       for real-mode reads - see the live-traced evidence above. mem_size is in KB (86Box
       convention, confirmed against this project's own 86box.cfg `mem_size = 5120` matching
       the traced 0x500000-byte offset exactly).
       NOTE: flags=0, not MEM_MAPPING_INTERNAL - see the low mapping's own comment above for why
       (mem_mapping_access_allowed() in mem.c only honors MEM_MAPPING_INTERNAL-flagged mappings
       on pages some other code already marked ACCESS_INTERNAL, which this high, above-installed-
       RAM address never is). Confirmed live: with MEM_MAPPING_INTERNAL here, every write to this
       range silently landed nowhere (read back as 0xFF, 86Box's "no mapping at all" default)
       regardless of mem_mapping_enable() being re-asserted every reset. */
    mem_mapping_add(&dev->bios_shadow_alias_mapping, 0xf0000 + ((uint32_t) mem_size * 1024), 0x10000,
                     inboard386_bios_shadow_read, NULL, NULL,
                     inboard386_bios_shadow_write, NULL, NULL,
                     dev->bios_shadow_ram, 0, dev);
    mem_mapping_enable(&dev->bios_shadow_alias_mapping);

    if (dev->is_xt) {
        io_sethandler(0x0060, 1, NULL, NULL, NULL, inboard386_write_60, NULL, NULL, dev);
        io_sethandler(0x00a0, 1, NULL, NULL, NULL, inboard386_write_a0, NULL, NULL, dev);
        io_sethandler(0x00a1, 1, inboard386_read_a1, NULL, NULL, inboard386_write_a1, NULL, NULL, dev);
        io_sethandler(0x0064, 1, inboard386_read_64, NULL, NULL, NULL, NULL, NULL, dev);
    } else {
        io_sethandler(0x0674, 1, NULL, NULL, NULL, inboard386_write_674, NULL, NULL, dev);
    }
    io_sethandler(0x0670, 1, NULL, NULL, NULL, inboard386_write_670, NULL, NULL, dev);

    /* The real host motherboard's 8259 PIC is a genuine XT-class discrete chip, wired up
       unchanged regardless of what CPU sits on the accelerator card - but 86Box's PIC model
       (pic.c ~line 546) picks its IMR-update timing behavior (synchronous vs the real 8088's
       deferred/near-zero-delay timer_on_auto() path) based on is286, which reflects the CPU
       package, not the actual PIC hardware present. Force the genuine-XT-hardware timing
       regardless of CPU classification - see INBOARD_86BOX_PORT_PLAN.md for how this was found
       (live-traced the real HLT/failure address, F000:E35C, to a PIC IMR self-test, not the PIT
       self-test this project spent most of the night on). */
    if (dev->is_xt)
        pic_set_force_xt_imr_timing(1);

    /* Same is286-as-hardware-proxy mistake, this time in dma.c: dma_reset_legacy()
       sets dma_at = is286 on every hard reset (and hardresetx86() explicitly
       re-asserts it again right before device_reset_all(), per its own "TODO: Hack"
       comment) - meaning a device-level dma_set_at(0) override loses the race against
       at least one dma_reset() call site that runs after devices reset. dma_at wrongly
       true makes dma_xt8237_active() report false, which silently disables the PIT1-
       driven DMA channel-0 refresh cycle entirely (dma_xt_refresh_try_schedule() bails
       out every time) - so the DMA status register's channel-0 terminal-count bit
       (port 0x08 bit 0) never sets. Real 5160 BIOS POST reads exactly that bit right
       after the PIC IMR/IRQ0 tests (F000:E4DF, "in al,8 / and al,1 / jne ...") to
       verify DRAM refresh is alive, and hangs (cli;hlt) if it's clear - see
       INBOARD_86BOX_PORT_PLAN.md for the live-traced discovery. dma_set_force_xt()
       is a dedicated override flag that dma_reset()/dma_reset_legacy() never touch,
       so - unlike dma_set_at() - it survives every subsequent hard reset. */
    if (dev->is_xt)
        dma_set_force_xt(1);

    inboard386_reset(dev);

    return dev;
}

static void
inboard386_close(void *priv)
{
    inboard386_t *dev = (inboard386_t *) priv;

    if (dev->is_xt)
        pic_set_force_xt_imr_timing(0);

    free(dev->bios_shadow_ram);
    free(dev->bios_rom_snapshot);
    free(dev);
}

/* Crystal choice is informational/documentation only at this stage -
   both stock speed grades use the same 4-level software wait-state
   protocol above; this exists so a user can record which real card
   variant (or crystal mod) their config represents. */
static const device_config_t inboard386_xt_config[] = {
    // clang-format off
    {
        .name           = "crystal",
        .description    = "Oscillator crystal",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = INBOARD_CRYSTAL_16MHZ,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "16 MHz (stock)",  .value = INBOARD_CRYSTAL_16MHZ  },
            { .description = "20 MHz (stock)",  .value = INBOARD_CRYSTAL_20MHZ  },
            { .description = "Custom/modified", .value = INBOARD_CRYSTAL_CUSTOM },
            { .description = ""                                                }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t inboard386_xt_device = {
    .name          = "Intel Inboard 386/PC (XT)",
    .internal_name = "inboard386_xt",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = inboard386_init,
    .close         = inboard386_close,
    .reset         = inboard386_reset,
    .available     = NULL,
    .speed_changed = inboard386_speed_changed,
    .force_redraw  = NULL,
    .config        = inboard386_xt_config
};

const device_t inboard386_at_device = {
    .name          = "Intel Inboard 386/PC (AT)",
    .internal_name = "inboard386_at",
    .flags         = DEVICE_ISA,
    .local         = 1,
    .init          = inboard386_init,
    .close         = inboard386_close,
    .reset         = inboard386_reset,
    .available     = NULL,
    .speed_changed = inboard386_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
