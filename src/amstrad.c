/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "io.h"
#include "keyboard.h"
#include "lpt.h"
#include "mouse.h"

#include "amstrad.h"

static uint8_t amstrad_dead;

uint8_t amstrad_read(uint16_t port, void *priv)
{
        pclog("amstrad_read : %04X\n",port);
        switch (port)
        {
                case 0x379:
                return 7 | readdacfifo();
                case 0x37a:
                if (romset == ROM_PC1512) return 0x20;
                if (romset == ROM_PC200)  return 0x80;
                return 0;
                case 0xdead:
                return amstrad_dead;
        }
        return 0xff;
}

void amstrad_write(uint16_t port, uint8_t val, void *priv)
{
        switch (port)
        {
                case 0xdead:
                amstrad_dead = val;
                break;
        }
}

static uint8_t mousex,mousey;

void amstrad_mouse_write(uint16_t addr, uint8_t val, void *priv)
{
//        pclog("Write mouse %04X %02X %04X:%04X\n", addr, val, CS, pc);
        if (addr==0x78) mousex=0;
        else            mousey=0;
}

uint8_t amstrad_mouse_read(uint16_t addr, void *priv)
{
//        printf("Read mouse %04X %04X:%04X %02X\n", addr, CS, pc, (addr == 0x78) ? mousex : mousey);
        if (addr==0x78) return mousex;
        return mousey;
}

static int oldb = 0;

void amstrad_mouse_poll(int x, int y, int b)
{
        mousex += x;
        mousey -= y;

        if ((b & 1) && !(oldb & 1))
           keyboard_send(0x7e);
        if ((b & 2) && !(oldb & 2))
           keyboard_send(0x7d);
        if (!(b & 1) && (oldb & 1))
           keyboard_send(0xfe);
        if (!(b & 2) && (oldb & 2))
           keyboard_send(0xfd);
        
        oldb = b;
}

void amstrad_init()
{
        lpt2_remove_ams();
        
        io_sethandler(0x0078, 0x0001, amstrad_mouse_read, NULL, NULL, amstrad_mouse_write, NULL, NULL,  NULL);
        io_sethandler(0x007a, 0x0001, amstrad_mouse_read, NULL, NULL, amstrad_mouse_write, NULL, NULL,  NULL);
        io_sethandler(0x0379, 0x0002, amstrad_read,       NULL, NULL, NULL,                NULL, NULL,  NULL);
        io_sethandler(0xdead, 0x0001, amstrad_read,       NULL, NULL, amstrad_write,       NULL, NULL,  NULL);

        mouse_poll = amstrad_mouse_poll;
}
