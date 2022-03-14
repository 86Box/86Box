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
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>


typedef struct {
    uint8_t regs[2];
} sio_detect_t;


static void
sio_detect_write(uint16_t port, uint8_t val, void *priv)
{
    sio_detect_t *dev = (sio_detect_t *) priv;

    pclog("sio_detect_write : port=%04x = %02X\n", port, val);

    dev->regs[port & 1] = val;

    return;
}


static uint8_t
sio_detect_read(uint16_t port, void *priv)
{
    sio_detect_t *dev = (sio_detect_t *) priv;

    pclog("sio_detect_read : port=%04x = %02X\n", port, dev->regs[port & 1]);

    return 0xff /*dev->regs[port & 1]*/;
}


static void
sio_detect_close(void *priv)
{
    sio_detect_t *dev = (sio_detect_t *) priv;

    free(dev);
}


static void *
sio_detect_init(const device_t *info)
{
    sio_detect_t *dev = (sio_detect_t *) malloc(sizeof(sio_detect_t));
    memset(dev, 0, sizeof(sio_detect_t));

    device_add(&fdc_at_smc_device);

    io_sethandler(0x0022, 0x0006,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x002e, 0x0002,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x0044, 0x0004,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x004e, 0x0002,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x0108, 0x0002,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x015c, 0x0002,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x0250, 0x0003,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x026e, 0x0002,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x0279, 0x0001,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(FDC_SECONDARY_ADDR, 0x0002,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x0398, 0x0002,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x03e3, 0x0001,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(FDC_PRIMARY_ADDR, 0x0002,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);
    io_sethandler(0x0a79, 0x0001,
		  sio_detect_read, NULL, NULL, sio_detect_write, NULL, NULL, dev);

    return dev;
}


const device_t sio_detect_device = {
    .name = "Super I/O Detection Helper",
    .internal_name = "sio_detect",
    .flags = 0,
    .local = 0,
    .init = sio_detect_init,
    .close = sio_detect_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
