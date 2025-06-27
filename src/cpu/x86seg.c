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

void
#ifdef OPS_286_386
x86_doabrt_2386(int x86_abrt)
#else
x86_doabrt(int x86_abrt)
#endif
{
#ifndef USE_NEW_DYNAREC
    CS = oldcs;
#endif
    cpu_state.pc             = cpu_state.oldpc;
    cpu_state.seg_cs.access  = (oldcpl << 5) | 0x80;
    cpu_state.seg_cs.ar_high = 0x10;

    if (msw & 1)
        op_pmodeint(x86_abrt, 0);
    else {
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
#ifndef USE_NEW_DYNAREC
        oxpc = cpu_state.pc;
#endif
        cpu_state.pc = readmemw(0, addr);
        op_loadcs(readmemw(0, addr + 2));
        return;
    }

    if (cpu_state.abrt || x86_was_reset)
        return;

    if (intgatesize == 16) {
        if (stack32) {
            writememw(ss, ESP - 2, abrt_error);
            ESP -= 2;
        } else {
            writememw(ss, ((SP - 2) & 0xffff), abrt_error);
            SP -= 2;
        }
    } else {
        if (stack32) {
            writememl(ss, ESP - 4, abrt_error);
            ESP -= 4;
        } else {
            writememl(ss, ((SP - 4) & 0xffff), abrt_error);
            SP -= 4;
        }
    }
}

static void
set_stack32(int s)
{
    stack32 = s;

    if (stack32)
        cpu_cur_status |= CPU_STATUS_STACK32;
    else
        cpu_cur_status &= ~CPU_STATUS_STACK32;
}

static void
set_use32(int u)
{
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
    s->limit = segdat[0] | ((segdat[3] & 0x000f) << 16);
    if (segdat[3] & 0x0080)
        s->limit = (s->limit << 12) | 0xfff;
    s->base = segdat[1] | ((segdat[2] & 0x00ff) << 16);
    if (is386)
        s->base |= ((segdat[3] >> 8) << 24);
    s->access  = segdat[2] >> 8;
    s->ar_high = segdat[3] & 0xff;

    if (((segdat[2] & 0x1800) != 0x1000) || !(segdat[2] & (1 << 10))) {
        /* Expand-down */
        s->limit_high = s->limit;
        s->limit_low  = 0;
    } else {
        s->limit_high = (segdat[3] & 0x40) ? 0xffffffff : 0xffff;
        s->limit_low  = s->limit + 1;
    }

    if (s == &cpu_state.seg_ds) {
        if ((s->base == 0) && (s->limit_low == 0) && (s->limit_high == 0xffffffff))
            cpu_cur_status &= ~CPU_STATUS_NOTFLATDS;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATDS;
    }
    if (s == &cpu_state.seg_ss) {
        if ((s->base == 0) && (s->limit_low == 0) && (s->limit_high == 0xffffffff))
            cpu_cur_status &= ~CPU_STATUS_NOTFLATSS;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATSS;
    }
}
#endif

static void
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

#ifdef USE_NEW_DYNAREC
int
#else
void
#endif
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
                x86ss(NULL, 0);
#ifdef USE_NEW_DYNAREC
                return 1;
#else
                return;
#endif
            }
            s->seg     = 0;
            s->access  = 0x80;
            s->ar_high = 0x10;
            s->base    = -1;
            if (s == &cpu_state.seg_ds)
                cpu_cur_status |= CPU_STATUS_NOTFLATDS;
#ifdef USE_NEW_DYNAREC
            return 0;
#else
            return;
#endif
        }
        addr = seg & 0xfff8;
        dt   = (seg & 0x0004) ? &ldt : &gdt;
        if ((addr + 7) > dt->limit) {
            x86gpf("loadseg(): Bigger than LDT limit", seg & 0xfffc);
#ifdef USE_NEW_DYNAREC
            return 1;
#else
            return;
#endif
        }
        addr += dt->base;
        read_descriptor(addr, segdat, segdat32, 1);
        if (cpu_state.abrt)
#ifdef USE_NEW_DYNAREC
            return 1;
#else
            return;
#endif
        dpl = (segdat[2] >> 13) & 3;
        if (s == &cpu_state.seg_ss) {
            if (!(seg & 0xfffc)) {
                x86gpf("loadseg(): Zero stack segment", seg & 0xfffc);
#ifdef USE_NEW_DYNAREC
                return 1;
#else
                return;
#endif
            }
            if ((seg & 0x0003) != CPL) {
                x86gpf("loadseg(): Stack segment RPL != CPL", seg & 0xfffc);
#ifdef USE_NEW_DYNAREC
                return 1;
#else
                return;
#endif
            }
            if (dpl != CPL) {
                x86gpf("loadseg(): Stack segment DPL != CPL", seg & 0xfffc);
#ifdef USE_NEW_DYNAREC
                return 1;
#else
                return;
#endif
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
#ifdef USE_NEW_DYNAREC
                    return 1;
#else
                    return;
#endif
            }
            if (!(segdat[2] & 0x8000)) {
                x86ss(NULL, seg & 0xfffc);
#ifdef USE_NEW_DYNAREC
                return 1;
#else
                return;
#endif
            }
            set_stack32((segdat[3] & 0x40) ? 1 : 0);
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
#ifdef USE_NEW_DYNAREC
                        return 1;
#else
                        return;
#endif
                    }
                    if ((CPL) > dpl) {
                        x86gpf("loadseg(): Normal segment DPL < CPL", seg & 0xfffc);
#ifdef USE_NEW_DYNAREC
                        return 1;
#else
                        return;
#endif
                    }
                    break;
                case 0x1e:
                case 0x1f: /* Readable conforming code */
                    break;
                default:
                    x86gpf("loadseg(): Unknown normal segment type", seg & 0xfffc);
