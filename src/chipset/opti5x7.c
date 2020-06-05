/*Based off the OPTI 82C546/82C547 datasheet. 
The earlier 596/597 appears to be register compatible with the 546/547 from testing.*/
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
    uint8_t	cur_reg,
		regs[64];
    port_92_t  *port_92;		
} opti5x7_t;

static void
opti5x7_recalcmapping(opti5x7_t *dev)
{
    uint32_t shflags = 0;

    shadowbios = 0;
    shadowbios_write = 0;


    shadowbios |= !!(dev->regs[0x06] & 0x05);
    shadowbios_write |= !!(dev->regs[0x06] & 0x0a);

    shflags = (dev->regs[0x06] & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    shflags |= (dev->regs[0x06] & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
    mem_set_mem_state(0xe0000, 0x10000, shflags);

    shflags = (dev->regs[0x06] & 0x04) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
    shflags |= (dev->regs[0x06] & 0x08) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY;
    mem_set_mem_state(0xf0000, 0x10000, shflags);

    flushmmucache();
}
static void
opti5x7_write(uint16_t addr, uint8_t val, void *priv)
{	
    opti5x7_t *dev = (opti5x7_t *) priv;
//  pclog("Write %02x to OPTi 5x7 address %02x\n", val, addr);
    
    switch (addr) {
	case 0x22:
		dev->cur_reg = val;
		break;	    
	case 0x24:	
			dev->regs[dev->cur_reg] = val;
			if (dev->cur_reg == 0x02) {
				cpu_cache_ext_enabled = val & 0x10;
			}
			if (dev->cur_reg == 0x06) {
				opti5x7_recalcmapping(dev);			
			}
		break;	    
    }
}


static uint8_t
opti5x7_read(uint16_t addr, void *priv)
{
    uint8_t ret = 0xff;
    opti5x7_t *dev = (opti5x7_t *) priv;

    switch (addr) {
	case 0x24:
//			pclog("Read from OPTI 5x7 register %02x\n", dev->cur_reg);
			ret = dev->regs[dev->cur_reg];
		break;
    }

    return ret;
}


static void
opti5x7_close(void *priv)
{
    opti5x7_t *dev = (opti5x7_t *) priv;

    free(dev);
}


static void *
opti5x7_init(const device_t *info)
{
    opti5x7_t *dev = (opti5x7_t *) malloc(sizeof(opti5x7_t));
    memset(dev, 0, sizeof(opti5x7_t));

    io_sethandler(0x0022, 0x0001, opti5x7_read, NULL, NULL, opti5x7_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti5x7_read, NULL, NULL, opti5x7_write, NULL, NULL, dev);

    dev->port_92 = device_add(&port_92_device);    
//  pclog("OPTi 5x7 init\n");
    opti5x7_recalcmapping(dev);
  

    return dev;
}

const device_t opti5x7_device = {
    "OPTi 82C5x6/82C5x7",
    0,
    0,
    opti5x7_init, opti5x7_close, NULL,
    NULL, NULL, NULL,
    NULL
};
