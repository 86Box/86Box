/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "io.h"
#include "mem.h"

#include "pci.h"

void    (*pci_card_write[32])(int func, int addr, uint8_t val, void *priv);
uint8_t  (*pci_card_read[32])(int func, int addr, void *priv);
void           *pci_priv[32];
static int pci_index, pci_func, pci_card, pci_bus, pci_enable, pci_key;
static int pci_min_card, pci_max_card;
int pci_burst_time, pci_nonburst_time;

void pci_cf8_write(uint16_t port, uint32_t val, void *p)
{
        pci_index = val & 0xff;
        pci_func = (val >> 8) & 7;
        pci_card = (val >> 11) & 31;
        pci_bus = (val >> 16) & 0xff;
        pci_enable = (val >> 31) & 1;
	// pclog("PCI card selected: %i\n", pci_card);
}

uint32_t pci_cf8_read(uint16_t port, void *p)
{
        return pci_index | (pci_func << 8) | (pci_card << 11) | (pci_bus << 16) | (pci_enable << 31);
}

void pci_write(uint16_t port, uint8_t val, void *priv)
{
//        pclog("pci_write: port=%04x val=%02x %08x:%08x\n", port, val, cs, pc);
        switch (port)
        {
                case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
                if (!pci_enable) 
                   return;
                   
//                pclog("PCI write bus %i card %i func %i index %02X val %02X  %04X:%04X\n", pci_bus, pci_card, pci_func, pci_index | (port & 3), val, CS, pc);
                
                if (!pci_bus && pci_card_write[pci_card])
                   pci_card_write[pci_card](pci_func, pci_index | (port & 3), val, pci_priv[pci_card]);
                
                break;
        }
}

uint8_t pci_read(uint16_t port, void *priv)
{
//        pclog("pci_read: port=%04x  %08x:%08x\n", port, cs, pc);
        switch (port)
        {
                case 0xcfc: case 0xcfd: case 0xcfe: case 0xcff:
                if (!pci_enable) 
                   return 0xff;

//                pclog("PCI read  bus %i card %i func %i index %02X\n", pci_bus, pci_card, pci_func, pci_index | (port & 3));

                if (!pci_bus && pci_card_read[pci_card])
                   return pci_card_read[pci_card](pci_func, pci_index | (port & 3), pci_priv[pci_card]);

                return 0xff;
        }
}

void pci_type2_write(uint16_t port, uint8_t val, void *priv);
uint8_t pci_type2_read(uint16_t port, void *priv);

void pci_type2_write(uint16_t port, uint8_t val, void *priv)
{
//        pclog("pci_type2_write: port=%04x val=%02x %08x:%08x\n", port, val, cs, pc);
        if (port == 0xcf8)
        {
                pci_func = (val >> 1) & 7;
                if (!pci_key && (val & 0xf0))
                        io_sethandler(0xc000, 0x1000, pci_type2_read, NULL, NULL, pci_type2_write, NULL, NULL, NULL);
                else
                        io_removehandler(0xc000, 0x1000, pci_type2_read, NULL, NULL, pci_type2_write, NULL, NULL, NULL);
                pci_key = val & 0xf0;
        }
        else if (port == 0xcfa)
        {
                pci_bus = val;
        }
        else
        {
                pci_card = (port >> 8) & 0xf;
                pci_index = port & 0xff;

//                pclog("PCI write bus %i card %i func %i index %02X val %02X  %04X:%04X\n", pci_bus, pci_card, pci_func, pci_index | (port & 3), val, CS, pc);
                
                if (!pci_bus && pci_card_write[pci_card])
                        pci_card_write[pci_card](pci_func, pci_index | (port & 3), val, pci_priv[pci_card]);
        }
}

uint8_t pci_type2_read(uint16_t port, void *priv)
{
//        pclog("pci_type2_read: port=%04x  %08x:%08x\n", port, cs, pc);
        if (port == 0xcf8)
        {
                return pci_key | (pci_func << 1);
        }
        else if (port == 0xcfa)
        {
                return pci_bus;
        }
        else
        {
                pci_card = (port >> 8) & 0xf;
                pci_index = port & 0xff;

//                pclog("PCI read  bus %i card %i func %i index %02X           %04X:%04X\n", pci_bus, pci_card, pci_func, pci_index | (port & 3), CS, pc);
                
                if (!pci_bus && pci_card_write[pci_card])
                        return pci_card_read[pci_card](pci_func, pci_index | (port & 3), pci_priv[pci_card]);
        }
        return 0xff;
}
                
void pci_init(int type, int min_card, int max_card)
{
        int c;

        PCI = 1;
        
        if (type == PCI_CONFIG_TYPE_1)
        {
                io_sethandler(0x0cf8, 0x0001, NULL, NULL, pci_cf8_read, NULL, NULL, pci_cf8_write,  NULL);
                io_sethandler(0x0cfc, 0x0004, pci_read, NULL, NULL, pci_write, NULL, NULL,  NULL);
        }
        else
        {
                io_sethandler(0x0cf8, 0x0001, pci_type2_read, NULL, NULL, pci_type2_write, NULL, NULL, NULL);
                io_sethandler(0x0cfa, 0x0001, pci_type2_read, NULL, NULL, pci_type2_write, NULL, NULL, NULL);
        }
        
        for (c = 0; c < 32; c++)
            pci_card_read[c] = pci_card_write[c] = pci_priv[c] = NULL;
        
        pci_min_card = min_card;
        pci_max_card = max_card;
}

void pci_add_specific(int card, uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv)
{
         pci_card_read[card] = read;
        pci_card_write[card] = write;
              pci_priv[card] = priv;
}

void pci_add(uint8_t (*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv)
{
        int c;
        
        for (c = pci_min_card; c <= pci_max_card; c++)
        {
                if (!pci_card_read[c] && !pci_card_write[c])
                {
                         pci_card_read[c] = read;
                        pci_card_write[c] = write;
                              pci_priv[c] = priv;
			// pclog("PCI device added to card: %i\n", c);
                        return;
                }
        }
}
