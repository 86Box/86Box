/* Copyright holders: Tenshi
   see COPYING for more details
*/
/*PRD format :
        
        word 0 - base address
        word 1 - bits 1 - 15 = byte count, bit 31 = end of transfer
*/
#include <string.h>
#include "ibm.h"
#include "cpu/cpu.h"
#include "cdrom.h"
#include "disc.h"
#include "dma.h"
#include "fdc.h"
#include "ide.h"
#include "io.h"
#include "keyboard_at.h"
#include "mem.h"
#include "pci.h"

#include "sio.h"

static uint8_t card_sio[256];

void sio_write(int func, int addr, uint8_t val, void *priv)
{
//        pclog("sio_write: func=%d addr=%02x val=%02x %04x:%08x\n", func, addr, val, CS, pc);

	if ((addr & 0xff) < 4)  return;

        if (func > 0)
           return;
        
        if (func == 0)
        {
                switch (addr)
                {
                        case 0x00: case 0x01: case 0x02: case 0x03:
                        case 0x08: case 0x09: case 0x0a: case 0x0b:
                        case 0x0e:
                        return;
                }
                card_sio[addr] = val;
		if (addr == 0x40)
		{
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
		}
		else if (addr == 0x4f)
		{
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
		}
        }
}

uint8_t sio_read(int func, int addr, void *priv)
{
//        pclog("sio_read: func=%d addr=%02x %04x:%08x\n", func, addr, CS, pc);

        if (func > 0)
           return 0xff;

	return card_sio[addr];
}

static int trc_reg = 0;

uint8_t trc_read(uint16_t port, void *priv)
{
	return trc_reg & 0xfb;
}

void trc_reset(uint8_t val)
{
	int i = 0;

	if (val & 2)
	{
		if (pci_reset_handler.pci_master_reset)
		{
			pci_reset_handler.pci_master_reset();
		}

		if (pci_reset_handler.pci_set_reset)
		{
			pci_reset_handler.pci_set_reset();
		}

		fdc_hard_reset();

		if (pci_reset_handler.super_io_reset)
		{
			pci_reset_handler.super_io_reset();
		}

		resetide();
		for (i = 0; i < CDROM_NUM; i++)
		{
			if (!cdrom_drives[i].bus_type)
			{
				cdrom_reset(i);
			}
		}

		port_92_reset();
		keyboard_at_reset();
	}
	resetx86();
}

void trc_write(uint16_t port, uint8_t val, void *priv)
{
	pclog("TRC Write: %02X\n", val);
	if (!(trc_reg & 4) && (val & 4))
	{
		trc_reset(val);
	}
	trc_reg = val & 0xfd;
}

void trc_init()
{
	trc_reg = 0;

	io_sethandler(0x0cf9, 0x0001, trc_read, NULL, NULL, trc_write, NULL, NULL, NULL);
}

void sio_reset(void)
{
        memset(card_sio, 0, 256);
        card_sio[0x00] = 0x86; card_sio[0x01] = 0x80; /*Intel*/
        card_sio[0x02] = 0x84; card_sio[0x03] = 0x04; /*82378ZB (SIO)*/
        card_sio[0x04] = 0x07; card_sio[0x05] = 0x00;
       	card_sio[0x06] = 0x00; card_sio[0x07] = 0x02;
        card_sio[0x08] = 0x00; /*A0 stepping*/
	card_sio[0x40] = 0x20;
	card_sio[0x42] = 0x24;
	card_sio[0x45] = 0x10;
	card_sio[0x46] = 0x0F;
	card_sio[0x48] = 0x01;
	card_sio[0x4A] = 0x10;
	card_sio[0x4B] = 0x0F;
	card_sio[0x4C] = 0x56;
	card_sio[0x4D] = 0x40;
	card_sio[0x4E] = 0x07;
	card_sio[0x4F] = 0x4F;
	card_sio[0x60] = card_sio[0x61] = card_sio[0x62] = card_sio[0x63] = 0x80;
	card_sio[0x80] = 0x78;
	card_sio[0xA0] = 0x08;
	card_sio[0xA8] = 0x0F;
}

void sio_init(int card)
{
        pci_add_specific(card, sio_read, sio_write, NULL);

	sio_reset();

	trc_init();

	port_92_reset();

        port_92_add();

	dma_alias_set();

	pci_reset_handler.pci_set_reset = sio_reset;
}
