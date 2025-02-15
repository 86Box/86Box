#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#ifndef INFINITY
#    define INFINITY (__builtin_inff())
#endif

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x86seg_common.h"
#include "x87_sf.h"
#include "x87.h"
#include <86box/io.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/machine.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include <86box/gdbstub.h>
#ifndef OPS_286_386
#    define OPS_286_386
#endif
#include "x86seg.h"
#include "386_common.h"
#ifdef USE_NEW_DYNAREC
#    include "codegen.h"
#endif

#undef CPU_BLOCK_END
#define CPU_BLOCK_END()

extern int codegen_flags_changed;

#ifdef ENABLE_386_LOG
int x386_do_log = ENABLE_386_LOG;

void
x386_log(const char *fmt, ...)
{
    va_list ap;

    if (x386_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define x386_log(fmt, ...)
#endif

#undef CPU_BLOCK_END
#define CPU_BLOCK_END()

#define getbytef()          \
    ((uint8_t) (fetchdat)); \
    cpu_state.pc++
#define getwordf()           \
    ((uint16_t) (fetchdat)); \
    cpu_state.pc += 2
#define getbyte2f()              \
    ((uint8_t) (fetchdat >> 8)); \
    cpu_state.pc++
#define getword2f()               \
    ((uint16_t) (fetchdat >> 8)); \
    cpu_state.pc += 2

static __inline void
fetch_ea_32_long(uint32_t rmdat)
{
    easeg         = cpu_state.ea_seg->base;
    if (cpu_rm == 4) {
        uint8_t sib = rmdat >> 8;

        switch (cpu_mod) {
            case 0:
                cpu_state.eaaddr = cpu_state.regs[sib & 7].l;
                cpu_state.pc++;
                break;
            case 1:
                cpu_state.pc++;
                cpu_state.eaaddr = ((uint32_t) (int8_t) getbyte()) + cpu_state.regs[sib & 7].l;
                break;
            case 2:
                cpu_state.eaaddr = (fastreadl(cs + cpu_state.pc + 1)) + cpu_state.regs[sib & 7].l;
                cpu_state.pc += 5;
                break;
        }
        /*SIB byte present*/
        if ((sib & 7) == 5 && !cpu_mod)
            cpu_state.eaaddr = getlong();
        else if ((sib & 6) == 4 && !cpu_state.ssegs) {
            easeg            = ss;
            cpu_state.ea_seg = &cpu_state.seg_ss;
        }
        if (((sib >> 3) & 7) != 4)
            cpu_state.eaaddr += cpu_state.regs[(sib >> 3) & 7].l << (sib >> 6);
    } else {
        cpu_state.eaaddr = cpu_state.regs[cpu_rm].l;
        if (cpu_mod) {
            if (cpu_rm == 5 && !cpu_state.ssegs) {
                easeg            = ss;
                cpu_state.ea_seg = &cpu_state.seg_ss;
            }
            if (cpu_mod == 1) {
                cpu_state.eaaddr += ((uint32_t) (int8_t) (rmdat >> 8));
                cpu_state.pc++;
            } else {
                cpu_state.eaaddr += getlong();
            }
        } else if (cpu_rm == 5) {
            cpu_state.eaaddr = getlong();
        }
    }
}

static __inline void
fetch_ea_16_long(uint32_t rmdat)
{
    easeg         = cpu_state.ea_seg->base;
    if (!cpu_mod && cpu_rm == 6) {
        cpu_state.eaaddr = getword();
    } else {
        switch (cpu_mod) {
            case 0:
                cpu_state.eaaddr = 0;
                break;
            case 1:
                cpu_state.eaaddr = (uint16_t) (int8_t) (rmdat >> 8);
                cpu_state.pc++;
                break;
            case 2:
                cpu_state.eaaddr = getword();
                break;
        }
        cpu_state.eaaddr += (*mod1add[0][cpu_rm]) + (*mod1add[1][cpu_rm]);
        if (mod1seg[cpu_rm] == &ss && !cpu_state.ssegs) {
            easeg            = ss;
            cpu_state.ea_seg = &cpu_state.seg_ss;
        }
        cpu_state.eaaddr &= 0xFFFF;
    }
}

#define fetch_ea_16(rmdat)       \
    cpu_state.pc++;              \
    cpu_mod = (rmdat >> 6) & 3;  \
    cpu_reg = (rmdat >> 3) & 7;  \
    cpu_rm  = rmdat & 7;         \
    if (cpu_mod != 3) {          \
        fetch_ea_16_long(rmdat); \
        if (cpu_state.abrt)      \
            return 1;            \
    }
#define fetch_ea_32(rmdat)       \
    cpu_state.pc++;              \
    cpu_mod = (rmdat >> 6) & 3;  \
    cpu_reg = (rmdat >> 3) & 7;  \
    cpu_rm  = rmdat & 7;         \
    if (cpu_mod != 3) {          \
        fetch_ea_32_long(rmdat); \
    }                            \
    if (cpu_state.abrt)          \
    return 1

#include "x86_flags.h"

#define PREFETCH_RUN(instr_cycles, bytes, modrm, reads, reads_l, writes, writes_l, ea32)      \
    do {                                                                                      \
        if (cpu_prefetch_cycles)                                                              \
            prefetch_run(instr_cycles, bytes, modrm, reads, reads_l, writes, writes_l, ea32); \
    } while (0)

#define PREFETCH_PREFIX()        \
    do {                         \
        if (cpu_prefetch_cycles) \
            prefetch_prefixes++; \
    } while (0)
#define PREFETCH_FLUSH() prefetch_flush()

#ifndef FPU_CYCLES
#    define FPU_CYCLES
#endif

#define OP_TABLE(name) ops_2386_##name
#define CLOCK_CYCLES(c)               \
    {                                 \
        if (fpu_cycles > 0) {         \
            fpu_cycles -= (c);        \
            if (fpu_cycles < 0) {     \
                cycles += fpu_cycles; \
            }                         \
        } else {                      \
            cycles -= (c);            \
        }                             \
    }

#define CLOCK_CYCLES_FPU(c)    cycles -= (c)
#define CONCURRENCY_CYCLES(c)  fpu_cycles = (c)

#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#define CHECK_READ_CS(size)                                                            \
    if (msw & 1 && !(cpu_state.eflags & VM_FLAG) && !(cpu_state.seg_cs.access & 0x80)) \
        x86np("Read from seg not present", cpu_state.seg_cs.seg & 0xfffc);             \
    else if ((cpu_state.pc < cpu_state.seg_cs.limit_low) ||                            \
        ((cpu_state.pc + size - 1) > cpu_state.seg_cs.limit_high))                     \
        x86gpf("Limit check (READ CS)", 0);

#include "386_ops.h"

void
exec386_2386(int32_t cycs)
{
    int      ol;

    int      vector;
    int      tempi;
    int32_t  cycdiff;
    int32_t  oldcyc;
    int32_t  cycle_period;
    int32_t  ins_cycles;
    uint32_t addr;

    cycles += cycs;

    while (cycles > 0) {
        cycle_period = (timer_target - (uint32_t) tsc) + 1;

        x86_was_reset = 0;
        cycdiff       = 0;
        oldcyc        = cycles;
        while (cycdiff < cycle_period) {
            int ins_fetch_fault = 0;
            ins_cycles = cycles;

#ifndef USE_NEW_DYNAREC
            oldcs  = CS;
            oldcpl = CPL;
#endif
            cpu_state.oldpc = cpu_state.pc;
            cpu_state.op32  = use32;

#ifndef USE_NEW_DYNAREC
            x86_was_reset = 0;
#endif

            cpu_state.ea_seg = &cpu_state.seg_ds;
            cpu_state.ssegs  = 0;

            fetchdat = fastreadl_fetch(cs + cpu_state.pc);
            ol = opcode_length[fetchdat & 0xff];
            if ((ol == 3) && opcode_has_modrm[fetchdat & 0xff] && (((fetchdat >> 14) & 0x03) == 0x03))
                ol = 2;
            if (cpu_16bitbus) {
                CHECK_READ_CS(MIN(ol, 2));
            } else {
                CHECK_READ_CS(MIN(ol, 4));
            }
            if (is386)
                ins_fetch_fault = cpu_386_check_instruction_fault();

            /* Breakpoint fault has priority over other faults. */
            if (ins_fetch_fault) {
                ins_fetch_fault = 0;
                cpu_state.abrt = 1;
            }

            if (!cpu_state.abrt) {
#ifdef ENABLE_386_LOG
                if (in_smm)
                    x386_log("[%04X:%08X] %08X\n", CS, cpu_state.pc, fetchdat);
#endif
                opcode = fetchdat & 0xFF;
                fetchdat >>= 8;
                trap |= !!(cpu_state.flags & T_FLAG);

                cpu_state.pc++;
                cpu_state.eflags &= ~(RF_FLAG);
                if (opcode == 0xf0)
                    in_lock = 1;
                x86_2386_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
                in_lock = 0;
                if (x86_was_reset)
                    break;
            }
#ifdef ENABLE_386_LOG
            else if (in_smm)
                x386_log("[%04X:%08X] ABRT\n", CS, cpu_state.pc);
#endif

#ifndef USE_NEW_DYNAREC
            if (!use32)
                cpu_state.pc &= 0xffff;
#endif

            if (cpu_flush_pending == 1)
                cpu_flush_pending++;
            else if (cpu_flush_pending == 2) {
                cpu_flush_pending = 0;
                flushmmucache_pc();
            }

            if (cpu_end_block_after_ins)
                cpu_end_block_after_ins--;

            if (cpu_state.abrt) {
                flags_rebuild();
                tempi          = cpu_state.abrt & ABRT_MASK;
                cpu_state.abrt = 0;
                x86_doabrt_2386(tempi);
                if (cpu_state.abrt) {
                    cpu_state.abrt = 0;
#ifndef USE_NEW_DYNAREC
                    CS = oldcs;
#endif
                    cpu_state.pc = cpu_state.oldpc;
                    x386_log("Double fault\n");
                    pmodeint_2386(8, 0);
                    if (cpu_state.abrt) {
                        cpu_state.abrt = 0;
                        softresetx86();
                        cpu_set_edx();
#ifdef ENABLE_386_LOG
                        x386_log("Triple fault - reset\n");
#endif
                    }
                }
            } else if (new_ne) {
                flags_rebuild();
                new_ne = 0;
#ifndef USE_NEW_DYNAREC
                oldcs = CS;
#endif
                cpu_state.oldpc = cpu_state.pc;
                x86_int(16);
            } else if (trap) {
                flags_rebuild();
                if (trap & 2) dr[6] |= 0x8000;
                if (trap & 1) dr[6] |= 0x4000;
                trap = 0;
#ifndef USE_NEW_DYNAREC
                oldcs = CS;
#endif
                cpu_state.oldpc = cpu_state.pc;
                x86_int(1);
            }

            if (smi_line)
                enter_smm_check(0);
            else if (nmi && nmi_enable && nmi_mask) {
#ifndef USE_NEW_DYNAREC
                oldcs = CS;
#endif
                cpu_state.oldpc = cpu_state.pc;
                x86_int(2);
                nmi_enable = 0;
#ifdef OLD_NMI_BEHAVIOR
                if (nmi_auto_clear) {
                    nmi_auto_clear = 0;
                    nmi            = 0;
                }
#else
                nmi = 0;
#endif
            } else if ((cpu_state.flags & I_FLAG) && pic.int_pending && !cpu_end_block_after_ins) {
                vector = picinterrupt();
                if (vector != -1) {
                    flags_rebuild();
                    if (msw & 1)
                        pmodeint_2386(vector, 0);
                    else {
                        writememw(ss, (SP - 2) & 0xFFFF, cpu_state.flags);
                        writememw(ss, (SP - 4) & 0xFFFF, CS);
                        writememw(ss, (SP - 6) & 0xFFFF, cpu_state.pc);
                        SP -= 6;
                        addr = (vector << 2) + idt.base;
                        cpu_state.flags &= ~I_FLAG;
                        cpu_state.flags &= ~T_FLAG;
                        cpu_state.pc = readmemw(0, addr);
                        loadcs_2386(readmemw(0, addr + 2));
                    }
                }
            }

            ins_cycles -= cycles;
            tsc += ins_cycles;

            cycdiff = oldcyc - cycles;

            if (timetolive) {
                timetolive--;
                if (!timetolive)
                    fatal("Life expired\n");
            }

            if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t) tsc))
                timer_process();

#ifdef USE_GDBSTUB
            if (gdbstub_instruction())
                return;
#endif
        }
    }
}
