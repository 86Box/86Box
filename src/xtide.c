/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"

#include "io.h"
#include "ide.h"
#include "xtide.h"

uint8_t xtide_high;

void xtide_write(uint16_t port, uint8_t val, void *priv)
{
        switch (port & 0xf)
        {
                case 0x0:
                writeidew(0, val | (xtide_high << 8));
                return;
                
                case 0x1: case 0x2: case 0x3:
                case 0x4: case 0x5: case 0x6: case 0x7:
                writeide(0, (port  & 0xf) | 0x1f0, val);
                return;
                
                case 0x8:
                xtide_high = val;
                return;
                
                case 0xe:
                writeide(0, 0x3f6, val);
                return;
        }
}

uint8_t xtide_read(uint16_t port, void *priv)
{
        uint16_t tempw;
        switch (port & 0xf)
        {
                case 0x0:
                tempw = readidew(0);
                xtide_high = tempw >> 8;
                return tempw & 0xff;
                               
                case 0x1: case 0x2: case 0x3:
                case 0x4: case 0x5: case 0x6: case 0x7:
                return readide(0, (port  & 0xf) | 0x1f0);
                
                case 0x8:
                return xtide_high;
                
                case 0xe:
                return readide(0, 0x3f6);
        }
}

void xtide_init()
{
        ide_init();
        io_sethandler(0x0300, 0x0010, xtide_read, NULL, NULL, xtide_write, NULL, NULL, NULL);
}
