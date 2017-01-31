/* Copyright holders: Tenshi
   see COPYING for more details
*/
/*
	0xE1 and 0xE2 Memory Registers
	Used by just about any emulated machine
*/

#include "ibm.h"

#include "io.h"
#include "memregs.h"

static uint8_t mem_regs[2] = {0xFF, 0xFF};

static uint8_t mem_reg_ffff = 0;

void memregs_write(uint16_t port, uint8_t val, void *priv)
{
	if (port == 0xffff)
	{
		mem_reg_ffff = 0;
	}

	mem_regs[(port & 1) ^ 1] = val;
}

uint8_t memregs_read(uint16_t port, void *priv)
{
	if (port == 0xffff)
	{
		return mem_reg_ffff;
	}

	return mem_regs[(port & 1) ^ 1];
}

void memregs_init()
{
	pclog("Memory Registers Init\n");

        io_sethandler(0x00e1, 0x0002, memregs_read, NULL, NULL, memregs_write, NULL, NULL,  NULL);
}

void powermate_memregs_init()
{
	pclog("Memory Registers Init\n");

        io_sethandler(0x00ed, 0x0002, memregs_read, NULL, NULL, memregs_write, NULL, NULL,  NULL);
        io_sethandler(0xffff, 0x0001, memregs_read, NULL, NULL, memregs_write, NULL, NULL,  NULL);
}
