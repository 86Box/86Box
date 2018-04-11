/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of PS/2 series Mouse devices.
 *
 * Version:	@(#)mouse_ps2.c	1.0.6	2018/03/18
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "86box.h"
#include "config.h"
#include "device.h"
#include "keyboard.h"
#include "mouse.h"


enum {
    MODE_STREAM,
    MODE_REMOTE,
    MODE_ECHO
};


typedef struct {
    const char	*name;				/* name of this device */
    int8_t	type;				/* type of this device */

    int		mode;

    uint8_t	flags;
    uint8_t	resolution;
    uint8_t	sample_rate;

    uint8_t	command;

    int		x, y, z, b;

    uint8_t	last_data[6];
} mouse_t;
#define FLAG_INTELLI	0x80			/* device is IntelliMouse */
#define FLAG_INTMODE	0x40			/* using Intellimouse mode */
#define FLAG_SCALED	0x20			/* enable delta scaling */
#define FLAG_ENABLED	0x10			/* dev is enabled for use */
#define FLAG_CTRLDAT	0x08			/* ctrl or data mode */


int mouse_scan = 0;


static void
ps2_write(uint8_t val, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;
    uint8_t temp;

    if (dev->flags & FLAG_CTRLDAT) {
	dev->flags &= ~FLAG_CTRLDAT;

	switch (dev->command) {
		case 0xe8:	/* set mouse resolution */
			dev->resolution = val;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xf3:	/* set sample rate */
			dev->sample_rate = val;
			keyboard_at_adddata_mouse(0xfa);
			break;

		default:
			keyboard_at_adddata_mouse(0xfc);
	}
    } else {
	dev->command = val;

	switch (dev->command) {
		case 0xe6:	/* set scaling to 1:1 */
			dev->flags &= ~FLAG_SCALED;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xe7:	/* set scaling to 2:1 */
			dev->flags |= FLAG_SCALED;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xe8:	/* set mouse resolution */
			dev->flags |= FLAG_CTRLDAT;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xe9:	/* status request */
			keyboard_at_adddata_mouse(0xfa);
			temp = (dev->flags & 0x3f);
			if (mouse_buttons & 0x01)
				temp |= 0x01;
			if (mouse_buttons & 0x02)
				temp |= 0x02;
			if (mouse_buttons & 0x04)
				temp |= 0x03;
			keyboard_at_adddata_mouse(temp);
			keyboard_at_adddata_mouse(dev->resolution);
			keyboard_at_adddata_mouse(dev->sample_rate);
			break;

		case 0xf2:	/* read ID */
			keyboard_at_adddata_mouse(0xfa);
			if (dev->flags & FLAG_INTMODE)
				keyboard_at_adddata_mouse(0x03);
			  else
				keyboard_at_adddata_mouse(0x00);
			break;

		case 0xf3:	/* set command mode */
			dev->flags |= FLAG_CTRLDAT;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xf4:	/* enable */
			dev->flags |= FLAG_ENABLED;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xf5:	/* disable */
			dev->flags &= ~FLAG_ENABLED;
			keyboard_at_adddata_mouse(0xfa);
			break;

		case 0xff:	/* reset */
			dev->mode  = MODE_STREAM;
			dev->flags &= 0x80;
			keyboard_at_adddata_mouse(0xfa);
			keyboard_at_adddata_mouse(0xaa);
			keyboard_at_adddata_mouse(0x00);
			break;

		default:
			keyboard_at_adddata_mouse(0xfe);
	}
    }

    if (dev->flags & FLAG_INTELLI) {
	for (temp=0; temp<5; temp++)	
		dev->last_data[temp] = dev->last_data[temp+1];
	dev->last_data[5] = val;

	if (dev->last_data[0] == 0xf3 && dev->last_data[1] == 0xc8 &&
	    dev->last_data[2] == 0xf3 && dev->last_data[3] == 0x64 &&
	    dev->last_data[4] == 0xf3 && dev->last_data[5] == 0x50)
		dev->flags |= FLAG_INTMODE;
    }
}


static int
ps2_poll(int x, int y, int z, int b, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;
    uint8_t buff[3];

    if (!x && !y && !z && b == dev->b) return(0xff);

    if (! (dev->flags & FLAG_ENABLED)) return(0xff);

    if (! mouse_scan) return(0xff);

    dev->x += x;
    dev->y -= y;
    dev->z -= z;
    if ((dev->mode == MODE_STREAM) &&
	((mouse_queue_end-mouse_queue_start) & 0x0f) < 13) {
	dev->b = b;

	if (dev->x > 255) dev->x = 255;
	if (dev->x < -256) dev->x = -256;
	if (dev->y > 255) dev->y = 255;
	if (dev->y < -256) dev->y = -256;
	if (dev->z < -8) dev->z = -8;
	if (dev->z > 7) dev->z = 7;

	memset(buff, 0x00, sizeof(buff));
	buff[0] = 0x08;
	if (dev->x < 0)
		buff[0] |= 0x10;
	if (dev->y < 0)
		buff[0] |= 0x20;
	if (mouse_buttons & 0x01)
		buff[0] |= 0x01;
	if (mouse_buttons & 0x02)
		buff[0] |= 0x02;
	if (dev->flags & FLAG_INTELLI) {
		if (mouse_buttons & 0x04)
			buff[0] |= 0x04;
	}
	buff[1] = (dev->x & 0xff);
	buff[2] = (dev->y & 0xff);

	keyboard_at_adddata_mouse(buff[0]);
	keyboard_at_adddata_mouse(buff[1]);
	keyboard_at_adddata_mouse(buff[2]);
	if (dev->flags & FLAG_INTELLI)
		keyboard_at_adddata_mouse(dev->z);

	dev->x = dev->y = dev->z = 0;
    }

    return(0);
}


/*
 * Initialize the device for use by the user.
 *
 * We also get called from the various machines.
 */
void *
mouse_ps2_init(const device_t *info)
{
    mouse_t *dev;
    int i;

    dev = (mouse_t *)malloc(sizeof(mouse_t));
    memset(dev, 0x00, sizeof(mouse_t));
    dev->name = info->name;
    dev->type = info->local;

    dev->mode = MODE_STREAM;
    i = device_get_config_int("buttons");
    if (i > 2)
        dev->flags |= FLAG_INTELLI;

    /* Hook into the general AT Keyboard driver. */
    keyboard_at_set_mouse(ps2_write, dev);

    pclog("%s: buttons=%d\n", dev->name, (dev->flags & FLAG_INTELLI)? 3 : 2);

    /* Tell them how many buttons we have. */
    mouse_set_buttons((dev->flags & FLAG_INTELLI) ? 3 : 2);

    /* Return our private data to the I/O layer. */
    return(dev);
}


static void
ps2_close(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    /* Unhook from the general AT Keyboard driver. */
    keyboard_at_set_mouse(NULL, NULL);

    free(dev);
}


static const device_config_t ps2_config[] = {
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


const device_t mouse_ps2_device = {
    "Standard PS/2 Mouse",
    0,
    MOUSE_TYPE_PS2,
    mouse_ps2_init, ps2_close, NULL,
    ps2_poll, NULL, NULL, NULL,
    ps2_config
};
