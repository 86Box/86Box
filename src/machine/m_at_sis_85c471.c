/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the SiS 85c471 chip.
 *
 *		SiS sis85c471 Super I/O Chip
 *		Used by DTK PKM-0038S E-2
 *
 * Version:	@(#)m_at_sis85c471.c	1.0.12	2018/11/09
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2015-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../io.h"
#include "../memregs.h"
#include "../device.h"
#include "../lpt.h"
#include "../serial.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "machine.h"


typedef struct {
    uint8_t cur_reg,
	    regs[39];
} sis_85c471_t;


static void
sis_85c471_write(uint16_t port, uint8_t val, void *priv)
{
    sis_85c471_t *dev = (sis_85c471_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t valxor;
    serial_t *uart[2];

    if (index) {
	if ((val >= 0x50) && (val <= 0x76))
		dev->cur_reg = val;
	return;
    } else {
	if ((dev->cur_reg < 0x50) || (dev->cur_reg > 0x76))
		return;
	valxor = val ^ dev->regs[dev->cur_reg - 0x50];
	/* Writes to 0x52 are blocked as otherwise, large hard disks don't read correctly. */
	if (dev->cur_reg != 0x52)
		dev->regs[dev->cur_reg - 0x50] = val;
    }

    switch(dev->cur_reg) {
	case 0x73:
		if (valxor & 0x40) {
			ide_pri_disable();
			if (val & 0x40)
				ide_pri_enable();
		}
		if (valxor & 0x20) {
			uart[0] = machine_get_serial(0);
			uart[1] = machine_get_serial(1);
			serial_remove(uart[0]);
			serial_remove(uart[1]);
			if (val & 0x20) {
				serial_setup(uart[0], SERIAL1_ADDR, SERIAL1_IRQ);
				serial_setup(uart[0], SERIAL2_ADDR, SERIAL2_IRQ);
			}
		}
		if (valxor & 0x10) {
			lpt1_remove();
			if (val & 0x10)
				lpt1_init(0x378);
		}
		break;
    }

    dev->cur_reg = 0;
}


static uint8_t
sis_85c471_read(uint16_t port, void *priv)
{
    sis_85c471_t *dev = (sis_85c471_t *) priv;
    uint8_t index = (port & 1) ? 0 : 1;
    uint8_t ret = 0xff;;

    if (index)
	ret = dev->cur_reg;
    else {
	if ((dev->cur_reg >= 0x50) && (dev->cur_reg <= 0x76)) {
		ret = dev->regs[dev->cur_reg - 0x50];
		dev->cur_reg = 0;
	}
    }

    return ret;
}


static void
sis_85c471_close(void *priv)
{
    sis_85c471_t *dev = (sis_85c471_t *) priv;

    free(dev);
}


static void *
sis_85c471_init(const device_t *info)
{
    int mem_size_mb, i = 0;

    sis_85c471_t *dev = (sis_85c471_t *) malloc(sizeof(sis_85c471_t));
    memset(dev, 0, sizeof(sis_85c471_t));

    lpt2_remove();

    dev->cur_reg = 0;
    for (i = 0; i < 0x27; i++)
	dev->regs[i] = 0x00;

    dev->regs[9] = 0x40;

    mem_size_mb = mem_size >> 10;
    switch (mem_size_mb) {
	case 0: case 1:
		dev->regs[9] |= 0;
		break;
	case 2: case 3:
		dev->regs[9] |= 1;
		break;
	case 4:
		dev->regs[9] |= 2;
		break;
	case 5:
		dev->regs[9] |= 0x20;
		break;
	case 6: case 7:
		dev->regs[9] |= 9;
		break;
	case 8: case 9:
		dev->regs[9] |= 4;
		break;
	case 10: case 11:
		dev->regs[9] |= 5;
		break;
	case 12: case 13: case 14: case 15:
		dev->regs[9] |= 0xB;
		break;
	case 16:
		dev->regs[9] |= 0x13;
		break;
	case 17:
		dev->regs[9] |= 0x21;
		break;
	case 18: case 19:
		dev->regs[9] |= 6;
		break;
	case 20: case 21: case 22: case 23:
		dev->regs[9] |= 0xD;
		break;
	case 24: case 25: case 26: case 27:
	case 28: case 29: case 30: case 31:
		dev->regs[9] |= 0xE;
		break;
	case 32: case 33: case 34: case 35:
		dev->regs[9] |= 0x1B;
		break;
	case 36: case 37: case 38: case 39:
		dev->regs[9] |= 0xF;
		break;
	case 40: case 41: case 42: case 43:
	case 44: case 45: case 46: case 47:
		dev->regs[9] |= 0x17;
		break;
	case 48:
		dev->regs[9] |= 0x1E;
		break;
	default:
		if (mem_size_mb < 64)
			dev->regs[9] |= 0x1E;
		else if ((mem_size_mb >= 65) && (mem_size_mb < 68))
			dev->regs[9] |= 0x22;
		else
			dev->regs[9] |= 0x24;
		break;
    }

    dev->regs[0x11] = 9;
    dev->regs[0x12] = 0xFF;
    dev->regs[0x23] = 0xF0;
    dev->regs[0x26] = 1;

    io_sethandler(0x0022, 0x0002,
		  sis_85c471_read, NULL, NULL, sis_85c471_write, NULL, NULL, dev);

    return dev;
}


const device_t sis_85c471_device = {
    "SiS 85c471",
    0,
    0,
    sis_85c471_init, sis_85c471_close, NULL,
    NULL, NULL, NULL,
    NULL
};


void
machine_at_dtk486_init(const machine_t *model)
{
    machine_at_ide_init(model);
    device_add(&fdc_at_device);

    memregs_init();
    device_add(&sis_85c471_device);
}
