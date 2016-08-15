/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "pci.h"

#include "um8881f.h"

static uint8_t card_16[256];
static uint8_t card_18[256];

void um8881f_write(int func, int addr, uint8_t val, void *priv)
{
//        pclog("um8881f_write : addr=%02x val=%02x %04x:%04x\n", addr, val, CS, pc);
        if (addr == 0x54)
        {
/*                if ((card_16[0x54] ^ val) & 0x01)
                {
                        if (val & 1)
                                mem_bios_set_state(0xe0000, 0x10000, 1, 1);
                        else
                                mem_bios_set_state(0xe0000, 0x10000, 0, 0);
                }*/
                flushmmucache_nopc();
        }
        if (addr == 0x55)
        {
                if ((card_16[0x55] ^ val) & 0xc0)
                {
/*                        switch (val & 0xc0)
                        {
                                case 0x00: mem_bios_set_state(0xf0000, 0x10000, 0, 1); break;
                                case 0x40: mem_bios_set_state(0xf0000, 0x10000, 0, 0); break;
                                case 0x80: mem_bios_set_state(0xf0000, 0x10000, 1, 1); break;
                                case 0xc0: mem_bios_set_state(0xf0000, 0x10000, 1, 0); break;
                        }*/
                        shadowbios = val & 0x80;
                        shadowbios_write = !(val & 0x40);
                        flushmmucache_nopc();
                }
        }
        if (addr >= 4) 
           card_16[addr] = val;
}

uint8_t um8881f_read(int func, int addr, void *priv)
{
        return card_16[addr];
}
 
void um8886f_write(int func, int addr, uint8_t val, void *priv)
{
        if (addr >= 4) 
           card_18[addr] = val;
}

uint8_t um8886f_read(int func, int addr, void *priv)
{
        return card_18[addr];
}

void um8881f_init()
{
        pci_add_specific(16, um8881f_read, um8881f_write, NULL);
        pci_add_specific(18, um8886f_read, um8886f_write, NULL);
        
        card_16[0] = card_18[0] = 0x60; /*UMC*/
        card_16[1] = card_18[1] = 0x10;
        card_16[2] = 0x81; card_16[3] = 0x88; /*UM8881 Host - PCI bridge*/
        card_18[2] = 0x86; card_18[3] = 0x88; /*UM8886 PCI - ISA bridge*/
}