#ifdef USE_NEW_DYNAREC
                    return 1;
#else
                    return;
#endif
            }
        }

        if (!(segdat[2] & 0x8000)) {
            x86np("Load data seg not present", seg & 0xfffc);
#ifdef USE_NEW_DYNAREC
            return 1;
#else
            return;
#endif
        }
        s->seg = seg;
        do_seg_load(s, segdat);

        cpl_override = 1;
        writememw(0, addr + 4, segdat[2] | 0x100); /* Set accessed bit */
        cpl_override = 0;
        s->checked   = 0;
#ifdef USE_DYNAREC
        if (s == &cpu_state.seg_ds)
            codegen_flat_ds = 0;
        if (s == &cpu_state.seg_ss)
            codegen_flat_ss = 0;
#endif
    } else {
        s->access  = 0xe2;
        s->ar_high = 0x10;
        s->base    = seg << 4;
        s->seg     = seg;
        s->checked = 1;
#ifdef USE_DYNAREC
        if (s == &cpu_state.seg_ds)
            codegen_flat_ds = 0;
        if (s == &cpu_state.seg_ss)
            codegen_flat_ss = 0;
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
    if (s == &cpu_state.seg_ss) {
        if (s->base == 0 && s->limit_low == 0 && s->limit_high == 0xffffffff)
            cpu_cur_status &= ~CPU_STATUS_NOTFLATSS;
        else
            cpu_cur_status |= CPU_STATUS_NOTFLATSS;
    }

#ifdef USE_NEW_DYNAREC
    return cpu_state.abrt;
#endif
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
#ifdef USE_NEW_DYNAREC
            oldcpl = CPL;
#endif

            cpl_override = 1;
            writememw(0, addr + 4, segdat[2] | 0x0100); /* Set accessed bit */
            cpl_override = 0;
        } else {
            /* System segment */
            if (!(segdat[2] & 0x8000)) {
                x86np("Load CS system seg not present", seg & 0xfffc);
                return;
            }
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
#ifdef USE_NEW_DYNAREC
        oldcpl = CPL;
#endif
    }
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
            writememw(0, addr + 4, segdat[2] | 0x0100); /* Set accessed bit */
            cpl_override = 0;

            CS        = (seg & 0xfffc) | CPL;
            segdat[2] = (segdat[2] & ~(3 << 13)) | (CPL << 13);

            do_seg_load(&cpu_state.seg_cs, segdat);
            if ((CPL == 3) && (oldcpl != 3))
                flushmmucache_nopc();
#ifdef USE_NEW_DYNAREC
            oldcpl = CPL;
#endif
            cycles -= timing_jmp_pm;
        } else { /* System segment */
            if (!(segdat[2] & 0x8000)) {
                x86np("Load CS JMP system selector not present", seg & 0xfffc);
                return;
            }
            type  = segdat[2] & 0x0f00;
            newpc = segdat[0];
            if (type & 0x0800)
                newpc |= (segdat[3] << 16);
            switch (type) {
                case 0x0400: /* Call gate */
                case 0x0c00:
                    cgate32 = (type & 0x0800);
                    cgate16 = !cgate32;
#ifndef USE_NEW_DYNAREC
                    oldcs = CS;
#endif
                    cpu_state.oldpc = cpu_state.pc;
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
                            if (DPL > CPL) {
                                x86gpf("loadcsjmp(): Non-conforming DPL > CPL", seg2 & 0xfffc);
                                return;
                            }
                            fallthrough;
                        case 0x1c00:
                        case 0x1d00:
                        case 0x1e00:
                        case 0x1f00: /* Conforming */
                            CS = seg2;
                            do_seg_load(&cpu_state.seg_cs, segdat);
                            if ((CPL == 3) && (oldcpl != 3))
                                flushmmucache_nopc();
#ifdef USE_NEW_DYNAREC
                            oldcpl = CPL;
#endif
                            set_use32(segdat[3] & 0x40);
                            cpu_state.pc = newpc;

                            cpl_override = 1;
                            writememw(0, addr + 4, segdat[2] | 0x100); /* Set accessed bit */
                            cpl_override = 0;
                            break;

                        default:
                            x86gpf("loadcsjmp(): Unknown type", seg2 & 0xfffc);
                            return;
                    }
                    cycles -= timing_jmp_pm_gate;
                    break;

                case 0x100: /* 286 Task gate */
                case 0x900: /* 386 Task gate */
                    cpu_state.pc = old_pc;
                    optype       = JMP;
                    cpl_override = 1;
                    op_taskswitch286(seg, segdat, segdat[2] & 0x800);
                    cpu_state.flags &= ~NT_FLAG;
                    cpl_override = 0;
                    return;

                default:
                    x86gpf("Load CS JMP call gate selector unknown type", 0);
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
#ifdef USE_NEW_DYNAREC
        oldcpl = CPL;
#endif
        cycles -= timing_jmp_rm;
    }
}

