/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../cpu/x86.h"
#include "../io.h"
#include "../device.h"
#include "../keyboard.h"
#include "../mem.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "machine.h"


static int headland_index;
static uint8_t headland_regs[256];


static void headland_write(uint16_t addr, uint8_t val, void *priv)
{
	uint8_t old_val;

        if (addr & 1)
        {
		old_val = headland_regs[headland_index];

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
                else if (headland_index == 0x87)
                {
                        if ((val & 1) && !(old_val & 1))
                                softresetx86();
                }
        }
        else
                headland_index = val;
}


static uint8_t headland_read(uint16_t addr, void *priv)
{
        if (addr & 1) 
        {
                if ((headland_index >= 0xc0 || headland_index == 0x20) && cpu_iscyrix)
                        return 0xff; /*Don't conflict with Cyrix config registers*/
                return headland_regs[headland_index];
        }
        return headland_index;
}


static void headland_init(void)
{
        io_sethandler(0x0022, 0x0002, headland_read, NULL, NULL, headland_write, NULL, NULL, NULL);
}


void
machine_at_headland_init(machine_t *model)
{
        machine_at_common_ide_init(model);

	device_add(&keyboard_at_ami_device);
	device_add(&fdc_at_device);

        headland_init();
}
