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
 * Version:	@(#)mouse_serial.c	1.0.26	2018/11/05
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


#define SERMOUSE_PORT			0			/* attach to Serial0 */

#define PHASE_IDLE			0
#define PHASE_ID			1
#define PHASE_DATA			2
#define PHASE_STATUS			3
#define PHASE_DIAGNOSTIC		4
#define PHASE_FORMAT_AND_REVISION	5


typedef struct {
    const char	*name;				/* name of this device */
    int8_t	type,				/* type of this device */
		port;
    uint8_t	flags, but,			/* device flags */
		want_data,
		status, format,
		prompt, continuous,
		id_len, id[255],
		data_len, data[5];
    int		abs_x, abs_y;

    int		pos;
    int64_t	delay;
    int64_t	period;
    int		oldb;
    int		phase;

    serial_t	*serial;
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
sermouse_callback(struct serial_s *serial, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    /* Start a timer to wake us up in a little while. */
    dev->pos = 0;
    dev->phase = PHASE_ID;
    dev->delay = dev->period;
}


static uint8_t
sermouse_data_msystems(mouse_t *dev, int x, int y, int b)
{
    dev->data[0] = 0x80;
    dev->data[0] |= (b & 0x01) ? 0x00 : 0x04;	/* left button */
    dev->data[0] |= (b & 0x02) ? 0x00 : 0x01;	/* middle button */
    dev->data[0] |= (b & 0x04) ? 0x00 : 0x02;	/* right button */
    dev->data[1] = x;
    dev->data[2] = -y;
    dev->data[3] = x;				/* same as byte 1 */
    dev->data[4] = -y;				/* same as byte 2 */

    return 5;
}


static uint8_t
sermouse_data_3bp(mouse_t *dev, int x, int y, int b)
{
    dev->data[0] |= (b & 0x01) ? 0x00 : 0x04;	/* left button */
    dev->data[0] |= (b & 0x02) ? 0x00 : 0x02;	/* middle button */
    dev->data[0] |= (b & 0x04) ? 0x00 : 0x01;	/* right button */
    dev->data[1] = x;
    dev->data[2] = -y;

    return 3;
}


static uint8_t
sermouse_data_mmseries(mouse_t *dev, int x, int y, int b)
{
    if (x < -127)
	x = -127;
    if (y < -127)
	y = -127;

    dev->data[0] = 0x80;
    if (x >= 0)
	dev->data[0] |= 0x10;
    if (y < 0)
	dev->data[0] |= 0x08;
    dev->data[0] |= (b & 0x01) ? 0x04 : 0x00;	/* left button */
    dev->data[0] |= (b & 0x02) ? 0x02 : 0x00;	/* middle button */
    dev->data[0] |= (b & 0x04) ? 0x01 : 0x00;	/* right button */
    dev->data[1] = abs(x);
    dev->data[2] = abs(y);

    return 3;
}


static uint8_t
sermouse_data_bp1(mouse_t *dev, int x, int y, int b)
{
    dev->data[0] = 0x80;
    dev->data[0] |= (b & 0x01) ? 0x10 : 0x00;	/* left button */
    dev->data[0] |= (b & 0x02) ? 0x08 : 0x00;	/* middle button */
    dev->data[0] |= (b & 0x04) ? 0x04 : 0x00;	/* right button */
    dev->data[1] = (x & 0x3f);
    dev->data[2] = (x >> 6);
    dev->data[3] = (y & 0x3f);
    dev->data[4] = (y >> 6);

    return 5;
}


static uint8_t
sermouse_data_ms(mouse_t *dev, int x, int y, int z, int b)
{
    uint8_t len;

    dev->data[0] = 0x40;
    dev->data[0] |= (((y >> 6) & 0x03) << 2);
    dev->data[0] |= ((x >> 6) & 0x03);
    if (b & 0x01)
	dev->data[0] |= 0x20;
    if (b & 0x02)
	dev->data[0] |= 0x10;
    dev->data[1] = x & 0x3F;
    dev->data[2] = y & 0x3F;
    if (dev->but == 3) {
	len = 3;
	if (dev->type == MOUSE_TYPE_LT3BUTTON) {
		if (b & 0x04) {
			dev->data[3] = 0x20;
			len++;
		}
	} else {
		if ((b ^ dev->oldb) & 0x04) {
			/* Microsoft 3-button mice send a fourth byte of 0x00 when the middle button
			   has changed. */
			dev->data[3] = 0x00;
			len++;
		}
	}
    } else if (dev->but == 4) {
	len = 4;
	dev->data[3] = z & 0x0F;
	if (b & 0x04)
		dev->data[3] |= 0x10;
    } else
	len = 3;

    return len;
}


static uint8_t
sermouse_data_hex(mouse_t *dev, int x, int y, int b)
{
    char ret[6] = { 0, 0, 0, 0, 0, 0 };
    uint8_t i, but = 0x00;

    but |= (b & 0x01) ? 0x04 : 0x00;	/* left button */
    but |= (b & 0x02) ? 0x02 : 0x00;	/* middle button */
    but |= (b & 0x04) ? 0x01 : 0x00;	/* right button */

    sprintf(ret, "%02X%02X%01X", (int8_t) y, (int8_t) x, but & 0x0f);

    for (i = 0; i < 5; i++)
	dev->data[i] = ret[4 - i];

    return 5;
}


static void
sermouse_report(int x, int y, int z, int b, mouse_t *dev)
{
    int len = 0;

    memset(dev->data, 0, 5);

    switch(dev->type) {
	case MOUSE_TYPE_MSYSTEMS:
		len = sermouse_data_msystems(dev, x, y, b);
		break;

	case MOUSE_TYPE_MICROSOFT:
	case MOUSE_TYPE_MS3BUTTON:
	case MOUSE_TYPE_MSWHEEL:
		len = sermouse_data_ms(dev, x, y, z, b);
		break;

	case MOUSE_TYPE_LOGITECH:
	case MOUSE_TYPE_LT3BUTTON:
		switch (dev->format) {
			case 0:
				len = sermouse_data_msystems(dev, x, y, b);
				break;
			case 1:
				len = sermouse_data_3bp(dev, x, y, b);
				break;
			case 2:
				len = sermouse_data_hex(dev, x, y, b);
				break;
			case 3:	/* Relative */
				len = sermouse_data_bp1(dev, x, y, b);
				break;
			case 5:
				len = sermouse_data_mmseries(dev, x, y, b);
				break;
			case 6:	/* Absolute */
				len = sermouse_data_bp1(dev, dev->abs_x, dev->abs_y, b);
				break;
			case 7:
				len = sermouse_data_ms(dev, x, y, z, b);
				break;
		}
		break;
    }

    dev->oldb = b;
    dev->data_len = len;

    dev->pos = 0;

    if (dev->phase != PHASE_DATA)
	dev->phase = PHASE_DATA;

    if (!dev->delay)
	dev->delay = dev->period;
}


/* Callback timer expired, now send our "mouse ID" to the serial port. */
static void
sermouse_timer(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    switch (dev->phase) {
	case PHASE_ID:
		serial_write_fifo(dev->serial, dev->id[dev->pos]);
		dev->pos++;
		if (dev->pos == dev->id_len) {
			dev->delay = 0LL;
			dev->pos = 0;
			dev->phase = PHASE_IDLE;
		} else
			dev->delay += dev->period;
		break;
	case PHASE_DATA:
		serial_write_fifo(dev->serial, dev->data[dev->pos]);
		dev->pos++;
		if (dev->pos == dev->data_len) {
			dev->delay = 0LL;
			dev->pos = 0;
			dev->phase = PHASE_IDLE;
		} else
			dev->delay += dev->period;
		break;
	case PHASE_STATUS:
		serial_write_fifo(dev->serial, dev->status);
		dev->delay = 0LL;
		dev->pos = 0;
		dev->phase = PHASE_IDLE;
		break;
	case PHASE_DIAGNOSTIC:
		if (dev->pos)
			serial_write_fifo(dev->serial, 0x00);
		else /* This should return the last button status, bits 2,1,0 = L,M,R. */
			serial_write_fifo(dev->serial, 0x00);
		dev->pos++;
		if (dev->pos == 3) {
			dev->delay = 0LL;
			dev->pos = 0;
			dev->phase = PHASE_IDLE;
		} else
			dev->delay += dev->period;
		break;
	case PHASE_FORMAT_AND_REVISION:
		serial_write_fifo(dev->serial, 0x10 | (dev->format << 1));
		dev->delay = 0LL;
		dev->pos = 0;
		dev->phase = PHASE_IDLE;
		break;
	default:
		dev->delay = 0LL;
		dev->pos = 0;
		dev->phase = PHASE_IDLE;
		break;
    }
}


static int
sermouse_poll(int x, int y, int z, int b, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    if (!x && !y && (b == dev->oldb) && dev->continuous)
	return(1);

    dev->oldb = b;
    dev->abs_x += x;
    dev->abs_y += y;
    if (dev->abs_x < 0)
	dev->abs_x = 0;
    if (dev->abs_x > 4095)
	dev->abs_x = 4095;
    if (dev->abs_y < 0)
	dev->abs_y = 0;
    if (dev->abs_y > 4095)
	dev->abs_y = 4095;

    if (dev->format == 3) {
	if (x > 2047) x = 2047;
	if (y > 2047) y = 2047;
	if (x <- 2048) x = -2048;
	if (y <- 2048) y = -2048;
    } else {
	if (x > 127) x = 127;
	if (y > 127) y = 127;
	if (x <- 128) x = -128;
	if (y <- 128) y = -128;
    }

    /* No report if we're either in prompt mode,
       or the mouse wants data. */
    if (!dev->prompt && !dev->want_data)
	sermouse_report(x, y, z, b, dev);

    return(0);
}


static void
ltsermouse_write(struct serial_s *serial, void *priv, uint8_t data)
{
    mouse_t *dev = (mouse_t *)priv;

#if 0
    /* Make sure to stop any transmission when we receive a byte. */
    if (dev->phase != PHASE_IDLE) {
	dev->delay = 0LL;
	dev->phase = PHASE_IDLE;
    }
#endif

    if (dev->want_data)  switch (dev->want_data) {
	case 0x2A:
		dev->data_len--;
		dev->want_data = 0;
		dev->delay = 0LL;
		dev->phase = PHASE_IDLE;
		switch (data) {
			default:
				mouse_serial_log("Serial mouse: Invalid period %02X, using 1200 bps\n", data);
			case 0x6E:
				dev->period = 7500LL;	/* 1200 bps */
				break;
			case 0x6F:
				dev->period = 3750LL;	/* 2400 bps */
				break;
			case 0x70:
				dev->period = 1875LL;	/* 4800 bps */
				break;
			case 0x71:
				dev->period = 938LL;	/* 9600 bps */
				break;
		}
		dev->period *= TIMER_USEC;
		break;
    } else  switch (data) {
	case 0x2A:
		dev->want_data = data;
		dev->data_len = 1;
		break;
	case 0x44:	/* Set prompt mode */
		dev->prompt = 1;
		dev->status |= 0x40;
		break;
	case 0x50:
		if (!dev->prompt) {
			dev->prompt = 1;
			dev->status |= 0x40;
		}
		/* TODO: Here we should send the current position. */
		break;
	case 0x73:	/* Status */
		dev->pos = 0;
		dev->phase = PHASE_STATUS;
		if (!dev->delay)
			dev->delay = dev->period;
		break;
	case 0x4A:	/* Report Rate Selection commands */
	case 0x4B:
	case 0x4C:
	case 0x52:
	case 0x4D:
	case 0x51:
	case 0x4E:
	case 0x4F:
		dev->prompt = 0;
		dev->status &= 0xBF;
		// dev->continuous = (data == 0x4F);
		break;
	case 0x41:
		dev->format = 6;	/* Aboslute Bit Pad One Format */
		dev->abs_x = dev->abs_y = 0;
		break;
	case 0x42:
		dev->format = 3;	/* Relative Bit Pad One Format */
		break;
	case 0x53:
		dev->format = 5;	/* MM Series Format */
		break;
	case 0x54:
		dev->format = 1;	/* Three Byte Packed Binary Format */
		break;
	case 0x55:	/* This is the Mouse Systems-compatible format */
		dev->format = 0;	/* Five Byte Packed Binary Format */
		break;
	case 0x56:
		dev->format = 7;	/* Microsoft Compatible Format */
		break;
	case 0x57:
		dev->format = 2;	/* Hexadecimal Format */
		break;
	case 0x05:
		dev->pos = 0;
		dev->phase = PHASE_DIAGNOSTIC;
		if (!dev->delay)
			dev->delay = dev->period;
		break;
	case 0x66:
		dev->pos = 0;
		dev->phase = PHASE_FORMAT_AND_REVISION;
		if (!dev->delay)
			dev->delay = dev->period;
		break;
    }
}


static void
sermouse_close(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    /* Detach serial port from the mouse. */
    if (dev && dev->serial && dev->serial->sd)
	memset(dev->serial->sd, 0, sizeof(serial_device_t));

    free(dev);
}


/* Initialize the device for use by the user. */
static void *
sermouse_init(const device_t *info)
{
    mouse_t *dev;

    dev = (mouse_t *)malloc(sizeof(mouse_t));
    memset(dev, 0x00, sizeof(mouse_t));
    dev->name = info->name;
    dev->but = device_get_config_int("buttons");
    dev->continuous = 1;
    if (dev->but > 2)
	dev->flags |= FLAG_3BTN;

    if (info->local == MOUSE_TYPE_MSYSTEMS) {
	dev->period = 8333LL * TIMER_USEC;	/* 1200 bps, 8 data bits, 1 start bit, 1 stop bit, no parity bit */
	dev->type = info->local;
	dev->id_len = 1;
	dev->id[0] = 'H';
    } else {
	dev->format = 7;
	dev->status = 0x0f;
	dev->period = 7500LL * TIMER_USEC;	/* 1200 bps, 7 data bits, 1 start bit, 1 stop bit, no parity bit */
	dev->id_len = 1;
	dev->id[0] = 'M';
	switch(dev->but) {
		case 2:
		default:
			dev->type = info->local ? MOUSE_TYPE_LOGITECH : MOUSE_TYPE_MICROSOFT;
			break;
		case 3:
			dev->type = info->local ? MOUSE_TYPE_LT3BUTTON : MOUSE_TYPE_MS3BUTTON;
			dev->id_len = 2;
			dev->id[1] = '3';
			break;
		case 4:
			dev->type = MOUSE_TYPE_MSWHEEL;
			dev->id_len = 6;
			dev->id[1] = 'Z';
			dev->id[2] = '@';
			break;
	}
    }

    dev->port = device_get_config_int("port");

    /* Attach a serial port to the mouse. */
    if (info->local)
	dev->serial = serial_attach(dev->port, sermouse_callback, ltsermouse_write, dev);
    else
	dev->serial = serial_attach(dev->port, sermouse_callback, NULL, dev);

    mouse_serial_log("%s: port=COM%d\n", dev->name, dev->port + 1);

    timer_add(sermouse_timer, &dev->delay, &dev->delay, dev);

    /* Tell them how many buttons we have. */
    mouse_set_buttons((dev->flags & FLAG_3BTN) ? 3 : 2);

    /* Return our private data to the I/O layer. */
    return(dev);
}


static const device_config_t mssermouse_config[] = {
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


static const device_config_t ltsermouse_config[] = {
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
    mssermouse_config
};

const device_t mouse_msserial_device = {
    "Microsoft Serial Mouse",
    0,
    0,
    sermouse_init, sermouse_close, NULL,
    sermouse_poll, NULL, NULL,
    mssermouse_config
};

const device_t mouse_ltserial_device = {
    "Logitech Serial Mouse",
    0,
    1,
    sermouse_init, sermouse_close, NULL,
    sermouse_poll, NULL, NULL,
    ltsermouse_config
};
