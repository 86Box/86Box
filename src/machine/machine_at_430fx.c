/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 430FX PCISet chip.
 *
 * Version:	@(#)machine_at_430fx.c	1.0.6	2017/10/07
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../ibm.h"
#include "../mem.h"
#include "../memregs.h"
#include "../rom.h"
#include "../pci.h"
#include "../device.h"
#include "../piix.h"
#include "../intel_flash.h"
#include "../sio.h"
#include "machine.h"


static uint8_t card_i430fx[256];


static void i430fx_map(uint32_t addr, uint32_t size, int state)
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


static void i430fx_write(int func, int addr, uint8_t val, void *priv)
{
        if (func)
		return;

        if (addr >= 0x10 && addr < 0x4f)
                return;

        switch (addr)
        {
                case 0x00: case 0x01: case 0x02: case 0x03:
                case 0x08: case 0x09: case 0x0a: case 0x0b:
                case 0x0c: case 0x0e:
                return;

                case 0x04: /*Command register*/
                val &= 0x02;
                val |= 0x04;
                break;
                case 0x05:
                val = 0;
                break;
                
                case 0x06: /*Status*/
                val = 0;
                break;
                case 0x07:
                val = 0x02;
                break;

                case 0x59: /*PAM0*/
                if ((card_i430fx[0x59] ^ val) & 0xf0)
                {
                        i430fx_map(0xf0000, 0x10000, val >> 4);
                        shadowbios = (val & 0x10);
                }
                pclog("i430fx_write : PAM0 write %02X\n", val);
                break;
                case 0x5a: /*PAM1*/
                if ((card_i430fx[0x5a] ^ val) & 0x0f)
                        i430fx_map(0xc0000, 0x04000, val & 0xf);
                if ((card_i430fx[0x5a] ^ val) & 0xf0)
                        i430fx_map(0xc4000, 0x04000, val >> 4);
                break;
                case 0x5b: /*PAM2*/
                if ((card_i430fx[0x5b] ^ val) & 0x0f)
                        i430fx_map(0xc8000, 0x04000, val & 0xf);
                if ((card_i430fx[0x5b] ^ val) & 0xf0)
                        i430fx_map(0xcc000, 0x04000, val >> 4);
                break;
                case 0x5c: /*PAM3*/
                if ((card_i430fx[0x5c] ^ val) & 0x0f)
                        i430fx_map(0xd0000, 0x04000, val & 0xf);
                if ((card_i430fx[0x5c] ^ val) & 0xf0)
                        i430fx_map(0xd4000, 0x04000, val >> 4);
                break;
                case 0x5d: /*PAM4*/
                if ((card_i430fx[0x5d] ^ val) & 0x0f)
                        i430fx_map(0xd8000, 0x04000, val & 0xf);
                if ((card_i430fx[0x5d] ^ val) & 0xf0)
                        i430fx_map(0xdc000, 0x04000, val >> 4);
                break;
                case 0x5e: /*PAM5*/
                if ((card_i430fx[0x5e] ^ val) & 0x0f)
                        i430fx_map(0xe0000, 0x04000, val & 0xf);
                if ((card_i430fx[0x5e] ^ val) & 0xf0)
                        i430fx_map(0xe4000, 0x04000, val >> 4);
                pclog("i430fx_write : PAM5 write %02X\n", val);
                break;
                case 0x5f: /*PAM6*/
                if ((card_i430fx[0x5f] ^ val) & 0x0f)
                        i430fx_map(0xe8000, 0x04000, val & 0xf);
                if ((card_i430fx[0x5f] ^ val) & 0xf0)
                        i430fx_map(0xec000, 0x04000, val >> 4);
                pclog("i430fx_write : PAM6 write %02X\n", val);
                break;
        }
                
        card_i430fx[addr] = val;
}


static uint8_t i430fx_read(int func, int addr, void *priv)
{
        if (func)
                return 0xff;

        return card_i430fx[addr];
}


static void i430fx_reset(void)
{
        memset(card_i430fx, 0, 256);
        card_i430fx[0x00] = 0x86; card_i430fx[0x01] = 0x80; /*Intel*/
        card_i430fx[0x02] = 0x22; card_i430fx[0x03] = 0x01; /*SB82437FX-66*/
        card_i430fx[0x04] = 0x06; card_i430fx[0x05] = 0x00;
        card_i430fx[0x06] = 0x00; card_i430fx[0x07] = 0x82;
	if (romset == ROM_MB500N)  card_i430fx[0x07] = 0x02;
        card_i430fx[0x08] = 0x00; /*A0 stepping*/
        card_i430fx[0x09] = 0x00; card_i430fx[0x0a] = 0x00; card_i430fx[0x0b] = 0x06;
        card_i430fx[0x52] = 0x40; /*256kb PLB cache*/
	if (romset == ROM_MB500N)
	{
		card_i430fx[0x52] = 0x42;
	        card_i430fx[0x53] = 0x14;
	        card_i430fx[0x56] = 0x52; /*DRAM control*/
	}
        card_i430fx[0x57] = 0x01;
        card_i430fx[0x60] = card_i430fx[0x61] = card_i430fx[0x62] = card_i430fx[0x63] = card_i430fx[0x64] = 0x02;
	if (romset == ROM_MB500N)
	{
	        card_i430fx[0x67] = 0x11;
	        card_i430fx[0x69] = 0x03;
	        card_i430fx[0x70] = 0x20;
	}
        card_i430fx[0x72] = 0x02;
	if (romset == ROM_MB500N)
	{
	        card_i430fx[0x74] = 0x0e;
	        card_i430fx[0x78] = 0x23;
	}
}


static void i430fx_pci_reset(void)
{
	i430fx_write(0, 0x59, 0x00, NULL);
}


static void i430fx_init(void)
{
        pci_add_card(0, i430fx_read, i430fx_write, NULL);

	i430fx_reset();
        
	pci_reset_handler.pci_master_reset = i430fx_pci_reset;
}


void
machine_at_p54tp4xe_init(machine_t *model)
{
        machine_at_ide_init(model);

	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
	pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
	pci_register_slot(0x0B, PCI_CARD_NORMAL, 2, 3, 4, 1);
	pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
	pci_register_slot(0x09, PCI_CARD_NORMAL, 4, 1, 2, 3);
	pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
        i430fx_init();
        piix3_init(7);
        fdc37c665_init();

        device_add(&intel_flash_bxt_device);
}


void
machine_at_endeavor_init(machine_t *model)
{
        machine_at_ide_init(model);

	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
	pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x08, PCI_CARD_ONBOARD, 4, 0, 0, 0);
	pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
	pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
	pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 1, 2);
	pci_register_slot(0x10, PCI_CARD_NORMAL, 4, 1, 2, 3);
	pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
        i430fx_init();
        piix_init(7);
        pc87306_init();

        device_add(&intel_flash_bxt_ami_device);
}


