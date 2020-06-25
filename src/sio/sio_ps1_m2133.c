/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the chipset used by the IBM PS/1 Model 2133 EMEA 451
 *		whose name is currently unknown.
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/sio.h>


typedef struct ps1_m2133_sio_t
{
        int idx;
        uint8_t regs[3];
	serial_t *uart[2];
} ps1_m2133_sio_t;

static uint16_t ps1_lpt_io[4] = {0x378, 0x3bc, 0x278, 0x378};
static uint16_t ps1_com_io[4] = {0x3f8, 0x2f8, 0x3e8, 0x2e8};

static uint8_t 
ps1_m2133_read(uint16_t port, void *p)
{
	ps1_m2133_sio_t *dev = (ps1_m2133_sio_t *)p;
	uint8_t ret = 0xff;

	switch (port) {
		case 0x398:
			ret = dev->idx;
			break;
                
		case 0x399:
			if (dev->idx < 3)
				ret = dev->regs[dev->idx];
			break;
	}
	return ret;
}

static void 
ps1_m2133_write(uint16_t port, uint8_t val, void *p)
{
	ps1_m2133_sio_t *dev = (ps1_m2133_sio_t *)p;
	uint16_t lpt_addr;

	switch (port) {
		case 0x398:
			dev->idx = val;
			break;
		
		case 0x399:
			if (dev->idx < 3) {
				dev->regs[dev->idx] = val;
				
				lpt1_remove();
				lpt2_remove();
				serial_remove(dev->uart[0]);
				serial_remove(dev->uart[1]);

				if (dev->regs[0] & 1) {
					lpt_addr = ps1_lpt_io[dev->regs[1] & 3];
					
					lpt1_init(lpt_addr);
					if ((lpt_addr == 0x378) || (lpt_addr == 0x3bc)) {
						if (((dev->regs[1] & 3) == 3) && (lpt_addr == 0x378)) {
							lpt1_irq(5);
						} else {
							lpt1_irq(7);
						}
					} else if (lpt_addr == 0x278) {
						lpt1_irq(5);
					}
				}
				
				if (dev->regs[0] & 2)
					serial_setup(dev->uart[0], ps1_com_io[(dev->regs[1] >> 2) & 3], 4);
				if (dev->regs[0] & 4)
					serial_setup(dev->uart[1], ps1_com_io[(dev->regs[1] >> 4) & 3], 3);
			}
			break;
	}
}

static void
ps1_m2133_reset(ps1_m2133_sio_t *dev)
{
	serial_remove(dev->uart[0]);
	serial_setup(dev->uart[0], SERIAL1_ADDR, SERIAL1_IRQ);

	serial_remove(dev->uart[1]);
	serial_setup(dev->uart[1], SERIAL2_ADDR, SERIAL2_IRQ);
	
	lpt1_remove();
	lpt1_init(0x378);
	lpt1_irq(7);
}

static void *
ps1_m2133_init(const device_t *info)
{
	ps1_m2133_sio_t *dev = (ps1_m2133_sio_t *) malloc(sizeof(ps1_m2133_sio_t));
	memset(dev, 0, sizeof(ps1_m2133_sio_t));

	dev->uart[0] = device_add_inst(&ns16450_device, 1);
	dev->uart[1] = device_add_inst(&ns16450_device, 2);	
	
        io_sethandler(0x0398, 0x0002, ps1_m2133_read, NULL, NULL, ps1_m2133_write, NULL, NULL, dev);
	
	ps1_m2133_reset(dev);
	
	return dev;
}

static void
ps1_m2133_close(void *p)
{
	ps1_m2133_sio_t *dev = (ps1_m2133_sio_t *)p;

	free(dev);
}

const device_t ps1_m2133_sio = {
	"IBM PS/1 Model 2133 EMEA 451 Super I/O",
	0,
	0,
	ps1_m2133_init, ps1_m2133_close, NULL,
	NULL, NULL, NULL,
	NULL
};
