/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdlib.h>
#include "ibm.h"
#include "CPU/cpu.h"
#include "io.h"
#include "mem.h"
#include "pci.h"
#include "device.h"
#include "model.h"


typedef struct sis496_t
{
        uint8_t pci_conf[256];
} sis496_t;


sis496_t sis496;


static void sis496_recalcmapping(void)
{
        int c;
        
        for (c = 0; c < 8; c++)
        {
                uint32_t base = 0xc0000 + (c << 15);
                if (sis496.pci_conf[0x44] & (1 << c))
                {
                        switch (sis496.pci_conf[0x45] & 3)
                        {
                                case 0:
                                mem_set_mem_state(base, 0x8000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
                                break;
                                case 1:
                                mem_set_mem_state(base, 0x8000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
                                break;
                                case 2:
                                mem_set_mem_state(base, 0x8000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                                break;
                                case 3:
                                mem_set_mem_state(base, 0x8000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                                break;
                        }
                }
                else
                        mem_set_mem_state(base, 0x8000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
        }

        flushmmucache();
        shadowbios = (sis496.pci_conf[0x44] & 0xf0);
}


static void sis496_write(int func, int addr, uint8_t val, void *p)
{
        switch (addr)
        {
                case 0x44: /*Shadow configure*/
                if ((sis496.pci_conf[0x44] & val) ^ 0xf0)
                {
                        sis496.pci_conf[0x44] = val;
                        sis496_recalcmapping();
                }
                break;
                case 0x45: /*Shadow configure*/
                if ((sis496.pci_conf[0x45] & val) ^ 0x01)
                {
                        sis496.pci_conf[0x45] = val;
                        sis496_recalcmapping();
                }
                break;
                
                case 0xc0:
                if (val & 0x80)
                        pci_set_irq_routing(PCI_INTA, val & 0xf);
                else
                        pci_set_irq_routing(PCI_INTA, PCI_IRQ_DISABLED);
                break;
                case 0xc1:
                if (val & 0x80)
                        pci_set_irq_routing(PCI_INTB, val & 0xf);
                else
                        pci_set_irq_routing(PCI_INTB, PCI_IRQ_DISABLED);
                break;
                case 0xc2:
                if (val & 0x80)
                        pci_set_irq_routing(PCI_INTC, val & 0xf);
                else
                        pci_set_irq_routing(PCI_INTC, PCI_IRQ_DISABLED);
                break;
                case 0xc3://                pclog("IRQ routing %02x %02x\n", addr, val);
                if (val & 0x80)
                        pci_set_irq_routing(PCI_INTD, val & 0xf);
                else
                        pci_set_irq_routing(PCI_INTD, PCI_IRQ_DISABLED);
                break;
        }
  
        if ((addr >= 4 && addr < 8) || addr >= 0x40)
           sis496.pci_conf[addr] = val;
}


static uint8_t sis496_read(int func, int addr, void *p)
{
        return sis496.pci_conf[addr];
}
 

static void sis496_reset(void)
{
        memset(&sis496, 0, sizeof(sis496_t));
        
        sis496.pci_conf[0x00] = 0x39; /*SiS*/
        sis496.pci_conf[0x01] = 0x10; 
        sis496.pci_conf[0x02] = 0x96; /*496/497*/
        sis496.pci_conf[0x03] = 0x04; 

        sis496.pci_conf[0x04] = 7;
        sis496.pci_conf[0x05] = 0;

        sis496.pci_conf[0x06] = 0x80;
        sis496.pci_conf[0x07] = 0x02;
        
        sis496.pci_conf[0x08] = 2; /*Device revision*/

        sis496.pci_conf[0x09] = 0x00; /*Device class (PCI bridge)*/
        sis496.pci_conf[0x0a] = 0x00;
        sis496.pci_conf[0x0b] = 0x06;

        sis496.pci_conf[0x0e] = 0x00; /*Single function device*/
}


static void sis496_pci_reset(void)
{
	uint8_t val = 0;

	val = sis496_read(0, 0x44, NULL);		/* Read current value of 0x44. */
	sis496_write(0, 0x44, val & 0xf, NULL);		/* Turn off shadow BIOS but keep the lower 4 bits. */
}


void sis496_init(void)
{
        pci_add_specific(5, sis496_read, sis496_write, NULL);

	sis496_reset();

	pci_reset_handler.pci_master_reset = sis496_pci_reset;

        pci_set_card_routing(15, PCI_INTA);
        pci_set_card_routing(13, PCI_INTD);
        pci_set_card_routing(11, PCI_INTC);
}


void sis496_close(void *p)
{
        sis496_t *sis496 = (sis496_t *)p;

        free(sis496);
}
