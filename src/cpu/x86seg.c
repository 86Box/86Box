/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          x86 CPU segment emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_flags.h"
#include "x86seg.h"
#include "x86seg_common.h"
#include "386_common.h"

#ifdef OPS_286_386
#define  seg_readmembl  readmembl_2386
#define  seg_readmemwl  readmemwl_2386
#define  seg_readmemll  readmemll_2386
#define  seg_writemembl writemembl_2386
#define  seg_writememwl writememwl_2386
#define  seg_writememll writememll_2386
#else
#define  seg_readmembl  readmembl
#define  seg_readmemwl  readmemwl
#define  seg_readmemll  readmemll
#define  seg_writemembl writemembl
#define  seg_writememwl writememwl
#define  seg_writememll writememll
#endif

#define DPL  ((segdat[2] >> 13) & 3)
#define DPL2 ((segdat2[2] >> 13) & 3)
#define DPL3 ((segdat3[2] >> 13) & 3)

/* The EXT bit of the error codes a task switch raises: set while an IDT
   task gate delivers a hardware interrupt or an exception, "an event
   external to the program" (SDM Vol. 3, Interrupt 10, Exception Error
   Code). */
static int task_ext;
/* Set while x86_doabrt() delivers an exception that pushes an error code:
   a task gate's EIP check follows the push ("Push(error code); FI; IF EIP
   not within code segment limit THEN #GP(0)", SDM Vol. 2, INT), so the
   switch leaves it pending. */
static int task_error_follows;
static int task_eip_pending;

static void task_eip_check(void);

/* A descriptor's Accessed bit: "the processor will set the Accessed bit if
   the bit is clear. During this operation, the processor asserts LOCK# so
   the descriptor will not be modified by another processor while it is
   being updated" (Pentium Vol. 3, 19.1.2); "the locked read and write
   occur only if the bit is not already set" (23.2.16). Only the access
   byte is written, never the base byte beside it from an old copy. addr
   is the descriptor's linear address, word4 its third word as read. */
static void
x86seg_set_accessed(uint32_t addr, uint16_t word4)
{
    /* A 486 or later skips the write when the bit is already set; a 386
       (and the RapidCAD) writes the descriptor back regardless. */
    if ((word4 & 0x0100) && is486 && !israpidcad)
        return;

    writememb(0, addr + 5, (uint8_t) ((word4 >> 8) | 0x01));
}

/* A TSS descriptor's Busy bit (the access byte's bit 1), set or cleared by
   a task switch (19.1.2). Answers 0 on a fault. */
static int
x86seg_tss_busy(uint32_t dt_base, uint16_t sel, int busy)
{
    uint32_t off = (sel & 0xfff8) + 5;
    uint8_t  v;

    v = readmemb(dt_base, off);
    if (cpu_state.abrt)
        return 0;
    writememb(dt_base, off, busy ? (v | 0x02) : (v & ~0x02));
    return !cpu_state.abrt;
}

#ifdef ENABLE_X86SEG_LOG
int x86seg_do_log = ENABLE_X86SEG_LOG;

static void
x86seg_log(const char *fmt, ...)
{
    va_list ap;

    if (x86seg_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define x86seg_log(fmt, ...)
#endif

#ifdef USE_DYNAREC
extern int cpu_block_end;
#endif

/* A data segment register loaded with a null selector */
static void
null_data_seg(x86seg *s)
{
    s->seg     = 0;
    s->access  = 0x80;
    s->ar_high = 0x10;
    s->base    = -1;
    if (s == &cpu_state.seg_ds)
        cpu_cur_status |= CPU_STATUS_NOTFLATDS;
    if (s == &cpu_state.seg_es)
        cpu_cur_status |= CPU_STATUS_NOTFLATES;
}

/* An exception's error code, sized by the gate it was delivered through, or
   for a task gate by the target TSS (pmodeint) */
static void
push_error_code(uint32_t error)
{
    if (intgatesize == 16) {
        if (stack32) {
            writememw(ss, ESP - 2, error);
            ESP -= 2;
        } else {
            writememw(ss, ((SP - 2) & 0xffff), error);
            SP -= 2;
        }
    } else {
        if (stack32) {
            writememl(ss, ESP - 4, error);
            ESP -= 4;
        } else {
            writememl(ss, ((SP - 4) & 0xffff), error);
            SP -= 4;
        }
    }
}

void
#ifdef OPS_286_386
x86_doabrt_2386(int x86_abrt)
#else
x86_doabrt(int x86_abrt)
#endif
{
    cpu_state.pc             = cpu_state.oldpc;
    cpu_state.seg_cs.access  = (oldcpl << 5) | 0x80;
    cpu_state.seg_cs.ar_high = 0x10;

    task_eip_pending = 0;
    if (msw & 1) {
        task_error_follows = 1;
        op_pmodeint(x86_abrt, 0);
        task_error_follows = 0;
    } else {
        uint32_t addr = (x86_abrt << 2) + idt.base;
        if (stack32) {
            writememw(ss, ESP - 2, cpu_state.flags);
            writememw(ss, ESP - 4, CS);
            writememw(ss, ESP - 6, cpu_state.pc);
            ESP -= 6;
        } else {
            writememw(ss, ((SP - 2) & 0xffff), cpu_state.flags);
            writememw(ss, ((SP - 4) & 0xffff), CS);
            writememw(ss, ((SP - 6) & 0xffff), cpu_state.pc);
            SP -= 6;
        }

        cpu_state.flags &= ~(I_FLAG | T_FLAG);
        cpu_state.pc = readmemw(0, addr);
        op_loadcs(readmemw(0, addr + 2));
        return;
    }

    if (cpu_state.abrt || x86_was_reset)
        return;

    push_error_code(abrt_error);
    if (task_eip_pending && !cpu_state.abrt) {
        task_eip_pending = 0;
        task_eip_check();
    }
}

/* A fault while delivering an exception: a #PF after a contributory
   exception is taken on its own (Pentium Vol. 3, Table 14-4); any other
   pair is a double fault, which always pushes an error code of 0. A
   fault while delivering that is left in cpu_state.abrt: shutdown. */
void
#ifdef OPS_286_386
x86_fault_in_delivery_2386(int first, int second)
#else
x86_fault_in_delivery(int first, int second)
#endif
{
    if ((second == 14) && (first != 14)) {
        op_doabrt(14);
        if (!cpu_state.abrt)
            return;
        cpu_state.abrt = 0;
        cpu_state.pc   = cpu_state.oldpc;
    }
    task_eip_pending   = 0;
    task_error_follows = 1;
    op_pmodeint(8, 0);
    task_error_follows = 0;
    if (cpu_state.abrt || x86_was_reset)
        return;
    push_error_code(0);
    if (task_eip_pending && !cpu_state.abrt) {
        task_eip_pending = 0;
        task_eip_check();
    }
}

/* The D and B bits come from the descriptor's last word, which the 80286
   reserves: its code and stacks are always 16-bit */
static inline void
set_stack32(int s)
{
    stack32 = is386 && s;

    if (stack32)
        cpu_cur_status |= CPU_STATUS_STACK32;
    else
        cpu_cur_status &= ~CPU_STATUS_STACK32;
}

static inline void
set_use32(int u)
{
    u     = is386 && u;
    use32 = u ? 0x300 : 0;

    if (u)
        cpu_cur_status |= CPU_STATUS_USE32;
    else
        cpu_cur_status &= ~CPU_STATUS_USE32;
}

#ifndef OPS_286_386
void
do_seg_load(x86seg *s, uint16_t *segdat)
{
    /* The descriptor's last word is reserved on the 80286 ("The Intel
       reserved bytes in the segment descriptor must be set to 0 for
       compatibility with the 80386", 80286 PRM 7.4) and
       ignored there: no limit[19:16], G, D/B or base[31:24]. */
    const uint16_t word3 = is386 ? segdat[3] : 0;

    s->limit = segdat[0] | ((word3 & 0x000f) << 16);
    if (word3 & 0x0080)
        s->limit = (s->limit << 12) | 0xfff;
    s->base = segdat[1] | ((segdat[2] & 0x00ff) << 16);
    s->base |= ((word3 >> 8) << 24);
    s->access  = segdat[2] >> 8;
    s->ar_high = word3 & 0xff;

    if (((segdat[2] & 0x1800) != 0x1000) || !(segdat[2] & (1 << 10))) {
        /* Expand-down */
        s->limit_high = s->limit;
        s->limit_low  = 0;
    } else {
        s->limit_high = (word3 & 0x40) ? 0xffffffff : 0xffff;
        s->limit_low  = s->limit + 1;
    }

    if (s == &cpu_state.seg_ds) {
        if ((s->base == 0) && (s->limit_low == 0) && (s->limit_high == 0xffffffff))
            cpu_cur_status &= ~CPU_STATUS_NOTFLATDS;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATDS;
    }
    if (s == &cpu_state.seg_es) {
        if ((s->base == 0) && (s->limit_low == 0) && (s->limit_high == 0xffffffff))
            cpu_cur_status &= ~CPU_STATUS_NOTFLATES;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATES;
    }
    if (s == &cpu_state.seg_ss) {
        if ((s->base == 0) && (s->limit_low == 0) && (s->limit_high == 0xffffffff))
            cpu_cur_status &= ~CPU_STATUS_NOTFLATSS;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATSS;
    }
}
#endif

static inline void
do_seg_v86_init(x86seg *s)
{
    s->access     = 0xe2;
    s->ar_high    = 0x10;
    s->limit      = 0xffff;
    s->limit_low  = 0;
    s->limit_high = 0xffff;
}

static void
check_seg_valid(x86seg *s)
{
    int           dpl   = (s->access >> 5) & 3;
    int           valid = 1;
    const x86seg *dt    = (s->seg & 0x0004) ? &ldt : &gdt;

    if (((s->seg & 0xfff8UL) + 7UL) > dt->limit)
        valid = 0;

    switch (s->access & 0x1f) {
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13: /* Data segments */
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x1a:
        case 0x1b: /* Readable non-conforming code */
            if (((s->seg & 3) > dpl) || ((CPL) > dpl)) {
                valid = 0;
                break;
            }
            break;

        case 0x1e:
        case 0x1f: /* Readable conforming code */
            break;

        default:
            valid = 0;
            break;
    }

    if (!valid)
        op_loadseg(0, s);
}

static void
read_descriptor(uint32_t addr, uint16_t *segdat, uint32_t *segdat32, int override)
{
    if (override)
        cpl_override = 1;
    if (cpu_16bitbus) {
        segdat[0] = readmemw(0, addr);
        segdat[1] = readmemw(0, addr + 2);
        segdat[2] = readmemw(0, addr + 4);
        segdat[3] = readmemw(0, addr + 6);
    } else {
        segdat32[0] = readmeml(0, addr);
        segdat32[1] = readmeml(0, addr + 4);
    }
    if (override)
        cpl_override = 0;
}

int
#ifdef OPS_286_386
loadseg_2386(uint16_t seg, x86seg *s)
#else
loadseg(uint16_t seg, x86seg *s)
#endif
{
    uint16_t      segdat[4];
    uint32_t      addr;
    uint32_t     *segdat32 = (uint32_t *) segdat;
    int           dpl;
    const x86seg *dt;

    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG)) {
        if (!(seg & 0xfffc)) {
            if (s == &cpu_state.seg_ss) {
                x86gpf("loadseg(): Null stack segment", 0);
                return 1;
            }
            s->seg     = seg;
            s->access  = 0x80;
            s->ar_high = 0x10;
            s->base    = -1;
            if (s == &cpu_state.seg_ds)
                cpu_cur_status |= CPU_STATUS_NOTFLATDS;
            if (s == &cpu_state.seg_es)
                cpu_cur_status |= CPU_STATUS_NOTFLATES;
            return 0;
        }
        addr = seg & 0xfff8;
        dt   = (seg & 0x0004) ? &ldt : &gdt;
        if ((addr + 7) > dt->limit) {
            x86gpf("loadseg(): Bigger than LDT limit", seg & 0xfffc);
            return 1;
        }
        addr += dt->base;
        read_descriptor(addr, segdat, segdat32, 1);
        if (cpu_state.abrt)
            return 1;
        dpl = (segdat[2] >> 13) & 3;
        if (s == &cpu_state.seg_ss) {
            if (!(seg & 0xfffc)) {
                x86gpf("loadseg(): Zero stack segment", seg & 0xfffc);
                return 1;
            }
            if ((seg & 0x0003) != CPL) {
                x86gpf("loadseg(): Stack segment RPL != CPL", seg & 0xfffc);
                return 1;
            }
            if (dpl != CPL) {
                x86gpf("loadseg(): Stack segment DPL != CPL", seg & 0xfffc);
                return 1;
            }
            switch ((segdat[2] >> 8) & 0x1f) {
                case 0x12:
                case 0x13:
                case 0x16:
                case 0x17:
                    /* R/W */
                    break;
                default:
                    x86gpf("loadseg(): Unknown stack segment type", seg & ~3);
                    return 1;
            }
            if (!(segdat[2] & 0x8000)) {
                x86ss(NULL, seg & 0xfffc);
                return 1;
            }
        } else if (s != &cpu_state.seg_cs) {
            x86seg_log("Seg data %04X %04X %04X %04X\n", segdat[0], segdat[1], segdat[2], segdat[3]);
            x86seg_log("Seg type %03X\n", segdat[2] & 0x1f00);
            switch ((segdat[2] >> 8) & 0x1f) {
                case 0x10:
                case 0x11:
                case 0x12:
                case 0x13: /* Data segments */
                case 0x14:
                case 0x15:
                case 0x16:
                case 0x17:
                case 0x1a:
                case 0x1b: /* Readable non-conforming code */
                    if ((seg & 0x0003) > dpl) {
                        x86gpf("loadseg(): Normal segment RPL > DPL", seg & 0xfffc);
                        return 1;
                    }
                    if ((CPL) > dpl) {
                        x86gpf("loadseg(): Normal segment DPL < CPL", seg & 0xfffc);
                        return 1;
                    }
                    break;
                case 0x1e:
                case 0x1f: /* Readable conforming code */
                    break;
                default:
                    x86gpf("loadseg(): Unknown normal segment type", seg & 0xfffc);
                    return 1;
            }
        }

        if (!(segdat[2] & 0x8000)) {
            x86np("Load data seg not present", seg & 0xfffc);
            return 1;
        }
        /* A page fault setting A must leave the old visible selector and
           descriptor cache intact, so complete that write before loading. */
        cpl_override = 1;
        x86seg_set_accessed(addr, segdat[2]); /* Set accessed bit */
        cpl_override = 0;
        if (cpu_state.abrt) {
            return 1;
        }
        s->seg = seg;
        do_seg_load(s, segdat);
        if (s == &cpu_state.seg_ss)
            set_stack32((segdat[3] & 0x40) ? 1 : 0);
        s->checked   = 0;
#ifdef USE_DYNAREC
        if (s == &cpu_state.seg_ds)
            codegen_flat_ds = 0;
        if (s == &cpu_state.seg_ss)
            codegen_flat_ss = 0;
        if (s == &cpu_state.seg_es)
            codegen_flat_es = 0;
#endif
    } else {
        s->access  = 0xe2;
        s->ar_high = 0x10;
        s->base    = seg << 4;
        s->seg     = seg;
        s->checked = 1;
        /* In V86 mode every segment load also resets the limit to 64K, unlike
           real mode where the cached limit is kept. Otherwise a task switch
           into a V86 task inherits stale protected-mode limits. */
        if (cpu_state.eflags & VM_FLAG)
            do_seg_v86_init(s);
#ifdef USE_DYNAREC
        if (s == &cpu_state.seg_ds)
            codegen_flat_ds = 0;
        if (s == &cpu_state.seg_ss)
            codegen_flat_ss = 0;
        if (s == &cpu_state.seg_es)
            codegen_flat_es = 0;
#endif
        if (s == &cpu_state.seg_ss && (cpu_state.eflags & VM_FLAG))
            set_stack32(0);
    }

    if (s == &cpu_state.seg_ds) {
        if (s->base == 0 && s->limit_low == 0 && s->limit_high == 0xffffffff)
            cpu_cur_status &= ~CPU_STATUS_NOTFLATDS;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATDS;
    }
    if (s == &cpu_state.seg_es) {
        if (s->base == 0 && s->limit_low == 0 && s->limit_high == 0xffffffff)
            cpu_cur_status &= ~CPU_STATUS_NOTFLATES;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATES;
    }
    if (s == &cpu_state.seg_ss) {
        if (s->base == 0 && s->limit_low == 0 && s->limit_high == 0xffffffff)
            cpu_cur_status &= ~CPU_STATUS_NOTFLATSS;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATSS;
    }

    return cpu_state.abrt;
}

