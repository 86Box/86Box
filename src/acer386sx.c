#include "ibm.h"
#include "io.h"
#include "cpu.h"

#include "acer386sx.h"

static int acer_index = 0;
static uint8_t acer_regs[256];

void acer386sx_write(uint16_t addr, uint8_t val, void *priv)
{
        if (addr & 1)
           acer_regs[acer_index] = val;
        else
           acer_index = val;
}

uint8_t acer386sx_read(uint16_t addr, void *priv)
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

void acer386sx_init()
{
        io_sethandler(0x0022, 0x0002, acer386sx_read, NULL, NULL, acer386sx_write, NULL, NULL,  NULL);
}
