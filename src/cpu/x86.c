/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Functions common to all emulated x86 CPU's.
 *
 * Authors: Andrew Jenner, <https://www.reenigne.org>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2015-2020 Andrew Jenner.
 *          Copyright 2016-2020 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define HAVE_STDARG_H
#include <86box/automation.h>
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x87_sf.h"
#include "x87.h"
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/ppi.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>

/* The opcode of the instruction currently being executed. */
uint8_t opcode;

/* The tables to speed up the setting of the Z, N, and P cpu_state.flags. */
uint8_t  znptable8[256];
uint16_t znptable16[65536];

/* A 16-bit zero, needed because some speed-up arrays contain pointers to it. */
uint16_t zero = 0;

/* MOD and R/M stuff. */
uint16_t *mod1add[2][8];
uint32_t *mod1seg[8];
uint32_t  rmdat;

/* XT CPU multiplier. */
uint64_t xt_cpu_multi;

/* Variables for handling the non-maskable interrupts (nmi in cpu_events). */
int nmi_auto_clear = 0;

int soft_reset_pci = 0;
int soft_reset_387 = 0;

/* Is the TRAP flag on? */
int trap = 0;

/* The current effective address's segment. */
uint32_t easeg;

/* This is for the OPTI 283 special reset handling mode. */
int reset_on_hlt;
int hlt_reset_pending;

int fpu_cycles = 0;

int in_lock = 0;

#ifdef ENABLE_X86_LOG
#if 0
void dumpregs(int);
#endif

int x86_do_log = ENABLE_X86_LOG;
int indump     = 0;