void
#ifdef OPS_286_386
loadcs_2386(uint16_t seg)
#else
loadcs(uint16_t seg)
#endif
{
    uint16_t      segdat[4];
    uint32_t      addr;
    uint32_t     *segdat32 = (uint32_t *) segdat;
    const x86seg *dt;

    x86seg_log("Load CS %04X\n", seg);

    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG)) {
        if (!(seg & 0xfffc)) {
            x86gpf("loadcs(): Protected mode selector is zero", 0);
            return;
        }

        addr = seg & 0xfff8;
        dt   = (seg & 0x0004) ? &ldt : &gdt;
        if ((addr + 7) > dt->limit) {
            x86gpf("loadcs(): Protected mode selector > DT limit", seg & 0xfffc);
            return;
        }
        addr += dt->base;

        read_descriptor(addr, segdat, segdat32, 1);
        if (cpu_state.abrt)
            return;
        if (segdat[2] & 0x1000) {
            /* Normal code segment */
            if (!(segdat[2] & 0x0400)) {
                /* Not conforming */
                if ((seg & 3) > CPL) {
                    x86gpf("loadcs(): Non-conforming RPL > CPL", seg & 0xfffc);
                    return;
                }
                if (CPL != DPL) {
                    x86gpf("loadcs(): Non-conforming CPL != DPL", seg & 0xfffc);
                    return;
                }
            }
            if (CPL < DPL) {
                x86gpf("loadcs(): CPL < DPL", seg & ~3);
                return;
            }
            if (!(segdat[2] & 0x8000)) {
                x86np("Load CS not present", seg & 0xfffc);
                return;
            }
            set_use32(segdat[3] & 0x40);
            CS = (seg & 0xfffc) | CPL;
            do_seg_load(&cpu_state.seg_cs, segdat);
            use32 = (segdat[3] & 0x40) ? 0x300 : 0;
            if ((CPL == 3) && (oldcpl != 3))
                flushmmucache_nopc();
            oldcpl = CPL;

            cpl_override = 1;
            x86seg_set_accessed(addr, segdat[2]); /* Set accessed bit */
            cpl_override = 0;
        } else {
            /* System segment */
            switch (segdat[2] & 0x0f00) {
                default:
                    x86gpf("Load CS system segment has bits 0-3 of access rights set", seg & 0xfffc);
                    return;
            }
        }
    } else {
        cpu_state.seg_cs.base       = (seg << 4);
        cpu_state.seg_cs.limit      = 0xffff;
        cpu_state.seg_cs.limit_low  = 0;
        cpu_state.seg_cs.limit_high = 0xffff;
        cpu_state.seg_cs.seg        = seg & 0xffff;
        cpu_state.seg_cs.access     = (cpu_state.eflags & VM_FLAG) ? 0xe2 : 0x82;
        cpu_state.seg_cs.ar_high    = 0x10;
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
        oldcpl = CPL;
    }
}

/* JMP or CALL to a TSS, or through a task gate: the checks made before
   the task switch. On return seg and segdat name the TSS; 0 means a fault
   was raised. JMP raises #GP everywhere; CALL raises #TS on the 386,
   Intel486 and Pentium books and #GP on the 80286 and P6 books, but the
   386 PRM's CALL raises #GP(TSS selector) for a TSS descriptor called
   directly that is not an available TSS. */
static int
task_target_check(uint16_t *seg, uint16_t *segdat, uint32_t *segdat32, int call)
{
    uint16_t sel  = *seg;
    int      gate = 0;
    uint32_t addr;

#define TASK_FAULT(s, gp386)                                       \
    do {                                                           \
        if (call && is386 && !is_p6 && !((gp386) && !is486))       \
            x86ts(NULL, (s) & 0xfffc);                             \
        else                                                       \
            x86gpf(NULL, (s) & 0xfffc);                            \
        return 0;                                                  \
    } while (0)

    if ((DPL < CPL) || (DPL < (sel & 3)))
        TASK_FAULT(sel, 0);
    if ((segdat[2] & 0x1f00) == 0x0500) { /* Task gate */
        if (!(segdat[2] & 0x8000)) {
            x86np("Task gate not present", sel & 0xfffc);
            return 0;
        }
        gate = 1;
        sel  = segdat[1];
        addr = sel & 0xfff8;
        if (!(sel & 0xfffc) || (sel & 0x0004) || ((addr + 7) > gdt.limit))
            TASK_FAULT(sel, 0);
        read_descriptor(gdt.base + addr, segdat, segdat32, 1);
        if (cpu_state.abrt)
            return 0;
    } else if (sel & 0x0004) /* TSS descriptors live only in the GDT */
        TASK_FAULT(sel, 0);
    if (((segdat[2] & 0x1f00) != 0x0100) && (!is386 || ((segdat[2] & 0x1f00) != 0x0900)))
        TASK_FAULT(sel, !gate);
    if (!(segdat[2] & 0x8000)) {
        x86np("TSS not present", sel & 0xfffc);
        return 0;
    }
#undef TASK_FAULT
    *seg = sel;
    return 1;
}

void
#ifdef OPS_286_386
loadcsjmp_2386(uint16_t seg, uint32_t old_pc)
#else
loadcsjmp(uint16_t seg, uint32_t old_pc)
#endif
{
    uint16_t      type;
    uint16_t      seg2;
    uint16_t      segdat[4];
    uint32_t      addr;
    uint32_t      newpc;
    uint32_t     *segdat32 = (uint32_t *) segdat;
    const x86seg *dt;

    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG)) {
        if (!(seg & 0xfffc)) {
            x86gpf("loadcsjmp(): Selector is zero", 0);
            return;
        }
        addr = seg & 0xfff8;
        dt   = (seg & 0x0004) ? &ldt : &gdt;
        if ((addr + 7) > dt->limit) {
            x86gpf("loacsjmp(): Selector > DT limit", seg & 0xfffc);
            return;
        }
        addr += dt->base;
        read_descriptor(addr, segdat, segdat32, 1);
        if (cpu_state.abrt)
            return;
        x86seg_log("%04X %04X %04X %04X\n", segdat[0], segdat[1], segdat[2], segdat[3]);
        if (segdat[2] & 0x1000) {
            /* Normal code segment */
            if (!(segdat[2] & 0x0400)) {
                /* Not conforming */
                if ((seg & 0x0003) > CPL) {
                    x86gpf("loadcsjmp(): segment PL > CPL", seg & 0xfffc);
                    return;
                }
                if (CPL != DPL) {
                    x86gpf("loadcsjmp(): CPL != DPL", seg & 0xfffc);
                    return;
                }
            }
            if (CPL < DPL) {
                x86gpf("loadcsjmp(): CPL < DPL", seg & 0xfffc);
                return;
            }
            if (!(segdat[2] & 0x8000)) {
                x86np("Load CS JMP not present", seg & 0xfffc);
                return;
            }
            set_use32(segdat[3] & 0x0040);

            cpl_override = 1;
            x86seg_set_accessed(addr, segdat[2]); /* Set accessed bit */
            cpl_override = 0;

            CS        = (seg & 0xfffc) | CPL;
            segdat[2] = (segdat[2] & ~(3 << 13)) | (CPL << 13);

            do_seg_load(&cpu_state.seg_cs, segdat);
            if ((CPL == 3) && (oldcpl != 3))
                flushmmucache_nopc();
            oldcpl = CPL;
            cycles -= timing_jmp_pm;
        } else { /* System segment */
            /* Each type tests P after its own checks: a not-present
               descriptor of a type JMP can't use is #GP(selector) */
            type  = segdat[2] & 0x0f00;
            newpc = segdat[0];
            if (type & 0x0800)
                newpc |= (segdat[3] << 16);
            switch (type) {
                case 0x0400: /* Call gate */
                case 0x0c00:
                    cgate32 = (type & 0x0800);
                    cgate16 = !cgate32;
                    oldcs = CS;
                    /* oldpc stays at the JMP: a fault from here reports it */
                    if (DPL < CPL) {
                        x86gpf("loadcsjmp(): Call gate DPL < CPL", seg & 0xfffc);
                        return;
                    }
                    if (DPL < (seg & 0x0003)) {
                        x86gpf("loadcsjmp(): Call gate DPL< RPL", seg & ~3);
                        return;
                    }
                    if (!(segdat[2] & 0x8000)) {
                        x86np("Load CS JMP call gate not present", seg & 0xfffc);
                        return;
                    }
                    seg2 = segdat[1];

                    if (!(seg2 & 0xfffc)) {
                        x86gpf("Load CS JMP call gate selector is NULL", 0);
                        return;
                    }
                    addr = seg2 & 0xfff8;
                    dt   = (seg2 & 0x0004) ? &ldt : &gdt;
                    if ((addr + 7) > dt->limit) {
                        x86gpf("loadcsjmp(): Call gate selector > DT limit", seg2 & 0xfffc);
                        return;
                    }
                    addr += dt->base;
                    read_descriptor(addr, segdat, segdat32, 1);
                    if (cpu_state.abrt)
                        return;

                    if (DPL > CPL) {
                        x86gpf("loadcsjmp(): ex DPL > CPL", seg2 & 0xfffc);
                        return;
                    }
                    if (!(segdat[2] & 0x8000)) {
                        x86np("Load CS JMP from call gate not present", seg2 & 0xfffc);
                        return;
                    }

                    switch (segdat[2] & 0x1f00) {
                        case 0x1800:
                        case 0x1900:
                        case 0x1a00:
                        case 0x1b00: /* Non-conforming code */
                            /* A JMP never changes privilege: the target must be at CPL */
                            if (DPL != CPL) {
                                x86gpf("loadcsjmp(): Non-conforming DPL != CPL", seg2 & 0xfffc);
                                return;
                            }
                            fallthrough;
                        case 0x1c00:
                        case 0x1d00:
                        case 0x1e00:
                        case 0x1f00: /* Conforming */
                            {
                                int jmp_cpl = CPL;

                                /* CPL stays, and CS.RPL is set to it */
                                CS = (seg2 & 0xfffc) | jmp_cpl;
                                do_seg_load(&cpu_state.seg_cs, segdat);
                                cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | (jmp_cpl << 5);
                            }
                            if ((CPL == 3) && (oldcpl != 3))
                                flushmmucache_nopc();
                            oldcpl = CPL;
                            set_use32(segdat[3] & 0x40);
                            cpu_state.pc = newpc;

                            cpl_override = 1;
                            x86seg_set_accessed(addr, segdat[2]); /* Set accessed bit */
                            cpl_override = 0;
                            break;

                        default:
                            x86gpf("loadcsjmp(): Unknown type", seg2 & 0xfffc);
                            return;
                    }
                    cycles -= timing_jmp_pm_gate;
                    break;

                case 0x100: /* 286 TSS */
                case 0x900: /* 386 TSS */
                case 0x500: /* Task gate */
                    if (!task_target_check(&seg, segdat, segdat32, 0))
                        return;
                    cpu_state.pc = old_pc;
                    optype       = JMP;
                    cpl_override = 1;
                    /* NT comes from the new task's EFLAGS image */
                    op_taskswitch286(seg, segdat, segdat[2] & 0x800);
                    cpl_override = 0;
                    return;

                default:
                    x86gpf("Load CS JMP system selector unknown type", seg & 0xfffc);
                    return;
            }
        }
    } else {
        cpu_state.seg_cs.base       = seg << 4;
        cpu_state.seg_cs.limit      = 0xffff;
        cpu_state.seg_cs.limit_low  = 0;
        cpu_state.seg_cs.limit_high = 0xffff;
        cpu_state.seg_cs.seg        = seg;
        cpu_state.seg_cs.access     = (cpu_state.eflags & VM_FLAG) ? 0xe2 : 0x82;
        cpu_state.seg_cs.ar_high    = 0x10;
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
        oldcpl = CPL;
        cycles -= timing_jmp_rm;
    }
}

