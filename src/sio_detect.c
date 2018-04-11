/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Super I/O chip detection code.
 *
 * Version:	@(#)sio_detect.c	1.0.0	2018/01/16
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "device.h"
#include "io.h"
#include "floppy/fdd.h"
#include "floppy/fdc.h"
#include "sio.h"


static uint8_t detect_regs[2];


static void superio_detect_write(uint16_t port, uint8_t val, void *priv)
{
        pclog("superio_detect_write : port=%04x = %02X\n", port, val);

	detect_regs[port & 1] = val;

	return;
}


static uint8_t superio_detect_read(uint16_t port, void *priv)
{
        pclog("superio_detect_read : port=%04x = %02X\n", port, detect_regs[port & 1]);

	return detect_regs[port & 1];
}


void superio_detect_init(void)
{
	device_add(&fdc_at_smc_device);

        io_sethandler(0x24, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x26, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x2e, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x44, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x46, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x4e, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x108, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x250, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x370, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
        io_sethandler(0x3f0, 0x0002, superio_detect_read, NULL, NULL, superio_detect_write, NULL, NULL,  NULL);
}
