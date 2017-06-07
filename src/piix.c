/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the Intel PIIX and PIIX3 Xcelerators.
 *
 *		Emulation core dispatcher.
 *
 * Version:	@(#)piix.c	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

/*PRD format :
        
        word 0 - base address
        word 1 - bits 1 - 15 = byte count, bit 31 = end of transfer
*/
#include <string.h>

#include "ibm.h"
#include "dma.h"
#include "ide.h"
#include "io.h"
#include "mem.h"
#include "pci.h"

#include "piix.h"

uint8_t piix_33 = 0;

uint8_t piix_bus_master_read(uint16_t port, void *priv);
void piix_bus_master_write(uint16_t port, uint8_t val, void *priv);

static uint8_t piix_type = 1;
static uint8_t card_piix[256], card_piix_ide[256];

void piix_write(int func, int addr, uint8_t val, void *priv)
{
	uint16_t old_base = (card_piix_ide[0x20] & 0xf0) | (card_piix_ide[0x21] << 8);
        if (func > 1)
           return;
        
        if (func == 1) /*IDE*/
        {
                switch (addr)
                {
                        case 0x04:
                        card_piix_ide[0x04] = (val & 5) | 2;
                        break;
                        case 0x07:
                        card_piix_ide[0x07] = val & 0x3e;
                        break;
                        case 0x0d:
                        card_piix_ide[0x0d] = val;
                        break;
                        
                        case 0x20:
                        card_piix_ide[0x20] = (val & ~0x0f) | 1;
                        break;
                        case 0x21:
                        card_piix_ide[0x21] = val;
                        break;
                        
                        case 0x40:
                        card_piix_ide[0x40] = val;
                        break;
                        case 0x41:
                        if ((val ^ card_piix_ide[0x41]) & 0x80)
                        {
                                ide_pri_disable();
                                if (val & 0x80)
                                        ide_pri_enable();
                        }
                        card_piix_ide[0x41] = val;
                        break;
                        case 0x42:
                        card_piix_ide[0x42] = val;
                        break;
                        case 0x43:
                        if ((val ^ card_piix_ide[0x43]) & 0x80)
                        {
                                ide_sec_disable();
                                if (val & 0x80)
                                   ide_sec_enable();                                  
                        }
                        card_piix_ide[0x43] = val;
                        break;
			case 0x44:
			if (piix_type >= 3)  card_piix_ide[0x44] = val;
			break;
                }
                if (addr == 4 || (addr & ~3) == 0x20) /*Bus master base address*/                
                {
                        uint16_t base = (card_piix_ide[0x20] & 0xf0) | (card_piix_ide[0x21] << 8);
                        io_removehandler(old_base, 0x10, piix_bus_master_read, NULL, NULL, piix_bus_master_write, NULL, NULL,  NULL);
                        if (card_piix_ide[0x04] & 1)
			{
				io_sethandler(base, 0x10, piix_bus_master_read, NULL, NULL, piix_bus_master_write, NULL, NULL,  NULL);
			}
                }
        }
        else
        {
		/* pclog("PIIX writing value %02X to register %02X\n", val, addr); */
                if ((addr >= 0x0f) && (addr < 0x4c))
                        return;

                switch (addr)
                {
                        case 0x00: case 0x01: case 0x02: case 0x03:
                        case 0x08: case 0x09: case 0x0a: case 0x0b:
                        case 0x0e:
                        return;
                        
                        case 0x60:
			pclog("Set IRQ routing: INT A -> %02X\n", val);
                        if (val & 0x80)
                                pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
                        else
                                pci_set_irq_routing(PCI_INTA, val & 0xf);
                        break;
                        case 0x61:
			pclog("Set IRQ routing: INT B -> %02X\n", val);
                        if (val & 0x80)
                                pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
                        else
                                pci_set_irq_routing(PCI_INTB, val & 0xf);
                        break;
                        case 0x62:
			pclog("Set IRQ routing: INT C -> %02X\n", val);
                        if (val & 0x80)
                                pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
                        else
                                pci_set_irq_routing(PCI_INTC, val & 0xf);
                        break;
                        case 0x63:
			pclog("Set IRQ routing: INT D -> %02X\n", val);
                        if (val & 0x80)
                                pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
                        else
                                pci_set_irq_routing(PCI_INTD, val & 0xf);
                        break;
                }
		if (addr == 0x4C)
		{
			if (!((val ^ card_piix[addr]) & 0x80))
			{
		                card_piix[addr] = val;
				return;
			}

	                card_piix[addr] = val;
			if (val & 0x80)
			{
				if (piix_type == 3)
				{
					dma_alias_remove();
				}
				else
				{
					dma_alias_remove_piix();
				}
			}
			else
			{
				dma_alias_set();
			}
		}
		else if (addr == 0x6A)
		{
			if (piix_type == 1)
				card_piix[addr] = (val & 0xFC) | (card_piix[addr] | 3);
			else if (piix_type == 3)
				card_piix[addr] = (val & 0xFD) | (card_piix[addr] | 2);
		}
		else
	                card_piix[addr] = val;
        }
}

uint8_t piix_read(int func, int addr, void *priv)
{
        if (func > 1)
           return 0xff;

        if (func == 1) /*IDE*/
        {
		if (addr == 4)
		{
			return (card_piix_ide[addr] & 5) | 2;
		}
		else if (addr == 5)
		{
			return 0;
		}
		else if (addr == 6)
		{
			return 0x80;
		}
		else if (addr == 7)
		{
			return card_piix_ide[addr] & 0x3E;
		}
		else if (addr == 0xD)
		{
			return card_piix_ide[addr] & 0xF0;
		}
		else if (addr == 0x20)
		{
			return (card_piix_ide[addr] & 0xF0) | 1;
		}
		else if (addr == 0x22)
		{
			return 0;
		}
		else if (addr == 0x23)
		{
			return 0;
		}
		else if (addr == 0x41)
		{
			if (piix_type == 1)
				return card_piix_ide[addr] & 0xB3;
			else if (piix_type == 3)
				return card_piix_ide[addr] & 0xF3;
		}
		else if (addr == 0x43)
		{
			if (piix_type == 1)
				return card_piix_ide[addr] & 0xB3;
			else if (piix_type == 3)
				return card_piix_ide[addr] & 0xF3;
		}
		else
		{
                	return card_piix_ide[addr];
		}
        }
        else
	{
		if ((addr & 0xFC) == 0x60)
		{
			return card_piix[addr] & 0x8F;
		}
		if (addr == 4)
		{
			return (card_piix[addr] & 0x80) | 7;
		}
		else if (addr == 5)
		{
			if (piix_type == 1)
				return 0;
			else if (piix_type == 3)
				return card_piix[addr] & 1;
		}
		else if (addr == 6)
		{
			return card_piix[addr] & 0x80;
		}
		else if (addr == 7)
		{
			if (piix_type == 1)
				return card_piix[addr] & 0x3E;
			else if (piix_type == 3)
				return card_piix[addr];
		}
		else if (addr == 0x69)
		{
			return card_piix[addr] & 0xFE;
		}
		else if (addr == 0x6A)
		{
			if (piix_type == 1)
				return card_piix[addr] & 0x07;
			else if (piix_type == 3)
				return card_piix[addr] & 0xD1;
		}
		else if (addr == 0x6B)
		{
			if (piix_type == 1)
				return 0;
			else if (piix_type == 3)
				return card_piix[addr] & 0x80;
		}
		else if (addr == 0x70)
		{
			if (piix_type == 1)
				return card_piix[addr] & 0xCF;
			else if (piix_type == 3)
				return card_piix[addr] & 0xEF;
		}
		else if (addr == 0x71)
		{
			if (piix_type == 1)
				return card_piix[addr] & 0xCF;
			else if (piix_type == 3)
				return 0;
		}
		else if (addr == 0x76)
		{
			if (piix_type == 1)
				return card_piix[addr] & 0x8F;
			else if (piix_type == 3)
				return card_piix[addr] & 0x87;
		}
		else if (addr == 0x77)
		{
			if (piix_type == 1)
				return card_piix[addr] & 0x8F;
			else if (piix_type == 3)
				return card_piix[addr] & 0x87;
		}
		else if (addr == 0x80)
		{
			if (piix_type == 1)
				return 0;
			else if (piix_type == 3)
				return card_piix[addr] & 0x7F;
		}
		else if (addr == 0x82)
		{
			if (piix_type == 1)
				return 0;
			else if (piix_type == 3)
				return card_piix[addr] & 0x0F;
		}
		else if (addr == 0xA0)
		{
			return card_piix[addr] & 0x1F;
		}
		else if (addr == 0xA3)
		{
			if (piix_type == 1)
				return 0;
			else if (piix_type == 3)
				return card_piix[addr] & 1;
		}
		else if (addr == 0xA7)
		{
			if (piix_type == 1)
				return card_piix[addr] & 0xEF;
			else if (piix_type == 3)
				return card_piix[addr];
		}
		else if (addr == 0xAB)
		{
			if (piix_type == 1)
				return card_piix[addr] & 0xFE;
			else if (piix_type == 3)
				return card_piix[addr];
		}
		else
			return card_piix[addr];
	}

	return 0;
}

struct
{
        uint8_t command;
        uint8_t status;
        uint32_t ptr, ptr_cur;
        int count;
        uint32_t addr;
        int eot;
} piix_busmaster[2];

static void piix_bus_master_next_addr(int channel)
{
        piix_busmaster[channel].addr = ((*(uint32_t *)(&ram[piix_busmaster[channel].ptr_cur])) & ~1) % (mem_size * 1024);
        piix_busmaster[channel].count = (*(uint32_t *)(&ram[piix_busmaster[channel].ptr_cur + 4])) & 0xfffe;
        piix_busmaster[channel].eot = (*(uint32_t *)(&ram[piix_busmaster[channel].ptr_cur + 4])) >> 31;
        piix_busmaster[channel].ptr_cur += 8;
}

void piix_bus_master_write(uint16_t port, uint8_t val, void *priv)
{
        int channel = (port & 8) ? 1 : 0;
        switch (port & 7)
        {
                case 0:
                if ((val & 1) && !(piix_busmaster[channel].command & 1)) /*Start*/
                {
                        piix_busmaster[channel].ptr_cur = piix_busmaster[channel].ptr;
                        piix_bus_master_next_addr(channel);
                        piix_busmaster[channel].status |= 1;
                }
                if (!(val & 1) && (piix_busmaster[channel].command & 1)) /*Stop*/
                   piix_busmaster[channel].status &= ~1;
                   
                piix_busmaster[channel].command = val;
                break;
                case 2:
                piix_busmaster[channel].status = (val & 0x60) | ((piix_busmaster[channel].status & ~val) & 6) | (piix_busmaster[channel].status & 1);
                break;
                case 4:
                piix_busmaster[channel].ptr = (piix_busmaster[channel].ptr & 0xffffff00) | val;
		piix_busmaster[channel].ptr %= (mem_size * 1024);
                break;
                case 5:
                piix_busmaster[channel].ptr = (piix_busmaster[channel].ptr & 0xffff00ff) | (val << 8);
		piix_busmaster[channel].ptr %= (mem_size * 1024);
                break;
                case 6:
                piix_busmaster[channel].ptr = (piix_busmaster[channel].ptr & 0xff00ffff) | (val << 16);
		piix_busmaster[channel].ptr %= (mem_size * 1024);
                break;
                case 7:
                piix_busmaster[channel].ptr = (piix_busmaster[channel].ptr & 0x00ffffff) | (val << 24);
		piix_busmaster[channel].ptr %= (mem_size * 1024);
                break;

        }
}
                
uint8_t piix_bus_master_read(uint16_t port, void *priv)
{
        int channel = (port & 8) ? 1 : 0;
        switch (port & 7)
        {
                case 0:
                return piix_busmaster[channel].command;
                case 2:
                return piix_busmaster[channel].status;
                case 4:
                return piix_busmaster[channel].ptr;
                case 5:
                return piix_busmaster[channel].ptr >> 8;
                case 6:
                return piix_busmaster[channel].ptr >> 16;
                case 7:
                return piix_busmaster[channel].ptr >> 24;
        }
        return 0xff;
}

int piix_bus_master_get_count(int channel)
{
	return piix_busmaster[channel].count;
}

int piix_bus_master_get_eot(int channel)
{
	return piix_busmaster[channel].eot;
}

int piix_bus_master_dma_read_ex(int channel, uint8_t *data)
{
        int transferred = 0;

        if (!(piix_busmaster[channel].status & 1))
           return 1;                                    /*DMA disabled*/

	mem_invalidate_range(piix_busmaster[channel].addr, piix_busmaster[channel].addr + piix_busmaster[channel].count - 1);
                
	memcpy(&ram[piix_busmaster[channel].addr], data, piix_busmaster[channel].count);
	transferred += piix_busmaster[channel].count;
	piix_busmaster[channel].addr += piix_busmaster[channel].count;
	piix_busmaster[channel].addr %= (mem_size * 1024);
	piix_busmaster[channel].count = 0;

	if (piix_busmaster[channel].eot) /*End of transfer?*/
	{
		piix_busmaster[channel].status &= ~1;
		return -1;
	}
	else
	{
		piix_bus_master_next_addr(channel);
	}
	return 0;
}

int piix_bus_master_dma_read(int channel, uint8_t *data, int transfer_length)
{
        int transferred = 0;
        
        if (!(piix_busmaster[channel].status & 1))
           return 1;                                    /*DMA disabled*/
           
        while (transferred < transfer_length)
        {
                if ((piix_busmaster[channel].count < (transfer_length - transferred)) && piix_busmaster[channel].eot && (transfer_length == 512))
		{
			fatal("DMA on channel %i - Read count less than %04X! Addr %08X Count %04X EOT %i\n", channel, transfer_length, piix_busmaster[channel].addr, piix_busmaster[channel].count, piix_busmaster[channel].eot);
		}

                mem_invalidate_range(piix_busmaster[channel].addr, piix_busmaster[channel].addr + transfer_length - 1);
                
                if (piix_busmaster[channel].count < (transfer_length - transferred))
                {
                        memcpy(&ram[piix_busmaster[channel].addr], data + transferred, piix_busmaster[channel].count);
                        transferred += piix_busmaster[channel].count;
                        piix_busmaster[channel].addr += piix_busmaster[channel].count;
			piix_busmaster[channel].addr %= (mem_size * 1024);
                        piix_busmaster[channel].count = 0;
                }                       
                else
                {
                        memcpy(&ram[piix_busmaster[channel].addr], data + transferred, transfer_length - transferred);
                        piix_busmaster[channel].addr += (transfer_length - transferred);
                        piix_busmaster[channel].count -= (transfer_length - transferred);
                        transferred += (transfer_length - transferred);                        
                }

                if (!piix_busmaster[channel].count)
                {
                        if (piix_busmaster[channel].eot) /*End of transfer?*/
                        {
                                piix_busmaster[channel].status &= ~1;
                        }
                        else
                           piix_bus_master_next_addr(channel);
                }
        }
        return 0;
}

int piix_bus_master_dma_write(int channel, uint8_t *data, int transfer_length)
{
        int transferred = 0;

        if (!(piix_busmaster[channel].status & 1))
           return 1;                                    /*DMA disabled*/

        while (transferred < transfer_length)
        {
                if ((piix_busmaster[channel].count < (transfer_length - transferred)) && piix_busmaster[channel].eot && (transfer_length == 512))
		{
			fatal("DMA on channel %i - Write count less than %04X! Addr %08X Count %04X EOT %i\n", channel, transfer_length, piix_busmaster[channel].addr, piix_busmaster[channel].count, piix_busmaster[channel].eot);
		}
                
                if (piix_busmaster[channel].count < (transfer_length - transferred))
                {
                        memcpy(data + transferred, &ram[piix_busmaster[channel].addr], piix_busmaster[channel].count);
                        transferred += piix_busmaster[channel].count;
                        piix_busmaster[channel].addr += piix_busmaster[channel].count;
			piix_busmaster[channel].addr %= (mem_size * 1024);
                        piix_busmaster[channel].count = 0;
                }                       
                else
                {
                        memcpy(data + transferred, &ram[piix_busmaster[channel].addr], transfer_length - transferred);
                        piix_busmaster[channel].addr += (transfer_length - transferred);
                        piix_busmaster[channel].count -= (transfer_length - transferred);
                        transferred += (transfer_length - transferred);                        
                }

                if (!piix_busmaster[channel].count)
                {
                        if (piix_busmaster[channel].eot) /*End of transfer?*/
                        {
                                piix_busmaster[channel].status &= ~1;
                        }
                        else
                           piix_bus_master_next_addr(channel);
                }
        }
        return 0;
}

void piix_bus_master_set_irq(int channel)
{
        piix_busmaster[channel].status |= 4;
}

/* static int reset_reg = 0;

static uint8_t rc_read(uint16_t port, void *priv)
{
	return reset_reg & 0xfb;
}

static void rc_write(uint16_t port, uint8_t val, void *priv)
{
	if (!(reset_reg & 4) && (val & 4))
	{
		if (reset_reg & 2)
		{
			resetpchard();
		}
		else
		{
			if (piix_type == 3)
			{
				piix3_reset();
			}
			else
			{
				piix_reset();
			}
			resetide();
			softresetx86();
		}
	}
	reset_reg = val;
} */

void piix_reset(void)
{
        memset(card_piix, 0, 256);
        card_piix[0x00] = 0x86; card_piix[0x01] = 0x80; /*Intel*/
        card_piix[0x02] = 0x2e; card_piix[0x03] = 0x12; /*82371FB (PIIX)*/
        card_piix[0x04] = 0x07; card_piix[0x05] = 0x00;
        card_piix[0x06] = 0x80; card_piix[0x07] = 0x02;
        card_piix[0x08] = 0x00; /*A0 stepping*/
        card_piix[0x09] = 0x00; card_piix[0x0a] = 0x01; card_piix[0x0b] = 0x06;
        card_piix[0x0e] = 0x80; /*Multi-function device*/
        card_piix[0x4c] = 0x4d;
        card_piix[0x4e] = 0x03;
        card_piix[0x60] = card_piix[0x61] = card_piix[0x62] = card_piix[0x63] = 0x80;
        card_piix[0x69] = 0x02;
        card_piix[0x70] = card_piix[0x71] = 0x80;
        card_piix[0x76] = card_piix[0x77] = 0x0c;
        card_piix[0x78] = 0x02; card_piix[0x79] = 0x00;
        card_piix[0xa0] = 0x08;
        card_piix[0xa2] = card_piix[0xa3] = 0x00;
        card_piix[0xa4] = card_piix[0xa5] = card_piix[0xa6] = card_piix[0xa7] = 0x00;
        card_piix[0xa8] = 0x0f;
        card_piix[0xaa] = card_piix[0xab] = 0x00;
        card_piix[0xac] = 0x00;
        card_piix[0xae] = 0x00;

        card_piix_ide[0x00] = 0x86; card_piix_ide[0x01] = 0x80; /*Intel*/
        card_piix_ide[0x02] = 0x30; card_piix_ide[0x03] = 0x12; /*82371FB (PIIX)*/
        card_piix_ide[0x04] = 0x00; card_piix_ide[0x05] = 0x00;
        card_piix_ide[0x06] = 0x80; card_piix_ide[0x07] = 0x02;
        card_piix_ide[0x08] = 0x00;
        card_piix_ide[0x09] = 0x80; card_piix_ide[0x0a] = 0x01; card_piix_ide[0x0b] = 0x01;
        card_piix_ide[0x0d] = 0x00;
        card_piix_ide[0x0e] = 0x00;
        card_piix_ide[0x20] = 0x01; card_piix_ide[0x21] = card_piix_ide[0x22] = card_piix_ide[0x23] = 0x00; /*Bus master interface base address*/
        card_piix_ide[0x40] = card_piix_ide[0x41] = 0x00;
        card_piix_ide[0x42] = card_piix_ide[0x43] = 0x00;
}

void piix3_reset(void)
{
        memset(card_piix, 0, 256);
        card_piix[0x00] = 0x86; card_piix[0x01] = 0x80; /*Intel*/
        card_piix[0x02] = 0x00; card_piix[0x03] = 0x70; /*82371SB (PIIX3)*/
        card_piix[0x04] = 0x07; card_piix[0x05] = 0x00;
        card_piix[0x06] = 0x80; card_piix[0x07] = 0x02;
        card_piix[0x08] = 0x00; /*A0 stepping*/
        card_piix[0x09] = 0x00; card_piix[0x0a] = 0x01; card_piix[0x0b] = 0x06;
        card_piix[0x0e] = 0x80; /*Multi-function device*/
        card_piix[0x4c] = 0x4d;
        card_piix[0x4e] = card_piix[0x4f] = 0x03;
        card_piix[0x60] = card_piix[0x61] = card_piix[0x62] = card_piix[0x63] = 0x80;
        card_piix[0x69] = 0x02;
        card_piix[0x70] = 0x80;
        card_piix[0x76] = card_piix[0x77] = 0x0c;
        card_piix[0x78] = 0x02; card_piix[0x79] = 0x00;
        card_piix[0x80] = card_piix[0x82] = 0x00;
        card_piix[0xa0] = 0x08;
        card_piix[0xa2] = card_piix[0xa3] = 0x00;
        card_piix[0xa4] = card_piix[0xa5] = card_piix[0xa6] = card_piix[0xa7] = 0x00;
        card_piix[0xa8] = 0x0f;
        card_piix[0xaa] = card_piix[0xab] = 0x00;
        card_piix[0xac] = 0x00;
        card_piix[0xae] = 0x00;

        card_piix_ide[0x00] = 0x86; card_piix_ide[0x01] = 0x80; /*Intel*/
        card_piix_ide[0x02] = 0x10; card_piix_ide[0x03] = 0x70; /*82371SB (PIIX3)*/
        card_piix_ide[0x04] = 0x00; card_piix_ide[0x05] = 0x00;
        card_piix_ide[0x06] = 0x80; card_piix_ide[0x07] = 0x02;
        card_piix_ide[0x08] = 0x00;
        card_piix_ide[0x09] = 0x80; card_piix_ide[0x0a] = 0x01; card_piix_ide[0x0b] = 0x01;
        card_piix_ide[0x0d] = 0x00;
        card_piix_ide[0x0e] = 0x00;
        card_piix_ide[0x20] = 0x01; card_piix_ide[0x21] = card_piix_ide[0x22] = card_piix_ide[0x23] = 0x00; /*Bus master interface base address*/
        card_piix_ide[0x40] = card_piix_ide[0x41] = 0x00;
        card_piix_ide[0x42] = card_piix_ide[0x43] = 0x00;
	card_piix_ide[0x44] = 0x00;
}

void piix_init(int card, int pci_a, int pci_b, int pci_c, int pci_d)
{
        pci_add_specific(card, piix_read, piix_write, NULL);

	piix_reset();

	piix_type = 1;
        
        ide_set_bus_master(piix_bus_master_dma_read, piix_bus_master_dma_write, piix_bus_master_set_irq);

	trc_init();

	port_92_reset();

	port_92_add();

	dma_alias_set();

	pci_reset_handler.pci_set_reset = piix_reset;

        pci_set_card_routing(pci_a, PCI_INTA);
        pci_set_card_routing(pci_b, PCI_INTB);
        pci_set_card_routing(pci_c, PCI_INTC);
        pci_set_card_routing(pci_d, PCI_INTD);
}

void piix3_init(int card, int pci_a, int pci_b, int pci_c, int pci_d)
{
        pci_add_specific(card, piix_read, piix_write, NULL);
        
	piix3_reset();

	piix_type = 3;
        
        ide_set_bus_master(piix_bus_master_dma_read, piix_bus_master_dma_write, piix_bus_master_set_irq);

	trc_init();

	port_92_reset();

	port_92_add();

	dma_alias_set();

	pci_reset_handler.pci_set_reset = piix3_reset;

        pci_set_card_routing(pci_a, PCI_INTA);
        pci_set_card_routing(pci_b, PCI_INTB);
        pci_set_card_routing(pci_c, PCI_INTC);
        pci_set_card_routing(pci_d, PCI_INTD);
}
