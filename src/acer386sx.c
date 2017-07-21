/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "cpu/cpu.h"
#include "io.h"
#include "device.h"
#include "model.h"


static int acer_index = 0;
static uint8_t acer_regs[256];


static void acer386sx_write(uint16_t addr, uint8_t val, void *priv)
{
        if (addr & 1)
           acer_regs[acer_index] = val;
        else
           acer_index = val;
}


static uint8_t acer386sx_read(uint16_t addr, void *priv)
{
        if (addr & 1)
        {
                if ((acer_index >= 0xc0 || acer_index == 0x20) && cpu_iscyrix)
                   return 0xff; /*Don't conflict with Cyrix config registers*/
                return acer_regs[acer_index];
        }
        else
           return acer_index;
}


void acer386sx_init(void)
{
        io_sethandler(0x0022, 0x0002, acer386sx_read, NULL, NULL, acer386sx_write, NULL, NULL,  NULL);
}
