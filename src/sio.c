/*PRD format :
        
        word 0 - base address
        word 1 - bits 1 - 15 = byte count, bit 31 = end of transfer
*/
#include <string.h>

#include "ibm.h"
#include "ide.h"
#include "io.h"
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
        }
}

uint8_t sio_read(int func, int addr, void *priv)
{
//        pclog("sio_read: func=%d addr=%02x %04x:%08x\n", func, addr, CS, pc);

        if (func > 0)
           return 0xff;

	return card_sio[addr];
}

void sio_init(int card)
{
        pci_add_specific(card, sio_read, sio_write, NULL);
        
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
