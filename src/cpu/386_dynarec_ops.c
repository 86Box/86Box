#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#ifndef INFINITY
#    define INFINITY (__builtin_inff())
#endif

#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include "x86.h"
#include "x86_ops.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x87_sf.h"
#include "x87.h"
#include "x86_flags.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/gdbstub.h>
#include "codegen.h"
#include <86box/plat_unused.h>
#include <86box/plat_fallthrough.h>

#define CPU_BLOCK_END() cpu_block_end = 1

#ifndef IS_DYNAREC
#    define IS_DYNAREC
#endif

#include "386_common.h"

static __inline void
fetch_ea_32_long(UNUSED(uint32_t rmdat))
{
    eal_r = eal_w = NULL;
    easeg         = cpu_state.ea_seg->base;
    if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC) {
        uint32_t addr = easeg + cpu_state.eaaddr;
        if (readlookup2[addr >> 12] != (uintptr_t) LOOKUP_INV)
            eal_r = (uint32_t *) (readlookup2[addr >> 12] + addr);
        if (writelookup2[addr >> 12] != (uintptr_t) LOOKUP_INV)
            eal_w = (uint32_t *) (writelookup2[addr >> 12] + addr);
    }
}

static __inline void
fetch_ea_16_long(UNUSED(uint32_t rmdat))
{
    eal_r = eal_w = NULL;
    easeg         = cpu_state.ea_seg->base;
    if (easeg != 0xFFFFFFFF && ((easeg + cpu_state.eaaddr) & 0xFFF) <= 0xFFC) {
        uint32_t addr = easeg + cpu_state.eaaddr;
        if (readlookup2[addr >> 12] != (uintptr_t) LOOKUP_INV)
            eal_r = (uint32_t *) (readlookup2[addr >> 12] + addr);
        if (writelookup2[addr >> 12] != (uintptr_t) LOOKUP_INV)
            eal_w = (uint32_t *) (writelookup2[addr >> 12] + addr);
    }
}

/*A hand-over's displacement walk, which codegen_skip leaves to the op: made
  where the interpreter's fetch_ea reads the displacement, after the op's own
  checks (MMX_ENTER, FP_ENTER, ILLEGAL_ON), which fault without it. The
  compiled code stores the address before every hand-over with a memory
  operand, so one left by an op that faulted first is never taken. Nonzero
  with the #PF, which the op returns with as from the interpreter's
  fetch_ea, whose eaaddr then lacks what the displacement's bytes from the
  fault on add (16 bits with a16), and which an x87 op's escape wrapper
  records as the data pointer (bit 1 of the stored word)*/
static __inline int
dynarec_handover_walk(int a16)
{
    uint32_t walk = cpu_state.handover_walk;

    if (!walk)
        return 0;
    cpu_state.handover_walk = 0;
    if (!dynarec_fetch_walk(walk & ~0xfff))
        return 0;
    cpu_state.eaaddr -= cpu_state.handover_walk_part;
    if (a16)
        cpu_state.eaaddr &= 0xffff;
    if (walk & 2)
        cpu_state.fpu_ea = cpu_state.eaaddr;
    return 1;
}

#define fetch_ea_16(rmdat)            \
    cpu_state.pc++;                   \
    if (cpu_mod != 3) {               \
        if (dynarec_handover_walk(1)) \
            return 1;                 \
        fetch_ea_16_long(rmdat);      \
    }
#define fetch_ea_32(rmdat)            \
    cpu_state.pc++;                   \
    if (cpu_mod != 3) {               \
        if (dynarec_handover_walk(0)) \
            return 1;                 \
        fetch_ea_32_long(rmdat);      \
    }

#define PREFETCH_RUN(instr_cycles, bytes, modrm, reads, read_ls, writes, write_ls, ea32)
#define PREFETCH_PREFIX()
#define PREFETCH_FLUSH()

#define OP_TABLE(name) dynarec_ops_##name

#define CLOCK_CYCLES(c)
#if 0
#    define CLOCK_CYCLES_FPU(c)
#    define CONCURRENCY_CYCLES(c) fpu_cycles = (c)
#else
#    define CLOCK_CYCLES_FPU(c)
#    define CONCURRENCY_CYCLES(c)
#endif
#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#include "386_ops.h"