static inline void
PUSHW(uint16_t v)
{
    if (stack32) {
        writememw(ss, ESP - 2, v);
        if (cpu_state.abrt)
            return;
        ESP -= 2;
    } else {
        writememw(ss, ((SP - 2) & 0xffff), v);
        if (cpu_state.abrt)
            return;
        SP -= 2;
    }
}

static inline void
PUSHL(uint32_t v)
{
    if (cpu_16bitbus) {
        PUSHW(v >> 16);
        PUSHW(v & 0xffff);
    } else {
        if (stack32) {
            writememl(ss, ESP - 4, v);
            if (cpu_state.abrt)
                return;
            ESP -= 4;
        } else {
            writememl(ss, ((SP - 4) & 0xffff), v);
            if (cpu_state.abrt)
                return;
            SP -= 4;
        }
    }
}

static inline void
PUSHL_SEL(uint32_t v)
{
    if (cpu_16bitbus) {
        PUSHW(v >> 16);
        PUSHW(v & 0xffff);
    } else {
        if (stack32) {
            writememw(ss, ESP - 4, v);
            if (cpu_state.abrt)
                return;
            ESP -= 4;
        } else {
            writememw(ss, ((SP - 4) & 0xffff), v);
            if (cpu_state.abrt)
                return;
            SP -= 4;
        }
    }
}

static inline uint16_t
POPW(void)
{
    uint16_t tempw;
    if (stack32) {
        tempw = readmemw(ss, ESP);
        if (cpu_state.abrt)
            return 0;
        ESP += 2;
    } else {
        tempw = readmemw(ss, SP);
        if (cpu_state.abrt)
            return 0;
        SP += 2;
    }
    return tempw;
}

static inline uint32_t
POPL(void)
{
    uint32_t templ;

    if (cpu_16bitbus) {
        templ = POPW();
        templ |= (POPW() << 16);
    } else {
        if (stack32) {
            templ = readmeml(ss, ESP);
            if (cpu_state.abrt)
                return 0;
            ESP += 4;
        } else {
            templ = readmeml(ss, SP);
            if (cpu_state.abrt)
                return 0;
            SP += 4;
        }
    }

    return templ;
}

#ifdef OPS_286_386
void
loadcscall_2386(uint16_t seg, uint32_t old_pc)
#else
void
loadcscall(uint16_t seg, uint32_t old_pc)
#endif
{
    uint16_t      seg2;
    uint16_t      newss;
    uint16_t      segdat[4];
    uint16_t      segdat2[4];
    uint32_t      addr;
    uint32_t      oldssbase = ss;
    uint32_t      oaddr;
    uint32_t      newpc;
    uint32_t     *segdat32  = (uint32_t *) segdat;
    uint32_t     *segdat232 = (uint32_t *) segdat2;
    int           count;
    int           type;
    uint32_t      oldss;
    uint32_t      oldsp;
    uint32_t      newsp;
    uint32_t      oldsp2;
    uint32_t      oldss_limit_high = cpu_state.seg_ss.limit_high;
    const x86seg *dt;

    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG)) {
        x86seg_log("Protected mode CS load! %04X\n", seg);
        if (!(seg & 0xfffc)) {
            x86gpf("loadcscall(): Protected mode selector is zero", 0);
            return;
        }
        addr = seg & 0xfff8;
        dt   = (seg & 0x0004) ? &ldt : &gdt;
        if ((addr + 7) > dt->limit) {
            x86gpf("loadcscall(): Selector > DT limit", seg & 0xfffc);
            return;
        }
        addr += dt->base;
        read_descriptor(addr, segdat, segdat32, 1);
        if (cpu_state.abrt)
            return;
        type  = segdat[2] & 0x0f00;
        newpc = segdat[0];
        if (type & 0x0800)
            newpc |= segdat[3] << 16;

        x86seg_log("Code seg call - %04X - %04X %04X %04X\n", seg, segdat[0], segdat[1], segdat[2]);
        if (segdat[2] & 0x1000) {
            if (!(segdat[2] & 0x0400)) { /* Not conforming */
                if ((seg & 0x0003) > CPL) {
                    x86gpf("loadcscall(): Non-conforming RPL > CPL", seg & 0xfffc);
                    return;
                }
                if (CPL != DPL) {
                    x86gpf("loadcscall(): Non-conforming CPL != DPL", seg & 0xfffc);
                    return;
                }
            }
            if (CPL < DPL) {
                x86gpf("loadcscall(): CPL < DPL", seg & 0xfffc);
                return;
            }
            if (!(segdat[2] & 0x8000)) {
                x86np("Load CS call not present", seg & 0xfffc);
                return;
            }
            set_use32(segdat[3] & 0x0040);

            cpl_override = 1;
            x86seg_set_accessed(addr, segdat[2]); /* Set accessed bit */
            cpl_override = 0;

            /* Conforming segments don't change CPL, so preserve existing CPL */
            if (segdat[2] & 0x0400) {
                seg       = (seg & 0xfffc) | CPL;
                segdat[2] = (segdat[2] & ~(3 << (5 + 8))) | (CPL << (5 + 8));
            } else /* On non-conforming segments, set RPL = CPL */
                seg = (seg & 0xfffc) | CPL;
            CS = seg;
            do_seg_load(&cpu_state.seg_cs, segdat);
            if ((CPL == 3) && (oldcpl != 3))
                flushmmucache_nopc();
            oldcpl = CPL;
#ifdef ENABLE_X86SEG_LOG
            x86seg_log("Complete\n");
#endif
            cycles -= timing_call_pm;
        } else {
            type = segdat[2] & 0x0f00;
            x86seg_log("Type %03X\n", type);
            switch (type) {
                case 0x0400: /* Call gate */
                case 0x0c00: /* 386 Call gate */
                    x86seg_log("Callgate %08X\n", cpu_state.pc);
                    cgate32 = (type & 0x0800);
                    cgate16 = !cgate32;
                    oldcs = CS;
                    count = segdat[2] & 0x001f;
                    if (DPL < CPL) {
                        x86gpf("loadcscall(): ex DPL < CPL", seg & 0xfffc);
                        return;
                    }
                    if (DPL < (seg & 0x0003)) {
                        x86gpf("loadcscall(): ex DPL < RPL", seg & 0xfffc);
                        return;
                    }
                    if (!(segdat[2] & 0x8000)) {
                        x86np("Call gate not present", seg & 0xfffc);
                        return;
                    }
                    seg2 = segdat[1];

                    x86seg_log("New address : %04X:%08X\n", seg2, newpc);

                    if (!(seg2 & 0xfffc)) {
                        x86gpf("loadcscall(): ex selector is NULL", 0);
                        return;
                    }
                    addr = seg2 & 0xfff8;
                    dt   = (seg2 & 0x0004) ? &ldt : &gdt;
                    if ((addr + 7) > dt->limit) {
                        x86gpf("loadcscall(): ex Selector > DT limit", seg2 & 0xfffc);
                        return;
                    }
                    addr += dt->base;
                    read_descriptor(addr, segdat, segdat32, 1);
                    if (cpu_state.abrt)
                        return;

                    x86seg_log("Code seg2 call - %04X - %04X %04X %04X\n", seg2, segdat[0], segdat[1], segdat[2]);

                    if (DPL > CPL) {
                        x86gpf("loadcscall(): ex DPL > CPL", seg2 & 0xfffc);
                        return;
                    }
                    if (!(segdat[2] & 0x8000)) {
                        x86seg_log("Call gate CS not present %04X\n", seg2);
                        x86np("Call gate CS not present", seg2 & 0xfffc);
                        return;
                    }

                    switch (segdat[2] & 0x1f00) {
                        case 0x1800:
                        case 0x1900:
                        case 0x1a00:
                        case 0x1b00: /* Non-conforming code */
                            if (DPL < CPL) {
                                uint16_t oldcs = CS;
                                oaddr = addr;
                                /* Load new stack */
                                oldss = SS;
                                oldsp = oldsp2 = ESP;
                                /* SS:ESP for the new level must lie inside the TSS */
                                if ((tr.access & 8) ? ((4 + (DPL << 3) + 7) > tr.limit) : ((2 + (DPL << 2) + 3) > tr.limit)) {
                                    x86ts("Call gate: new stack past the TSS limit", tr.seg & 0xfffc);
                                    return;
                                }
                                cpl_override   = 1;
                                if (tr.access & 8) {
                                    addr  = 4 + tr.base + (DPL << 3);
                                    newss = readmemw(0, addr + 4);
                                    if (cpu_16bitbus) {
                                        newsp = readmemw(0, addr);
                                        newsp |= (readmemw(0, addr + 2) << 16);
                                    } else
                                        newsp = readmeml(0, addr);
                                } else {
                                    addr  = 2 + tr.base + (DPL * 4);
                                    newss = readmemw(0, addr + 2);
                                    newsp = readmemw(0, addr);
                                }
                                cpl_override = 0;
                                if (cpu_state.abrt)
                                    return;
                                x86seg_log("New stack %04X:%08X\n", newss, newsp);
                                if (!(newss & 0xfffc)) {
                                    x86ts(NULL, newss & 0xfffc);
                                    return;
                                }
                                addr = newss & 0xfff8;
                                dt   = (newss & 0x0004) ? &ldt : &gdt;
                                if ((addr + 7) > dt->limit) {
                                    x86ts("Call gate SS beyond DT limit", newss & 0xfffc);
                                    return;
                                }
                                addr += dt->base;
                                x86seg_log("Read stack seg\n");
                                read_descriptor(addr, segdat2, segdat232, 1);
                                if (cpu_state.abrt)
                                    return;
                                x86seg_log("Read stack seg done!\n");
                                if (((newss & 0x0003) != DPL) || (DPL2 != DPL)) {
                                    x86ts(NULL, newss & 0xfffc);
                                    return;
                                }
                                if ((segdat2[2] & 0x1a00) != 0x1200) {
                                    x86ts("Call gate loading SS unknown type", newss & 0xfffc);
                                    return;
                                }
                                if (!(segdat2[2] & 0x8000)) {
                                    x86ss("Call gate loading SS not present", newss & 0xfffc);
                                    return;
                                }
                                if (!stack32)
                                    oldsp &= 0xffff;
                                SS = newss;
                                set_stack32((segdat2[3] & 0x0040) ? 1 : 0);
                                if (stack32)
                                    ESP = newsp;
                                else
                                    SP = newsp;

                                do_seg_load(&cpu_state.seg_ss, segdat2);

                                x86seg_log("Set access 1\n");
                                cpl_override = 1;
                                x86seg_set_accessed(addr, segdat2[2]); /* Set accessed bit */
                                cpl_override = 0;

                                CS = (seg2 & ~3) | DPL;
                                do_seg_load(&cpu_state.seg_cs, segdat);
                                if ((CPL == 3) && (oldcpl != 3))
                                    flushmmucache_nopc();
                                oldcpl = CPL;
                                set_use32(segdat[3] & 0x0040);
                                cpu_state.pc = newpc;

                                x86seg_log("Set access 2\n");

                                cpl_override = 1;
                                x86seg_set_accessed(oaddr, segdat[2]); /* Set accessed bit */
                                cpl_override = 0;

                                x86seg_log("Type %04X\n", type);
                                if (type == 0x0c00) {
                                    is586 ? PUSHL(oldss) : PUSHL_SEL(oldss);
                                    PUSHL(oldsp2);
                                    if (cpu_state.abrt) {
                                        SS  = oldss;
                                        ESP = oldsp2;
                                        CS = oldcs;
                                        return;
                                    }
                                    if (count) {
                                        while (count--) {
                                            uint32_t temp_val;
                                            switch (oldss_limit_high - oldsp - (count << 2)) {
                                                default:
                                                case 3:
                                                    /* We are at least an entire DWORD away from the limit,
                                                       read long. */
                                                    PUSHL(readmeml(oldssbase, oldsp + (count << 2)));
                                                    break;
                                                case 2:
                                                    /* We are 3 bytes away from the limit,
                                                       read word + byte. */
                                                    temp_val = readmemw(oldssbase, oldsp + (count << 2));
                                                    temp_val |= (readmemb(oldssbase, oldsp +
                                                                          (count << 2) + 2) << 16);
                                                    PUSHL(temp_val);
                                                    break;
                                                case 1:
                                                    /* We are a WORD away from the limit, read word. */
                                                    PUSHL(readmemw(oldssbase, oldsp + (count << 2)));
                                                    break;
                                                case 0:
                                                    /* We are a BYTE away from the limit, read byte. */
                                                    PUSHL(readmemb(oldssbase, oldsp + (count << 2)));
                                                    break;
                                            }
                                            if (cpu_state.abrt) {
                                                SS  = oldss;
                                                ESP = oldsp2;
                                                CS = oldcs;
                                                return;
                                            }
                                        }
                                    }
                                } else {
                                    x86seg_log("Stack %04X\n", SP);
                                    PUSHW(oldss);
                                    x86seg_log("Write SS to %04X:%04X\n", SS, SP);
                                    PUSHW(oldsp2);
                                    if (cpu_state.abrt) {
                                        SS  = oldss;
                                        ESP = oldsp2;
                                        CS = oldcs;
                                        return;
                                    }
                                    x86seg_log("Write SP to %04X:%04X\n", SS, SP);
                                    if (count) {
                                        while (count--) {
                                            switch (oldss_limit_high - (oldsp & 0xffff) - (count << 1)) {
                                                default:
                                                case 1:
                                                    /* We are at least an entire WORD away from the limit,
                                                       read word. */
                                                    PUSHW(readmemw(oldssbase, (oldsp & 0xffff) +
                                                          (count << 1)));
                                                    break;
                                                case 0:
                                                    /* We are a BYTE away from the limit, read byte. */
                                                    PUSHW(readmemb(oldssbase, (oldsp & 0xffff) +
                                                          (count << 1)));
                                                    break;
                                            }
                                            if (cpu_state.abrt) {
                                                SS  = oldss;
                                                ESP = oldsp2;
                                                CS = oldcs;
                                                return;
                                            }
                                        }
                                    }
                                }
                                cycles -= timing_call_pm_gate_inner;
                                break;
                            } else if (DPL > CPL) {
                                x86gpf("loadcscall(): Call PM Gate Inner DPL > CPL", seg2 & 0xfffc);
                                return;
                            }
                            fallthrough;
                        case 0x1c00:
                        case 0x1d00:
                        case 0x1e00:
                        case 0x1f00: /* Conforming */
                            {
                                int call_cpl = CPL;

                                /* Same privilege: CPL stays (a conforming segment's
                                   DPL doesn't raise it), CS.RPL is set to it */
                                CS = (seg2 & ~3) | call_cpl;
                                do_seg_load(&cpu_state.seg_cs, segdat);
                                cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | (call_cpl << 5);
                            }
                            if ((CPL == 3) && (oldcpl != 3))
                                flushmmucache_nopc();
                            oldcpl = CPL;
                            set_use32(segdat[3] & 0x0040);
                            cpu_state.pc = newpc;

                            cpl_override = 1;
                            x86seg_set_accessed(addr, segdat[2]); /* Set accessed bit */
                            cpl_override = 0;
                            cycles -= timing_call_pm_gate;
                            break;

                        default:
                            x86gpf("loadcscall(): Unknown subtype", seg2 & 0xfffc);
                            return;
                    }
                    break;

                case 0x0100: /* 286 TSS */
                case 0x0900: /* 386 TSS */
                case 0x0500: /* Task gate */
                    if (!task_target_check(&seg, segdat, segdat32, 1))
                        return;
                    cpu_state.pc = old_pc;
                    /* A task switch links the tasks through the TSS back-link and NT,
                       it does not push a return address on the new task's stack. */
                    cgate_task   = 1;
                    cpl_override = 1;
                    op_taskswitch286(seg, segdat, segdat[2] & 0x0800);
                    cpl_override = 0;
                    break;

                default:
                    x86gpf("loadcscall(): Unknown type", seg & 0xfffc);
                    return;
            }
        }
    } else {
        cpu_state.seg_cs.base       = seg << 4;
        cpu_state.seg_cs.limit      = 0xffff;
        cpu_state.seg_cs.limit_low  = 0;
        cpu_state.seg_cs.limit_high = 0xffff;
        cpu_state.seg_cs.seg        = seg;
        cpu_state.seg_cs.access     = (cpu_state.eflags & VM_FLAG) ? 0xe2 : 0x82;
        cpu_state.seg_cs.ar_high    = 0x10;
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
        oldcpl = CPL;
    }
}

