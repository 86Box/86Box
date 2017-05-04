/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "pci.h"

#include "sis496.h"

typedef struct sis496_t
{
        uint8_t pci_conf[256];
} sis496_t;

void sis496_recalcmapping(sis496_t *sis496)
{
        int c;
        
        for (c = 0; c < 8; c++)
        {
                uint32_t base = 0xc0000 + (c << 15);
                if (sis496->pci_conf[0x44] & (1 << c))
                {
                        switch (sis496->pci_conf[0x45] & 3)
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
        shadowbios = (sis496->pci_conf[0x44] & 0xf0);
}

void sis496_write(int func, int addr, uint8_t val, void *p)
{
        sis496_t *sis496 = (sis496_t *)p;
        switch (addr)
        {
                case 0x44: /*Shadow configure*/
                if ((sis496->pci_conf[0x44] & val) ^ 0xf0)
                {
                        sis496->pci_conf[0x44] = val;
                        sis496_recalcmapping(sis496);
                }
                break;
                case 0x45: /*Shadow configure*/
                if ((sis496->pci_conf[0x45] & val) ^ 0x01)
                {
                        sis496->pci_conf[0x45] = val;
                        sis496_recalcmapping(sis496);
                }
                break;
        }
                
        if ((addr >= 4 && addr < 8) || addr >= 0x40)
           sis496->pci_conf[addr] = val;
}

uint8_t sis496_read(int func, int addr, void *p)
{
        sis496_t *sis496 = (sis496_t *)p;
        
        return sis496->pci_conf[addr];
}
 
void *sis496_init()
{
        sis496_t *sis496 = malloc(sizeof(sis496_t));
        memset(sis496, 0, sizeof(sis496_t));
        
        pci_add_specific(5, sis496_read, sis496_write, sis496);
        
        sis496->pci_conf[0x00] = 0x39; /*SiS*/
        sis496->pci_conf[0x01] = 0x10; 
        sis496->pci_conf[0x02] = 0x96; /*496/497*/
        sis496->pci_conf[0x03] = 0x04; 

        sis496->pci_conf[0x04] = 7;
        sis496->pci_conf[0x05] = 0;

        sis496->pci_conf[0x06] = 0x80;
        sis496->pci_conf[0x07] = 0x02;
        
        sis496->pci_conf[0x08] = 2; /*Device revision*/

        sis496->pci_conf[0x09] = 0x00; /*Device class (PCI bridge)*/
        sis496->pci_conf[0x0a] = 0x00;
        sis496->pci_conf[0x0b] = 0x06;
        
        sis496->pci_conf[0x0e] = 0x00; /*Single function device*/

	return sis496;
}

void sis496_close(void *p)
{
        sis496_t *sis496 = (sis496_t *)p;

        free(sis496);
}

device_t sis496_device =
{
        "SiS 496/497",
        0,
        sis496_init,
        sis496_close,
        NULL,
        NULL,
        NULL,
        NULL
};
