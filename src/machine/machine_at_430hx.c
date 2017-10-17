/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 430HX PCISet chip.
 *
 * Version:	@(#)machine_at_430hx.c	1.0.7	2017/10/16
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../memregs.h"
#include "../pci.h"
#include "../device.h"
#include "../piix.h"
#include "../intel_flash.h"
#include "../sio.h"
#include "machine.h"


static uint8_t card_i430hx[256];


static void i430hx_map(uint32_t addr, uint32_t size, int state)
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


static void i430hx_write(int func, int addr, uint8_t val, void *priv)
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
                val &= 0x80;
                val |= 0x02;
                break;
                
                case 0x59: /*PAM0*/
                if ((card_i430hx[0x59] ^ val) & 0xf0)
                {
                        i430hx_map(0xf0000, 0x10000, val >> 4);
                        shadowbios = (val & 0x10);
                }
                break;
                case 0x5a: /*PAM1*/
                if ((card_i430hx[0x5a] ^ val) & 0x0f)
                        i430hx_map(0xc0000, 0x04000, val & 0xf);
                if ((card_i430hx[0x5a] ^ val) & 0xf0)
                        i430hx_map(0xc4000, 0x04000, val >> 4);
                break;
                case 0x5b: /*PAM2*/
                if ((card_i430hx[0x5b] ^ val) & 0x0f)
                        i430hx_map(0xc8000, 0x04000, val & 0xf);
                if ((card_i430hx[0x5b] ^ val) & 0xf0)
                        i430hx_map(0xcc000, 0x04000, val >> 4);
                break;
                case 0x5c: /*PAM3*/
                if ((card_i430hx[0x5c] ^ val) & 0x0f)
                        i430hx_map(0xd0000, 0x04000, val & 0xf);
                if ((card_i430hx[0x5c] ^ val) & 0xf0)
                        i430hx_map(0xd4000, 0x04000, val >> 4);
                break;
                case 0x5d: /*PAM4*/
                if ((card_i430hx[0x5d] ^ val) & 0x0f)
                        i430hx_map(0xd8000, 0x04000, val & 0xf);
                if ((card_i430hx[0x5d] ^ val) & 0xf0)
                        i430hx_map(0xdc000, 0x04000, val >> 4);
                break;
                case 0x5e: /*PAM5*/
                if ((card_i430hx[0x5e] ^ val) & 0x0f)
                        i430hx_map(0xe0000, 0x04000, val & 0xf);
                if ((card_i430hx[0x5e] ^ val) & 0xf0)
                        i430hx_map(0xe4000, 0x04000, val >> 4);
                break;
                case 0x5f: /*PAM6*/
                if ((card_i430hx[0x5f] ^ val) & 0x0f)
                        i430hx_map(0xe8000, 0x04000, val & 0xf);
                if ((card_i430hx[0x5f] ^ val) & 0xf0)
                        i430hx_map(0xec000, 0x04000, val >> 4);
                break;
		case 0x72: /*SMRAM*/
                if ((card_i430hx[0x72] ^ val) & 0x48)
                        i430hx_map(0xa0000, 0x20000, ((val & 0x48) == 0x48) ? 3 : 0);
		break;
        }
                
        card_i430hx[addr] = val;
}


static uint8_t i430hx_read(int func, int addr, void *priv)
{
        if (func)
           return 0xff;

        return card_i430hx[addr];
}
 

static void i430hx_reset(void)
{
        memset(card_i430hx, 0, 256);
        card_i430hx[0x00] = 0x86; card_i430hx[0x01] = 0x80; /*Intel*/
        card_i430hx[0x02] = 0x50; card_i430hx[0x03] = 0x12; /*82439HX*/
        card_i430hx[0x04] = 0x06; card_i430hx[0x05] = 0x00;
        card_i430hx[0x06] = 0x00; card_i430hx[0x07] = 0x02;
        card_i430hx[0x08] = 0x00; /*A0 stepping*/
        card_i430hx[0x09] = 0x00; card_i430hx[0x0a] = 0x00; card_i430hx[0x0b] = 0x06;
	card_i430hx[0x51] = 0x20;
	card_i430hx[0x52] = 0xB5; /*512kb cache*/

	card_i430hx[0x59] = 0x40;
	card_i430hx[0x5A] = card_i430hx[0x5B] = card_i430hx[0x5C] = card_i430hx[0x5D] = card_i430hx[0x5E] = card_i430hx[0x5F] = 0x44;

        card_i430hx[0x56] = 0x52; /*DRAM control*/
        card_i430hx[0x57] = 0x01;
        card_i430hx[0x60] = card_i430hx[0x61] = card_i430hx[0x62] = card_i430hx[0x63] = card_i430hx[0x64] = card_i430hx[0x65] = card_i430hx[0x66] = card_i430hx[0x67] = 0x02;
        card_i430hx[0x68] = 0x11;
        card_i430hx[0x72] = 0x02;
}
    