void
#ifdef OPS_286_386
pmoderetf_2386(int is32, uint16_t off)
#else
pmoderetf(int is32, uint16_t off)
#endif
{
    uint16_t      segdat[4];
    uint16_t      segdat2[4];
    uint16_t      seg;
    uint16_t      newss;
    uint32_t      newpc;
    uint32_t      newsp;
    uint32_t      addr;
    uint32_t      oaddr;
    uint32_t      oldsp     = ESP;
    uint32_t     *segdat32  = (uint32_t *) segdat;
    uint32_t     *segdat232 = (uint32_t *) segdat2;
    const x86seg *dt;

    x86seg_log("RETF %i %04X:%04X  %08X %04X\n", is32, CS, cpu_state.pc, cr0, cpu_state.eflags);
    if (is32) {
        newpc = POPL();
        seg   = POPL();
    } else {
        x86seg_log("PC read from %04X:%04X\n", SS, SP);
        newpc = POPW();
        x86seg_log("CS read from %04X:%04X\n", SS, SP);
        seg = POPW();
    }
    if (cpu_state.abrt) {
        ESP = oldsp; /* a faulting pop leaves ESP as it was */
        return;
    }

    x86seg_log("Return to %04X:%08X\n", seg, newpc);
    if ((seg & 0x0003) < CPL) {
        ESP = oldsp;
        x86gpf("pmoderetf(): seg < CPL", seg & 0xfffc);
        return;
    }
    if (!(seg & 0xfffc)) {
        ESP = oldsp;
        x86gpf("pmoderetf(): seg is NULL", 0);
        return;
    }
    addr = seg & 0xfff8;
    dt   = (seg & 0x0004) ? &ldt : &gdt;
    if ((addr + 7) > dt->limit) {
        ESP = oldsp;
        x86gpf("pmoderetf(): Selector > DT limit", seg & 0xfffc);
        return;
    }
    addr += dt->base;
    read_descriptor(addr, segdat, segdat32, 1);
    if (cpu_state.abrt) {
        ESP = oldsp;
        return;
    }
    oaddr = addr;

    x86seg_log("CPL %i RPL %i %i\n", CPL, seg & 0x0003, is32);

    if (stack32)
        ESP += off;
    else
        SP += off;

    if (CPL == (seg & 0x0003)) {
        x86seg_log("RETF CPL = RPL  %04X\n", segdat[2]);
        switch (segdat[2] & 0x1f00) {
            case 0x1000:
            case 0x1100:
            case 0x1200:
            case 0x1300:
                 /* Data segment, apparently valid when CPL is the same, used by MS LINK for DOS. */
                 fallthrough;
            case 0x1800:
            case 0x1900:
            case 0x1a00:
            case 0x1b00: /* Non-conforming */
                if (CPL != DPL) {
                    ESP = oldsp;
                    x86gpf("pmoderetf(): Non-conforming CPL != DPL", seg & 0xfffc);
                    return;
                }
                break;
            case 0x1c00:
            case 0x1d00:
            case 0x1e00:
            case 0x1f00: /* Conforming */
                if (CPL < DPL) {
                    ESP = oldsp;
                    x86gpf("pmoderetf(): Conforming CPL < DPL", seg & 0xfffc);
                    return;
                }
                break;
            default:
                ESP = oldsp;
                x86gpf("pmoderetf(): Unknown type", seg & 0xfffc);
                return;
        }
        if (!(segdat[2] & 0x8000)) {
            ESP = oldsp;
            x86np("RETF CS not present", seg & 0xfffc);
            return;
        }

        cpl_override = 1;
        x86seg_set_accessed(addr, segdat[2]); /* Set accessed bit */
        cpl_override = 0;

        cpu_state.pc = newpc;
        if (segdat[2] & 0x0400)
            segdat[2] = (segdat[2] & ~(3 << 13)) | ((seg & 3) << 13);
        CS = seg;
        do_seg_load(&cpu_state.seg_cs, segdat);
        cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~(3 << 5)) | ((CS & 3) << 5);
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
        oldcpl = CPL;
        set_use32(segdat[3] & 0x0040);

        cycles -= timing_retf_pm;
    } else {
        switch (segdat[2] & 0x1f00) {
            case 0x1000:
            case 0x1100:
            case 0x1200:
            case 0x1300:
                 /* Data segment, apparently valid when CPL is the same, used by MS LINK for DOS. */
                 fallthrough;
            case 0x1800:
            case 0x1900:
            case 0x1a00:
            case 0x1b00: /* Non-conforming */
                if ((seg & 0x0003) != DPL) {
                    ESP = oldsp;
                    x86gpf("pmoderetf(): Non-conforming RPL != DPL", seg & 0xfffc);
                    return;
                }
                x86seg_log("RETF non-conforming, %i %i\n", seg & 0x0003, DPL);
                break;
            case 0x1c00:
            case 0x1d00:
            case 0x1e00:
            case 0x1f00: /* Conforming */
                if ((seg & 0x0003) < DPL) {
                    ESP = oldsp;
                    x86gpf("pmoderetf(): Conforming RPL < DPL", seg & 0xfffc);
                    return;
                }
                x86seg_log("RETF conforming, %i %i\n", seg & 0x0003, DPL);
                break;
            default:
                ESP = oldsp;
                x86gpf("pmoderetf(): Unknown type", seg & 0xfffc);
                return;
        }
        if (!(segdat[2] & 0x8000)) {
            ESP = oldsp;
            x86np("RETF CS not present", seg & 0xfffc);
            return;
        }
        if (is32) {
            newsp = POPL();
            newss = POPL();
            if (cpu_state.abrt) {
                ESP = oldsp; /* the imm16 and the pops undone */
                return;
            }
        } else {
            x86seg_log("SP read from %04X:%04X\n", SS, SP);
            newsp = POPW();
            x86seg_log("SS read from %04X:%04X\n", SS, SP);
            newss = POPW();
            if (cpu_state.abrt) {
                ESP = oldsp; /* the imm16 and the pops undone */
                return;
            }
        }
        x86seg_log("Read new stack : %04X:%04X (%08X)\n", newss, newsp, ldt.base);
        if (!(newss & 0xfffc)) {
            ESP = oldsp;
            x86gpf("pmoderetf(): New SS selector is zero", newss & ~3);
            return;
        }
        addr = newss & 0xfff8;
        dt   = (newss & 0x0004) ? &ldt : &gdt;
        if ((addr + 7) > dt->limit) {
            ESP = oldsp;
            x86gpf("pmoderetf(): New SS selector > DT limit", newss & 0xfffc);
            return;
        }
        addr += dt->base;
        read_descriptor(addr, segdat2, segdat232, 1);
        if (cpu_state.abrt) {
            ESP = oldsp;
            return;
        }
        x86seg_log("Segment data %04X %04X %04X %04X\n", segdat2[0], segdat2[1], segdat2[2], segdat2[3]);
        if ((newss & 0x0003) != (seg & 0x0003)) {
            ESP = oldsp;
            x86gpf("pmoderetf(): New SS RPL > CS RPL", newss & 0xfffc);
            return;
        }
        if ((segdat2[2] & 0x1a00) != 0x1200) {
            ESP = oldsp;
            x86gpf("pmoderetf(): New SS unknown type", newss & 0xfffc);
            return;
        }
        if (DPL2 != (seg & 3)) {
            ESP = oldsp;
            x86gpf("pmoderetf(): New SS DPL != CS RPL", newss & 0xfffc);
            return;
        }
        /* A not-present stack segment is a stack fault (386 Table 6-2,
           P6 RET pseudocode) */
        if (!(segdat2[2] & 0x8000)) {
            ESP = oldsp;
            x86ss("RETF loading SS not present", newss & 0xfffc);
            return;
        }
        SS = newss;
        set_stack32((segdat2[3] & 0x0040) ? 1 : 0);
        if (stack32)
            ESP = newsp;
        else
            SP = newsp;
        do_seg_load(&cpu_state.seg_ss, segdat2);

        cpl_override = 1;
        x86seg_set_accessed(addr, segdat2[2]); /* Set accessed bit */
        x86seg_set_accessed(oaddr, segdat[2]); /* Set accessed bit */
        cpl_override = 0;
        /* Conforming segments don't change CPL, so CPL = RPL */
        if (segdat[2] & 0x0400)
            segdat[2] = (segdat[2] & ~(3 << 13)) | ((seg & 3) << 13);
        cpu_state.pc = newpc;
        CS           = seg;
        do_seg_load(&cpu_state.seg_cs, segdat);
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
        oldcpl = CPL;
        set_use32(segdat[3] & 0x0040);

        if (stack32)
            ESP += off;
        else
            SP += off;

        check_seg_valid(&cpu_state.seg_ds);
        check_seg_valid(&cpu_state.seg_es);
        check_seg_valid(&cpu_state.seg_fs);
        check_seg_valid(&cpu_state.seg_gs);
        cycles -= timing_retf_pm_outer;
    }
}

