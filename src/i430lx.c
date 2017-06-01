/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 430LX PCISet chip.
 *
 * Version:	@(#)i430lx.c	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#include <string.h>

#include "ibm.h"
#include "mem.h"
#include "pci.h"

#include "i430lx.h"

static uint8_t card_i430lx[256];

static void i430lx_map(uint32_t addr, uint32_t size, int state)
{
        switch (state & 3)
        {
                case 0:
                mem_set_mem_state(addr, size, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
                break;
                case 1:
                mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                break;
                case 2:
                mem_set_mem_state(addr, size, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
                break;
                case 3:
                mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                break;
        }
        flushmmucache_nopc();        
}

void i430lx_write(int func, int addr, uint8_t val, void *priv)
{
        if (func)
           return;

        if ((addr >= 0x10) && (addr < 0x4f))
                return;
                
        switch (addr)
        {
                case 0x00: case 0x01: case 0x02: case 0x03:
                case 0x08: case 0x09: case 0x0a: case 0x0b:
                case 0x0c: case 0x0e:
                return;
                
                case 0x04: /*Command register*/
                val &= 0x42;
                val |= 0x04;
                break;
                case 0x05:
                val &= 0x01;
                break;
                
                case 0x06: /*Status*/
                val = 0;
                break;
                case 0x07:
                val = 0x02;
                break;
                
                case 0x59: /*PAM0*/
                if ((card_i430lx[0x59] ^ val) & 0xf0)
                {
                        i430lx_map(0xf0000, 0x10000, val >> 4);
                        shadowbios = (val & 0x10);
                }
                pclog("i430lx_write : PAM0 write %02X\n", val);
                break;
                case 0x5a: /*PAM1*/
                if ((card_i430lx[0x5a] ^ val) & 0x0f)
                        i430lx_map(0xc0000, 0x04000, val & 0xf);
                if ((card_i430lx[0x5a] ^ val) & 0xf0)
                        i430lx_map(0xc4000, 0x04000, val >> 4);
                break;
                case 0x5b: /*PAM2*/
		if (romset == ROM_REVENGE)
		{
	                if ((card_i430lx[0x5b] ^ val) & 0x0f)
        	                i430lx_map(0xc8000, 0x04000, val & 0xf);
	                if ((card_i430lx[0x5b] ^ val) & 0xf0)
        	                i430lx_map(0xcc000, 0x04000, val >> 4);
		}
                break;
                case 0x5c: /*PAM3*/
                if ((card_i430lx[0x5c] ^ val) & 0x0f)
                        i430lx_map(0xd0000, 0x04000, val & 0xf);
                if ((card_i430lx[0x5c] ^ val) & 0xf0)
                        i430lx_map(0xd4000, 0x04000, val >> 4);
                break;
                case 0x5d: /*PAM4*/
                if ((card_i430lx[0x5d] ^ val) & 0x0f)
                        i430lx_map(0xd8000, 0x04000, val & 0xf);
                if ((card_i430lx[0x5d] ^ val) & 0xf0)
                        i430lx_map(0xdc000, 0x04000, val >> 4);
                break;
                case 0x5e: /*PAM5*/
                if ((card_i430lx[0x5e] ^ val) & 0x0f)
                        i430lx_map(0xe0000, 0x04000, val & 0xf);
                if ((card_i430lx[0x5e] ^ val) & 0xf0)
                        i430lx_map(0xe4000, 0x04000, val >> 4);
                pclog("i430lx_write : PAM5 write %02X\n", val);
                break;
                case 0x5f: /*PAM6*/
                if ((card_i430lx[0x5f] ^ val) & 0x0f)
                        i430lx_map(0xe8000, 0x04000, val & 0xf);
                if ((card_i430lx[0x5f] ^ val) & 0xf0)
                        i430lx_map(0xec000, 0x04000, val >> 4);
                pclog("i430lx_write : PAM6 write %02X\n", val);
                break;
        }
                
        card_i430lx[addr] = val;
}

uint8_t i430lx_read(int func, int addr, void *priv)
{
        if (func)
                return 0xff;

        return card_i430lx[addr];
}

void i430lx_reset(void)
{
        memset(card_i430lx, 0, 256);
        card_i430lx[0x00] = 0x86; card_i430lx[0x01] = 0x80; /*Intel*/
        card_i430lx[0x02] = 0xa3; card_i430lx[0x03] = 0x04; /*82434LX*/
        card_i430lx[0x04] = 0x06; card_i430lx[0x05] = 0x00;
        card_i430lx[0x06] = 0x00; card_i430lx[0x07] = 0x02;
        card_i430lx[0x08] = 0x03; /*A3 stepping*/
        card_i430lx[0x09] = 0x00; card_i430lx[0x0a] = 0x00; card_i430lx[0x0b] = 0x06;
        card_i430lx[0x50] = 0x80;
        card_i430lx[0x52] = 0x40; /*256kb PLB cache*/
        card_i430lx[0x57] = 0x31;
        card_i430lx[0x60] = card_i430lx[0x61] = card_i430lx[0x62] = card_i430lx[0x63] = card_i430lx[0x64] = 0x02;
}

void i430lx_pci_reset(void)
{
	i430lx_write(0, 0x59, 0x00, NULL);
}

void i430lx_init()
{
        pci_add_specific(0, i430lx_read, i430lx_write, NULL);

	i430lx_reset();

	pci_reset_handler.pci_master_reset = i430lx_pci_reset;
}
