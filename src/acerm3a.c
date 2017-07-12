/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "cpu/cpu.h"
#include "io.h"
#include "device.h"
#include "model.h"


static int acerm3a_index;


static void acerm3a_write(uint16_t port, uint8_t val, void *p)
{
        if (port == 0xea)
                acerm3a_index = val;
}


static uint8_t acerm3a_read(uint16_t port, void *p)
{
        if (port == 0xeb)
        {
                switch (acerm3a_index)
                {
                        case 2:
                        return 0xfd;
                }
        }
        return 0xff;
}


void acerm3a_io_init(void)
{
        io_sethandler(0x00ea, 0x0002, acerm3a_read, NULL, NULL, acerm3a_write, NULL, NULL, NULL);
}
