#include <string.h>

#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "pci.h"

#include "i440fx.h"

static uint8_t card_i440fx[256];

static void i440fx_map(uint32_t addr, uint32_t size, int state)
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

void i440fx_write(int func, int addr, uint8_t val, void *priv)
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
                if ((card_i440fx[0x59] ^ val) & 0xf0)
                {
                        i440fx_map(0xf0000, 0x10000, val >> 4);
                        shadowbios = (val & 0x10);
                }
                // pclog("i440fx_write : PAM0 write %02X\n", val);
                break;
                case 0x5a: /*PAM1*/
                if ((card_i440fx[0x5a] ^ val) & 0x0f)
                        i440fx_map(0xc0000, 0x04000, val & 0xf);
                if ((card_i440fx[0x5a] ^ val) & 0xf0)
                        i440fx_map(0xc4000, 0x04000, val >> 4);
                break;
                case 0x5b: /*PAM2*/
                if ((card_i440fx[0x5b] ^ val) & 0x0f)
                        i440fx_map(0xc8000, 0x04000, val & 0xf);
                if ((card_i440fx[0x5b] ^ val) & 0xf0)
                        i440fx_map(0xcc000, 0x04000, val >> 4);
                break;
                case 0x5c: /*PAM3*/
                if ((card_i440fx[0x5c] ^ val) & 0x0f)
                        i440fx_map(0xd0000, 0x04000, val & 0xf);
                if ((card_i440fx[0x5c] ^ val) & 0xf0)
                        i440fx_map(0xd4000, 0x04000, val >> 4);
                break;
                case 0x5d: /*PAM4*/
                if ((card_i440fx[0x5d] ^ val) & 0x0f)
                        i440fx_map(0xd8000, 0x04000, val & 0xf);
                if ((card_i440fx[0x5d] ^ val) & 0xf0)
                        i440fx_map(0xdc000, 0x04000, val >> 4);
                break;
                case 0x5e: /*PAM5*/
                if ((card_i440fx[0x5e] ^ val) & 0x0f)
                        i440fx_map(0xe0000, 0x04000, val & 0xf);
                if ((card_i440fx[0x5e] ^ val) & 0xf0)
                        i440fx_map(0xe4000, 0x04000, val >> 4);
                // pclog("i440fx_write : PAM5 write %02X\n", val);
                break;
                case 0x5f: /*PAM6*/
                if ((card_i440fx[0x5f] ^ val) & 0x0f)
                        i440fx_map(0xe8000, 0x04000, val & 0xf);
                if ((card_i440fx[0x5f] ^ val) & 0xf0)
                        i440fx_map(0xec000, 0x04000, val >> 4);
                // pclog("i440fx_write : PAM6 write %02X\n", val);
                break;
        }
                
        card_i440fx[addr] = val;
}

uint8_t i440fx_read(int func, int addr, void *priv)
{
        if (func)
           return 0xff;

        return card_i440fx[addr];
}
 
    
void i440fx_init()
{
        pci_add_specific(0, i440fx_read, i440fx_write, NULL);
        
        memset(card_i440fx, 0, 256);
        card_i440fx[0x00] = 0x86; card_i440fx[0x01] = 0x80; /*Intel*/
        card_i440fx[0x02] = 0x37; card_i440fx[0x03] = 0x12; /*82441FX*/
        card_i440fx[0x04] = 0x03; card_i440fx[0x05] = 0x01;
        card_i440fx[0x06] = 0x00; card_i440fx[0x07] = 0x00;
        card_i440fx[0x08] = 0x02; /*A0 stepping*/
        card_i440fx[0x09] = 0x00; card_i440fx[0x0a] = 0x00; card_i440fx[0x0b] = 0x06;
	card_i440fx[0x2c] = 0xf4;
	card_i440fx[0x2d] = 0x1a;
	card_i440fx[0x2e] = 0x00;
	card_i440fx[0x2f] = 0x11;
        // card_i440fx[0x53] = 0x80;
        // card_i440fx[0x55] = 0x11;
        card_i440fx[0x57] = 0x10;
        card_i440fx[0x58] = 0x00;
	card_i440fx[0x59] = 0x10;
	card_i440fx[0x5a] = card_i440fx[0x5b] = card_i440fx[0x5c] = card_i440fx[0x5d] = card_i440fx[0x5e] = 0x11;
	card_i440fx[0x5f] = 0x31;
        // card_i440fx[0x60] = card_i440fx[0x61] = card_i440fx[0x62] = card_i440fx[0x63] = card_i440fx[0x64] = card_i440fx[0x65] = card_i440fx[0x66] = card_i440fx[0x67] = 0x02;
        // card_i440fx[0x70] = 0x20;
        // card_i440fx[0x71] = 0x10;
        card_i440fx[0x72] = 0x0A;
}
