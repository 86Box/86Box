/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Serial Mouse devices.
 *
 * TODO:	Add the Genius Serial Mouse.
 *
 * Version:	@(#)mouse_serial.c	1.0.25	2018/10/17
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "device.h"
#include "timer.h"
#include "serial.h"
#include "mouse.h"


#define SERMOUSE_PORT	0			/* attach to Serial0 */


typedef struct {
    const char	*name;				/* name of this device */
    int8_t	type,				/* type of this device */
		port;
    uint8_t	flags;				/* device flags */

    int		pos;
    int64_t	delay;
    int		oldb;

    SERIAL	*serial;
} mouse_t;
#define FLAG_INPORT	0x80			/* device is MS InPort */
#define FLAG_3BTN	0x20			/* enable 3-button mode */
#define FLAG_SCALED	0x10			/* enable delta scaling */
#define FLAG_INTR	0x04			/* dev can send interrupts */
#define FLAG_FROZEN	0x02			/* do not update counters */
#define FLAG_ENABLED	0x01			/* dev is enabled for use */


#ifdef ENABLE_MOUSE_SERIAL_LOG
int mouse_serial_do_log = ENABLE_MOUSE_SERIAL_LOG;


static void
mouse_serial_log(const char *fmt, ...)
{
    va_list ap;

    if (mouse_serial_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define mouse_serial_log(fmt, ...)
#endif


/* Callback from serial driver: RTS was toggled. */
static void
sermouse_callback(struct SERIAL *serial, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    /* Start a timer to wake us up in a little while. */
    dev->pos = -1;
    serial_clear_fifo((SERIAL *) serial);
    dev->delay = 5000LL * (1LL << TIMER_SHIFT);
}


/* Callback timer expired, now send our "mouse ID" to the serial port. */
static void
sermouse_timer(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    dev->delay = 0LL;

    if (dev->pos != -1) return;

    dev->pos = 0;
    switch(dev->type) {
	case MOUSE_TYPE_MSYSTEMS:
		/* Identifies Mouse Systems serial mouse. */
		serial_write_fifo(dev->serial, 'H');
		break;

	case MOUSE_TYPE_MICROSOFT:
		/* Identifies a two-button Microsoft Serial mouse. */
		serial_write_fifo(dev->serial, 'M');
		break;

	case MOUSE_TYPE_LOGITECH:
		/* Identifies a two-button Logitech Serial mouse. */
		serial_write_fifo(dev->serial, 'M');
		serial_write_fifo(dev->serial, '3');
		break;

	case MOUSE_TYPE_MSWHEEL:
		/* Identifies multi-button Microsoft Wheel Mouse. */
		serial_write_fifo(dev->serial, 'M');
		serial_write_fifo(dev->serial, 'Z');
		break;

	default:
		mouse_serial_log("%s: unsupported mouse type %d?\n", dev->type);
    }
}


static int
sermouse_poll(int x, int y, int z, int b, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;
    uint8_t buff[16];
    int len;

    if (!x && !y && b == dev->oldb) return(1);

#if 0
    mouse_serial_log("%s: poll(%d,%d,%d,%02x)\n", dev->name, x, y, z, b);
#endif

    dev->oldb = b;

    if (x > 127) x = 127;
    if (y > 127) y = 127;
    if (x <- 128) x = -128;
    if (y <- 128) y = -128;

    len = 0;
    switch(dev->type) {
	case MOUSE_TYPE_MSYSTEMS:
		buff[0] = 0x80;
		buff[0] |= (b & 0x01) ? 0x00 : 0x04;	/* left button */
		buff[0] |= (b & 0x02) ? 0x00 : 0x01;	/* middle button */
		buff[0] |= (b & 0x04) ? 0x00 : 0x02;	/* right button */
		buff[1] = x;
		buff[2] = -y;
		buff[3] = x;				/* same as byte 1 */
		buff[4] = -y;				/* same as byte 2 */
		len = 5;
		break;

	case MOUSE_TYPE_MICROSOFT:
	case MOUSE_TYPE_LOGITECH:
	case MOUSE_TYPE_MSWHEEL:
		buff[0] = 0x40;
		buff[0] |= (((y >> 6) & 0x03) << 2);
		buff[0] |= ((x >> 6) & 0x03);
		if (b & 0x01) buff[0] |= 0x20;
		if (b & 0x02) buff[0] |= 0x10;
		buff[1] = x & 0x3F;
		buff[2] = y & 0x3F;
		if (dev->type == MOUSE_TYPE_LOGITECH) {
			len = 3;
			if (b & 0x04) {
				buff[3] = 0x20;
				len++;
			}
		} else if (dev->type == MOUSE_TYPE_MSWHEEL) {
			len = 4;
			buff[3] = z & 0x0F;
			if (b & 0x04)
				buff[3] |= 0x10;
		} else
			len = 3;
		break;
    }

#if 0
    mouse_serial_log("%s: [", dev->name);
    for (b=0; b<len; b++) mouse_serial_log(" %02X", buff[b]);
    mouse_serial_log(" ] (%d)\n", len);
#endif

    /* Send the packet to the bottom-half of the attached port. */
    if (dev->serial != NULL) {
	for (b=0; b<len; b++)
		serial_write_fifo(dev->serial, buff[b]);
    }

    return(0);
}


static void
sermouse_close(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    /* Detach serial port from the mouse. */
    if ((dev != NULL) && (dev->serial != NULL)) {
	dev->serial->rcr_callback = NULL;
	dev->serial->rcr_callback_p = NULL;
    }

    free(dev);
}


/* Initialize the device for use by the user. */
static void *
sermouse_init(const device_t *info)
{
    mouse_t *dev;
    int i;

    dev = (mouse_t *)malloc(sizeof(mouse_t));
    memset(dev, 0x00, sizeof(mouse_t));
    dev->name = info->name;
    i = device_get_config_int("buttons");
    if (i > 2)
	dev->flags |= FLAG_3BTN;

    if (info->local == MOUSE_TYPE_MSYSTEMS)
	dev->type = info->local;
    else {
	switch(i) {
		case 2:
		default:
			dev->type = MOUSE_TYPE_MICROSOFT;
			break;
		case 3:
			dev->type = MOUSE_TYPE_LOGITECH;
			break;
		case 4:
			dev->type = MOUSE_TYPE_MSWHEEL;
			break;
	}
    }

    dev->port = device_get_config_int("port");

    /* Attach a serial port to the mouse. */
    if (dev->port == 0)
	dev->serial = &serial1;
      else
	dev->serial = &serial2;
    dev->serial->rcr_callback = sermouse_callback;
    dev->serial->rcr_callback_p = dev;

    mouse_serial_log("%s: port=COM%d\n", dev->name, dev->port+1);

    timer_add(sermouse_timer, &dev->delay, &dev->delay, dev);

    /* Tell them how many buttons we have. */
    mouse_set_buttons((dev->flags & FLAG_3BTN) ? 3 : 2);

    /* Return our private data to the I/O layer. */
    return(dev);
}


static const device_config_t sermouse_config[] = {
    {
	"port", "Serial Port", CONFIG_SELECTION, "", 0, {
		{
			"COM1", 0
		},
		{
			"COM2", 1
		},
		{
			""
		}
	}
    },
    {
	"buttons", "Buttons", CONFIG_SELECTION, "", 2, {
		{
			"Two", 2
		},
		{
			"Three", 3
		},
		{
			"Wheel", 4
		},
		{
			""
		}
	}
    },
    {
	"", "", -1
    }
};


const device_t mouse_mssystems_device = {
    "Mouse Systems Serial Mouse",
    0,
    MOUSE_TYPE_MSYSTEMS,
    sermouse_init, sermouse_close, NULL,
    sermouse_poll, NULL, NULL,
    sermouse_config
};

const device_t mouse_msserial_device = {
    "Microsoft/Logitech Serial Mouse",
    0,
    0,
    sermouse_init, sermouse_close, NULL,
    sermouse_poll, NULL, NULL,
    sermouse_config
};