/*The handler every dynarec table uses for an opcode this CPU doesn't have,
  so the recompiler can leave those to it instead of compiling them*/
const OpFn dynarec_op_illegal = ILLEGAL;
/*The handlers the FPU tables use for an escape this FPU doesn't have (FUCOM
  and FUCOMPP before the 387, FNSTSW AX on the 8087): #UD on the 486 and
  later, nothing before (x87_ops.h)*/
const OpFn dynarec_fpu_illegal_a16 = FPU_ILLEGAL_a16;
const OpFn dynarec_fpu_illegal_a32 = FPU_ILLEGAL_a32;

/*The compiled IRET and IRETD in protected mode, V86 mode at IOPL 3
  included: opIRET's and opIRETD's pmodeiret call and their tail, which
  write globals the compiled code cannot*/
void
dynarec_iret_pm(int is32)
{
    optype = IRET;
    op_pmodeiret(is32);
    optype = 0;
    flags_extract();
    nmi_enable       = 1;
    rf_flag_no_clear = 1;
    if (cpu_debug_regs_486)
        dynarec_rf_after_op();
}

/*The CS load and the tail of the compiled IRET's real-mode and CR4.VME
  forms, which pop and merge the flags themselves*/
void
dynarec_iret_cs(uint16_t seg)
{
    op_loadcs(seg);
    cycles -= timing_iret_rm;
    nmi_enable       = 1;
    rf_flag_no_clear = 1;
    if (cpu_debug_regs_486)
        dynarec_rf_after_op();
}

/*What the interpreted loop does with RF after an instruction that did not
  fault, with the 486 debug registers option on: RF cleared, unless the
  instruction kept it (rf_flag_no_clear, consumed here). Called after a
  hand-over's call and at the end of the compiled code's routines that can
  set rf_flag_no_clear, a global outside cpu_state*/
void
dynarec_rf_after_op(void)
{
    if (cpu_state.abrt)
        return;
    if (!rf_flag_no_clear)
        cpu_state.eflags &= ~RF_FLAG;
    rf_flag_no_clear = 0;
}

/*The walk of a compiled block's second page where the interpreter's code
  fetch first reads from it (getpccache(): mmutranslate_read on every change
  of fetch page): the Accessed bits, and the #PF a page that is no longer
  present raises, from oldpc. Nonzero then, which leaves the block*/
int
dynarec_fetch_walk(uint32_t addr)
{
    if (!(cr0 >> 31))
        return 0;
    (void) mmutranslatereal(addr, 0);

    return !!cpu_state.abrt;
}

/*The compiled SGDT, SIDT, LGDT and LIDT memory forms, the operand at
  cpu_state.eaaddr: op0F01_common's operand accesses and register writes in
  its order, the base still read after the limit word faulted. The privilege
  and null-selector tests are the compiled code's. 1 when a fault was raised*/
static int
dynarec_dtr_access(uint32_t op)
{
    uint32_t base;
    uint16_t limit;

    fetch_ea_32_long(0);
    switch (op & 0x38) {
        case 0x00: /*SGDT*/
        case 0x08: /*SIDT*/
            CHECK_WRITE_EA(6);
            CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 1);
            seteaw_mem((op & 0x08) ? idt.limit : gdt.limit);
            writememl(easeg, cpu_state.eaaddr + 2, (op & 0x08) ? idt.base : gdt.base);
            break;
        default: /*LGDT, LIDT*/
            limit = geteaw_mem();
            base  = readmeml(0, easeg + cpu_state.eaaddr + 2);
            if (cpu_state.abrt)
                return 1;
            if (!(op & 0x100))
                base &= 0xffffff;
            if (op & 0x08) {
                idt.limit = limit;
                idt.base  = base;
            } else {
                gdt.limit = limit;
                gdt.base  = base;
            }
            break;
    }
    return !!cpu_state.abrt;
}

