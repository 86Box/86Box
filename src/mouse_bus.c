/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Bus Mouse devices.
 *
 *		These devices were made by both Microsoft and Logitech. At
 *		first, Microsoft used the same protocol as Logitech, but did
 *		switch to their new protocol for their InPort interface. So,
 *		although alike enough to be handled in the same driver, they
 *		are not the same.
 *
 *		This code is based on my Minix driver for the Logitech(-mode)
 *		interface. Although that driver blindly took IRQ5, the board
 *		seems to be able to tell the driver what IRQ it is set for.
 *		When testing on MS-DOS (6.22), the 'mouse.exe' driver did not
 *		want to start, and only after disassembling it and inspecting
 *		the code it was discovered that driver actually does use the
 *		IRQ reporting feature. In a really, really weird way, too: it
 *		sets up the board, and then reads the CTRL register which is
 *		supposed to return that IRQ value. Depending on whether or 
 *		not the FREEZE bit is set, it has to return either the two's
 *		complemented (negated) value, or (if clear) just the value.
 *		The mouse.com driver reads both values 10,000 times, and
 *		then makes up its mind.  Maybe an effort to 'debounce' the
 *		reading of the DIP switches?  Oh-well.
 *
 * NOTES:	Verified with:
 *		  AMI WinBIOS 486 (5A, no IRQ detect, OK, IRQ5 only)
 *		  Microsoft Mouse.com V2.00 (DOS V6.22, 5A, OK)
 *		  Microsoft Mouse.exe V9.1 (DOS V6.22, A5, OK)
 *		  Logitech LMouse.com V6.02 (DOS V6.22)
 *		  Logitech LMouse.com V6.43 (DOS V6.22)
 *		  Microsoft WfW V3.11 on DOS V6.22
 *		  GEOS V1.0 (OK, IRQ5 only)
 *		  GEOS V2.0 (OK, IRQ5 only)
 *		  Microsoft Windows 95 OSR2
 *		  Microsoft Windows 98 SE
 *		  Microsoft Windows NT 3.1
 *		  Microsoft Windows NT 3.51
 *
 *		Based on an early driver for MINIX 1.5.
 *
 * TODO:	Re-integrate the InPort part. Currently,
 *		only the Logitech part is considered to
 *		be OK.
 *
 * Version:	@(#)mouse_bus.c	1.0.25	2017/12/04
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 1989-2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "86box.h"
#include "config.h"
#include "io.h"
#include "pic.h"
#include "timer.h"
#include "device.h"
#include "mouse.h"


#define MOUSE_PORT		0x023c		/* default */
#define MOUSE_IRQ		5		/* default (DOS,NT31) */
#define MOUSE_DEBUG		2


/* Our mouse device. */
typedef struct mouse {
    const char	*name;				/* name of this device */
    int8_t	type;				/* type of this device */
    int8_t	irq;				/* IRQ channel to use */
    uint8_t	flags;				/* device flags */

    uint8_t	r_magic,			/* MAGIC register */
		r_ctrl,				/* CONTROL register (WR) */
		r_conf,				/* CONFIG register */
		r_cmd;				/* (MS) current command */

    uint8_t	seq;				/* general counter */

    uint8_t	but,				/* current mouse status */
		but_last;
    int8_t	x, y;
    int8_t	x_delay,
		y_delay;

    int64_t	timer;				/* mouse event timer */

    uint8_t	(*read)(struct mouse *, uint16_t);
    void	(*write)(struct mouse *, uint16_t, uint8_t);
} mouse_t;
#define FLAG_INPORT	0x80			/* device is MS InPort */
#define FLAG_3BTN	0x20			/* enable 3-button mode */
#define FLAG_SCALED	0x10			/* enable delta scaling */
#define FLAG_INTR	0x04			/* dev can send interrupts */
#define FLAG_FROZEN	0x02			/* do not update counters */
#define FLAG_ENABLED	0x01			/* dev is enabled for use */


/* Definitions for Logitech. */
#define	LTMOUSE_DATA	0			/* DATA register */
#define	LTMOUSE_MAGIC	1			/* signature magic register */
# define LTMAGIC_BYTE1	0xa5			/* most drivers use this */
# define LTMAGIC_BYTE2	0x5a			/* some drivers use this */
#define	LTMOUSE_CTRL	2			/* CTRL register */
# define LTCTRL_FREEZE	0x80			/* do not sample when set */
# define LTCTRL_RD_Y_HI	0x60
# define LTCTRL_RD_Y_LO	0x40
# define LTCTRL_RD_X_HI	0x20
# define LTCTRL_RD_X_LO	0x00
# define LTCTRL_RD_MASK	0x60
# define LTCTRL_IDIS	0x10
# define LTCTRL_IENB	0x00
#define	LTMOUSE_CONFIG	3			/* CONFIG register */

/* Definitions for Microsoft. */
#define	MSMOUSE_CTRL	0			/* CTRL register */
# define MSCTRL_RESET	0x80			/* reset controller */
# define MSCTRL_FREEZE	0x20			/* HOLD- freeze data */
# define MSCTRL_IENB	0x01
# define MSCTRL_COMMAND	0x07
# define MSCTRL_RD_Y	0x02
# define MSCTRL_RD_X	0x01
# define MSCTRL_RD_BUT	0x00
#define	MSMOUSE_DATA	1			/* DATA register */
# define MSDATA_BASE	0x10
# define MSDATA_IRQ	0x01
#define	MSMOUSE_MAGIC	2			/* MAGIC register */
# define MAGIC_MSBYTE1	0xde			/* indicates MS InPort */
# define MAGIC_MSBYTE2	0x12
#define	MSMOUSE_CONFIG	3			/* CONFIG register */


/* Reset the controller state. */
static void
ms_reset(mouse_t *dev)
{
    dev->flags &= 0xf0;

    dev->seq = 0;

    dev->x = dev->y = 0;
    dev->but = 0x00;

    dev->flags |= FLAG_INTR;
}


/* Handle a WRITE to an InPort register. */
static void
ms_write(mouse_t *dev, uint16_t port, uint8_t val)
{
    switch (port) {
	case MSMOUSE_CTRL:
		switch (val) {
			case MSCTRL_RESET:
				dev->r_ctrl = 0x00;
				dev->r_cmd = 0x00;
				break;

			case MSCTRL_COMMAND:
			case MSCTRL_RD_BUT:
			case MSCTRL_RD_X:
			case MSCTRL_RD_Y:
				dev->r_cmd = val;
				break;

			case 0x87:
				dev->r_ctrl = 0x00;
				dev->r_cmd = MSCTRL_COMMAND;
				break;
		}
		break;

	case MSMOUSE_DATA:
		picintc(1 << dev->irq);
		if (val == MSDATA_IRQ) {
			picint(1<<dev->irq);
		} else switch (dev->r_cmd) {
			case MSCTRL_COMMAND:
				dev->r_ctrl = val;
				if (val & MSCTRL_IENB)
					dev->flags |= FLAG_INTR;
				  else
					dev->flags &= ~FLAG_INTR;
				break;

			default:
				break;
		}
		break;

	case MSMOUSE_MAGIC:
		break;

	case MSMOUSE_CONFIG:
		break;
    }
}


/* Handle a READ from an InPort register. */
static uint8_t
ms_read(mouse_t *dev, uint16_t port)
{
    uint8_t ret = 0x00;

    switch (port) {
	case MSMOUSE_CTRL:
		ret = dev->r_ctrl;
		break;

	case MSMOUSE_DATA:
		switch (dev->r_cmd) {
			case MSCTRL_RD_BUT:
				ret = dev->but;
				ret |= 0x40;
				break;

			case MSCTRL_RD_X:
				ret = dev->x;
				break;

			case MSCTRL_RD_Y:
				ret = dev->y;
				break;

			case MSCTRL_COMMAND:
				ret = dev->r_ctrl;
				break;
		}
		break;

	case MSMOUSE_MAGIC:
		if (dev->seq & 0x01)
			ret = MAGIC_MSBYTE2;
		  else
			ret = MAGIC_MSBYTE1;
		dev->seq++;
		break;

	case MSMOUSE_CONFIG:
		/* Not really present in real hardware. */
		break;
    }

    return(ret);
}


/* Called at 30hz */
static void
bm_timer(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    dev->timer += ((1000000.0 / 30.0) * TIMER_USEC);

#if 0
    /* The controller updates the data on every interrupt
	We just don't copy it to the current_X if the 'hold' bit is set */
    if (dev->is_inport) {
	if ((dev->but & (1<<2)) ||
		((dev->but_last & (1<<2)) && !(dev->but & (1<<2))))
		dev->but |= (1<<5);
	if ((dev->but & (1<<1)) ||
		((dev->but_last & (1<<1)) && !(dev->but & (1<<1))))
		dev->but |= (1<<4);
	if ((dev->but & (1<<0)) ||
	    ((dev->but_last & (1<<0)) && !(dev->buttons & (1<<0))))
		dev->but |= (1<<3);
	dev->but_last = dev>but;
    }
#endif

    if (dev->flags & FLAG_INTR)
	picint(1 << dev->irq);
}


/* Initialize the Microsoft Bus Mouse interface. */
static void
ms_init(mouse_t *dev)
{
    /* Initialize registers. */
    dev->r_ctrl = 0x00;
    dev->r_conf = 0x00;

    dev->flags |= FLAG_INPORT;

    /* Initialize I/O handlers. */
    dev->read = ms_read;
    dev->write = ms_write;

    timer_add(bm_timer, &dev->timer, TIMER_ALWAYS_ENABLED, dev);
}


/* Reset the controller state. */
static void
lt_reset(mouse_t *dev)
{
    dev->flags &= 0xf0;

    dev->r_magic = 0x00;
    dev->r_ctrl = (LTCTRL_IENB);
    dev->r_conf = 0x00;

    dev->seq = 0;

    dev->x = dev->y = 0;
    dev->but = 0x00;

    dev->flags |= FLAG_INTR;
}


/* Handle a WRITE to a Logitech register. */
static void
lt_write(mouse_t *dev, uint16_t port, uint8_t val)
{
    uint8_t b;

    switch (port) {
	case LTMOUSE_DATA:	/* [00] data register */
		break;

	case LTMOUSE_MAGIC:	/* [01] magic data register */
		switch(val) {
			case LTMAGIC_BYTE1:
			case LTMAGIC_BYTE2:
				lt_reset(dev);
				dev->r_magic = val;
				dev->flags |= FLAG_ENABLED;
				break;
		}
		break;

	case LTMOUSE_CTRL:	/* [02] control register */
		b = (dev->r_ctrl ^ val);
		if (b & LTCTRL_FREEZE) {
			if (val & LTCTRL_FREEZE) {
				/* Hold the sampling while we do something. */
				dev->flags |= FLAG_FROZEN;
			} else {
				/* Reset current state. */
				dev->flags &= ~FLAG_FROZEN;
				dev->x = dev->y = 0;
				if (dev->but)
					dev->but |= 0x80;
			}
		}

		if (b & LTCTRL_IDIS) {
			/* Disable or enable interrupts. */
			if (val & LTCTRL_IDIS)
				dev->flags &= ~FLAG_INTR;
			  else
				dev->flags |= FLAG_INTR;
		}

		/* Save new register value. */
		dev->r_ctrl = val;

		/* Clear any pending interrupts. */
		picintc(1 << dev->irq);
		break;

	case LTMOUSE_CONFIG:	/* [03] config register */
		dev->r_conf = val;
		break;

	default:
		break;
    }
}


/* Handle a READ from a Logitech register. */
static uint8_t
lt_read(mouse_t *dev, uint16_t port)
{
    uint8_t ret = 0xff;

    /* The GEOS drivers actually check this. */
    if (! (dev->flags & FLAG_ENABLED)) return(ret);

    switch (port) {
	case LTMOUSE_DATA:	/* [00] data register */
		ret = 0x07;
		if (dev->but & 0x01)		/*LEFT*/
			ret &= ~0x04;
		if (dev->but & 0x02)		/*RIGHT*/
			ret &= ~0x01;
		if (dev->flags & FLAG_3BTN)
			if (dev->but & 0x04)	/*MIDDLE*/
				ret &= ~0x02;
		ret <<= 5;

		switch(dev->r_ctrl & LTCTRL_RD_MASK) {
			case LTCTRL_RD_X_LO:	/* X, low bits */
				ret |= (dev->x & 0x0f);
				break;

			case LTCTRL_RD_X_HI:	/* X, high bits */
				ret |= (dev->x >> 4) & 0x0f;
				break;

			case LTCTRL_RD_Y_LO:	/* Y, low bits */
				ret |= (dev->y & 0x0f);
				break;

			case LTCTRL_RD_Y_HI:	/* Y, high bits */
				ret |= (dev->y >> 4) & 0x0f;
				break;
		}
		break;

	case LTMOUSE_MAGIC:	/* [01] magic data register */
		/*
		 * Drivers write a magic byte to this register, usually
		 * this is either 5A (AMI WinBIOS, MS Mouse 2.0) or
		 * A5 (MS Mouse 9.1, Windows drivers, UNIX/Linux/Minix.)
		 */
		ret = dev->r_magic;
		break;

	case LTMOUSE_CTRL:	/* [02] control register */
		if (dev->r_ctrl & LTCTRL_IDIS) {
			/* IDIS, no interrupts, return all-off. */
			ret = 0x0f;
		} else if (dev->seq++ == 0) {
			/* !IDIS, return DIP switch setting. */
			ret = 0x0f;
			switch(dev->irq) {
				case 2:
					ret &= ~0x08;
					break;
	
				case 3:
					ret &= ~0x04;
					break;
	
				case 4:
					ret &= ~0x02;
					break;
	
				case 5:
					ret &= ~0x01;
					break;
			}
		} else {
			/* Return all-off (invalid data.) */
			ret = 0x0f;
		}
		break;

	case LTMOUSE_CONFIG:	/* [03] config register */
		ret = dev->r_conf;
		break;

	default:
		break;
    }

    return(ret);
}


/* Handle a WRITE operation to one of our registers. */
static void
bm_write(uint16_t port, uint8_t val, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

#if MOUSE_DEBUG
    pclog("%s: write(%d,%02x)\n", dev->name, port-MOUSE_PORT, val);
#endif

    dev->write(dev, port-MOUSE_PORT, val);
}


/* Handle a READ operation from one of our registers. */
static uint8_t
bm_read(uint16_t port, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;
    uint8_t ret;

    ret = dev->read(dev, port-MOUSE_PORT);

#if MOUSE_DEBUG > 1
    pclog("%s: read(%d): %02x\n", dev->name, port-MOUSE_PORT, ret);
#endif

    return(ret);
}


/* The emulator calls us with an update on the host mouse device. */
static int
bm_poll(int x, int y, int z, int b, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    /* Return early if nothing to do. */
    if (!x && !y && !z && (dev->but == b)) return(1);

    /* If we are not enabled, return. */
    if (! (dev->flags & FLAG_ENABLED)) return(0);

#if 0
    pclog("%s: poll(%d,%d,%d,%02x) %d\n",
	dev->name, x, y, z, b, !!(dev->flags & FLAG_FROZEN));
#endif

    /* If we are frozen, do not update the state. */
    if (! (dev->flags & FLAG_FROZEN)) {
	if (dev->flags & FLAG_SCALED) {
		/* Scale down the motion. */
		if ((x < -1) || (x > 1)) x >>= 1;
		if ((y < -1) || (y > 1)) y >>= 1;
	}

	/* Add the delta to our state. */
	x += dev->x;
	if (x > 127)
		x = 127;
	if (x < -128)
		x = -128;
	dev->x = (int8_t)x;

	y += dev->y;
	if (y > 127)
		y = 127;
	if (y < -1287)
		y = -1287;
	dev->y = (int8_t)y;

	dev->x_delay += x;
	dev->y_delay += y;

	dev->but = b;
    }

    /* Either way, generate an interrupt. */
    if ((dev->flags & FLAG_INTR) && !(dev->flags & FLAG_INPORT))
	picint(1 << dev->irq);

    return(0);
}


/* Release all resources held by the device. */
static void
bm_close(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    /* Release our I/O range. */
    io_removehandler(MOUSE_PORT, 4,
		     bm_read, NULL, NULL, bm_write, NULL, NULL, dev);

    free(dev);
}


/* Initialize the device for use by the user. */
static void *
bm_init(device_t *info)
{
    mouse_t *dev;

    dev = (mouse_t *)malloc(sizeof(mouse_t));
    memset(dev, 0x00, sizeof(mouse_t));
    dev->name = info->name;
    dev->type = info->local;
#if NOTYET
    dev->irq = device_get_config_int("irq");
#else
    dev->irq = config_get_int((char *)info->name, "irq", 0);
#endif
    if (dev->irq == 0)
	dev->irq = MOUSE_IRQ;

    pclog("%s: I/O=%04x, IRQ=%d\n", dev->name, MOUSE_PORT, dev->irq);

    switch(dev->type) {
	case MOUSE_TYPE_LOGIBUS:
		/* Initialize registers. */
		lt_reset(dev);

		/* Initialize I/O handlers. */
		dev->read = lt_read;
		dev->write = lt_write;
		break;

	case MOUSE_TYPE_INPORT:
		ms_init(dev);
		break;
    }

    /* Request an I/O range. */
    io_sethandler(MOUSE_PORT, 4,
		  bm_read, NULL, NULL, bm_write, NULL, NULL, dev);

    /* Return our private data to the I/O layer. */
    return(dev);
}


static device_config_t bm_config[] = {
    {
	"irq", "IRQ", CONFIG_SELECTION, "", 2, {
		{
			"IRQ 2", 2
		},
		{
			"IRQ 3", 3
		},
		{
			"IRQ 4", 4
		},
		{
			"IRQ 5", 5
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


device_t mouse_logibus_device = {
    "Logitech Bus Mouse",
    DEVICE_ISA,
    MOUSE_TYPE_LOGIBUS,
    bm_init, bm_close, NULL,
    bm_poll, NULL, NULL, NULL,
    bm_config
};

device_t mouse_msinport_device = {
    "Microsoft Bus Mouse (InPort)",
    DEVICE_ISA,
    MOUSE_TYPE_INPORT,
    bm_init, bm_close, NULL,
    bm_poll, NULL, NULL, NULL,
    bm_config
};


#if 0
@@@@@@
void busmouse_update_mouse_data(void *priv)
{
	mouse_bus_t *busmouse = (mouse_bus_t *)priv;
	
	int delta_x, delta_y;
	int hold;

	if (busmouse->x_delay > 127) {
		delta_x = 127;
		busmouse->x_delay -= 127;
	} else if (busmouse->x_delay < -128) {
		delta_x = -128;
		busmouse->x_delay += 128;
	} else {
		delta_x = busmouse->x_delay;
		busmouse->x_delay = 0;
	}
	if (busmouse->y_delay > 127) {
		delta_y = 127;
		busmouse->y_delay -= 127;
	} else if (busmouse->y_delay < -128) {
		delta_y = -128;
		busmouse->y_delay += 128;
	} else {
		delta_y = busmouse->y_delay;
		busmouse->y_delay = 0;
	}

	if (busmouse->is_inport) {
		hold = (busmouse->control_val & INP_HOLD_COUNTER) > 0;
	} else {
		hold = (busmouse->control_val & HOLD_COUNTER) > 0;
	}
	if (!hold) {
		busmouse->x = (uint8_t) delta_x;
		busmouse->y = (uint8_t) delta_y;
		busmouse->but = busmouse->mouse_buttons;
	}
}
#endif