static void
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

static void
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

static void
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

static uint16_t
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

static uint32_t
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
#ifdef USE_NEW_DYNAREC
void
loadcscall_2386(uint16_t seg, uint32_t old_pc)
#else
void
loadcscall_2386(uint16_t seg)
#endif
#else
#ifdef USE_NEW_DYNAREC
void
loadcscall(uint16_t seg, uint32_t old_pc)
#else
void
loadcscall(uint16_t seg)
#endif
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
            writememw(0, addr + 4, segdat[2] | 0x100); /* Set accessed bit */
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
#ifdef USE_NEW_DYNAREC
            oldcpl = CPL;
#endif
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
#ifndef USE_NEW_DYNAREC
                    oldcs = CS;
#endif
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
                        x86gpf("loadcscall(): ex Selector > DT limit", seg2 & 0xfff8);
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
#ifdef USE_NEW_DYNAREC
                                uint16_t oldcs = CS;
#endif
                                oaddr = addr;
                                /* Load new stack */
                                oldss = SS;
                                oldsp = oldsp2 = ESP;
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
                                    fatal("Bigger than DT limit %04X %08X %04X CSC SS\n", newss, addr, dt->limit);
                                    x86ts(NULL, newss & ~3);
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
                                writememw(0, addr + 4, segdat2[2] | 0x100); /* Set accessed bit */
                                cpl_override = 0;

                                CS = seg2;
                                do_seg_load(&cpu_state.seg_cs, segdat);
                                if ((CPL == 3) && (oldcpl != 3))
                                    flushmmucache_nopc();
#ifdef USE_NEW_DYNAREC
                                oldcpl = CPL;
#endif
                                set_use32(segdat[3] & 0x0040);
                                cpu_state.pc = newpc;

                                x86seg_log("Set access 2\n");

                                cpl_override = 1;
                                writememw(0, oaddr + 4, segdat[2] | 0x100); /* Set accessed bit */
                                cpl_override = 0;

                                x86seg_log("Type %04X\n", type);
                                if (type == 0x0c00) {
                                    is586 ? PUSHL(oldss) : PUSHL_SEL(oldss);
                                    PUSHL(oldsp2);
                                    if (cpu_state.abrt) {
                                        SS  = oldss;
                                        ESP = oldsp2;
#ifdef USE_NEW_DYNAREC
                                        CS = oldcs;
#endif
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
#ifdef USE_NEW_DYNAREC
                                                CS = oldcs;
#endif
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
#ifdef USE_NEW_DYNAREC
                                        CS = oldcs;
#endif
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
#ifdef USE_NEW_DYNAREC
                                                CS = oldcs;
#endif
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
                            CS = seg2;
                            do_seg_load(&cpu_state.seg_cs, segdat);
                            if ((CPL == 3) && (oldcpl != 3))
                                flushmmucache_nopc();
#ifdef USE_NEW_DYNAREC
                            oldcpl = CPL;
#endif
                            set_use32(segdat[3] & 0x0040);
                            cpu_state.pc = newpc;

                            cpl_override = 1;
                            writememw(0, addr + 4, segdat[2] | 0x100); /* Set accessed bit */
                            cpl_override = 0;
                            cycles -= timing_call_pm_gate;
                            break;

                        default:
                            x86gpf("loadcscall(): Unknown subtype", seg2 & 0xfffc);
                            return;
                    }
                    break;

                case 0x0100: /* 286 Task gate */
                case 0x0900: /* 386 Task gate */
#ifdef USE_NEW_DYNAREC
                    cpu_state.pc = old_pc;
#else
                    cpu_state.pc = oxpc;
#endif
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
#ifdef USE_NEW_DYNAREC
        oldcpl = CPL;
#endif
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
    if (cpu_state.abrt)
        return;

    x86seg_log("Return to %04X:%08X\n", seg, newpc);
    if ((seg & 0x0003) < CPL) {
        ESP = oldsp;
        x86gpf("pmoderetf(): seg < CPL", seg & 0xfffc);
        return;
    }
    if (!(seg & 0xfffc)) {
        x86gpf("pmoderetf(): seg is NULL", 0);
        return;
    }
    addr = seg & 0xfff8;
    dt   = (seg & 0x0004) ? &ldt : &gdt;
    if ((addr + 7) > dt->limit) {
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
                x86gpf("pmoderetf(): Unknown type", seg & 0xfffc);
                return;
        }
        if (!(segdat[2] & 0x8000)) {
            ESP = oldsp;
            x86np("RETF CS not present", seg & 0xfffc);
            return;
        }

        cpl_override = 1;
        writememw(0, addr + 4, segdat[2] | 0x100); /* Set accessed bit */
        cpl_override = 0;

        cpu_state.pc = newpc;
        if (segdat[2] & 0x0400)
            segdat[2] = (segdat[2] & ~(3 << 13)) | ((seg & 3) << 13);
        CS = seg;
        do_seg_load(&cpu_state.seg_cs, segdat);
        cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~(3 << 5)) | ((CS & 3) << 5);
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
#ifdef USE_NEW_DYNAREC
        oldcpl = CPL;