void
#ifdef OPS_286_386
pmodeint_2386(int num, int soft)
#else
pmodeint(int num, int soft)
#endif
{
    uint16_t      segdat[4];
    uint16_t      segdat2[4];
    uint16_t      segdat3[4];
    uint16_t      newss;
    uint16_t      seg = 0;
    int           type;
    int           new_cpl;
    uint32_t      addr;
    uint32_t      oaddr;
    uint32_t      oldss;
    uint32_t      oldsp;
    uint32_t      newsp;
    uint32_t     *segdat32  = (uint32_t *) segdat;
    uint32_t     *segdat232 = (uint32_t *) segdat2;
    uint32_t     *segdat332 = (uint32_t *) segdat3;
    const x86seg *dt;

    if ((cpu_state.eflags & VM_FLAG) && (IOPL != 3) && soft && !x86_int_sw_iopl_exempt) {
        x86seg_log("V86 banned int\n");
        x86gpf("pmodeint(): V86 banned int", 0);
        return;
    }
    addr = (num << 3);
    if ((addr + 7) > idt.limit) {
        if (num == 0x08) {
            /* Triple fault - reset! */
            softresetx86();
            cpu_set_edx();
        } else if (num == 0x0d) {
            op_pmodeint(8, 0);
            abrt_error = 0; /* the #DF's error code, pushed by x86_doabrt */
        } else
            x86gpf("pmodeint(): Vector > IDT limit", (num << 3) + 2 + !soft);
        x86seg_log("addr >= IDT.limit\n");
        return;
    }
    addr += idt.base;
    read_descriptor(addr, segdat, segdat32, 1);
    if (cpu_state.abrt) {
        x86seg_log("Abrt reading from %08X\n", addr);
        return;
    }
    oaddr = addr;

    x86seg_log("Addr %08X seg %04X %04X %04X %04X\n", addr, segdat[0], segdat[1], segdat[2], segdat[3]);
    if (!(segdat[2] & 0x1f00)) {
        /* This fires on all V86 interrupts in EMM386. Mark as expected to prevent code churn */
        if (cpu_state.eflags & VM_FLAG)
            x86gpf_expected("pmodeint(): Expected vector descriptor with bad type", (num << 3) + 2 + !soft);
        else
            x86gpf("pmodeint(): Vector descriptor with bad type", (num << 3) + 2 + !soft);
        return;
    }
    if ((DPL < CPL) && soft) {
        x86gpf("pmodeint(): Vector DPL < CPL", (num << 3) + 2);
        return;
    }
    type = segdat[2] & 0x1f00;
    if (((type == 0x0e00) || (type == 0x0f00)) && !is386) {
        x86gpf("pmodeint(): Gate type illegal on 286", (num << 3) + 2 + !soft);
        return;
    }
    switch (type) {
        case 0x0600:
        case 0x0700:
        case 0x0e00:
        case 0x0f00: /* Interrupt and trap gates */
            intgatesize = (type >= 0x0800) ? 32 : 16;
            if (!(segdat[2] & 0x8000)) {
                x86np("Int gate not present", (num << 3) + 2 + !soft);
                return;
            }
            seg     = segdat[1];
            new_cpl = seg & 0x0003;

            addr = seg & 0xfff8;
            dt   = (seg & 0x0004) ? &ldt : &gdt;
            if ((addr + 7) > dt->limit) {
                x86gpf("pmodeint(): Interrupt or trap gate selector > DT limit", (seg & 0xfffc) + !soft);
                return;
            }
            addr += dt->base;
            read_descriptor(addr, segdat2, segdat232, 1);
            if (cpu_state.abrt)
                return;
            oaddr = addr;
            /* The new CPL is the code segment's DPL (a same-level or conforming
               target keeps CPL, below), never the gate selector's RPL */
            new_cpl = DPL2;

            if (DPL2 > CPL) {
                x86gpf("pmodeint(): Interrupt or trap gate DPL > CPL", (seg & 0xfffc) + !soft);
                return;
            }
            switch (segdat2[2] & 0x1f00) {
                case 0x1000:
                case 0x1100:
                case 0x1200:
                case 0x1300:
                     /* Data segment, apparently valid when CPL is the same, used by MS CodeView for DOS. */
                     fallthrough;
                case 0x1800:
                case 0x1900:
                case 0x1a00:
                case 0x1b00: /* Non-conforming */
                    if (DPL2 < CPL) {
                        if (!(segdat2[2] & 0x8000)) {
                            x86np("Int gate CS not present", (segdat[1] & 0xfffc) + !soft);
                            return;
                        }
                        if ((cpu_state.eflags & VM_FLAG) && DPL2) {
                            x86gpf("pmodeint(): Interrupt or trap gate non-zero DPL in V86 mode", (segdat[1] & 0xfffc) + !soft);
                            return;
                        }
                        /* Load new stack */
                        oldss        = SS;
                        oldsp        = ESP;
                        /* SS:ESP for the new level must lie inside the TSS */
                        if ((tr.access & 8) ? ((4 + (DPL2 << 3) + 7) > tr.limit) : ((2 + (DPL2 << 2) + 3) > tr.limit)) {
                            x86ts("pmodeint(): new stack past the TSS limit", (tr.seg & 0xfffc) + !soft);
                            return;
                        }
                        cpl_override = 1;
                        if (tr.access & 8) {
                            addr  = 4 + tr.base + (DPL2 << 3);
                            newss = readmemw(0, addr + 4);
                            newsp = readmeml(0, addr);
                        } else {
                            addr  = 2 + tr.base + (DPL2 << 2);
                            newss = readmemw(0, addr + 2);
                            newsp = readmemw(0, addr);
                        }
                        cpl_override = 0;
                        /* The new stack from the TSS is checked as the TSS's:
                           #TS for a null, out-of-table, wrong-privilege or
                           wrong-type SS, #SS only for one not present
                           (Pentium Vol. 3, INT n, inter-privilege-level
                           interrupt). The null SS's error code is the EXT
                           bit alone, 0 for a software interrupt. */
                        if (!(newss & 0xfffc)) {
                            x86ts("pmodeint(): Interrupt or trap gate stack segment is NULL", !soft);
                            return;
                        }
                        addr = newss & 0xfff8;
                        dt   = (newss & 0x0004) ? &ldt : &gdt;
                        if ((addr + 7) > dt->limit) {
                            x86ts("pmodeint(): Interrupt or trap gate stack segment > DT", (newss & 0xfffc) + !soft);
                            return;
                        }
                        addr += dt->base;
                        read_descriptor(addr, segdat3, segdat332, 1);
                        if (cpu_state.abrt)
                            return;
                        if ((newss & 3) != DPL2) {
                            x86ts("pmodeint(): Interrupt or trap gate stack segment RPL != DPL", (newss & 0xfffc) + !soft);
                            return;
                        }
                        if (DPL3 != DPL2) {
                            x86ts("pmodeint(): Interrupt or trap gate stack segment DPL != DPL", (newss & 0xfffc) + !soft);
                            return;
                        }
                        if ((segdat3[2] & 0x1a00) != 0x1200) {
                            x86ts("pmodeint(): Interrupt or trap gate stack segment bad type", (newss & 0xfffc) + !soft);
                            return;
                        }
                        if (!(segdat3[2] & 0x8000)) {
                            x86ss("Int gate loading SS not present", (newss & 0xfffc) + !soft);
                            return;
                        }
                        SS = newss;
                        set_stack32((segdat3[3] & 0x0040) ? 1 : 0);
                        if (stack32)
                            ESP = newsp;
                        else
                            SP = newsp;
                        do_seg_load(&cpu_state.seg_ss, segdat3);

                        cpl_override = 1;
                        x86seg_set_accessed(addr, segdat3[2]); /* Set accessed bit */
                        cpl_override = 0;

                        x86seg_log("New stack %04X:%08X\n", SS, ESP);
                        cpl_override = 1;
                        if (type >= 0x0800) {
                            if (cpu_state.eflags & VM_FLAG) {
                                if (is586) {
                                    PUSHL(GS);
                                    PUSHL(FS);
                                    PUSHL(DS);
                                    PUSHL(ES);
                                } else {
                                    PUSHL_SEL(GS);
                                    PUSHL_SEL(FS);
                                    PUSHL_SEL(DS);
                                    PUSHL_SEL(ES);
                                }
                                if (cpu_state.abrt)
                                    return;
                                /* Null selectors, as protected mode loads them
                                   (VM is still set, so loadseg would take its
                                   real-mode path): a use before reload is #GP */
                                null_data_seg(&cpu_state.seg_ds);
                                null_data_seg(&cpu_state.seg_es);
                                null_data_seg(&cpu_state.seg_fs);
                                null_data_seg(&cpu_state.seg_gs);
                            }
                            is586 ? PUSHL(oldss) : PUSHL_SEL(oldss);
                            PUSHL(oldsp);
                            PUSHL(cpu_state.flags | (cpu_state.eflags << 16));
                            is586 ? PUSHL(CS) : PUSHL_SEL(CS);
                            PUSHL(cpu_state.pc);
                            if (cpu_state.abrt)
                                return;
                        } else {
                            PUSHW(oldss);
                            PUSHW(oldsp);
                            PUSHW(cpu_state.flags);
                            PUSHW(CS);
                            PUSHW(cpu_state.pc);
                            if (cpu_state.abrt)
                                return;
                        }
                        cpl_override            = 0;
                        cpu_state.seg_cs.access = 0x80;
                        cycles -= timing_int_pm_outer - timing_int_pm;
                        break;
                    } else if (DPL2 != CPL) {
                        x86gpf("pmodeint(): DPL != CPL", (seg & 0xfffc) + !soft);
                        return;
                    }
                    fallthrough;
                case 0x1c00:
                case 0x1d00:
                case 0x1e00:
                case 0x1f00: /* Conforming */
                    if (!(segdat2[2] & 0x8000)) {
                        x86np("Int gate CS not present", (segdat[1] & 0xfffc) + !soft);
                        return;
                    }
                    /* From V86 mode the handler must be a non-conforming DPL 0
                       segment (the stack-switch path above); a conforming or
                       same-level (DPL 3) one is #GP(selector) */
                    if (cpu_state.eflags & VM_FLAG) {
                        x86gpf("pmodeint(): conforming or same-level CS in V86 mode", (seg & 0xfffc) + !soft);
                        return;
                    }
                    if (type > 0x0800) {
                        PUSHL(cpu_state.flags | (cpu_state.eflags << 16));
                        is586 ? PUSHL(CS) : PUSHL_SEL(CS);
                        PUSHL(cpu_state.pc);
                        if (cpu_state.abrt)
                            return;
                    } else {
                        PUSHW(cpu_state.flags);
                        PUSHW(CS);
                        PUSHW(cpu_state.pc);
                        if (cpu_state.abrt)
                            return;
                    }
                    new_cpl = CS & 3;
                    break;
                default:
                    x86gpf("pmodeint(): Unknown type", (seg & 0xfffc) + !soft);
                    return;
            }
            do_seg_load(&cpu_state.seg_cs, segdat2);
            CS                      = (seg & 0xfffc) | new_cpl;
            cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | (new_cpl << 5);
            if ((CPL == 3) && (oldcpl != 3))
                flushmmucache_nopc();
            oldcpl = CPL;
            if (type > 0x0800)
                cpu_state.pc = segdat[0] | (segdat[3] << 16);
            else
                cpu_state.pc = segdat[0];
            set_use32(segdat2[3] & 0x40);

            cpl_override = 1;
            x86seg_set_accessed(oaddr, segdat2[2]); /* Set accessed bit */
            cpl_override = 0;

            cpu_state.eflags &= ~VM_FLAG;
            cpu_cur_status &= ~CPU_STATUS_V86;
            if (!(type & 0x100))
                cpu_state.flags &= ~I_FLAG;
            cpu_state.flags &= ~(T_FLAG | NT_FLAG);
            cycles -= timing_int_pm;
            break;

        case 0x500: /* Task gate */
            if (!(segdat[2] & 0x8000)) {
                x86np("Int task gate not present", (num << 3) + 2 + !soft);
                return;
            }
            /* The TSS must be global, in the GDT, an available TSS and
               present: #TS(TSS selector) on the 386/486/P5 books,
               #GP(TSS selector) on the 80286 and P6 books */
            seg  = segdat[1];
            addr = seg & 0xfff8;
            if (!(seg & 0xfffc) || (seg & 0x0004) || ((addr + 7) > gdt.limit)) {
                if (is_p6 || !is386)
                    x86gpf("pmodeint(): Task gate TSS selector null, local or > GDT limit", (seg & 0xfffc) + !soft);
                else
                    x86ts("pmodeint(): Task gate TSS selector null, local or > GDT limit", (seg & 0xfffc) + !soft);
                return;
            }
            addr += gdt.base;
            read_descriptor(addr, segdat2, segdat232, 1);
            if (cpu_state.abrt)
                return;
            if (((segdat2[2] & 0x1f00) != 0x0100) && (!is386 || ((segdat2[2] & 0x1f00) != 0x0900))) {
                if (is_p6 || !is386)
                    x86gpf("pmodeint(): Task gate target not an available TSS", (seg & 0xfffc) + !soft);
                else
                    x86ts("pmodeint(): Task gate target not an available TSS", (seg & 0xfffc) + !soft);
                return;
            }
            if (!(segdat2[2] & 0x8000)) {
                x86np("Int task TSS not present", (seg & 0xfffc) + !soft);
                return;
            }
            /* The error code, if any, is pushed onto the new task's stack with
               the size of the target TSS (word for a 286 TSS, dword for a 386 one). */
            intgatesize  = (segdat2[2] & 0x0800) ? 32 : 16;
            optype       = OPTYPE_INT;
            task_ext     = !soft;
            cpl_override = 1;
            op_taskswitch286(seg, segdat2, segdat2[2] & 0x0800);
            cpl_override = 0;
            task_ext     = 0;
            break;

        default:
            x86gpf("Protected mode interrupt unknown type", (num << 3) + 2 + !soft);
            return;
    }
}

