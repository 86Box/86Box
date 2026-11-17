/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          x86 CPU segment emulation commmon parts.
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
#include "x86.h"
#include "x86seg_common.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

uint8_t opcode2;

int cgate16;
int cgate32;
int cgate_task;

int intgatesize;

call_gate_undo_t call_gate_undo;

#define CALL_GATE_STATUS (CPU_STATUS_USE32 | CPU_STATUS_STACK32 | CPU_STATUS_NOTFLATSS | CPU_STATUS_NOTWIDESS | CPU_STATUS_NOT64K)

/* A CALL through a call gate to a more privileged level loads the new CS,
   the new stack and the new CPL before it pushes the caller's SS:ESP, copies
   the parameters and pushes the return address (Pentium manual vol. 3
   p. 25-53). A fault in any of those leaves the state from before the CALL
   (p. 14-2), so what the transfer changed is kept here to be put back. */
void
call_gate_undo_save(void)
{
    call_gate_undo.stack_seg = cpu_state.seg_ss;
    call_gate_undo.code_seg  = cpu_state.seg_cs;
    call_gate_undo.esp       = ESP;
    call_gate_undo.stack32   = stack32;
    call_gate_undo.use32     = use32;
    call_gate_undo.status    = cpu_cur_status;
    call_gate_undo.oldcpl    = oldcpl;
    call_gate_undo.active    = 1;
}

void
call_gate_undo_restore(void)
{
    cpu_state.seg_ss = call_gate_undo.stack_seg;
    cpu_state.seg_cs = call_gate_undo.code_seg;
    mem_held_release();
    ESP              = call_gate_undo.esp;
    stack32          = call_gate_undo.stack32;
    use32            = call_gate_undo.use32;
    cpu_cur_status   = (cpu_cur_status & ~CALL_GATE_STATUS) | (call_gate_undo.status & CALL_GATE_STATUS);
    /* Back in ring 3: what the transfer added to the lookup tables at the
       gate's level skipped the ring 3 checks of the page walk */
    if ((CPL == 3) && (oldcpl != 3))
        flushmmucache_ring3();
    oldcpl           = call_gate_undo.oldcpl;
    call_gate_undo.active = 0;
}

static void
seg_reset(x86seg *s)
{
    s->access     = 0x82;
    s->ar_high    = 0x10;
    s->limit      = 0xffff;
    s->limit_low  = 0;
    s->limit_high = 0xffff;
    if (s == &cpu_state.seg_cs) {
        if (!cpu_inited)
            fatal("seg_reset(&cpu_state.seg.cs) without an initialized CPU\n");
        if (is6117)
            s->base = 0x00ff0000;
        else if (is386ex)
            s->base = 0x03ff0000;
        else
            s->base = is286 ? (cpu_16bitbus ? 0x00ff0000 : 0xffff0000) : 0x000ffff0;
        s->seg = is286 ? 0xf000 : 0xffff;
    } else {
        s->base = 0;
        s->seg  = 0;
    }
}

void
x86seg_reset(void)
{
    seg_reset(&cpu_state.seg_cs);
    mem_held_release();
    seg_reset(&cpu_state.seg_ds);
    seg_reset(&cpu_state.seg_es);
    seg_reset(&cpu_state.seg_fs);
    seg_reset(&cpu_state.seg_gs);
    seg_reset(&cpu_state.seg_ss);
}

void
x86de(UNUSED(char *s), UNUSED(uint16_t error))
{
#ifdef BAD_CODE
    cpu_state.abrt = ABRT_DE;
    abrt_error     = error;
#else
    x86_int(0);
#endif
}

void
x86gen(void)
{
    x86_int(1);
}

void
x86gpf(UNUSED(char *s), uint16_t error)
{
    cpu_state.abrt = ABRT_GPF;
    abrt_error     = error;
}

void
x86gpf_expected(UNUSED(char *s), uint16_t error)
{
    cpu_state.abrt = ABRT_GPF | ABRT_EXPECTED;
    abrt_error     = error;
}

void
x86ss(UNUSED(char *s), uint16_t error)
{
    cpu_state.abrt = ABRT_SS;
    abrt_error     = error;
}

void
x86ts(UNUSED(char *s), uint16_t error)
{
    cpu_state.abrt = ABRT_TS;
    abrt_error     = error;
}

void
x86np(UNUSED(char *s), uint16_t error)
{
    cpu_state.abrt = ABRT_NP;
    abrt_error     = error;
}