/*op: the reg field, the operand size at 0x100 and the operand's segment
  register number (ES CS SS DS FS GS) from bit 9. The interpreter's checks
  read ea_seg; it is put back after, as the recompiler takes it to be what
  the block last set for its own interpreter calls*/
int
dynarec_dtr(uint32_t op)
{
    x86seg *const segs[6] = { &cpu_state.seg_es, &cpu_state.seg_cs, &cpu_state.seg_ss,
                              &cpu_state.seg_ds, &cpu_state.seg_fs, &cpu_state.seg_gs };
    x86seg       *ea_seg  = cpu_state.ea_seg;
    int           ret;

    cpu_state.ea_seg = segs[(op >> 9) % 6];
    ret              = dynarec_dtr_access(op);
    cpu_state.ea_seg = ea_seg;
    return ret;
}

/*The compiled CALL far, direct and indirect: the interpreter's CALL_FAR
  macro, so the pushes in the call gate's size and the CS put back after a
  faulting push are the op's. The target offset comes in cpu_state.pc;
  ret_pc is the address after the instruction*/
int
dynarec_call_far_w(uint16_t new_cs, uint32_t ret_pc)
{
    uint32_t old_cs;
    x86seg   old_cs_seg;
    uint32_t old_use32;
    uint32_t old_status;
    uint32_t old_pc;
    uint16_t new_pc = cpu_state.pc;

    cpu_state.pc = ret_pc;
    CALL_FAR_w(new_cs, new_pc);
    /*A task switch can keep the RF it loads*/
    if (cpu_debug_regs_486)
        dynarec_rf_after_op();
    return 0;
}

int
dynarec_call_far_l(uint16_t new_cs, uint32_t ret_pc)
{
    uint32_t old_cs;
    x86seg   old_cs_seg;
    uint32_t old_use32;
    uint32_t old_status;
    uint32_t old_pc;
    uint32_t new_pc = cpu_state.pc;

    cpu_state.pc = ret_pc;
    CALL_FAR_l(new_cs, new_pc);
    if (cpu_debug_regs_486)
        dynarec_rf_after_op();
    return 0;
}

/*The compiled LMSW's register write, op0F01_common's: PE, MP, EM and TS
  load, PE cannot be cleared, and the status follows PE. cpu_cur_status is
  not in cpu_state, where the compiled code stores*/
void
dynarec_lmsw(uint16_t tempw)
{
    if (msw & 1)
        tempw |= 1;
    msw = (msw & ~0xF) | (tempw & 0xF);
    if (msw & 1)
        cpu_cur_status |= CPU_STATUS_PMODE;
    else
        cpu_cur_status &= ~CPU_STATUS_PMODE;
}

/*The compiled MOV CR0,r32's write, opMOV_CRx_r's case 0 after the
  privilege and cpu_cr0_invalid() tests, which the compiled code makes. rm
  is the source register, read from cpu_state, where the call has written
  the block's copy back (the compiled code passes only 16-bit registers).
  1, with nothing changed, when setting PG with PAE on fails the PDPTE
  load: the compiled code raises the #GP. The compiled block ends after
  every MOV CR0, so the op's block end on a PG change is not repeated*/
int
dynarec_mov_cr0(uint32_t rm)
{
    const uint32_t val     = cpu_state.regs[rm].l;
    uint32_t       old_cr0 = cr0;

    if ((val & 0x80000000) && !(cr0 & 0x80000000) && (cr4 & CR4_PAE) && pae_check_load(cr3))
        return 1;
    if ((val ^ cr0) & 0x00000001)
        flushmmucache();
    else if ((val ^ cr0) & 0x80000000) {
        if (is_p6)
            flushmmucache();
        else {
            flushmmucache_nopc();
            cpu_flush_pending = 1;
        }
    } else if ((val ^ cr0) & WP_FLAG)
        flushmmucache_write();
    if ((val & 0x01) && !(cr0 & 0x01))
        cpu_state.seg_cs.access &= 0x9f;
    cr0 = val;
    if (cpu_16bitbus)
        cr0 |= 0x10;
    if (hascache && !(cr0 & (1 << 30)))
        cpu_cache_int_enabled = 1;
    else
        cpu_cache_int_enabled = 0;
    if (hascache && ((cr0 ^ old_cr0) & (1 << 30)))
        cpu_update_waitstates();
    if ((cr0 ^ old_cr0) & (1 << 30))
        cpu_car_update();
    if (cr0 & 1)
        cpu_cur_status |= CPU_STATUS_PMODE;
    else
        cpu_cur_status &= ~CPU_STATUS_PMODE;

    return 0;
}