void
#ifdef OPS_286_386
pmodeiret_2386(int is32)
#else
pmodeiret(int is32)
#endif
{
    uint16_t      newss;
    uint16_t      seg = 0;
    uint16_t      segdat[4];
    uint16_t      segdat2[4];
    uint16_t      segs[4];
    uint32_t      tempflags;
    uint32_t      flagmask;
    uint16_t      eflagmask;
    uint32_t      newpc;
    uint32_t      newsp;
    uint32_t      addr;
    uint32_t      oaddr;
    uint32_t      oldsp     = ESP;
    uint32_t     *segdat32  = (uint32_t *) segdat;
    uint32_t     *segdat232 = (uint32_t *) segdat2;
    const x86seg *dt;

    if (is386 && (cpu_state.eflags & VM_FLAG)) {
        if (IOPL != 3) {
            x86gpf("Protected mode IRET: IOPL != 3", 0);
            return;
        }
        if (is32) {
            newpc     = POPL();
            seg       = POPL();
            tempflags = POPL();
        } else {
            newpc     = POPW();
            seg       = POPW();
            tempflags = POPW();
        }
        if (cpu_state.abrt) {
            ESP = oldsp; /* a faulting pop leaves ESP as it was */
            return;
        }

        cpu_state.pc                = newpc;
        cpu_state.seg_cs.base       = seg << 4;
        cpu_state.seg_cs.limit      = 0xffff;
        cpu_state.seg_cs.limit_low  = 0;
        cpu_state.seg_cs.limit_high = 0xffff;
        cpu_state.seg_cs.access |= 0x80;
        cpu_state.seg_cs.ar_high = 0x10;
        CS                       = seg;
        cpu_state.flags          = (cpu_state.flags & 0x3000) | (tempflags & 0xcfd5) | 2;
        /* IRETD in V86 mode loads all but VM, IOPL, VIP and VIF */
        if (is32)
            cpu_state.eflags = (cpu_state.eflags & (VM_FLAG | VIF_FLAG | VIP_FLAG)) |
                               ((tempflags >> 16) & (RF_FLAG | AC_FLAG | VID_FLAG) & cpu_eflags_implemented());
        cycles -= timing_iret_rm;
        return;
    }

    if (cpu_state.flags & NT_FLAG) {
        /* The back link must name a global, busy, present TSS: #TS(back
           link) on the 80286, 386, Intel486 and Pentium books, #GP on the
           P6 book; #NP(back link) when not present */
        cpl_override = 1;
        seg  = readmemw(tr.base, 0);
        cpl_override = 0;
        if (cpu_state.abrt)
            return;
        addr = seg & 0xfff8;
        if ((seg & 0x0004) || ((addr + 7) > gdt.limit)) {
            if (is_p6)
                x86gpf("pmodeiret(): back link local or past the GDT limit", seg & 0xfffc);
            else
                x86ts("pmodeiret(): back link local or past the GDT limit", seg & 0xfffc);
            return;
        }
        read_descriptor(gdt.base + addr, segdat, segdat32, 1);
        if (cpu_state.abrt)
            return;
        if (((segdat[2] & 0x1f00) != 0x0300) && (!is386 || ((segdat[2] & 0x1f00) != 0x0b00))) {
            if (is_p6)
                x86gpf("pmodeiret(): back link not a busy TSS", seg & 0xfffc);
            else
                x86ts("pmodeiret(): back link not a busy TSS", seg & 0xfffc);
            return;
        }
        if (!(segdat[2] & 0x8000)) {
            x86np("pmodeiret(): back link TSS not present", seg & 0xfffc);
            return;
        }
        cpl_override = 1;
        op_taskswitch286(seg, segdat, segdat[2] & 0x0800);
        cpl_override = 0;
        return;
    }

    flagmask = 0xffff;
    if (CPL != 0)
        flagmask &= ~0x3000;
    if (IOPL < CPL)
        flagmask &= ~0x200;
    /* Per RETURN-TO-{SAME,OUTER}-PRIVILEGE-LEVEL, a 32-bit IRET loads RF, AC
       and ID at any CPL and loads VIF and VIP only at CPL 0. VM is never
       loaded here: entry to V86 mode is the separate CPL 0 path below. Like
       flagmask above, this is decided by the CPL of the IRET itself, before
       CS is reloaded. */
    eflagmask = RF_FLAG | AC_FLAG | VID_FLAG;
    if (CPL == 0)
        eflagmask |= VIF_FLAG | VIP_FLAG;
    eflagmask &= cpu_eflags_implemented();
    if (is32) {
        newpc     = POPL();
        seg       = POPL();
        tempflags = POPL();
        if (cpu_state.abrt) {
            ESP = oldsp;
            return;
        }
        if (is386 && (CPL == 0) && ((tempflags >> 16) & VM_FLAG)) {
            newsp   = POPL();
            newss   = POPL();
            segs[0] = POPL();
            segs[1] = POPL();
            segs[2] = POPL();
            segs[3] = POPL();
            if (cpu_state.abrt) {
                ESP = oldsp;
                return;
            }
            cpu_state.eflags = (tempflags >> 16) & cpu_eflags_implemented();
            cpu_cur_status |= CPU_STATUS_V86;
            op_loadseg(segs[0], &cpu_state.seg_es);
            do_seg_v86_init(&cpu_state.seg_es);
            op_loadseg(segs[1], &cpu_state.seg_ds);
            do_seg_v86_init(&cpu_state.seg_ds);
            cpu_cur_status |= CPU_STATUS_NOTFLATDS | CPU_STATUS_NOTFLATES;
            op_loadseg(segs[2], &cpu_state.seg_fs);
            do_seg_v86_init(&cpu_state.seg_fs);
            op_loadseg(segs[3], &cpu_state.seg_gs);
            do_seg_v86_init(&cpu_state.seg_gs);

            cpu_state.pc                = newpc & 0xffff;
            cpu_state.seg_cs.base       = seg << 4;
            cpu_state.seg_cs.limit      = 0xffff;
            cpu_state.seg_cs.limit_low  = 0;
            cpu_state.seg_cs.limit_high = 0xffff;
            CS                          = seg;
            cpu_state.seg_cs.access     = 0xe2;
            cpu_state.seg_cs.ar_high    = 0x10;
            if ((CPL == 3) && (oldcpl != 3))
                flushmmucache_nopc();
            oldcpl = CPL;

            ESP = newsp;
            op_loadseg(newss, &cpu_state.seg_ss);
            do_seg_v86_init(&cpu_state.seg_ss);
            cpu_cur_status |= CPU_STATUS_NOTFLATSS;
            use32 = 0;
            cpu_cur_status &= ~CPU_STATUS_USE32;
            cpu_state.flags = (tempflags & 0xffd5) | 2;
            cycles -= timing_iret_v86;
            return;
        }
    } else {
        newpc     = POPW();
        seg       = POPW();
        tempflags = POPW();
        if (cpu_state.abrt) {
            ESP = oldsp;
            return;
        }
    }
    if (!(seg & 0xfffc)) {
        ESP = oldsp;
        x86gpf("pmodeiret(): Selector is NULL", 0);
        return;
    }

    addr = seg & 0xfff8;
    dt   = (seg & 0x0004) ? &ldt : &gdt;
    if ((addr + 7) > dt->limit) {
        ESP = oldsp;
        x86gpf("pmodeiret(): Selector > DT limit", seg & 0xfffc);
        return;
    }
    addr += dt->base;
    if ((seg & 0x0003) < CPL) {
        ESP = oldsp;
        x86gpf("pmodeiret(): RPL < CPL", seg & 0xfffc);
        return;
    }
    read_descriptor(addr, segdat, segdat32, 1);
    if (cpu_state.abrt) {
        ESP = oldsp;
        return;
    }

    switch (segdat[2] & 0x1f00) {
        case 0x1000:
        case 0x1100:
        case 0x1200:
        case 0x1300:
             /* Data segment, apparently valid when CPL is the same, used by MS CodeView for DOS. */
             fallthrough;
        case 0x1800:
        case 0x1900:
        case 0x1a00:
        case 0x1b00: /* Non-conforming code */
            if ((seg & 0x0003) != DPL) {
                ESP = oldsp;
                x86gpf("pmodeiret(): Non-conforming RPL != DPL", seg & 0xfffc);
                return;
            }
            break;
        case 0x1C00:
        case 0x1D00:
        case 0x1E00:
        case 0x1F00: /* Conforming code */
            if ((seg & 0x0003) < DPL) {
                ESP = oldsp;
                x86gpf("pmodeiret(): Conforming RPL < DPL", seg & ~3);
                return;
            }
            break;
        default:
            ESP = oldsp;
            x86gpf("pmodeiret(): Unknown type", seg & 0xfffc);
            return;
    }
    if (!(segdat[2] & 0x8000)) {
        ESP = oldsp;
        x86np("IRET CS not present", seg & 0xfffc);
        return;
    }
    if ((seg & 0x0003) == CPL) {
        CS = seg;
        do_seg_load(&cpu_state.seg_cs, segdat);
        cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | ((CS & 0x0003) << 5);
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
        oldcpl = CPL;
        set_use32(segdat[3] & 0x0040);

        cpl_override = 1;
        x86seg_set_accessed(addr, segdat[2]); /* Set accessed bit */
        cpl_override = 0;
        cycles -= timing_iret_pm;
    } else { /* Return to outer level */
        oaddr = addr;
        x86seg_log("Outer level\n");
        if (is32) {
            newsp = POPL();
            newss = POPL();
            if (cpu_state.abrt) {
                ESP = oldsp;
                return;
            }
        } else {
            newsp = POPW();
            newss = POPW();
            if (cpu_state.abrt) {
                ESP = oldsp;
                return;
            }
        }

        x86seg_log("IRET load stack %04X:%04X\n", newss, newsp);

        if (!(newss & 0xfffc)) {
            ESP = oldsp;
            x86gpf("pmodeiret(): New SS selector is zero", newss & 0xfffc);
            return;
        }
        addr = newss & 0xfff8;
        dt   = (newss & 0x0004) ? &ldt : &gdt;
        if ((addr + 7) > dt->limit) {
            ESP = oldsp;
            x86gpf("pmodeiret(): New SS selector > DT limit", newss & 0xfffc);
            return;
        }
        addr += dt->base;
        read_descriptor(addr, segdat2, segdat232, 1);
        if (cpu_state.abrt) {
            ESP = oldsp;
            return;
        }
        if ((newss & 3) != (seg & 3)) {
            ESP = oldsp;
            x86gpf("pmodeiret(): New SS RPL > CS RPL", newss & 0xfffc);
            return;
        }
        if ((segdat2[2] & 0x1a00) != 0x1200) {
            ESP = oldsp;
            x86gpf("pmodeiret(): New SS bad type", newss & 0xfffc);
            return;
        }
        if (DPL2 != (seg & 0x0003)) {
            ESP = oldsp;
            x86gpf("pmodeiret(): New SS DPL != CS RPL", newss & 0xfffc);
            return;
        }
        if (!(segdat2[2] & 0x8000)) {
            ESP = oldsp;
            x86np("IRET loading SS not present", newss & 0xfffc);
            return;
        }
        SS = newss;
        set_stack32((segdat2[3] & 0x40) ? 1 : 0);
        if (stack32)
            ESP = newsp;
        else
            SP = newsp;
        do_seg_load(&cpu_state.seg_ss, segdat2);

        cpl_override = 1;
        x86seg_set_accessed(addr, segdat2[2]); /* Set accessed bit */
        x86seg_set_accessed(oaddr, segdat[2]); /* Set accessed bit */
        cpl_override = 0;
        /* Conforming segments don't change CPL, so CPL = RPL */
        if (segdat[2] & 0x0400)
            segdat[2] = (segdat[2] & ~(3 << 13)) | ((seg & 3) << 13);

        CS = seg;
        do_seg_load(&cpu_state.seg_cs, segdat);
        cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | ((CS & 3) << 5);
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
        oldcpl = CPL;
        set_use32(segdat[3] & 0x40);

        check_seg_valid(&cpu_state.seg_ds);
        check_seg_valid(&cpu_state.seg_es);
        check_seg_valid(&cpu_state.seg_fs);
        check_seg_valid(&cpu_state.seg_gs);
        cycles -= timing_iret_pm_outer;
    }
    cpu_state.pc    = newpc;
    cpu_state.flags = (cpu_state.flags & ~flagmask) | (tempflags & flagmask & 0xffd5) | 2;
    if (is32)
        cpu_state.eflags = (cpu_state.eflags & (uint16_t) ~eflagmask) |
                           ((tempflags >> 16) & eflagmask);
}

