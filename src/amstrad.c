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

void amstrad_init()
{
        lpt2_remove_ams();
        
        io_sethandler(0x0379, 0x0002, amstrad_read,       NULL, NULL, NULL,                NULL, NULL,  NULL);
        io_sethandler(0xdead, 0x0001, amstrad_read,       NULL, NULL, amstrad_write,       NULL, NULL,  NULL);
}
