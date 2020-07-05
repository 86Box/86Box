/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Contaq 82C59X chipset.
 *
 *
 *      Note: This chipset has no datasheet. Everything were done via reverse
 *      engineering. It's not known if other boards that use this chipset are
 *      pleased to work with that implementation.
 *
 *      Authors: Tiseno100
 *
 *		Copyright 2020 Tiseno100
 *
 */

/*

Contaq 82C59X Configuration Registers

The chipset is programmed by setting the index port 22h while 23h writes the actual data to the pointed port:
22h Index Port
23h Data Port

Register 13h

Bit 0: ???? (Wait State Related Possibly)
Bit 1: ???? (Wait State Related Possibly)
Bit 2: ???? (Wait State Related Possibly)
Bit 3: ????
Bit 4: External Cache Enable
Bit 5: ????
Bit 6: ????
Bit 7: ????

Register 14h

Bit 0: ????
Bit 1: ????
Bit 2: ????
Bit 3: ????
Bit 4: Memory Relocation Enable
Bit 5: ????
Bit 6: RAS Precharge Control (0 3 Sysclk | 1 2 Sysclk)
Bit 7: ????


Register 15h

Bit 0: ????
Bit 1: Enable Shadowing (Possibly)
Bit 2: C0000-C3FFF
Bit 3: C4000-C7FFF
Bit 4: C8000-CBFFF
Bit 5: CC000-CFFFF
Bit 6: ???? (Gets enabled if we Shadow the ROM. Write protect?)
Bit 7: F0000-FFFFF


Register 16h

Bit 0: ????
Bit 1: Enable Shadowing (Possibly)
Bit 2: D0000-D3FFF
Bit 3: D4000-D7FFF
Bit 4: D8000-DBFFF
Bit 5: DC000-DFFFF
Bit 6: ????
Bit 7: ????


Register 17h

Bit 0: ????
Bit 1: Enable Shadowing (Possibly)
Bit 2: E0000-E3FFF
Bit 3: E4000-E7FFF
Bit 4: E8000-EBFFF
Bit 5: EC000-EFFFF
Bit 6: Video ROM Cacheable
Bit 7: System ROM Cacheable


Register 19h

Bit 0: ????
Bit 1: ????
Bit 2: ????
Bit 3: ????
Bit 4: Non-Cache Block 1 Enable
Bit 5: ????
Bit 6: ????
Bit 7: ????

Register 1bh

Bit 0: ????
Bit 1: ????
Bit 2: ????
Bit 3: ????
Bit 4: Non-Cache Block 2 Enable
Bit 5: ????
Bit 6: ???? (Gets enabled for a short while till it gets disabled once again. SRAM Burst Control?)
Bit 7: ????

Register 20h

Bit 0: ????
Bit 1: ????
Bit 2: IO Recovery Control (0 Normal | 1 Extra)
Bit 3: ????
Bit 4: ????
Bit 5: ????
Bit 6: ????
Bit 7: ????

*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

typedef struct
{
    uint8_t	index,
	regs[256];
} contaq_t;

static void contaq_shadow_recalc(contaq_t *dev)
{

uint32_t base;
uint32_t i = 0;

/* The Contaq seems to Write Protect the Shadow when enabled */

/* Register 15h */
for(i = 2; i <= 5; i++){
    base = 0xc0000 + ((i-2) << 14);
    if((dev->regs[0x15] & 0x02) & (dev->regs[0x15] & (1 << i)))
    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
    else
    mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY); 
}

if(dev->regs[0x15] & 0x80)
{
shadowbios = 1;
shadowbios_write = 0;
mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
} 
else
{
shadowbios = 0;
shadowbios_write = 1;
mem_set_mem_state_both(0xf0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_INTERNAL);
}

/* Register 16h */
for(i = 2; i <= 5; i++){
    base = 0xd0000 + ((i-2) << 14);
    if((dev->regs[0x15] & 0x02) & (dev->regs[0x15] & (1 << i)))
    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_DISABLED);
    else
    mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);   
}

/* Register 17h */
for(i = 2; i <= 5; i++){
    base = 0xe0000 + ((i-2) << 14);
    if((dev->regs[0x15] & 0x02) & (dev->regs[0x15] & (1 << i)))
    mem_set_mem_state_both(base, 0x4000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
    else
    mem_set_mem_state_both(base, 0x4000, MEM_READ_EXTANY | MEM_WRITE_EXTANY); 
}

}

static void
contaq_write(uint16_t addr, uint8_t val, void *priv)
{
    contaq_t *dev = (contaq_t *) priv;

    switch (addr) {
	case 0x22:
		dev->index = val;
		break;
	case 0x23:
        pclog("Contaq: dev->regs[%02x] = %02x\n", dev->index, val);
		dev->regs[dev->index] = val;

        switch(dev->index){
            case 0x13:
            if(dev->regs[0x15] & 0x10)
            cpu_cache_ext_enabled = 1;
            cpu_update_waitstates();
            break;

            case 0x15:
            case 0x16:
            case 0x17:
            contaq_shadow_recalc(dev);
            break;
        }
		break;
    }
}


static uint8_t
contaq_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    contaq_t *dev = (contaq_t *) priv;

    switch (addr) {
	case 0x23:
        /* Avoid conflicting with Cyrix registers */
        if((dev->index == 0x20) && cpu_iscyrix)
        {
        ret = 0xff;
        } 
        else
        {
		ret = dev->regs[dev->index];
        }
		break;
    }

    return ret;
}


static void
contaq_close(void *priv)
{
    contaq_t *dev = (contaq_t *) priv;

    free(dev);
}


static void *
contaq_init(const device_t *info)
{
    contaq_t *dev = (contaq_t *) malloc(sizeof(contaq_t));
    memset(dev, 0, sizeof(contaq_t));

    device_add(&port_92_inv_device);

    io_sethandler(0x022, 0x0001, contaq_read, NULL, NULL, contaq_write, NULL, NULL, dev);
    io_sethandler(0x023, 0x0001, contaq_read, NULL, NULL, contaq_write, NULL, NULL, dev);
    
    dev->regs[0x11] = 0x00;
    dev->regs[0x12] = 0xfe;
    dev->regs[0x13] = 0x00;
    dev->regs[0x14] = 0x00;
    dev->regs[0x15] = 0x00;
    dev->regs[0x16] = 0x00;
    dev->regs[0x17] = 0x00;
    dev->regs[0x18] = 0x00;
    dev->regs[0x19] = 0x00;
    dev->regs[0x1a] = 0x00;
    dev->regs[0x1b] = 0x40;
    dev->regs[0x20] = 0x00;

    contaq_shadow_recalc(dev);

    return dev;
}


const device_t contaq_82c59x_device = {
    "Contaq 82C59X",
    0,
    0,
    contaq_init, contaq_close, NULL,
    NULL, NULL, NULL,
    NULL
};
