/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the memory I/O scratch registers on ports 0xE1
 *		and 0xE2, used by just about any emulated machine.
 *
 * Version:	@(#)memregs.c	1.0.3	2017/10/16
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "ibm.h"
#include "io.h"
#include "memregs.h"


static uint8_t mem_regs[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static uint8_t mem_reg_ffff = 0;


void memregs_write(uint16_t port, uint8_t val, void *priv)
{
	if (port == 0xffff)
	{
		mem_reg_ffff = 0;
	}

	mem_regs[port & 0xf] = val;
}

uint8_t memregs_read(uint16_t port, void *priv)
{
	if (port == 0xffff)
	{
		return mem_reg_ffff;
	}

	return mem_regs[port & 0xf];
}

void memregs_init(void)
{
	pclog("Memory Registers Init\n");

        io_sethandler(0x00e1, 0x0002, memregs_read, NULL, NULL, memregs_write, NULL, NULL,  NULL);
}

void powermate_memregs_init(void)
{
	pclog("Memory Registers Init\n");

        io_sethandler(0x00ed, 0x0002, memregs_read, NULL, NULL, memregs_write, NULL, NULL,  NULL);
        io_sethandler(0xffff, 0x0001, memregs_read, NULL, NULL, memregs_write, NULL, NULL,  NULL);
}
