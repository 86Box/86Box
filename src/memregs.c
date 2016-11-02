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

void memregs_write(uint16_t port, uint8_t val, void *priv)
{
	mem_regs[port - 0xE1] = val;
}

uint8_t memregs_read(uint16_t port, void *priv)
{
	return mem_regs[port - 0xE1];
}

void memregs_init()
{
	pclog("Memory Registers Init\n");

        io_sethandler(0x00e1, 0x0002, memregs_read, NULL, NULL, memregs_write, NULL, NULL,  NULL);
}