/* After a task switch, "IF EIP not within code segment limit THEN #GP(0)";
   the 386, Intel486 and Pentium books raise #TS(0) for CALL instead. */
static void
task_eip_check(void)
{
    if (cpu_state.pc <= cpu_state.seg_cs.limit)
        return;
    if ((optype == CALL) && is386 && !is_p6)
        x86ts("Task switch: EIP past the CS limit", 0);
    else
        x86gpf("Task switch: EIP past the CS limit", 0);
}

/* The dynamic part of a TSS, and the fields a task switch reads */
typedef struct task_image_t {
    uint32_t cr3;
    uint32_t pc;
    uint32_t flags;
    uint32_t regs[8]; /* EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI */
    uint16_t sel_es;
    uint16_t sel_cs;
    uint16_t sel_ss;
    uint16_t sel_ds;
    uint16_t sel_fs;
    uint16_t sel_gs;
    uint16_t sel_ldt;
    int      t_bit;
} task_image_t;

static void
task_read_image(uint32_t base, int is32, task_image_t *t)
{
    memset(t, 0, sizeof(task_image_t));
    if (is32) {
        t->cr3   = readmeml(base, 0x1c);
        t->pc    = readmeml(base, 0x20);
        t->flags = readmeml(base, 0x24);
        for (int i = 0; i < 8; i++)
            t->regs[i] = readmeml(base, 0x28 + (i << 2));
        t->sel_es  = readmemw(base, 0x48);
        t->sel_cs  = readmemw(base, 0x4c);
        t->sel_ss  = readmemw(base, 0x50);
        t->sel_ds  = readmemw(base, 0x54);
        t->sel_fs  = readmemw(base, 0x58);
        t->sel_gs  = readmemw(base, 0x5c);
        t->sel_ldt = readmemw(base, 0x60);
        t->t_bit   = readmemb(base, 0x64) & 1;
    } else {
        t->pc    = readmemw(base, 0x0e);
        t->flags = readmemw(base, 0x10);
        for (int i = 0; i < 8; i++)
            t->regs[i] = readmemw(base, 0x12 + (i << 1));
        t->sel_es  = readmemw(base, 0x22);
        t->sel_cs  = readmemw(base, 0x24);
        t->sel_ss  = readmemw(base, 0x26);
        t->sel_ds  = readmemw(base, 0x28);
        t->sel_ldt = readmemw(base, 0x2a);
    }
}

/* The outgoing task's dynamic fields, in its own TSS's format. CR3 is a
   static field the processor "reads, but does not change" (Pentium Vol. 3,
   13.1), so it is not written. */
static void
task_save(uint32_t flags)
{
    if (tr.access & 8) {
        writememl(tr.base, 0x20, cpu_state.pc);
        writememl(tr.base, 0x24, flags);
        for (int i = 0; i < 8; i++)
            writememl(tr.base, 0x28 + (i << 2), cpu_state.regs[i].l);
        writememl(tr.base, 0x48, ES);
        writememl(tr.base, 0x4c, CS);
        writememl(tr.base, 0x50, SS);
        writememl(tr.base, 0x54, DS);
        writememl(tr.base, 0x58, FS);
        writememl(tr.base, 0x5c, GS);
    } else {
        writememw(tr.base, 0x0e, cpu_state.pc);
        writememw(tr.base, 0x10, flags);
        for (int i = 0; i < 8; i++)
            writememw(tr.base, 0x12 + (i << 1), cpu_state.regs[i].w);
        writememw(tr.base, 0x22, ES);
        writememw(tr.base, 0x24, CS);
        writememw(tr.base, 0x26, SS);
        writememw(tr.base, 0x28, DS);
    }
}

/* The descriptor a selector from the new TSS names: 0 for a null
   selector, -1 past its table's limit, 1 read into sd (a fault reading it
   is left in cpu_state.abrt) */
static int
task_desc(uint16_t sel, uint16_t *sd, uint32_t *addr)
{
    const x86seg *dt = (sel & 0x0004) ? &ldt : &gdt;

    if (!(sel & 0xfffc))
        return 0;
    if (((sel & 0xfff8) + 7) > dt->limit)
        return -1;
    *addr = dt->base + (sel & 0xfff8);
    read_descriptor(*addr, sd, (uint32_t *) sd, 1);
    return 1;
}

/* LDTR from its descriptor; the G bit and base[31:24] only past the 80286 */
static void
task_load_ldt(const uint16_t *sd)
{
    ldt.limit = sd[0];
    ldt.base  = sd[1] | ((sd[2] & 0x00ff) << 16);
    if (is386) {
        ldt.limit |= (sd[3] & 0x000f) << 16;
        if (sd[3] & 0x0080)
            ldt.limit = (ldt.limit << 12) | 0xfff;
        ldt.base |= (sd[3] >> 8) << 24;
    }
}

/* A data segment register (SS included) of the new task, its checks made:
   an 8086 segment in V86 mode, else its descriptor, which is marked
   accessed */
static void
task_load_seg(x86seg *s, uint16_t sel, const uint16_t *sd, uint32_t addr)
{
    if (cpu_state.eflags & VM_FLAG) {
        s->seg  = sel;
        s->base = sel << 4;
        do_seg_v86_init(s);
        s->checked = 1;
        if (s == &cpu_state.seg_ss)
            set_stack32(0);
    } else if (!(sel & 0xfffc)) {
        null_data_seg(s);
        s->seg = sel;
    } else {
        uint16_t d[4] = { sd[0], sd[1], sd[2], sd[3] };

        cpl_override = 1;
        x86seg_set_accessed(addr, d[2]);
        cpl_override = 0;
        if (cpu_state.abrt)
            return;
        s->seg = sel;
        do_seg_load(s, d);
        if (s == &cpu_state.seg_ss)
            set_stack32((d[3] & 0x40) ? 1 : 0);
        s->checked = 0;
    }
#ifdef USE_DYNAREC
    if (s == &cpu_state.seg_ds)
        codegen_flat_ds = 0;
    if (s == &cpu_state.seg_ss)
        codegen_flat_ss = 0;
    if (s == &cpu_state.seg_es)
        codegen_flat_es = 0;
#endif
    if (s == &cpu_state.seg_ds) {
        if ((s->base == 0) && (s->limit_low == 0) && (s->limit_high == 0xffffffff))
            cpu_cur_status &= ~CPU_STATUS_NOTFLATDS;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATDS;
    }
    if (s == &cpu_state.seg_es) {
        if ((s->base == 0) && (s->limit_low == 0) && (s->limit_high == 0xffffffff))
            cpu_cur_status &= ~CPU_STATUS_NOTFLATES;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATES;
    }
    if (s == &cpu_state.seg_ss) {
        if ((s->base == 0) && (s->limit_low == 0) && (s->limit_high == 0xffffffff))
            cpu_cur_status &= ~CPU_STATUS_NOTFLATSS;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATSS;
    }
}

/* CS of the new task, its checks made. CPL is the selector's RPL: "The new
   task begins executing at the privilege level indicated by the RPL of the
   new contents of the CS register" (386 PRM 7.5), which a conforming
   segment's DPL need not equal. */
static void
task_load_cs(uint16_t sel, const uint16_t *sd, uint32_t addr, int cpl)
{
    if (cpu_state.eflags & VM_FLAG) {
        CS                    = sel;
        cpu_state.seg_cs.base = sel << 4;
        do_seg_v86_init(&cpu_state.seg_cs);
        set_use32(0);
        return;
    }

    uint16_t d[4] = { sd[0], sd[1], sd[2], sd[3] };

    cpl_override = 1;
    x86seg_set_accessed(addr, d[2]);
    cpl_override = 0;
    if (cpu_state.abrt)
        return;
    CS = sel;
    do_seg_load(&cpu_state.seg_cs, d);
    cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | (cpl << 5);
    /* The D bit sets the operand size whatever the TSS's type */
    set_use32(d[3] & 0x0040);
}

/* The 80286's checks and loads after the commit point, in the order of
   SWITCH_TASKS (80286 PRM, Appendix B): LDT, SS, CS, then DS and ES */
static int
task_checks_286(const task_image_t *t, int cpl, int ext)
{
    uint16_t sd[4];
    uint32_t addr = 0;
    int      r;
    int      dpl;

    /* A null LDT selector leaves LDTR unusable, as LLDT does */
    if (t->sel_ldt & 0xfffc) {
        if ((t->sel_ldt & 0x0004) || (((t->sel_ldt & 0xfff8) + 7) > gdt.limit)) {
            x86ts("taskswitch286(): LDT selector", (t->sel_ldt & 0xfffc) | ext);
            return 0;
        }
        read_descriptor(gdt.base + (t->sel_ldt & 0xfff8), sd, (uint32_t *) sd, 1);
        if (cpu_state.abrt)
            return 0;
        if ((sd[2] & 0x1f00) != 0x0200) {
            x86ts("taskswitch286(): LDT descriptor", (t->sel_ldt & 0xfffc) | ext);
            return 0;
        }
        if (!(sd[2] & 0x8000)) {
            x86ts("taskswitch286(): LDT not present", (t->sel_ldt & 0xfffc) | ext);
            return 0;
        }
        task_load_ldt(sd);
    }

    r = task_desc(t->sel_ss, sd, &addr);
    if (cpu_state.abrt)
        return 0;
    dpl = (sd[2] >> 13) & 3;
    if ((r <= 0) || ((t->sel_ss & 3) != cpl) || (dpl != cpl) || ((sd[2] & 0x1a00) != 0x1200)) {
        x86ts("taskswitch286(): SS", (t->sel_ss & 0xfffc) | ext);
        return 0;
    }
    if (!(sd[2] & 0x8000)) {
        x86ss("taskswitch286(): SS not present", (t->sel_ss & 0xfffc) | ext);
        return 0;
    }
    task_load_seg(&cpu_state.seg_ss, t->sel_ss, sd, addr);
    if (cpu_state.abrt)
        return 0;

    r = task_desc(t->sel_cs, sd, &addr);
    if (cpu_state.abrt)
        return 0;
    dpl = (sd[2] >> 13) & 3;
    if ((r <= 0) || ((sd[2] & 0x1800) != 0x1800) || ((sd[2] & 0x0400) ? (dpl > cpl) : (dpl != cpl))) {
        x86ts("taskswitch286(): CS", (t->sel_cs & 0xfffc) | ext);
        return 0;
    }
    if (!(sd[2] & 0x8000)) {
        x86np("taskswitch286(): CS not present", (t->sel_cs & 0xfffc) | ext);
        return 0;
    }
    task_load_cs(t->sel_cs, sd, addr, cpl);
    if (cpu_state.abrt)
        return 0;

    const uint16_t sel[2] = { t->sel_ds, t->sel_es };
    x86seg *const  seg[2] = { &cpu_state.seg_ds, &cpu_state.seg_es };
    for (int i = 0; i < 2; i++) {
        r = task_desc(sel[i], sd, &addr);
        if (cpu_state.abrt)
            return 0;
        if (r != 0) {
            dpl = (sd[2] >> 13) & 3;
            if ((r < 0) || !(sd[2] & 0x1000) || ((sd[2] & 0x0a00) == 0x0800) ||
                (((sd[2] & 0x0c00) != 0x0c00) && ((dpl < cpl) || (dpl < (sel[i] & 3))))) {
                x86ts("taskswitch286(): data segment", (sel[i] & 0xfffc) | ext);
                return 0;
            }
            if (!(sd[2] & 0x8000)) {
                x86np("taskswitch286(): data segment not present", (sel[i] & 0xfffc) | ext);
                return 0;
            }
        }
        task_load_seg(seg[i], sel[i], sd, addr);
        if (cpu_state.abrt)
            return 0;
    }
    return 1;
}

/* The 386 and later processors' checks after the commit point, in the
   order of "Checks Made during a Task Switch" (386 PRM Table 7-1, Intel486
   PRM Table 7-1, Pentium Vol. 3 Table 13-1; "the order used in the P6
   family", SDM Vol. 3 Table 6-1), then the loads */
