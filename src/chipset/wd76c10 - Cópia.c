/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the WD76C10 System Controller chip.
 *
 * Version:	@(#)wd76c10.c	1.0.0	2019/05/14
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../timer.h"
#include "../io.h"
#include "../keyboard.h"
#include "../mem.h"
#include "../port_92.h"
#include "../serial.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../video/vid_paradise.h"
#include "chipset.h"


typedef struct {
    int		type;

    uint16_t	reg_0092;
    uint16_t	reg_2072;
    uint16_t	reg_2872;
    uint16_t	reg_5872;

    serial_t	*uart[2];

    fdc_t	*fdc;
} wd76c10_t;


static uint16_t
wd76c10_read(uint16_t port, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;
    int16_t ret = 0xffff;

    switch (port) {
	case 0x2072:
		ret = dev->reg_2072;
		break;

	case 0x2872:
		ret = dev->reg_2872;
		break;

	case 0x5872:
		ret = dev->reg_5872;
		break;
    }

    return(ret);
}


static void
wd76c10_write(uint16_t port, uint16_t val, void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;

    switch (port) {
	case 0x2072:
		dev->reg_2072 = val;

                serial_remove(dev->uart[0]);
                if (!(val & 0x10))
                {
                        switch ((val >> 5) & 7)
                        {
                                case 1: serial_setup(dev->uart[0], 0x3f8, 4); break;
                                case 2: serial_setup(dev->uart[0], 0x2f8, 4); break;
                                case 3: serial_setup(dev->uart[0], 0x3e8, 4); break;
                                case 4: serial_setup(dev->uart[0], 0x2e8, 4); break;
                                default: break;
                        }
                }
                serial_remove(dev->uart[1]);
                if (!(val & 0x01))
                {
                        switch ((val >> 1) & 7)
                        {
                                case 1: serial_setup(dev->uart[1], 0x3f8, 3); break;
                                case 2: serial_setup(dev->uart[1], 0x2f8, 3); break;
                                case 3: serial_setup(dev->uart[1], 0x3e8, 3); break;
                                case 4: serial_setup(dev->uart[1], 0x2e8, 3); break;
                                default: break;
                        }
                }
                break;

	case 0x2872:
		dev->reg_2872 = val;

		fdc_remove(dev->fdc);
		if (! (val & 1))
			fdc_set_base(dev->fdc, 0x03f0);
		break;

	case 0x5872:
		dev->reg_5872 = val;
		break;
    }
}


static uint8_t
wd76c10_readb(uint16_t port, void *priv)
{
    if (port & 1)
	return(wd76c10_read(port & ~1, priv) >> 8);

    return(wd76c10_read(port, priv) & 0xff);
}


static void
wd76c10_writeb(uint16_t port, uint8_t val, void *priv)
{
    uint16_t temp = wd76c10_read(port, priv);

    if (port & 1)
	wd76c10_write(port & ~1, (temp & 0x00ff) | (val << 8), priv);
    else
	wd76c10_write(port     , (temp & 0xff00) | val, priv);
}


static void
wd76c10_close(void *priv)
{
    wd76c10_t *dev = (wd76c10_t *)priv;

    free(dev);
}


static void *
wd76c10_init(const device_t *info)
{
    wd76c10_t *dev;

    dev = (wd76c10_t *) malloc(sizeof(wd76c10_t));
    memset(dev, 0x00, sizeof(wd76c10_t));
    dev->type = info->local;

    dev->fdc = (fdc_t *)device_add(&fdc_at_device);

    dev->uart[0] = device_add_inst(&i8250_device, 1);
    dev->uart[1] = device_add_inst(&i8250_device, 2);

    device_add(&port_92_word_device);

    io_sethandler(0x2072, 2,
		  wd76c10_readb,wd76c10_read,NULL,
		  wd76c10_writeb,wd76c10_write,NULL, dev);
    io_sethandler(0x2872, 2,
		  wd76c10_readb,wd76c10_read,NULL,
		  wd76c10_writeb,wd76c10_write,NULL, dev);
    io_sethandler(0x5872, 2,
		  wd76c10_readb,wd76c10_read,NULL,
		  wd76c10_writeb,wd76c10_write,NULL, dev);

    return(dev);
}


const device_t wd76c10_device = {
    "WD 76C10",
    0,
    0,
    wd76c10_init, wd76c10_close, NULL,
    NULL, NULL, NULL,
    NULL
};