/*The compiled MOV CR2,r32 (rm as for dynarec_mov_cr0): the IR has no store
  to a global outside cpu_state*/
void
dynarec_mov_cr2(uint32_t rm)
{
    cr2 = cpu_state.regs[rm].l;
}

/*The compiled MOV CR3,r32, opMOV_CRx_r's case 3 (rm as for
  dynarec_mov_cr0): 1, with nothing changed, when PAE paging's PDPTE load
  fails*/
int
dynarec_mov_cr3(uint32_t rm)
{
    const uint32_t val = cpu_state.regs[rm].l;

    if ((cr4 & CR4_PAE) && (cr0 & 0x80000000) && pae_check_load(val))
        return 1;
    cr3 = val;
    flushmmucache();

    return 0;
}

/*The compiled MOV CR4,r32 on a CPU with CR4, opMOV_CRx_r's case 4 after its
  reserved-bit test, which the compiled code makes (rm as for
  dynarec_mov_cr0): 1, with nothing changed, when setting PAE with paging on
  fails the PDPTE load*/
int
dynarec_mov_cr4(uint32_t rm)
{
    const uint32_t val = cpu_state.regs[rm].l;

    if ((val & CR4_PAE) && !(cr4 & CR4_PAE) && (cr0 & 0x80000000) && pae_check_load(cr3))
        return 1;
    if (((val ^ cr4) & cpu_CR4_mask) & (CR4_PSE | CR4_PAE | CR4_PGE))
        flushmmucache();
    cr4 = val & cpu_CR4_mask;

    return 0;
}

/*The general detect test of the compiled MOV r32,DRx and MOV DRx,r32 with
  the 486 debug registers option, after the privilege test and before the
  EA, as in opMOV_r_DRx and opMOV_DRx_r: with DR7.GD set, #DB with DR6.BD,
  delivered at once from oldpc, and 1*/
int
dynarec_mov_drx_gd(void)
{
    if (!(dr[7] & 0x2000))
        return 0;
    dr[6] |= 0x2000;
    x86gen();
    return 1;
}

/*The compiled MOV r32,DRx and MOV DRx,r32 after the privilege and general
  detect tests, which the compiled code makes: op is the debug register, the
  r/m register << 3, and 0x40 for a write. DR4 and DR5 with CR4.DE raise
  #UD, delivered at once from oldpc: 1 then. Otherwise DR4 and DR5 are DR6
  and DR7, and DR6 and DR7 are written through their masks, as in
  opMOV_r_DRx and opMOV_DRx_r. The register is read and written in
  cpu_state, the call having written the block's copy back*/
int
dynarec_mov_drx(uint32_t op)
{
    const int dr_nr = op & 7;
    const int rm    = (op >> 3) & 7;

    if (((dr_nr == 4) || (dr_nr == 5)) && (cr4 & 0x8)) {
        x86illegal();
        return 1;
    }
    if (op & 0x40) {
        if (dr_nr < 4)
            dr[dr_nr] = cpu_state.regs[rm].l;
        else if ((dr_nr == 4) || (dr_nr == 6))
            dr[6] = (dr[6] & 0xffff0ff0) | (cpu_state.regs[rm].l & (is586 ? 0x0000e00f : 0x0000f00f));
        else {
            dr[7] = (cpu_state.regs[rm].l | 0x00000400) & (is586 ? ~0x0000d800 : ~0);
            cpu_dr7_update();
        }
    } else {
        if (dr_nr < 4)
            cpu_state.regs[rm].l = dr[dr_nr];
        else if ((dr_nr == 4) || (dr_nr == 6))
            cpu_state.regs[rm].l = dr[6];
        else
            cpu_state.regs[rm].l = dr[7];
    }

    return 0;
}
