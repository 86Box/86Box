/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <string.h>

#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "pci.h"

#include "i430hx.h"

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

void i430hx_write(int func, int addr, uint8_t val, void *priv)
{
        if (func)
           return;
           
        switch (addr)
        {
                case 0x00: case 0x01: case 0x02: case 0x03:
                case 0x08: case 0x09: case 0x0a: case 0x0b:
                case 0x0e:
                return;
                
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
        }
                
        card_i430hx[addr] = val;
}

uint8_t i430hx_read(int func, int addr, void *priv)
{
        if (func)
           return 0xff;

        return card_i430hx[addr];
}
 
void i430hx_reset(void)
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
    
void i430hx_pci_reset(void)
{
	i430hx_write(0, 0x59, 0x00, NULL);
}

void i430hx_init()
{
        pci_add_specific(0, i430hx_read, i430hx_write, NULL);

	i430hx_reset();

	pci_reset_handler.pci_master_reset = i430hx_pci_reset;
}