void
machine_at_zappa_init(machine_t *model)
{
        machine_at_ide_init(model);

	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
	pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
	pci_register_slot(0x0E, PCI_CARD_NORMAL, 3, 4, 1, 2);
	pci_register_slot(0x0F, PCI_CARD_NORMAL, 2, 3, 4, 1);
	pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
        i430fx_init();
        piix_init(7);
        pc87306_init();

        device_add(&intel_flash_bxt_ami_device);
}


void
machine_at_mb500n_init(machine_t *model)
{
        machine_at_ide_init(model);

        pci_init(PCI_CONFIG_TYPE_1);
	pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x14, PCI_CARD_NORMAL, 1, 2, 3, 4);
	pci_register_slot(0x13, PCI_CARD_NORMAL, 2, 3, 4, 1);
	pci_register_slot(0x12, PCI_CARD_NORMAL, 3, 4, 1, 2);
	pci_register_slot(0x11, PCI_CARD_NORMAL, 4, 1, 2, 3);
	pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
        i430fx_init();
        piix_init(7);
        fdc37c665_init();

        device_add(&intel_flash_bxt_device);
}


void
machine_at_president_init(machine_t *model)
{
        machine_at_ide_init(model);

	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
	pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
	pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1);
	pci_register_slot(0x0A, PCI_CARD_NORMAL, 3, 4, 1, 2);
	pci_register_slot(0x0B, PCI_CARD_NORMAL, 4, 1, 2, 3);
	pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
        i430fx_init();
        piix_init(7);
        w83877f_init();

        device_add(&intel_flash_bxt_device);
}


void
machine_at_thor_init(machine_t *model)
{
        machine_at_ide_init(model);

	memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
	pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x08, PCI_CARD_ONBOARD, 4, 0, 0, 0);
	pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
	pci_register_slot(0x0E, PCI_CARD_NORMAL, 2, 3, 4, 1);
	pci_register_slot(0x0F, PCI_CARD_NORMAL, 3, 4, 2, 1);
	pci_register_slot(0x10, PCI_CARD_NORMAL, 4, 3, 2, 1);
	pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
        i430fx_init();
        piix_init(7);
        pc87306_init();

        device_add(&intel_flash_bxt_ami_device);
}
