/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "CPU/cpu.h"
#include "io.h"
#include "device.h"
#include "model.h"


static uint8_t olivetti_m24_read(uint16_t port, void *priv)
{
        switch (port)
        {
                case 0x66:
                return 0x00;
                case 0x67:
                return 0x20 | 0x40 | 0x0C;
        }
        return 0xff;
}


void olivetti_m24_init(void)
{
        io_sethandler(0x0066, 0x0002, olivetti_m24_read, NULL, NULL, NULL, NULL, NULL, NULL);
}
