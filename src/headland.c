/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "cpu.h"

#include "headland.h"

static int headland_index;
static uint8_t headland_regs[256];

void headland_write(uint16_t addr, uint8_t val, void *priv)
{
        if (addr & 1)
        {
                if (headland_index == 0xc1 && !is486) val = 0;
                headland_regs[headland_index] = val;
                if (headland_index == 0x82)
                {
                        shadowbios = val & 0x10;
                        shadowbios_write = !(val & 0x10);
                        if (shadowbios)
                                mem_set_mem_state(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
                        else
                                mem_set_mem_state(0xf0000, 0x10000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
                }
        }
        else
                headland_index = val;
}

uint8_t headland_read(uint16_t addr, void *priv)
{
        if (addr & 1) 
        {
                if ((headland_index >= 0xc0 || headland_index == 0x20) && cpu_iscyrix)
                        return 0xff; /*Don't conflict with Cyrix config registers*/
                return headland_regs[headland_index];
        }
        return headland_index;
}

void headland_init()
{
        io_sethandler(0x0022, 0x0002, headland_read, NULL, NULL, headland_write, NULL, NULL, NULL);
}
