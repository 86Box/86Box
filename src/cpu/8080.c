/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		8080 CPU emulation.
 *
 * Authors:	Cacodemon345
 *
 *		Copyright 2022 Cacodemon345
 */


#include <stdint.h>
#include <stdlib.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/i8080.h>
#include <86box/mem.h>

static int prefetching = 1, completed = 1;
static int in_rep = 0, repeating = 0, rep_c_flag = 0;
static int oldc, clear_lock = 0;
static int refresh = 0, cycdiff;

static void
clock_start(void)
{
    cycdiff = cycles;
}


static void
clock_end(void)
{
    int diff = cycdiff - cycles;

    /* On 808x systems, clock speed is usually crystal frequency divided by an integer. */
    tsc += (uint64_t)diff * ((uint64_t)xt_cpu_multi >> 32ULL);		/* Shift xt_cpu_multi by 32 bits to the right and then multiply. */
    if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t)tsc))
	timer_process();
}

static void
wait(int c, int bus)
{
    cycles -= c;
    if (bus < 2) {
	    clock_end();
	    clock_start();
    }
}

static uint8_t
readmemb(uint32_t a)
{
    uint8_t ret;

    wait(4, 1);
    ret = read_mem_b(a);

    return ret;
}

static uint8_t
ins_fetch(i8080* cpu)
{
    uint8_t ret = readmemb(cpu->pmembase + cpu->pc);

    cpu->pc++;
    return ret;
}

void
transfer_from_808x(i8080* cpu)
{
    cpu->hl = BX;
    cpu->bc = CX;
    cpu->de = DX;
    cpu->a = AL;
    cpu->flags = cpu_state.flags & 0xFF;
    cpu->sp = BP;
    cpu->pc = cpu_state.pc;
    cpu->oldpc = cpu_state.oldpc;
    cpu->pmembase = cs;
    cpu->dmembase = ds;
}

void
transfer_to_808x(i8080* cpu)
{
    BX = cpu->hl;
    CX = cpu->bc;
    DX = cpu->de;
    AL = cpu->a;
    cpu_state.flags &= 0xFF00;
    cpu_state.flags |= cpu->flags & 0xFF;
    BP = cpu->sp;
    cpu_state.pc = cpu->pc;
}

uint8_t
getreg_i8080(i8080 *cpu, uint8_t reg)
{
    uint8_t ret = 0xFF;
    switch(reg)
    {
        case 0x0: ret = cpu->b; break;
        case 0x1: ret = cpu->c; break;
        case 0x2: ret = cpu->d; break;
        case 0x3: ret = cpu->e; break;
        case 0x4: ret = cpu->h; break;
        case 0x5: ret = cpu->l; break;
        case 0x6: ret = readmemb(cpu->dmembase + cpu->sp); break;
        case 0x7: ret = cpu->a; break;
    }
    return ret;
}

void
interpret_exec8080(i8080* cpu, uint8_t opcode, uint8_t (*fetch_instruction)(i8080*))
{
    switch (opcode) {
        case 0x00:
        {
            break;
        }
    }
}

/* Actually implement i8080 emulation. */
void
exec8080(i8080* cpu, int cycs)
{
    uint8_t temp = 0, temp2;
    uint8_t old_af;
	uint8_t handled = 0;
    uint16_t addr, tempw;
    uint16_t new_ip;
    int bits;

    cycles += cycs;

    while (cycles > 0) {
        clock_start();

        if (!repeating) {
            cpu->oldpc = cpu->pc;
            opcode = ins_fetch(cpu);
            oldc = cpu->flags & C_FLAG_I8080;
            wait(1, 0);
        }
        completed = 1;
        if (completed) {
            repeating = 0;
            in_rep = 0;
            rep_c_flag = 0;
            clock_end();
            //check_interrupts();
        }
    }
}