static int
task_checks_386(const task_image_t *t, int cpl, int ext)
{
    const int      vm     = !!(cpu_state.eflags & VM_FLAG);
    const uint16_t sel[4] = { t->sel_ds, t->sel_es, t->sel_fs, t->sel_gs };
    x86seg *const  seg[4] = { &cpu_state.seg_ds, &cpu_state.seg_es, &cpu_state.seg_fs, &cpu_state.seg_gs };
    uint16_t       lsd[4] = { 0 };
    uint16_t       csd[4] = { 0 };
    uint16_t       ssd[4] = { 0 };
    uint16_t       dsd[4][4];
    uint32_t       cs_addr  = 0;
    uint32_t       ss_addr  = 0;
    uint32_t       d_addr[4] = { 0 };
    int            cs_r     = 0;
    int            ss_r     = 0;
    int            r;
    int            dpl;

    /* LDT selector of the new task is valid: a GDT selector naming an LDT
       descriptor (null leaves LDTR unusable, as LLDT does) */
    if (t->sel_ldt & 0xfffc) {
        if ((t->sel_ldt & 0x0004) || (((t->sel_ldt & 0xfff8) + 7) > gdt.limit)) {
            x86ts("taskswitch286(): LDT selector", (t->sel_ldt & 0xfffc) | ext);
            return 0;
        }
        read_descriptor(gdt.base + (t->sel_ldt & 0xfff8), lsd, (uint32_t *) lsd, 1);
        if (cpu_state.abrt)
            return 0;
        if ((lsd[2] & 0x1f00) != 0x0200) {
            x86ts("taskswitch286(): LDT descriptor", (t->sel_ldt & 0xfffc) | ext);
            return 0;
        }
        task_load_ldt(lsd);
    }

    if (!vm) {
        /* Code segment DPL matches selector RPL */
        cs_r = task_desc(t->sel_cs, csd, &cs_addr);
        if (cpu_state.abrt)
            return 0;
        if ((cs_r > 0) && ((csd[2] & 0x1800) == 0x1800)) {
            dpl = (csd[2] >> 13) & 3;
            if ((csd[2] & 0x0400) ? (dpl > cpl) : (dpl != cpl)) {
                x86ts("taskswitch286(): CS DPL", (t->sel_cs & 0xfffc) | ext);
                return 0;
            }
        }
        /* SS selector is valid, the segment present, its DPL the CPL */
        ss_r = task_desc(t->sel_ss, ssd, &ss_addr);
        if (cpu_state.abrt)
            return 0;
        if ((ss_r <= 0) || ((ssd[2] & 0x1a00) != 0x1200)) {
            x86ts("taskswitch286(): SS", (t->sel_ss & 0xfffc) | ext);
            return 0;
        }
        if (!(ssd[2] & 0x8000)) {
            x86ss("taskswitch286(): SS not present", (t->sel_ss & 0xfffc) | ext);
            return 0;
        }
        if (((ssd[2] >> 13) & 3) != cpl) {
            x86ts("taskswitch286(): SS DPL != CPL", (t->sel_ss & 0xfffc) | ext);
            return 0;
        }
    }

    /* LDT of the new task is present */
    if ((t->sel_ldt & 0xfffc) && !(lsd[2] & 0x8000)) {
        ldt.base  = 0;
        ldt.limit = 0;
        x86ts("taskswitch286(): LDT not present", (t->sel_ldt & 0xfffc) | ext);
        return 0;
    }

    if (!vm) {
        /* CS selector is valid, the segment present */
        if ((cs_r <= 0) || ((csd[2] & 0x1800) != 0x1800)) {
            x86ts("taskswitch286(): CS", (t->sel_cs & 0xfffc) | ext);
            return 0;
        }
        if (!(csd[2] & 0x8000)) {
            x86np("taskswitch286(): CS not present", (t->sel_cs & 0xfffc) | ext);
            return 0;
        }
        /* Stack segment DPL matches selector RPL */
        if (((ssd[2] >> 13) & 3) != (t->sel_ss & 3)) {
            x86ts("taskswitch286(): SS DPL != RPL", (t->sel_ss & 0xfffc) | ext);
            return 0;
        }
        /* DS, ES, FS and GS: valid, readable, present, DPL >= CPL and RPL
           unless conforming */
        for (int i = 0; i < 4; i++) {
            r = task_desc(sel[i], dsd[i], &d_addr[i]);
            if (cpu_state.abrt)
                return 0;
            if (r == 0)
                continue;
            if ((r < 0) || !(dsd[i][2] & 0x1000)) {
                x86ts("taskswitch286(): data segment selector", (sel[i] & 0xfffc) | ext);
                return 0;
            }
            if ((dsd[i][2] & 0x0a00) == 0x0800) {
                x86ts("taskswitch286(): data segment not readable", (sel[i] & 0xfffc) | ext);
                return 0;
            }
            if (!(dsd[i][2] & 0x8000)) {
                x86np("taskswitch286(): data segment not present", (sel[i] & 0xfffc) | ext);
                return 0;
            }
            dpl = (dsd[i][2] >> 13) & 3;
            if (((dsd[i][2] & 0x0c00) != 0x0c00) && ((dpl < cpl) || (dpl < (sel[i] & 3)))) {
                x86ts("taskswitch286(): data segment DPL", (sel[i] & 0xfffc) | ext);
                return 0;
            }
        }
    }

    task_load_cs(t->sel_cs, csd, cs_addr, cpl);
    if (cpu_state.abrt)
        return 0;
    task_load_seg(&cpu_state.seg_ss, t->sel_ss, ssd, ss_addr);
    for (int i = 0; (i < 4) && !cpu_state.abrt; i++)
        task_load_seg(seg[i], sel[i], dsd[i], d_addr[i]);
    return !cpu_state.abrt;
}

void
#ifdef OPS_286_386
taskswitch286_2386(uint16_t seg, uint16_t *segdat, int is32)
#else
taskswitch286(uint16_t seg, uint16_t *segdat, int is32)
#endif
{
    const int    ext = task_ext;
    task_image_t img;
    uint32_t     base;
    uint32_t     limit;
    uint32_t     old_flags;
    int          cpl;

    base  = segdat[1] | ((segdat[2] & 0x00ff) << 16);
    limit = segdat[0];
    if (is386) {
        base |= (segdat[3] >> 8) << 24;
        limit |= (segdat[3] & 0x000f) << 16;
        if (segdat[3] & 0x0080)
            limit = (limit << 12) | 0xfff;
    }

    cpu_386_flags_rebuild();
    old_flags = cpu_state.flags | (cpu_state.eflags << 16);
    if (optype == IRET)
        old_flags &= ~NT_FLAG; /* the saved image's NT is cleared */

    if (!is386) {
        /* SWITCH_TASKS (80286 PRM, Appendix B): the new task is marked
           busy first; the current TSS must hold the state saved */
        if ((optype != IRET) && !x86seg_tss_busy(gdt.base, seg, 1))
            return;
        if (tr.limit < 41) {
            x86ts("taskswitch286(): current TSS limit < 41", (seg & 0xfffc) | ext);
            return;
        }
        task_save(old_flags);
        if (cpu_state.abrt)
            return;
    } else {
        /* Checked in the old task's context: the new TSS's limit, and its
           pages present ("Checks that the current (old) TSS, new TSS ... are
           paged into system memory", SDM Vol. 3, 6.3 step 5) */
        if (limit < (is32 ? 103 : 43)) {
            x86ts("taskswitch286(): TSS limit too small", (seg & 0xfffc) | ext);
            return;
        }
        task_read_image(base, is32, &img);
        if (cpu_state.abrt)
            return;
        task_save(old_flags);
        if (cpu_state.abrt)
            return;
        /* The Pentium sets the new Busy bit before the save and clears it
           again if the save faults; "The end result of the instruction is
           the same" (AMD-K5 TRM, A.6.2) */
        if ((optype != IRET) && !x86seg_tss_busy(gdt.base, seg, 1))
            return;
    }

    /* Busy bits (TSS descriptors live only in the GDT) and the link */
    if ((optype == JMP) || (optype == IRET)) {
        if (!x86seg_tss_busy(gdt.base, tr.seg, 0))
            return;
    }
    if ((optype == OPTYPE_INT) || (optype == CALL)) {
        writememw(base, 0, tr.seg);
        if (cpu_state.abrt)
            return;
    }

    /* Commit: from here a fault is taken in the new task */
    tr.seg     = seg;
    tr.base    = base;
    tr.limit   = limit;
    tr.access  = (segdat[2] >> 8) | 0x02; /* now busy */
    tr.ar_high = is386 ? (segdat[3] & 0xff) : 0;

    if (!is386 && (limit < 43)) {
        /* The 80286 checks the new TSS's limit after loading TR */
        x86ts("taskswitch286(): TSS limit < 43", (seg & 0xfffc) | ext);
        return;
    }
    /* The new state, read after the save: the two TSSs can overlap (an IRET
       to the task the link names may be to the current one) */
    task_read_image(base, is32, &img);
    if (cpu_state.abrt)
        return;
    if ((optype == OPTYPE_INT) || (optype == CALL))
        img.flags |= NT_FLAG;

    msw |= 8; /* CR0.TS */

    cpu_state.pc    = img.pc;
    cpu_state.oldpc = img.pc;
    if (is32) {
        cpu_state.flags  = img.flags;
        cpu_state.eflags = img.flags >> 16;
    } else {
        cpu_state.flags = img.flags;
        cpu_state.eflags &= ~VM_FLAG; /* a 16-bit TSS has no VM */
    }
    cpu_386_flags_extract();
    for (int i = 0; i < 8; i++)
        cpu_state.regs[i].l = is32 ? img.regs[i] : (img.regs[i] | 0xffff0000);

#ifdef USE_DEBUG_REGS_486
    dr[7] &= 0xFFFFFFAA;
#else
    if (!cpu_use_exec)
        dr[7] &= 0xFFFFFFAA;
#endif

    /* "The processor first loads all the segment registers with segment
       selectors from the TSS, then checks their contents for validity"
       (SDM Vol. 3, Interrupt 10); a check that fails leaves the rest loaded
       but unchecked. A fault from here is taken at the new task's CS:EIP
       and CPL. */
    cpl = (cpu_state.eflags & VM_FLAG) ? 3 : (img.sel_cs & 3);
    if ((cpl == 3) && (CPL != 3))
        flushmmucache_nopc();
    cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | (cpl << 5);
    oldcpl                  = cpl;
    oldcs                   = img.sel_cs;
    CS                      = img.sel_cs;
    ES                      = img.sel_es;
    SS                      = img.sel_ss;
    DS                      = img.sel_ds;
    if (is386) {
        FS = img.sel_fs;
        GS = img.sel_gs;
    }
    ldt.seg   = img.sel_ldt;
    ldt.base  = 0;
    ldt.limit = 0;
    if (cpu_state.eflags & VM_FLAG)
        cpu_cur_status |= CPU_STATUS_V86;
    else
        cpu_cur_status &= ~CPU_STATUS_V86;

    if (is32) {
        cr3 = img.cr3;
        pae_load_pdptes(); /* used only with PAE paging */
        flushmmucache();
    }

    if (is386 ? !task_checks_386(&img, cpl, ext) : !task_checks_286(&img, cpl, ext))
        return;

#ifdef USE_DEBUG_REGS_486
    rf_flag_no_clear = 1;
#else
    if (!cpu_use_exec)
        rf_flag_no_clear = 1;
#endif

    /* An exception's error code goes on the new task's stack before its
       EIP is checked: x86_doabrt() makes that check after the push */
    if ((optype == OPTYPE_INT) && task_error_follows)
        task_eip_pending = 1;
    else {
        task_eip_check();
        if (cpu_state.abrt)
            return;
    }

    if (img.t_bit) {
#ifdef USE_DEBUG_REGS_486
        trap |= 2;
#else
        if (cpu_use_exec)
            trap = 2;
        else
            trap |= 2;
#endif
#ifdef USE_DYNAREC
        cpu_block_end = 1;
#endif
    }
}

void
#ifdef OPS_286_386
cyrix_write_seg_descriptor_2386(uint32_t addr, x86seg *seg)
#else
cyrix_write_seg_descriptor(uint32_t addr, x86seg *seg)
#endif
{
    uint32_t limit_raw = seg->limit;

    if (seg->ar_high & 0x80)
        limit_raw >>= 12;

    if ((limit_raw == 0xffffffff) && !(seg->ar_high & 0x80)) {
        x86seg_log("4G segment limit without page granularity!\n");
        seg->ar_high |= 0x80;
        limit_raw >>= 12;
    }

    writememl(0, addr, (limit_raw & 0xffff) | (seg->base << 16));
    writememl(0, addr + 4, ((seg->base >> 16) & 0xff) | (seg->access << 8) | (limit_raw & 0xf0000) | (seg->ar_high << 16) | (seg->base & 0xff000000));
}

void
#ifdef OPS_286_386
cyrix_load_seg_descriptor_2386(uint32_t addr, x86seg *seg)
#else
cyrix_load_seg_descriptor(uint32_t addr, x86seg *seg)
#endif
{
    uint16_t segdat[4];
    uint16_t selector;

    segdat[0] = readmemw(0, addr);
    segdat[1] = readmemw(0, addr + 2);
    segdat[2] = readmemw(0, addr + 4);
    segdat[3] = readmemw(0, addr + 6);
    selector  = readmemw(0, addr + 8);

    if (!cpu_state.abrt) {
        do_seg_load(seg, segdat);
        seg->seg     = selector;
        seg->checked = 0;
        if (seg == &cpu_state.seg_ds) {
            if (seg->base == 0 && seg->limit_low == 0 && seg->limit_high == 0xffffffff)
                cpu_cur_status &= ~CPU_STATUS_NOTFLATDS;
            else
                cpu_cur_status |= CPU_STATUS_NOTFLATDS;
        }
        if (seg == &cpu_state.seg_es) {
            if (seg->base == 0 && seg->limit_low == 0 && seg->limit_high == 0xffffffff)
                cpu_cur_status &= ~CPU_STATUS_NOTFLATES;
            else
                cpu_cur_status |= CPU_STATUS_NOTFLATES;
        }

        if (seg == &cpu_state.seg_cs)
            set_use32(segdat[3] & 0x40);

        if (seg == &cpu_state.seg_ss) {
            if (seg->base == 0 && seg->limit_low == 0 && seg->limit_high == 0xffffffff)
                cpu_cur_status &= ~CPU_STATUS_NOTFLATSS;
            else
                cpu_cur_status |= CPU_STATUS_NOTFLATSS;
            set_stack32((segdat[3] & 0x40) ? 1 : 0);
        }
    }
}