static void i430hx_pci_reset(void)
{
	i430hx_write(0, 0x59, 0x00, NULL);
	i430hx_write(0, 0x72, 0x02, NULL);
}


static void i430hx_init(void)
{
        pci_add_card(0, i430hx_read, i430hx_write, NULL);

	i430hx_reset();

	pci_reset_handler.pci_master_reset = i430hx_pci_reset;
}


void
machine_at_acerm3a_init(machine_t *model)
{
        machine_at_ide_init(model);

	powermate_memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
	pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
	pci_register_slot(0x0D, PCI_CARD_NORMAL, 2, 3, 4, 1);
	pci_register_slot(0x0E, PCI_CARD_NORMAL, 3, 4, 1, 2);
	pci_register_slot(0x1F, PCI_CARD_NORMAL, 4, 1, 2, 3);
	pci_register_slot(0x10, PCI_CARD_ONBOARD, 4, 0, 0, 0);
        i430hx_init();
        piix3_init(7);
        fdc37c932fr_init();

        device_add(&intel_flash_bxb_device);
}


void
machine_at_acerv35n_init(machine_t *model)
{
        machine_at_ide_init(model);

	powermate_memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
	pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
	pci_register_slot(0x12, PCI_CARD_NORMAL, 2, 3, 4, 1);
	pci_register_slot(0x13, PCI_CARD_NORMAL, 3, 4, 1, 2);
	pci_register_slot(0x14, PCI_CARD_NORMAL, 4, 1, 2, 3);
	pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
        i430hx_init();
        piix3_init(7);
        fdc37c932fr_init();

        device_add(&intel_flash_bxb_device);
}


void
machine_at_ap53_init(machine_t *model)
{
        machine_at_ide_init(model);

        memregs_init();
        powermate_memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
	pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x11, PCI_CARD_NORMAL, 1, 2, 3, 4);
	pci_register_slot(0x12, PCI_CARD_NORMAL, 2, 3, 4, 1);
	pci_register_slot(0x13, PCI_CARD_NORMAL, 3, 4, 1, 2);
	pci_register_slot(0x14, PCI_CARD_NORMAL, 4, 1, 2, 3);
	pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x06, PCI_CARD_ONBOARD, 1, 2, 3, 4);
        i430hx_init();
        piix3_init(7);
        fdc37c669_init();

        device_add(&intel_flash_bxt_device);
}


void
machine_at_p55t2p4_init(machine_t *model)
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
        i430hx_init();
        piix3_init(7);
        w83877f_init();

        device_add(&intel_flash_bxt_device);
}


void
machine_at_p55t2s_init(machine_t *model)
{
        machine_at_ide_init(model);

        memregs_init();
        powermate_memregs_init();
        pci_init(PCI_CONFIG_TYPE_1);
	pci_register_slot(0x00, PCI_CARD_SPECIAL, 0, 0, 0, 0);
	pci_register_slot(0x12, PCI_CARD_NORMAL, 1, 2, 3, 4);
	pci_register_slot(0x13, PCI_CARD_NORMAL, 4, 1, 2, 3);
	pci_register_slot(0x14, PCI_CARD_NORMAL, 3, 4, 1, 2);
	pci_register_slot(0x11, PCI_CARD_NORMAL, 2, 3, 4, 1);
	pci_register_slot(0x07, PCI_CARD_SPECIAL, 0, 0, 0, 0);
        i430hx_init();
        piix3_init(7);
        pc87306_init();

        device_add(&intel_flash_bxt_device);
}