#endif
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
            if (cpu_state.abrt)
                return;
        } else {
            x86seg_log("SP read from %04X:%04X\n", SS, SP);
            newsp = POPW();
            x86seg_log("SS read from %04X:%04X\n", SS, SP);
            newss = POPW();
            if (cpu_state.abrt)
                return;
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
        if (!(segdat2[2] & 0x8000)) {
            ESP = oldsp;
            x86np("RETF loading SS not present", newss & 0xfffc);
            return;
        }
        if (DPL2 != (seg & 3)) {
            ESP = oldsp;
            x86gpf("pmoderetf(): New SS DPL != CS RPL", newss & 0xfffc);
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
        writememw(0, addr + 4, segdat2[2] | 0x100); /* Set accessed bit */
        writememw(0, oaddr + 4, segdat[2] | 0x100); /* Set accessed bit */
        cpl_override = 0;
        /* Conforming segments don't change CPL, so CPL = RPL */
        if (segdat[2] & 0x0400)
            segdat[2] = (segdat[2] & ~(3 << 13)) | ((seg & 3) << 13);
        cpu_state.pc = newpc;
        CS           = seg;
        do_seg_load(&cpu_state.seg_cs, segdat);
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
#ifdef USE_NEW_DYNAREC
        oldcpl = CPL;
#endif
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

    if ((cpu_state.eflags & VM_FLAG) && (IOPL != 3) && soft) {
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
        } else if (num == 0x0d)
            op_pmodeint(8, 0);
        else
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
            x86gpf_expected("pmodeint(): Expected vector descriptor with bad type", (num << 3) + 2);
        else
            x86gpf("pmodeint(): Vector descriptor with bad type", (num << 3) + 2);
        return;
    }
    if ((DPL < CPL) && soft) {
        x86gpf("pmodeint(): Vector DPL < CPL", (num << 3) + 2);
        return;
    }
    type = segdat[2] & 0x1f00;
    if (((type == 0x0e00) || (type == 0x0f00)) && !is386) {
        x86gpf("pmodeint(): Gate type illegal on 286", seg & 0xfffc);
        return;
    }
    switch (type) {
        case 0x0600:
        case 0x0700:
        case 0x0e00:
        case 0x0f00: /* Interrupt and trap gates */
            intgatesize = (type >= 0x0800) ? 32 : 16;
            if (!(segdat[2] & 0x8000)) {
                x86np("Int gate not present", (num << 3) | 2);
                return;
            }
            seg     = segdat[1];
            new_cpl = seg & 0x0003;

            addr = seg & 0xfff8;
            dt   = (seg & 0x0004) ? &ldt : &gdt;
            if ((addr + 7) > dt->limit) {
                x86gpf("pmodeint(): Interrupt or trap gate selector > DT limit", seg & 0xfffc);
                return;
            }
            addr += dt->base;
            read_descriptor(addr, segdat2, segdat232, 1);
            if (cpu_state.abrt)
                return;
            oaddr = addr;

            if (DPL2 > CPL) {
                x86gpf("pmodeint(): Interrupt or trap gate DPL > CPL", seg & 0xfffc);
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
                            x86np("Int gate CS not present", segdat[1] & 0xfffc);
                            return;
                        }
                        if ((cpu_state.eflags & VM_FLAG) && DPL2) {
                            x86gpf("pmodeint(): Interrupt or trap gate non-zero DPL in V86 mode", segdat[1] & 0xfffc);
                            return;
                        }
                        /* Load new stack */
                        oldss        = SS;
                        oldsp        = ESP;
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
                        if (!(newss & 0xfffc)) {
                            x86ss("pmodeint(): Interrupt or trap gate stack segment is NULL", newss & 0xfffc);
                            return;
                        }
                        addr = newss & 0xfff8;
                        dt   = (newss & 0x0004) ? &ldt : &gdt;
                        if ((addr + 7) > dt->limit) {
                            x86ss("pmodeint(): Interrupt or trap gate stack segment > DT", newss & 0xfffc);
                            return;
                        }
                        addr += dt->base;
                        read_descriptor(addr, segdat3, segdat332, 1);
                        if (cpu_state.abrt)
                            return;
                        if ((newss & 3) != DPL2) {
                            x86ss("pmodeint(): Interrupt or trap gate tack segment RPL > DPL", newss & 0xfffc);
                            return;
                        }
                        if (DPL3 != DPL2) {
                            x86ss("pmodeint(): Interrupt or trap gate tack segment DPL > DPL", newss & 0xfffc);
                            return;
                        }
                        if ((segdat3[2] & 0x1a00) != 0x1200) {
                            x86ss("pmodeint(): Interrupt or trap gate stack segment bad type", newss & 0xfffc);
                            return;
                        }
                        if (!(segdat3[2] & 0x8000)) {
                            x86np("Int gate loading SS not present", newss & 0xfffc);
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
                        writememw(0, addr + 4, segdat3[2] | 0x100); /* Set accessed bit */
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
                                op_loadseg(0, &cpu_state.seg_ds);
                                op_loadseg(0, &cpu_state.seg_es);
                                op_loadseg(0, &cpu_state.seg_fs);
                                op_loadseg(0, &cpu_state.seg_gs);
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
                        x86gpf("pmodeint(): DPL != CPL", seg & 0xfffc);
                        return;
                    }
                    fallthrough;
                case 0x1c00:
                case 0x1d00:
                case 0x1e00:
                case 0x1f00: /* Conforming */
                    if (!(segdat2[2] & 0x8000)) {
                        x86np("Int gate CS not present", segdat[1] & 0xfffc);
                        return;
                    }
                    if ((cpu_state.eflags & VM_FLAG) && (DPL2 < CPL)) {
                        x86gpf("pmodeint(): DPL < CPL in V86 mode", seg & ~0xfffc);
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
                    x86gpf("pmodeint(): Unknown type", seg & 0xfffc);
                    return;
            }
            do_seg_load(&cpu_state.seg_cs, segdat2);
            CS                      = (seg & 0xfffc) | new_cpl;
            cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | (new_cpl << 5);
            if ((CPL == 3) && (oldcpl != 3))
                flushmmucache_nopc();
#ifdef USE_NEW_DYNAREC
            oldcpl = CPL;
#endif
            if (type > 0x0800)
                cpu_state.pc = segdat[0] | (segdat[3] << 16);
            else
                cpu_state.pc = segdat[0];
            set_use32(segdat2[3] & 0x40);

            cpl_override = 1;
            writememw(0, oaddr + 4, segdat2[2] | 0x100); /* Set accessed bit */
            cpl_override = 0;

            cpu_state.eflags &= ~VM_FLAG;
            cpu_cur_status &= ~CPU_STATUS_V86;
            if (!(type & 0x100))
                cpu_state.flags &= ~I_FLAG;
            cpu_state.flags &= ~(T_FLAG | NT_FLAG);
            cycles -= timing_int_pm;
            break;

        case 0x500: /* Task gate */
            seg  = segdat[1];
            addr = seg & 0xfff8;
            dt   = (seg & 0x0004) ? &ldt : &gdt;
            if ((addr + 7) > dt->limit) {
                x86gpf("pmodeint(): Task gate selector > DT limit", seg & 0xfffc);
                return;
            }
            addr += dt->base;
            read_descriptor(addr, segdat2, segdat232, 1);
            if (cpu_state.abrt)
                return;
            if (!(segdat2[2] & 0x8000)) {
                x86np("Int task gate not present", segdat[1] & 0xfffc);
                return;
            }
            optype       = OPTYPE_INT;
            cpl_override = 1;
            op_taskswitch286(seg, segdat2, segdat2[2] & 0x0800);
            cpl_override = 0;
            break;

        default:
            x86gpf("Protected mode interrupt unknown type", seg & 0xfffc);
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
#ifndef USE_NEW_DYNAREC
        oxpc = cpu_state.pc;
#endif
        if (is32) {
            newpc     = POPL();
            seg       = POPL();
            tempflags = POPL();
        } else {
            newpc     = POPW();
            seg       = POPW();
            tempflags = POPW();
        }
        if (cpu_state.abrt)
            return;

        cpu_state.pc                = newpc;
        cpu_state.seg_cs.base       = seg << 4;
        cpu_state.seg_cs.limit      = 0xffff;
        cpu_state.seg_cs.limit_low  = 0;
        cpu_state.seg_cs.limit_high = 0xffff;
        cpu_state.seg_cs.access |= 0x80;
        cpu_state.seg_cs.ar_high = 0x10;
        CS                       = seg;
        cpu_state.flags          = (cpu_state.flags & 0x3000) | (tempflags & 0xcfd5) | 2;
        cycles -= timing_iret_rm;
        return;
    }

    if (cpu_state.flags & NT_FLAG) {
        cpl_override = 1;
        seg  = readmemw(tr.base, 0);
        cpl_override = 0;
        addr = seg & 0xfff8;
        if (seg & 0x0004) {
            x86seg_log("TS LDT %04X %04X IRET\n", seg, gdt.limit);
            x86ts("pmodeiret(): Selector points to LDT", seg & 0xfffc);
            return;
        } else {
            if ((addr + 7) > gdt.limit) {
                x86ts(NULL, seg & 0xfffc);
                return;
            }
            addr += gdt.base;
        }
        read_descriptor(addr, segdat, segdat32, 1);
        cpl_override = 1;
        op_taskswitch286(seg, segdat, segdat[2] & 0x0800);
        cpl_override = 0;
        return;
    }

#ifndef USE_NEW_DYNAREC
    oxpc = cpu_state.pc;
#endif
    flagmask = 0xffff;
    if (CPL != 0)
        flagmask &= ~0x3000;
    if (IOPL < CPL)
        flagmask &= ~0x200;
    if (is32) {
        newpc     = POPL();
        seg       = POPL();
        tempflags = POPL();
        if (cpu_state.abrt) {
            ESP = oldsp;
            return;
        }
        if (is386 && ((tempflags >> 16) & VM_FLAG)) {
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
            cpu_state.eflags = tempflags >> 16;
            cpu_cur_status |= CPU_STATUS_V86;
            op_loadseg(segs[0], &cpu_state.seg_es);
            do_seg_v86_init(&cpu_state.seg_es);
            op_loadseg(segs[1], &cpu_state.seg_ds);
            do_seg_v86_init(&cpu_state.seg_ds);
            cpu_cur_status |= CPU_STATUS_NOTFLATDS;
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
#ifdef USE_NEW_DYNAREC
            oldcpl = CPL;
#endif

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
#ifdef USE_NEW_DYNAREC
        oldcpl = CPL;
#endif
        set_use32(segdat[3] & 0x0040);

        cpl_override = 1;
        writememw(0, addr + 4, segdat[2] | 0x100); /* Set accessed bit */
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
            SP = oldsp;
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
        writememw(0, addr + 4, segdat2[2] | 0x100); /* Set accessed bit */
        writememw(0, oaddr + 4, segdat[2] | 0x100); /* Set accessed bit */
        cpl_override = 0;
        /* Conforming segments don't change CPL, so CPL = RPL */
        if (segdat[2] & 0x0400)
            segdat[2] = (segdat[2] & ~(3 << 13)) | ((seg & 3) << 13);

        CS = seg;
        do_seg_load(&cpu_state.seg_cs, segdat);
        cpu_state.seg_cs.access = (cpu_state.seg_cs.access & ~0x60) | ((CS & 3) << 5);
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
#ifdef USE_NEW_DYNAREC
        oldcpl = CPL;
#endif
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
        cpu_state.eflags = tempflags >> 16;
}

void
#ifdef OPS_286_386
taskswitch286_2386(uint16_t seg, uint16_t *segdat, int is32)
#else
taskswitch286(uint16_t seg, uint16_t *segdat, int is32)
#endif
{
    uint16_t      tempw;
    uint16_t      new_ldt;
    uint16_t      new_es;
    uint16_t      new_cs;
    uint16_t      new_ss;
    uint16_t      new_ds;
    uint16_t      new_fs;
    uint16_t      new_gs;
    uint16_t      segdat2[4];
    uint32_t      base;
    uint32_t      limit;
    uint32_t      templ;
    uint32_t      new_cr3 = 0;
    uint32_t      new_eax;
    uint32_t      new_ebx;
    uint32_t      new_ecx;
    uint32_t      new_edx;
    uint32_t      new_esp;
    uint32_t      new_ebp;
    uint32_t      new_esi;
    uint32_t      new_edi;
    uint32_t      new_pc;
    uint32_t      new_flags;
    uint32_t      t_bit;
    uint32_t      addr;
    uint32_t     *segdat232 = (uint32_t *) segdat2;
    const x86seg *dt;

    base  = segdat[1] | ((segdat[2] & 0x00ff) << 16);
    limit = segdat[0];
    if (is386) {
        base |= (segdat[3] >> 8) << 24;
        limit |= (segdat[3] & 0x000f) << 16;
    }

    if (is32) {
        if (limit < 103) {
            x86ts("taskswitch286(): limit < 103", seg);
            return;
        }

        if ((optype == JMP) || (optype == CALL) || (optype == OPTYPE_INT)) {
            if (tr.seg & 0x0004)
                tempw = readmemw(ldt.base, (seg & 0xfff8) + 4);
            else
                tempw = readmemw(gdt.base, (seg & 0xfff8) + 4);
            if (cpu_state.abrt)
                return;
            tempw |= 0x0200;
            if (tr.seg & 0x0004)
                writememw(ldt.base, (seg & 0xfff8) + 4, tempw);
            else
                writememw(gdt.base, (seg & 0xfff8) + 4, tempw);
        }
        if (cpu_state.abrt)
            return;

        if (optype == IRET)
            cpu_state.flags &= ~NT_FLAG;

        cpu_386_flags_rebuild();
        writememl(tr.base, 0x1C, cr3);
        writememl(tr.base, 0x20, cpu_state.pc);
        writememl(tr.base, 0x24, cpu_state.flags | (cpu_state.eflags << 16));

        writememl(tr.base, 0x28, EAX);
        writememl(tr.base, 0x2C, ECX);
        writememl(tr.base, 0x30, EDX);
        writememl(tr.base, 0x34, EBX);
        writememl(tr.base, 0x38, ESP);
        writememl(tr.base, 0x3C, EBP);
        writememl(tr.base, 0x40, ESI);
        writememl(tr.base, 0x44, EDI);

        writememl(tr.base, 0x48, ES);
        writememl(tr.base, 0x4C, CS);
        writememl(tr.base, 0x50, SS);
        writememl(tr.base, 0x54, DS);
        writememl(tr.base, 0x58, FS);
        writememl(tr.base, 0x5C, GS);

        if ((optype == JMP) || (optype == IRET)) {
            if (tr.seg & 0x0004)
                tempw = readmemw(ldt.base, (tr.seg & 0xfff8) + 4);
            else
                tempw = readmemw(gdt.base, (tr.seg & 0xfff8) + 4);
            if (cpu_state.abrt)
                return;
            tempw &= ~0x0200;
            if (tr.seg & 0x0004)
                writememw(ldt.base, (tr.seg & 0xfff8) + 4, tempw);
            else
                writememw(gdt.base, (tr.seg & 0xfff8) + 4, tempw);
        }
        if (cpu_state.abrt)
            return;

        if ((optype == OPTYPE_INT) || (optype == CALL)) {
            writememl(base, 0, tr.seg);
            if (cpu_state.abrt)
                return;
        }

        new_cr3   = readmeml(base, 0x1C);
        new_pc    = readmeml(base, 0x20);
        new_flags = readmeml(base, 0x24);
        if ((optype == OPTYPE_INT) || (optype == CALL))
            new_flags |= NT_FLAG;

        new_eax = readmeml(base, 0x28);
        new_ecx = readmeml(base, 0x2C);
        new_edx = readmeml(base, 0x30);
        new_ebx = readmeml(base, 0x34);
        new_esp = readmeml(base, 0x38);
        new_ebp = readmeml(base, 0x3C);
        new_esi = readmeml(base, 0x40);
        new_edi = readmeml(base, 0x44);

        new_es  = readmemw(base, 0x48);
        new_cs  = readmemw(base, 0x4C);
        new_ss  = readmemw(base, 0x50);
        new_ds  = readmemw(base, 0x54);
        new_fs  = readmemw(base, 0x58);
        new_gs  = readmemw(base, 0x5C);
        new_ldt = readmemw(base, 0x60);
        t_bit   = readmemb(base, 0x64) & 1;

        cr0 |= 8;

        cr3 = new_cr3;
        flushmmucache();

        cpu_state.pc     = new_pc;
        cpu_state.flags  = new_flags;
        cpu_state.eflags = new_flags >> 16;
        cpu_386_flags_extract();

        ldt.seg   = new_ldt;
        templ     = (ldt.seg & ~7) + gdt.base;
        ldt.limit = readmemw(0, templ);
        if (readmemb(0, templ + 6) & 0x80) {
            ldt.limit <<= 12;
            ldt.limit |= 0xfff;
        }
        ldt.base = (readmemw(0, templ + 2)) | (readmemb(0, templ + 4) << 16) | (readmemb(0, templ + 7) << 24);

        if (cpu_state.eflags & VM_FLAG) {
            op_loadcs(new_cs);
            set_use32(0);
            cpu_cur_status |= CPU_STATUS_V86;
        } else {
            if (!(new_cs & 0xfffc)) {
                x86ts("taskswitch286(): New CS selector is null", 0);
                return;
            }
            addr = new_cs & 0xfff8;
            dt   = (new_cs & 0x0004) ? &ldt : &gdt;
            if ((addr + 7) > dt->limit) {
                x86ts("taskswitch286(): New CS selector > DT limit", new_cs & 0xfffc);
                return;
            }
            addr += dt->base;
            read_descriptor(addr, segdat2, segdat232, 0);
            if (!(segdat2[2] & 0x8000)) {
                x86np("TS loading CS not present", new_cs & 0xfffc);
                return;
            }
            switch (segdat2[2] & 0x1f00) {
                case 0x1800:
                case 0x1900:
                case 0x1a00:
                case 0x1b00: /* Non-conforming */
                    if ((new_cs & 0x0003) != DPL2) {
                        x86ts("TS loading CS RPL != DPL2", new_cs & 0xfffc);
                        return;
                    }
                    break;
                case 0x1c00:
                case 0x1d00:
                case 0x1e00:
                case 0x1f00: /* Conforming */
                    if ((new_cs & 0x0003) < DPL2) {
                        x86ts("TS loading CS RPL < DPL2", new_cs & 0xfffc);
                        return;
                    }
                    break;
                default:
                    x86ts("TS loading CS unknown type", new_cs & 0xfffc);
                    return;
            }

            CS = new_cs;
            do_seg_load(&cpu_state.seg_cs, segdat2);
            if ((CPL == 3) && (oldcpl != 3))
                flushmmucache_nopc();
#ifdef USE_NEW_DYNAREC
            oldcpl = CPL;
#endif
            set_use32(segdat2[3] & 0x0040);
            cpu_cur_status &= ~CPU_STATUS_V86;
        }

        EAX = new_eax;
        ECX = new_ecx;
        EDX = new_edx;
        EBX = new_ebx;
        ESP = new_esp;
        EBP = new_ebp;
        ESI = new_esi;
        EDI = new_edi;

        op_loadseg(new_es, &cpu_state.seg_es);
        op_loadseg(new_ss, &cpu_state.seg_ss);
        op_loadseg(new_ds, &cpu_state.seg_ds);
        op_loadseg(new_fs, &cpu_state.seg_fs);
        op_loadseg(new_gs, &cpu_state.seg_gs);

        if (!cpu_use_exec)
            rf_flag_no_clear = 1;

        if (t_bit) {
            if (cpu_use_exec)
                trap = 2;
            else
                trap |= 2;
#ifdef USE_DYNAREC
            cpu_block_end = 1;
#endif
        }
    } else {
        if (limit < 43) {
            x86ts(NULL, seg);
            return;
        }

        if ((optype == JMP) || (optype == CALL) || (optype == OPTYPE_INT)) {
            if (tr.seg & 0x0004)
                tempw = readmemw(ldt.base, (seg & 0xfff8) + 4);
            else
                tempw = readmemw(gdt.base, (seg & 0xfff8) + 4);
            if (cpu_state.abrt)
                return;
            tempw |= 0x200;
            if (tr.seg & 0x0004)
                writememw(ldt.base, (seg & 0xfff8) + 4, tempw);
            else
                writememw(gdt.base, (seg & 0xfff8) + 4, tempw);
        }
        if (cpu_state.abrt)
            return;

        if (optype == IRET)
            cpu_state.flags &= ~NT_FLAG;

        cpu_386_flags_rebuild();
        writememw(tr.base, 0x0e, cpu_state.pc);
        writememw(tr.base, 0x10, cpu_state.flags);

        writememw(tr.base, 0x12, AX);
        writememw(tr.base, 0x14, CX);
        writememw(tr.base, 0x16, DX);
        writememw(tr.base, 0x18, BX);
        writememw(tr.base, 0x1a, SP);
        writememw(tr.base, 0x1c, BP);
        writememw(tr.base, 0x1e, SI);
        writememw(tr.base, 0x20, DI);

        writememw(tr.base, 0x22, ES);
        writememw(tr.base, 0x24, CS);
        writememw(tr.base, 0x26, SS);
        writememw(tr.base, 0x28, DS);

        if ((optype == JMP) || (optype == IRET)) {
            if (tr.seg & 0x0004)
                tempw = readmemw(ldt.base, (tr.seg & 0xfff8) + 4);
            else
                tempw = readmemw(gdt.base, (tr.seg & 0xfff8) + 4);
            if (cpu_state.abrt)
                return;
            tempw &= ~0x200;
            if (tr.seg & 0x0004)
                writememw(ldt.base, (tr.seg & 0xfff8) + 4, tempw);
            else
                writememw(gdt.base, (tr.seg & 0xfff8) + 4, tempw);
        }
        if (cpu_state.abrt)
            return;

        if ((optype == OPTYPE_INT) || (optype == CALL)) {
            writememw(base, 0, tr.seg);
            if (cpu_state.abrt)
                return;
        }

        new_pc    = readmemw(base, 0x0e);
        new_flags = readmemw(base, 0x10);
        if ((optype == OPTYPE_INT) || (optype == CALL))
            new_flags |= NT_FLAG;

        new_eax = readmemw(base, 0x12);
        new_ecx = readmemw(base, 0x14);
        new_edx = readmemw(base, 0x16);
        new_ebx = readmemw(base, 0x18);
        new_esp = readmemw(base, 0x1a);
        new_ebp = readmemw(base, 0x1c);
        new_esi = readmemw(base, 0x1e);
        new_edi = readmemw(base, 0x20);

        new_es  = readmemw(base, 0x22);
        new_cs  = readmemw(base, 0x24);
        new_ss  = readmemw(base, 0x26);
        new_ds  = readmemw(base, 0x28);
        new_ldt = readmemw(base, 0x2a);

        msw |= 8;

        cpu_state.pc    = new_pc;
        cpu_state.flags = new_flags;
        cpu_386_flags_extract();

        ldt.seg   = new_ldt;
        templ     = (ldt.seg & 0xfff8) + gdt.base;
        ldt.limit = readmemw(0, templ);
        ldt.base  = (readmemw(0, templ + 2)) | (readmemb(0, templ + 4) << 16);
        if (is386) {
            if (readmemb(0, templ + 6) & 0x80) {
                ldt.limit <<= 12;
                ldt.limit |= 0xfff;
            }
            ldt.base |= (readmemb(0, templ + 7) << 24);
        }

        if (!(new_cs & 0xfff8) && !(new_cs & 0x0004)) {
            x86ts(NULL, 0);
            return;
        }
        addr = new_cs & 0xfff8;
        dt   = (new_cs & 0x0004) ? &ldt : &gdt;
        if ((addr + 7) > dt->limit) {
            x86ts(NULL, new_cs & 0xfffc);
            return;
        }
        addr += dt->base;
        read_descriptor(addr, segdat2, segdat232, 0);
        if (!(segdat2[2] & 0x8000)) {
            x86np("TS loading CS not present", new_cs & 0xfffc);
            return;
        }
        switch (segdat2[2] & 0x1f00) {
            case 0x1800:
            case 0x1900:
            case 0x1a00:
            case 0x1b00: /* Non-conforming */
                if ((new_cs & 0x0003) != DPL2) {
                    x86ts(NULL, new_cs & 0xfffc);
                    return;
                }
                break;
            case 0x1c00:
            case 0x1d00:
            case 0x1e00:
            case 0x1f00: /* Conforming */
                if ((new_cs & 0x0003) < DPL2) {
                    x86ts(NULL, new_cs & 0xfffc);
                    return;
                }
                break;
            default:
                x86ts(NULL, new_cs & 0xfffc);
                return;
        }

        CS = new_cs;
        do_seg_load(&cpu_state.seg_cs, segdat2);
        if ((CPL == 3) && (oldcpl != 3))
            flushmmucache_nopc();
#ifdef USE_NEW_DYNAREC
        oldcpl = CPL;
#endif
        set_use32(0);

        EAX = new_eax | 0xffff0000;
        ECX = new_ecx | 0xffff0000;
        EDX = new_edx | 0xffff0000;
        EBX = new_ebx | 0xffff0000;
        ESP = new_esp | 0xffff0000;
        EBP = new_ebp | 0xffff0000;
        ESI = new_esi | 0xffff0000;
        EDI = new_edi | 0xffff0000;

        op_loadseg(new_es, &cpu_state.seg_es);
        op_loadseg(new_ss, &cpu_state.seg_ss);
        op_loadseg(new_ds, &cpu_state.seg_ds);
        if (is386) {
            op_loadseg(0, &cpu_state.seg_fs);
            op_loadseg(0, &cpu_state.seg_gs);
        }
    }

    tr.seg     = seg;
    tr.base    = base;
    tr.limit   = limit;
    tr.access  = segdat[2] >> 8;
    tr.ar_high = segdat[3] & 0xff;
    if (!cpu_use_exec)
        dr[7] &= 0xFFFFFFAA;
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
