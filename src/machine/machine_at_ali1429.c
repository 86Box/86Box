/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../ibm.h"
#include "../cpu/cpu.h"
#include "../io.h"
#include "../mem.h"
#include "../device.h"
#include "../hdd/hdc.h"
#include "../hdd/hdc_ide.h"
#include "machine_at.h"
#include "machine_at_ali1429.h"


static int ali1429_index;
static uint8_t ali1429_regs[256];


static void ali1429_recalc(void)
{
        int c;
        
        for (c = 0; c < 8; c++)
        {
                uint32_t base = 0xc0000 + (c << 15);
                if (ali1429_regs[0x13] & (1 << c))
                {
                        switch (ali1429_regs[0x14] & 3)
                        {
                                case 0:
                                mem_set_mem_state(base, 0x8000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
                                break;
                                case 1: 
                                mem_set_mem_state(base, 0x8000, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                                break;
                                case 2:
                                mem_set_mem_state(base, 0x8000, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
                                break;
                                case 3:
                                mem_set_mem_state(base, 0x8000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                                break;
                        }
                }
                else
                        mem_set_mem_state(base, 0x8000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
        }
        
        flushmmucache();
}

void ali1429_write(uint16_t port, uint8_t val, void *priv)
{
        if (!(port & 1)) 
                ali1429_index = val;
        else
        {
                ali1429_regs[ali1429_index] = val;
                switch (ali1429_index)
                {
                        case 0x13:
                        ali1429_recalc();
                        break;
                        case 0x14:
                        shadowbios = val & 1;
                        shadowbios_write = val & 2;
                        ali1429_recalc();
                        break;
                }
        }
}

uint8_t ali1429_read(uint16_t port, void *priv)
{
        if (!(port & 1)) 
                return ali1429_index;
        if ((ali1429_index >= 0xc0 || ali1429_index == 0x20) && cpu_iscyrix)
                return 0xff; /*Don't conflict with Cyrix config registers*/
        return ali1429_regs[ali1429_index];
}


static void ali1429_reset(void)
{
        memset(ali1429_regs, 0xff, 256);
}


static void ali1429_init(void)
{
        io_sethandler(0x0022, 0x0002, ali1429_read, NULL, NULL, ali1429_write, NULL, NULL, NULL);
}

void machine_at_ali1429_init(void)
{
        ali1429_reset();

        machine_at_ide_init();
        ali1429_init();

	secondary_ide_check();
}