static void
x86_log(const char *fmt, ...)
{
    va_list ap;

    if (x86_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}

#if 0
void
dumpregs(int force)
{
    int   c;
    char *seg_names[4] = { "ES", "CS", "SS", "DS" };

    /* Only dump when needed, and only once.. */
    if (indump || (!force && !dump_on_exit))
        return;

    x86_log("EIP=%08X CS=%04X DS=%04X ES=%04X SS=%04X FLAGS=%04X\n",
            cpu_state.pc, CS, DS, ES, SS, cpu_state.flags);
    x85_log("Old CS:EIP: %04X:%08X; %i ins\n", oldcs, cpu_state.oldpc, ins);
    for (c = 0; c < 4; c++) {
        x86_log("%s : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",
                seg_names[c], _opseg[c]->base, _opseg[c]->limit,
                _opseg[c]->access, _opseg[c]->limit_low, _opseg[c]->limit_high);
    }
    if (is386) {
        x86_log("FS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",
                seg_fs, cpu_state.seg_fs.limit, cpu_state.seg_fs.access, cpu_state.seg_fs.limit_low,
                cpu_state.seg_fs.limit_high);
        x86_log("GS : base=%06X limit=%08X access=%02X  limit_low=%08X limit_high=%08X\n",
                gs, cpu_state.seg_gs.limit, cpu_state.seg_gs.access, cpu_state.seg_gs.limit_low,
                cpu_state.seg_gs.limit_high);
        x86_log("GDT : base=%06X limit=%04X\n", gdt.base, gdt.limit);
        x86_log("LDT : base=%06X limit=%04X\n", ldt.base, ldt.limit);
        x86_log("IDT : base=%06X limit=%04X\n", idt.base, idt.limit);
        x86_log("TR  : base=%06X limit=%04X\n", tr.base, tr.limit);
        x86_log("386 in %s mode: %i-bit data, %-i-bit stack\n",
                (msw & 1) ? ((cpu_state.eflags & VM_FLAG) ? "V86" : "protected") : "real",
                (use32) ? 32 : 16, (stack32) ? 32 : 16);
        x86_log("CR0=%08X CR2=%08X CR3=%08X CR4=%08x\n", cr0, cr2, cr3, cr4);
        x86_log("EAX=%08X EBX=%08X ECX=%08X EDX=%08X\nEDI=%08X ESI=%08X EBP=%08X ESP=%08X\n",
                EAX, EBX, ECX, EDX, EDI, ESI, EBP, ESP);
    } else {
        x86_log("808x/286 in %s mode\n", (msw & 1) ? "protected" : "real");
        x86_log("AX=%04X BX=%04X CX=%04X DX=%04X DI=%04X SI=%04X BP=%04X SP=%04X\n",
                AX, BX, CX, DX, DI, SI, BP, SP);
    }
    x86_log("Entries in readlookup : %i    writelookup : %i\n", readlnum, writelnum);
    x87_dumpregs();
    indump = 0;
}
#endif
#else
#    define x86_log(fmt, ...)
#endif

/* Preparation of the various arrays needed to speed up the MOD and R/M work. */
static void
makemod1table(void)
{
    mod1add[0][0] = &BX;
    mod1add[0][1] = &BX;
    mod1add[0][2] = &BP;
    mod1add[0][3] = &BP;
    mod1add[0][4] = &SI;
    mod1add[0][5] = &DI;
    mod1add[0][6] = &BP;
    mod1add[0][7] = &BX;
    mod1add[1][0] = &SI;
    mod1add[1][1] = &DI;
    mod1add[1][2] = &SI;
    mod1add[1][3] = &DI;
    mod1add[1][4] = &zero;
    mod1add[1][5] = &zero;
    mod1add[1][6] = &zero;
    mod1add[1][7] = &zero;
    mod1seg[0]    = &ds;
    mod1seg[1]    = &ds;
    mod1seg[2]    = &ss;
    mod1seg[3]    = &ss;
    mod1seg[4]    = &ds;
    mod1seg[5]    = &ds;
    mod1seg[6]    = &ss;
    mod1seg[7]    = &ds;
}

/* Prepare the ZNP table needed to speed up the setting of the Z, N, and P cpu_state.flags. */
static void
makeznptable(void)
{
    int c;
    int d;
    int e;

    for (c = 0; c < 256; c++) {
        d = 0;
        for (e = 0; e < 8; e++) {
            if (c & (1 << e))
                d++;
        }
        if (d & 1)
            znptable8[c] = 0;
        else
            znptable8[c] = P_FLAG;
#ifdef ENABLE_X86_LOG
        if (c == 0xb1)
            x86_log("znp8 b1 = %i %02X\n", d, znptable8[c]);
#endif
        if (!c)
            znptable8[c] |= Z_FLAG;
        if (c & 0x80)
            znptable8[c] |= N_FLAG;
    }

    for (c = 0; c < 65536; c++) {
        d = 0;
        for (e = 0; e < 8; e++) {
            if (c & (1 << e))
                d++;
        }
        if (d & 1)
            znptable16[c] = 0;
        else
            znptable16[c] = P_FLAG;
#ifdef ENABLE_X86_LOG
        if (c == 0xb1)
            x86_log("znp16 b1 = %i %02X\n", d, znptable16[c]);
        if (c == 0x65b1)
            x86_log("znp16 65b1 = %i %02X\n", d, znptable16[c]);
#endif
        if (!c)
            znptable16[c] |= Z_FLAG;
        if (c & 0x8000)
            znptable16[c] |= N_FLAG;
    }
}

extern void codegen_set_rounding_mode(int mode); /* x87.h */
/* The board's processor reset: the MP table hook looks afresh (ioapic.c). */
extern void ioapic_processor_reset(void);
/* The processor's RESET or INIT pin resets its local APIC (lapic.c). */
extern void lapic_index_board_reset(uint8_t index);

/* A soft reset on a processor with an INIT pin: what INIT keeps rather
   than what RESET sets (Pentium Vol. 3, Table 16-1; 16.2: the INIT pin
   resets "without altering the FPU state"). */
static int reset_init;

/* Common reset function. */
/* 1 soft, 2 hard: a reset raised inside a recompiled block, done once the
   block has returned (x86_reset_deferred), with the sources of its soft
   resets */
static int reset_deferred;
static int reset_deferred_src;

/* A 387 beside the CPU, taken through RESET with it by the board. It comes
   out as FNINIT leaves it, but with IM clear and IE, ES and B set (80387
   PRM, 1987, 6.2, p. 6-5; Intel387 DX data sheet, 3-281): CW 037Eh, SW
   8081h, every tag empty, TOP 0. The data registers and the exception
   pointers are left alone: RESET is defined by FNINIT, which does not
   change them (80387 PRM, Table 6-1). The 387 holds ERROR# low after
   RESET, with BUSY# high (80387 PRM, Table 3.3), until FNINIT, FNCLEX,
   FNSTENV or FNSAVE (3.1.7). FERR# is taken as already up and the board's
   IGNNE# latch as set, so this error raises no IRQ 13 and holds no
   instruction back, the board side as it is with no error pending. That
   is what the CS8230 and the OPTi 82C291 do, which latch IRQ 13 only on an
   error that comes while the coprocessor is busy (C&T, the
   80386-to-coprocessor note for the CS8230, Rev 1.2, 2.1 and Figure 1.0;
   OPTi 82C291 data sheet, 4.10); for the other boards what their IRQ 13
   logic does with it is not documented, and none is added. */
static void
reset_387(void)
{
    const uint16_t cw = 0x037e;
    const uint16_t sw = 0x8081;

    if (fpu_softfloat) {
        fpu_state.cwd = cw;
        fpu_state.swd = sw;
        fpu_state.tos = 0;
        fpu_state.tag = 0xffff;
    } else {
        /* Round to nearest and 64-bit precision: neither status bit the
           blocks are keyed on */
        cpu_state.npxc = cw;
        codegen_set_rounding_mode(0);
        cpu_cur_status &= ~(CPU_STATUS_FPU_PC24 | CPU_STATUS_FPU_RC_NZ);
        cpu_state.npxs = sw;
        cpu_state.TOP  = 0;
        /* Tagged empty, the registers kept: an extended denormal keeps
           TAG_DENORMAL (x87.h) */
        for (int i = 0; i < 8; i++)
            cpu_state.tag[i] &= TAG_DENORMAL;
        cpu_state.ismmx = 0;
    }
    fpu_ferr  = 1;
    fpu_ignne = 1;
}

/* An 8087, 80C187, 287 or 287XL through RESET: as FNINIT leaves it. The
   control word x87_initial_control_word() gives (037FH, the 8087's 03FFH),
   the exception flags and TOP 0, every tag empty; the data registers and
   the exception pointers are not changed, and the condition code is left
   undefined (8087: iAPX 86,88 User's Manual, 1981, Table S-7; 80C187 data
   sheet 270640-004, Initialization; 80286 and 80287 PRM, 1987, Table 3-1,
   with the 037FH of the Intel287 XL/XLT data sheet, 2.6, "the same values
   as in an 80287 after RESET"). */
static void
reset_fpu_init(void)
{
    const uint16_t cw = x87_initial_control_word();

    if (fpu_softfloat) {
        fpu_state.cwd = cw;
        fpu_state.swd = (fpu_type == FPU_8087) ? (fpu_state.swd & 0x4700) : 0;
        fpu_state.tos = 0;
        fpu_state.tag = 0xffff;
    } else {
        /* Round to nearest and 64-bit precision: neither status bit the
           blocks are keyed on */
        cpu_state.npxc = cw;
        codegen_set_rounding_mode(0);
        cpu_cur_status &= ~(CPU_STATUS_FPU_PC24 | CPU_STATUS_FPU_RC_NZ);
        cpu_state.npxs = (fpu_type == FPU_8087) ? (cpu_state.npxs & 0x4700) : 0;
        cpu_state.TOP  = 0;
        /* As reset_387: TAG_DENORMAL stays */
        for (int i = 0; i < 8; i++)
            cpu_state.tag[i] &= TAG_DENORMAL;
    }
    cpu_state.ismmx = 0;
}

/* RESET's FPU on the Pentium, the P6 family, the K5, K6 and K6-2: control
   word 0040H, status 0, every tag "zero" -- 5555H -- with the registers,
   the MMX registers, the instruction and data pointers and the opcode zero
   (Pentium Vol. 1, Jul 1995, Table 4-2; Pentium Pro Vol. 3, Jan 1996,
   Table 8-2; the MMX registers, SDM 1999 Vol. 3, Table 8-1; AMD-K6 data
   sheet 20695E, Table 28; AMD-K6-2 data sheet 21850, Table 32; the K5's
   "identical to that of the Pentium processor", AMD-K5 TRM 18524C, Table
   5-15). */
static void
reset_fpu_zero(void)
{
    if (fpu_softfloat)
        SF_FPU_reset();
    else {
        /* As x87_set_control_word leaves it: round to nearest, and a
           precision control of 00 is the 24-bit key blocks compile under. */
        cpu_state.npxc = 0x0040;
        codegen_set_rounding_mode(0);
        cpu_cur_status |= CPU_STATUS_FPU_PC24;
        cpu_cur_status &= ~CPU_STATUS_FPU_RC_NZ;
        cpu_state.npxs = 0;
        cpu_state.TOP  = 0;
        for (int i = 0; i < 8; i++) {
            cpu_state.ST[i]   = 0.0;
            cpu_state.tag[i]  = TAG_VALID;
            cpu_state.MM[i].q = 0;
        }
    }
    x87_clear_pointers();
    cpu_state.ismmx = 0;
}

/* RESET's FPU, where its book gives it. A 387 is reset with the CPU on
   every board: its RESETIN "should be connected to the 80386 RESET pin"
   (80387 PRM, 1987, 3.1.4; Intel387 SX data sheet, 4.1.4), so it takes
   reset_387(), which raises no IRQ 13 and holds no instruction back: the
   board side as it is with no error pending, since what a board's IRQ 13
   logic does with ERROR# low after RESET is in few boards' books. The
   others -- the Intel486 family, whose books leave a power-on to the
   board's BIST request, and the CPUs whose books give no FPU state after
   RESET -- keep what they had: the soft FPU its reset state, the other FPU
   its contents. */
static void
reset_fpu_hard(void)
{
    int zero = 0;

    if ((fpu_type == FPU_INTERNAL) && (cpu_s != NULL)) {
        zero = cpu_init_keeps_state();
        switch (cpu_s->cpu_type) {
            case CPU_K5:
            case CPU_5K86:
            case CPU_K6:
            case CPU_K6_2:
            case CPU_K6_2C:
                zero = 1;
                break;
            default:
                break;
        }
    }

    if ((fpu_type == FPU_8087) || (fpu_type == FPU_80187) || (fpu_type == FPU_287) || (fpu_type == FPU_287XL))
        reset_fpu_init();
    else if (fpu_type == FPU_387)
        reset_387();
    else if (zero)
        reset_fpu_zero();
    else if (fpu_softfloat) {
        SF_FPU_reset();
        cpu_state.ismmx = 0;
    }
}

/* The CPUs whose books leave the first prefetch after RESET unmasked by
   A20M# (mem_a20_reset_line): the Intel486 data book 241731-002 covers the
   Intel486 SX, DX, DX2 and DX4 and the SL Enhanced parts; the Am486DX/DX2
   hardware reference manual covers those two only (the Am486DX4, DXL and
   Enhanced Am486 data sheets give the RESET rule without the latency). */
static int
reset_line_unmasked(void)
{
    switch (cpu_s->cpu_type) {
        case CPU_i486SX:
        case CPU_i486DX:
        case CPU_i486SX_SLENH:
        case CPU_i486DX_SLENH:
            return 1;
        case CPU_Am486DX:
            return !strcmp(cpu_f->internal_name, "am486dx") || !strcmp(cpu_f->internal_name, "am486dx2");
        default:
            return 0;
    }
}

static void
reset_common(int hard, int src)
{
#ifdef USE_DYNAREC
    /* A port or memory access in the block may have run a timer that resets
       the CPU (the keyboard controller's reset pulse): the rest of the block
       would overwrite the reset state. x86_was_reset ends the block at its
       next I/O instruction or block link. */
    if (inrecomp) {
        reset_deferred = MAX(reset_deferred, hard + 1);
        reset_deferred_src |= src;
        x86_was_reset  = 1;
        return;
    }
#endif
    automation_reset(hard);
    /* A soft reset is the INIT pin: on the P54C, the P55C and the P6 family
       it leaves the FPU, the caches and the MSRs (cpu_init_keeps_state). */
    reset_init = !hard && cpu_init_keeps_state();

#ifdef ENABLE_X86_LOG
    if (hard)
        x86_log("x86 reset\n");
#endif

    if (!hard && reset_on_hlt) {
        hlt_reset_pending++;
        if (hlt_reset_pending == 2)
            hlt_reset_pending = 0;
        else
            return;
    }

    /* The BIOS's POST runs again, and builds its MP table again, perhaps
       elsewhere. */
    ioapic_processor_reset();

    /* Make sure to gracefully leave SMM. */
    if (in_smm)
        leave_smm();

    /* Needed for the ALi M1533. */
    if (is486 && (hard || soft_reset_pci)) {
        pci_reset();
        if (!hard && soft_reset_pci) {
            dma_reset();
            /* TODO: Hack, but will do for time being, because all AT machines currently are 286+,
                     and vice-versa. */
            dma_set_at(is286);
            device_reset_all(DEVICE_ALL);
        }
    }

    /* A reset leaves no NMI in progress and none pending: the processor
       comes out of RESET accepting NMIs, and an NMI that was latched or
       being serviced is gone with the state it was taken in. Only the
       blocking and the pending request are the processor's; the chipset's
       NMI mask (port 70h) is not, and stays where it is. */
    nmi_enable     = 1;
    nmi            = 0;
    nmi_auto_clear = 0;

    use32          = 0;
    cpu_cur_status = 0;
    stack32        = 0;
    if (!reset_init)
        msr.fcr    = (1 << 8) | (1 << 9) | (1 << 12) | (1 << 16) | (1 << 19) | (1 << 21);
    msw            = 0;
    new_ne         = 0;
    fpu_ignne      = 0;
    fpu_ferr       = 0;
    /* FOP: INIT leaves the FPU as it was (Pentium Vol. 3, 16.2). */
    if (!reset_init)
        x87_op = 0;
    /* RESET: reset_fpu_hard(). A soft reset gives the soft FPU its reset
       state too, except where the processor's book says it leaves the FPU
       as it was (cpu_soft_reset_keeps_fpu, INIT on the P54C, P55C and P6
       family among them). The reset state is not the MMX one. */
    if (hard)
        reset_fpu_hard();
    else if (fpu_softfloat && !cpu_soft_reset_keeps_fpu()) {
        SF_FPU_reset();
        cpu_state.ismmx = 0;
    }
    /* A board whose reset logic drives the 387's RESET from this source as
       well as the CPU's (soft_reset_387). A 287 is left alone: on the
       CS8230 its RESET comes from the system reset and port F1h only, and
       the 82C291 takes a 387SX. */
    if (!hard && (src & soft_reset_387) && (fpu_type == FPU_387))
        reset_387();

    ccr0 = ccr1 = ccr2 = ccr3 = ccr4 = ccr5 = ccr6 = ccr7 = 0;
    ccr4 = 0x85;
    cyrix.arr[3].base = 0x30000;
    cyrix.arr[3].size = 65536;
    if (is_gxm)
        cpu_gxm_reset();

    if (reset_init)
        /* INIT leaves CD and NW, and sets ET (Pentium Vol. 3, Table 16-1,
           note 2). */
        cr0 = (cr0 & 0x60000000) | 0x10;
    else if (hascache)
        /* CD, NW and ET: "The state of control register CR0 is 60000010H"
           after reset on the Intel486 and later (SDM 1999 vol. 3, 8.1.1) */
        cr0 = 0x60000010;
    else
        cr0 = 0;
    if (is386 && !is486 && ((fpu_type == FPU_387) || (fpu_type == FPU_NONE)))
        cr0 |= 0x10;
    if (is486 || isibm486)
        cr0 |= 0x10; /* ET is hardwired to 1 */
    /* The cache timing follows CD: INIT keeps it (above), and a CD of 0
       kept is a cache still on. */
    cpu_cache_int_enabled = hascache && !(cr0 & (1 << 30));
    cpu_update_waitstates();
    cr4              = 0;
    cpu_state.eflags = 0;
    cgate32          = 0;
    if (is386 && (cpu_debug_regs_486 || !is486)) {
        for (uint8_t i = 0; i < 4; i++)
            dr[i] = 0x00000000;
        dr[6] = is586 ? 0xffff0ff0 : 0xffff1ff0;
        dr[7] = 0x00000400;
    }
    /* The Pentium's RESET and INIT columns alike: CR2 and CR3 0, DR0-3 0,
       DR6 FFFF0FF0h, DR7 400h (Vol. 3, Table 16-1). */
    if (cpu_is_p5()) {
        cr2 = 0;
        cr3 = 0;
        for (uint8_t i = 0; i < 4; i++)
            dr[i] = 0x00000000;
        dr[6] = 0xffff0ff0;
        dr[7] = 0x00000400;
    }
    cpu_dr7_update();
    if (is286) {
        if (is486)
            loadcs(0xF000);
        else
            loadcs_2386(0xF000);
        cpu_state.pc = 0xFFF0;
        mem_a20_reset_line_arm(reset_line_unmasked());
        if (hard) {
            rammask = cpu_16bitbus ? 0xFFFFFF : 0xFFFFFFFF;
            if (is6117 || is386ex)
                rammask |= 0x3000000;
            mem_a20_key = mem_a20_alt = mem_a20_state = 0;
        }
    }
    idt.base        = 0;
    cpu_state.flags = 2;
    trap            = 0;

    idt.limit = is386 ? 0x03ff : 0xffff;
    if (is586) {
        /* Pentium on: GDTR and IDTR base 0 limit FFFFh; LDTR and TR
           selector 0, base 0, limit FFFFh */
        idt.limit = 0xffff;
        gdt.base  = 0;
        gdt.limit = 0xffff;
        ldt.seg   = 0;
        ldt.base  = 0;
        ldt.limit = 0xffff;
        tr.seg    = 0;
        tr.base   = 0;
        tr.limit  = 0xffff;
    }
    /* EDX: the processor's signature after RESET and INIT alike (386 DX PRM,
       1990, 10.1; Intel486 DX data sheet, Table 6.2; Pentium Vol. 1, Jul
       1995, Table 4-2; SDM 1999 Vol. 3, Table 8-1), set here, when the
       reset is done */
    if (is386 || hard) {
        EAX = EBX = ECX = ESI = EDI = EBP = ESP = 0;
        EDX = cpu_s->edx_reset;
    }

    if (hard) {
        makeznptable();
        resetreadlookup();
        makemod1table();
    }
    x86seg_reset();
    cpu_update_64k_status();
#ifdef USE_DYNAREC
    /* The data segments come out of reset with a 64K limit: not flat, so
       the recompiler keeps their limit checks */
    cpu_cur_status |= CPU_STATUS_NOTFLATDS | CPU_STATUS_NOTFLATSS | CPU_STATUS_NOTFLATES | CPU_STATUS_NOTWIDEDS | CPU_STATUS_NOTWIDESS;
#endif
#ifdef USE_DYNAREC
    if (hard)
        codegen_reset();
#endif
    cpu_flush_pending = 0;
    cpu_old_paging = 0;
    if (!hard)
        flushmmucache();
    x86_was_reset = 1;
    cpu_alt_reset = 0;

    /* INIT keeps the model-specific registers (Pentium Vol. 1, 3.3): the
       vendor reset, which clears every MSR, is RESET's. */
    if (!reset_init) {
        cpu_ven_reset();
        /* RESET empties the caches too. */
        cpu_car_invalidate();
    }
    /* The local APIC goes through INIT at the INIT pin, and after RESET's
       vendor reset as well, which sets its registers but not what it holds
       beside them. */
    lapic_index_board_reset(0);

    in_smm = smi_latched = 0;
    /* Out of any HLT: the block end must not step past the reset vector's
       first byte (386_dynarec.c). */
    cpu_halted = 0;
    /* RESET and INIT are break events of the clock control. */
    cpu_stpclk = 0;
    smi_line = smm_in_hlt = 0;
    smi_block             = 0;

    if (hard) {
        if (is486)
            smbase = is_am486dxl ? 0x00060000 : 0x00030000;
        ppi_reset();
    }
    in_sys = 0;

    shadowbios = shadowbios_write = 0;
    alt_access = cpu_end_block_after_ins = 0;

    if (hard) {
        reset_on_hlt = hlt_reset_pending = 0;
        cpu_tr_reset();

        /* If we have an AT or PS/2 keyboard controller, make sure the A20 state
           is correct. */
        device_reset_all(DEVICE_KBC);
    } else
        device_reset_all(DEVICE_SOFTRESET);

    if (!is286) {
        if (is_nec)
            reset_vx0(hard);
        else
            reset_808x(hard);
    }

    in_lock    = 0;

    cpu_cpurst_on_sr = 0;

    cpu_force_interpreter = 0;
}

/* Hard reset. */
void
resetx86(void)
{
    reset_common(1, 0);

    soft_reset_mask = 0;
}

/* Soft reset. */
#ifdef USE_DYNAREC
void
x86_reset_deferred(void)
{
    if (reset_deferred) {
        const int hard = reset_deferred - 1;
        const int src  = reset_deferred_src;

        reset_deferred     = 0;
        reset_deferred_src = 0;
        cpu_state.abrt     = 0; /* a fault later in the block came after the reset */
        reset_common(hard, src);
    }
}
#endif

/* src: where it came from (SOFT_RESET_*), 0 for a chipset's own reset
   register or another source no board resets the coprocessor on */
void
softresetx86_from(int src)
{
    if (soft_reset_mask)
        return;

    reset_common(0, src);
}

void
softresetx86(void)
{
    softresetx86_from(0);
}

/* Actual hard reset. */
void
hardresetx86(void)
{
    dma_reset();
    /* TODO: Hack, but will do for time being, because all AT machines currently are 286+,
       and vice-versa. */
    dma_set_at(is286);
    device_reset_all(DEVICE_ALL);

    cpu_alt_reset = 0;

    mem_a20_alt = 0;
    mem_a20_recalc();

    flushmmucache();

    resetx86();
}

void
fpu_postamble(void)
{
    if (cpu_state.ea_seg != NULL) {
        cpu_state.fpu_DS = cpu_state.ea_seg->seg;
        cpu_state.fpu_ds = cpu_state.ea_seg->base;
    }

    cpu_state.fpu_ea = cpu_state.eaaddr;
}
