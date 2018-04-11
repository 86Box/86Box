/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of Intel System I/O PCI chip.
 *
 * Version:	@(#)intel_sio.c	1.0.7	2017/11/04
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
#include "cpu/cpu.h"
#include "io.h"
#include "dma.h"
#include "mem.h"
#include "pci.h"
#include "intel_sio.h"


static uint8_t card_sio[256];


static void sio_write(int func, int addr, uint8_t val, void *priv)
{
        if (func > 0)
                return;
        
	if (addr >= 0x0f && addr < 0x4c)
		return;

        switch (addr)
        {
                case 0x00: case 0x01: case 0x02: case 0x03:
                case 0x08: case 0x09: case 0x0a: case 0x0b:
                case 0x0e:
                return;
                        
                case 0x04: /*Command register*/
                val &= 0x08;
                val |= 0x07;
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

		case 0x40:
		if (!((val ^ card_sio[addr]) & 0x40))
		{
			return;
		}

		if (val & 0x40)
		{
			dma_alias_remove();
		}
		else
		{
			dma_alias_set();
		}
		break;

		case 0x4f:
		if (!((val ^ card_sio[addr]) & 0x40))
		{
			return;
		}

		if (val & 0x40)
		{
			port_92_add();
		}
		else
		{
			port_92_remove();
		}

                case 0x60:
                if (val & 0x80)
                        pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
                else
                        pci_set_irq_routing(PCI_INTA, val & 0xf);
                break;
                case 0x61:
                if (val & 0x80)
                        pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
                else
                        pci_set_irq_routing(PCI_INTC, val & 0xf);
                break;
                case 0x62:
                if (val & 0x80)
                        pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
                else
                        pci_set_irq_routing(PCI_INTB, val & 0xf);
                break;
                case 0x63:
                if (val & 0x80)
                        pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
                else
                        pci_set_irq_routing(PCI_INTD, val & 0xf);
                break;
        }
        card_sio[addr] = val;
}


static uint8_t sio_read(int func, int addr, void *priv)
{
        if (func > 0)
                return 0xff;

        return card_sio[addr];
}


static void sio_reset(void)
{
        memset(card_sio, 0, 256);
        card_sio[0x00] = 0x86; card_sio[0x01] = 0x80; /*Intel*/
        card_sio[0x02] = 0x84; card_sio[0x03] = 0x04; /*82378IB (SIO)*/
        card_sio[0x04] = 0x07; card_sio[0x05] = 0x00;
        card_sio[0x06] = 0x00; card_sio[0x07] = 0x02;
        card_sio[0x08] = 0x03; /*A0 stepping*/

        card_sio[0x40] = 0x20; card_sio[0x41] = 0x00;
        card_sio[0x42] = 0x04; card_sio[0x43] = 0x00;
        card_sio[0x44] = 0x20; card_sio[0x45] = 0x10;
        card_sio[0x46] = 0x0f; card_sio[0x47] = 0x00;
        card_sio[0x48] = 0x01; card_sio[0x49] = 0x10;
        card_sio[0x4a] = 0x10; card_sio[0x4b] = 0x0f;
        card_sio[0x4c] = 0x56; card_sio[0x4d] = 0x40;
        card_sio[0x4e] = 0x07; card_sio[0x4f] = 0x4f;
        card_sio[0x54] = 0x00; card_sio[0x55] = 0x00; card_sio[0x56] = 0x00;
        card_sio[0x60] = 0x80; card_sio[0x61] = 0x80; card_sio[0x62] = 0x80; card_sio[0x63] = 0x80;
        card_sio[0x80] = 0x78; card_sio[0x81] = 0x00;
        card_sio[0xa0] = 0x08;
        card_sio[0xa8] = 0x0f;
}


void sio_init(int card)
{
        pci_add_card(card, sio_read, sio_write, NULL);
        
	sio_reset();

	port_92_reset();

        port_92_add();

	dma_alias_set();

	pci_reset_handler.pci_set_reset = sio_reset;
}
