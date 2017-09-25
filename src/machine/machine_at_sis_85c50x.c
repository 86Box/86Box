/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the SiS 50x PCI chips.
 *
 * Version:	@(#)machine_at_sis_85c50x.c	1.0.3	2017/09/24
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../ibm.h"
#include "../io.h"
#include "../pci.h"
#include "../mem.h"
#include "machine_at.h"
#include "machine_at_sis_85c50x.h"


typedef struct sis_85c501_t
{
        uint8_t pci_conf[256];
	uint8_t turbo_reg;
} sis_85c501_t;

sis_85c501_t sis_85c501;

typedef struct sis_85c503_t
{
        uint8_t pci_conf[256];
} sis_85c503_t;

sis_85c503_t sis_85c503;

typedef struct sis_85c50x_t
{
        uint8_t isa_conf[12];
	uint8_t reg;
} sis_85c50x_isa_t;

sis_85c50x_isa_t sis_85c50x_isa;


static void sis_85c501_recalcmapping(void)
{
        int c, d;
        
	for (c = 0; c < 1; c++)
	{
	        for (d = 0; d < 4; d++)
        	{
			uint32_t base = 0xe0000 + (d << 14);
	                if (sis_85c501.pci_conf[0x54 + c] & (1 << (d + 4)))
	                {
        	                switch (sis_85c501.pci_conf[0x53] & 0x60)
	                        {
                	                case 0x00:
        	                        mem_set_mem_state(base, 0x4000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
	                                break;
                	                case 0x20:
        	                        mem_set_mem_state(base, 0x4000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
	                                break;
                        	        case 0x40:
                	                mem_set_mem_state(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        	                        break;
	                                case 0x60:
                                	mem_set_mem_state(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                        	        break;
                	        }
        	        }
	                else
        	                mem_set_mem_state(base, 0x4000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
	        }
	}

        flushmmucache();
        shadowbios = 1;
}


static void sis_85c501_write(int func, int addr, uint8_t val, void *p)
{
        /* pclog("sis_85c501_write : addr=%02x val=%02x\n", addr, val); */
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
                
                case 0x54: /*Shadow configure*/
                if ((sis_85c501.pci_conf[0x54] & val) ^ 0xf0)
                {
                        sis_85c501.pci_conf[0x54] = val;
                        sis_85c501_recalcmapping();
                }
                break;
        }
                
	sis_85c501.pci_conf[addr] = val;
}


static void sis_85c503_write(int func, int addr, uint8_t val, void *p)
{
        /* pclog("sis_85c503_write : addr=%02x val=%02x\n", addr, val); */

        if (func > 0)
                return;

        if (addr >= 0x0f && addr < 0x41)
                return;

	switch(addr)
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

       	        case 0x41:
		pclog("Set IRQ routing: INT A -> %02X\n", val);
       	        if (val & 0x80)
                        pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
               	else
       	                pci_set_irq_routing(PCI_INTA, val & 0xf);
                break;
               	case 0x42:
		pclog("Set IRQ routing: INT B -> %02X\n", val);
                if (val & 0x80)
       	                pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
                else
               	        pci_set_irq_routing(PCI_INTC, val & 0xf);
       	        break;
                case 0x43:
		pclog("Set IRQ routing: INT C -> %02X\n", val);
       	        if (val & 0x80)
                        pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
               	else
       	                pci_set_irq_routing(PCI_INTB, val & 0xf);
                break;
       	        case 0x44:
		pclog("Set IRQ routing: INT D -> %02X\n", val);
       	        if (val & 0x80)
                        pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
               	else
       	                pci_set_irq_routing(PCI_INTD, val & 0xf);
                break;
	}

	sis_85c503.pci_conf[addr] = val;
}


static void sis_85c50x_isa_write(uint16_t port, uint8_t val, void *priv)
{
	if (port & 1)
	{
		if (sis_85c50x_isa.reg <= 0xB)  sis_85c50x_isa.isa_conf[sis_85c50x_isa.reg] = val;
	}
	else
	{
		sis_85c50x_isa.reg = val;
	}
}


static uint8_t sis_85c501_read(int func, int addr, void *p)
{
        if (func)
                return 0xff;

        return sis_85c501.pci_conf[addr];
}


static uint8_t sis_85c503_read(int func, int addr, void *p)
{
        if (func > 0)
                return 0xff;

        return sis_85c503.pci_conf[addr];
}
 

static uint8_t sis_85c50x_isa_read(uint16_t port, void *priv)
{
	if (port & 1)
	{
		if (sis_85c50x_isa.reg <= 0xB)
			return sis_85c50x_isa.isa_conf[sis_85c50x_isa.reg];
		else
			return 0xff;
	}
	else
	{
		return sis_85c50x_isa.reg;
	}
}

static void sis_85c501_reset(void)
{
        memset(&sis_85c501, 0, sizeof(sis_85c501_t));
        sis_85c501.pci_conf[0x00] = 0x39; /*SiS*/
        sis_85c501.pci_conf[0x01] = 0x10; 
        sis_85c501.pci_conf[0x02] = 0x06; /*501/502*/
        sis_85c501.pci_conf[0x03] = 0x04; 

        sis_85c501.pci_conf[0x04] = 7;
        sis_85c501.pci_conf[0x05] = 0;

        sis_85c501.pci_conf[0x06] = 0x80;
        sis_85c501.pci_conf[0x07] = 0x02;
        
        sis_85c501.pci_conf[0x08] = 0; /*Device revision*/

        sis_85c501.pci_conf[0x09] = 0x00; /*Device class (PCI bridge)*/
        sis_85c501.pci_conf[0x0a] = 0x00;
        sis_85c501.pci_conf[0x0b] = 0x06;
        
        sis_85c501.pci_conf[0x0e] = 0x00; /*Single function device*/

        sis_85c501.pci_conf[0x50] = 0xbc;
        sis_85c501.pci_conf[0x51] = 0xfb;
        sis_85c501.pci_conf[0x52] = 0xad;
        sis_85c501.pci_conf[0x53] = 0xfe;

	shadowbios = 1;
}

static void sis_85c501_init(void)
{
        pci_add_card(0, sis_85c501_read, sis_85c501_write, NULL);
        
	sis_85c501_reset();

	pci_reset_handler.pci_master_reset = NULL;
}

static void sis_85c503_reset(void)
{
        memset(&sis_85c503, 0, sizeof(sis_85c503_t));
        sis_85c503.pci_conf[0x00] = 0x39; /*SiS*/
        sis_85c503.pci_conf[0x01] = 0x10; 
        sis_85c503.pci_conf[0x02] = 0x08; /*503*/
        sis_85c503.pci_conf[0x03] = 0x00; 

        sis_85c503.pci_conf[0x04] = 7;
        sis_85c503.pci_conf[0x05] = 0;

        sis_85c503.pci_conf[0x06] = 0x80;
        sis_85c503.pci_conf[0x07] = 0x02;
        
        sis_85c503.pci_conf[0x08] = 0; /*Device revision*/

        sis_85c503.pci_conf[0x09] = 0x00; /*Device class (PCI bridge)*/
        sis_85c503.pci_conf[0x0a] = 0x01;
        sis_85c503.pci_conf[0x0b] = 0x06;
        
        sis_85c503.pci_conf[0x0e] = 0x00; /*Single function device*/

	sis_85c503.pci_conf[0x41] = sis_85c503.pci_conf[0x42] = sis_85c503.pci_conf[0x43] = sis_85c503.pci_conf[0x44] = 0x80;
}

static void sis_85c503_init(int card)
{
        
        pci_add_card(card, sis_85c503_read, sis_85c503_write, NULL);

	sis_85c503_reset();

	trc_init();

	port_92_reset();

	port_92_add();

	pci_reset_handler.pci_set_reset = sis_85c503_reset;
}


static void sis_85c50x_isa_init(void)
{
        memset(&sis_85c50x_isa, 0, sizeof(sis_85c50x_isa_t));

	io_sethandler(0x22, 0x0002, sis_85c50x_isa_read, NULL, NULL, sis_85c50x_isa_write, NULL, NULL,  NULL);
}
