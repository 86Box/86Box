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
 * NOTES:	Ported from Bochs with extensive modifications per testing
 *		of the real hardware, testing of drivers, and the old code.
 *
 *		Logitech Bus Mouse verified with:
 *		  Logitech LMouse.com 3.12
 *		  Logitech LMouse.com 3.30
 *		  Logitech LMouse.com 3.41
 *		  Logitech LMouse.com 3.42
 *		  Logitech LMouse.com 4.00
 *		  Logitech LMouse.com 5.00
 *		  Logitech LMouse.com 6.00
 *		  Logitech LMouse.com 6.02 Beta
 *		  Logitech LMouse.com 6.02
 *		  Logitech LMouse.com 6.12
 *		  Logitech LMouse.com 6.20
 *		  Logitech LMouse.com 6.23
 *		  Logitech LMouse.com 6.30
 *		  Logitech LMouse.com 6.31E
 *		  Logitech LMouse.com 6.34
 *		  Logitech Mouse.exe 6.40
 *		  Logitech Mouse.exe 6.41
 *		  Logitech Mouse.exe 6.44
 *		  Logitech Mouse.exe 6.46
 *		  Logitech Mouse.exe 6.50
 *		  Microsoft Mouse.com 2.00
 *		  Microsoft Mouse.sys 3.00
 *		  Microsoft Windows 1.00 DR5
 *		  Microsoft Windows 3.10.026
 *		  Microsoft Windows NT 3.1
 *		  Microsoft Windows 95
 *
 *		InPort verified with:
 *		  Logitech LMouse.com 6.12
 *		  Logitech LMouse.com 6.41
 *		  Microsoft Windows NT 3.1
 *		  Microsoft Windows 98 SE
 *
 * Version:	@(#)mouse_bus.c	1.0.0	2018/05/23
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 200?-2018 Bochs.
 *		Copyright 2018 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "config.h"
#include "io.h"
#include "pic.h"
#include "timer.h"
#include "device.h"
#include "mouse.h"

#define IRQ_MASK ((1<<5) >> dev->irq)

/* MS Inport Bus Mouse Adapter */
#define INP_PORT_CONTROL     0x0000
#define INP_PORT_DATA        0x0001
#define INP_PORT_SIGNATURE   0x0002
#define INP_PORT_CONFIG      0x0003

#define INP_CTRL_READ_BUTTONS 0x00
#define INP_CTRL_READ_X       0x01
#define INP_CTRL_READ_Y       0x02
#define INP_CTRL_COMMAND      0x07
#define INP_CTRL_RAISE_IRQ    0x16
#define INP_CTRL_RESET        0x80

#define INP_HOLD_COUNTER      (1 << 5)
#define INP_ENABLE_TIMER_IRQ  (1 << 4)
#define INP_ENABLE_DATA_IRQ   (1 << 3)
#define INP_PERIOD_MASK       0x07

#define INP_PERIOD	      (33334LL * TIMER_USEC)

/* MS/Logictech Standard Bus Mouse Adapter */
#define BUSM_PORT_DATA        0x0000
#define BUSM_PORT_SIGNATURE   0x0001
#define BUSM_PORT_CONTROL     0x0002
#define BUSM_PORT_CONFIG      0x0003

/* The MS/Logitech Standard Bus Mouse sends data about 45 times a second */
#define BUSM_PERIOD	      (22223LL * TIMER_USEC)

#define HOLD_COUNTER  (1 << 7)
#define READ_X        (0 << 6)
#define READ_Y        (1 << 6)
#define READ_LOW      (0 << 5)
#define READ_HIGH     (1 << 5)
#define DISABLE_IRQ   (1 << 4)

#define READ_X_LOW    (READ_X | READ_LOW)
#define READ_X_HIGH   (READ_X | READ_HIGH)
#define READ_Y_LOW    (READ_Y | READ_LOW)
#define READ_Y_HIGH   (READ_Y | READ_HIGH)


/* Our mouse device. */
typedef struct mouse {
    int		type, model, base, irq, bn,
		mouse_delayed_dx, mouse_delayed_dy,
		mouse_buttons,
		current_x, current_y,
		current_b,
		control_val, mouse_buttons_last,
		config_val, sig_val,
		command_val, toggle_counter,
		data_int, hold,
		interrupts;

    double	period;

    int64_t	timer_enabled, timer;			/* mouse event timer */
} mouse_t;


#ifdef ENABLE_MOUSE_BUS_LOG
int bm_do_log = ENABLE_MOUSE_BUS_LOG;
#endif


static void
bm_log(const char *format, ...)
{
#ifdef ENABLE_MOUSE_BUS_LOG
    va_list ap;

    if (bm_do_log) {
	va_start(ap, format);
	pclog_ex(format, ap);
	va_end(ap);
    }
#endif
}


/* Handle a READ operation from one of our registers. */
static uint8_t
bm_read(uint16_t port, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;
    uint8_t value;

    if (dev->type == MOUSE_TYPE_INPORT) {
	switch (port & 0x03) {
		case INP_PORT_CONTROL:
			value = dev->control_val;
			break;
		case INP_PORT_DATA:
			switch (dev->command_val) {
				case INP_CTRL_READ_BUTTONS:
					value = dev->current_b | 0x80;
					if (dev->model)
						value |= 0x40;  /* Newer Logitech mouse drivers look for bit 6 set */
					break;
				case INP_CTRL_READ_X:
					value = dev->current_x;
					break;
				case INP_CTRL_READ_Y:
					value = dev->current_y;
					break;
				case INP_CTRL_COMMAND:
					value = dev->control_val;
					break;
				default:
					bm_log("ERROR: Reading data port in unsupported mode 0x%02x\n", dev->control_val);
			}
			break;
		case INP_PORT_SIGNATURE:
			if (dev->toggle_counter)
				value = 0x12;
			else
				value = 0xDE;
			dev->toggle_counter ^= 1;
			break;
		case INP_PORT_CONFIG:
			bm_log("ERROR: Unsupported read from port 0x%04x\n", port);
			break;
	}
    } else {
	switch (port & 0x03) {
		case BUSM_PORT_CONTROL:
			value = dev->control_val;
			dev->control_val |= 0x0F;

			if ((dev->toggle_counter > 0x3FF) && (!dev->model || dev->interrupts))
				dev->control_val &= ~IRQ_MASK;
			dev->toggle_counter = (dev->toggle_counter + 1) & 0x7FF;
			break;
		case BUSM_PORT_DATA:
			/* Testing and another source confirm that the buttons are
			   *ALWAYS* present, so I'm going to change this a bit. */
			switch (dev->control_val & 0x60) {
				case READ_X_LOW:
					value = dev->current_x & 0x0F;
					break;
				case READ_X_HIGH:
					value = (dev->current_x >> 4) & 0x0F;
					break;
				case READ_Y_LOW:
					value = dev->current_y & 0x0F;
					break;
				case READ_Y_HIGH:
					value = (dev->current_y >> 4) & 0x0F;
					break;
				default:
					bm_log("ERROR: Reading data port in unsupported mode 0x%02x\n", dev->control_val);
			}
			value |= ((dev->current_b ^ 7) << 5);
			break;
		case BUSM_PORT_CONFIG:
			value = dev->config_val;
			break;
		case BUSM_PORT_SIGNATURE:
			value = dev->sig_val;
			break;
	}
    }

    bm_log("DEBUG: read from address 0x%04x, value = 0x%02x\n", port, value);

    return value;
}


/* Handle a WRITE operation to one of our registers. */
static void
bm_write(uint16_t port, uint8_t val, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    bm_log("DEBUG: write  to address 0x%04x, value = 0x%02x\n", port, val);

    if (dev->type == MOUSE_TYPE_INPORT) {
	switch (port & 0x03) {
		case INP_PORT_CONTROL:
			/* Bit 7 is reset. */
			if (val & INP_CTRL_RESET)
				dev->control_val = 0;

			/* Bits 0-2 are the internal register index. */
			switch(val & 0x07) {
				case INP_CTRL_COMMAND:
				case INP_CTRL_READ_BUTTONS:
				case INP_CTRL_READ_X:
				case INP_CTRL_READ_Y:
					dev->command_val = val & 0x07;
					break;
				default:
					bm_log("ERROR: Unsupported command written to port 0x%04x (value = 0x%02x)\n", port, val);
			}
			break;
		case INP_PORT_DATA:
			picintc(1 << dev->irq);
			switch(dev->command_val) {
				case INP_CTRL_COMMAND:
					dev->hold = (val & INP_HOLD_COUNTER) > 0;

					dev->interrupts = (val & INP_ENABLE_TIMER_IRQ) > 0;
					dev->data_int = (val & INP_ENABLE_DATA_IRQ) > 0;

					switch(val & INP_PERIOD_MASK) {
						case 0:
							dev->period = 0.0;
							dev->timer_enabled = 0;
							break;
						case 1:
							dev->period = 1000000.0 / 30.0;
							dev->timer_enabled = (val & INP_ENABLE_TIMER_IRQ) ? *TIMER_ALWAYS_ENABLED : 0;
							break;
						case 2:
							dev->period = 1000000.0 / 50.0;
							dev->timer_enabled = (val & INP_ENABLE_TIMER_IRQ) ? *TIMER_ALWAYS_ENABLED : 0;
							break;
						case 3:
							dev->period = 1000000.0 / 100.0;
							dev->timer_enabled = (val & INP_ENABLE_TIMER_IRQ) ? *TIMER_ALWAYS_ENABLED : 0;
							break;
						case 4:
							dev->period = 1000000.0 / 200.0;
							dev->timer_enabled = (val & INP_ENABLE_TIMER_IRQ) ? *TIMER_ALWAYS_ENABLED : 0;
							break;
						case 6:
							if (val & INP_ENABLE_TIMER_IRQ)
								picint(1 << dev->irq);
							dev->control_val &= INP_PERIOD_MASK;
							dev->control_val |= (val & ~INP_PERIOD_MASK);
							return;
						default:
							bm_log("ERROR: Unsupported period written to port 0x%04x (value = 0x%02x)\n", port, val);
					}

					dev->control_val = val;

					break;
				default:
					bm_log("ERROR: Unsupported write to port 0x%04x (value = 0x%02x)\n", port, val);
			}
			break;
		case INP_PORT_SIGNATURE:
		case INP_PORT_CONFIG:
			bm_log("ERROR: Unsupported write to port 0x%04x (value = 0x%02x)\n", port, val);
			break;
	}
    } else {
	switch (port & 0x03) {
		case BUSM_PORT_CONTROL:
			dev->control_val = val | 0x0F;

			dev->interrupts = (val & DISABLE_IRQ) == 0;
			dev->hold = (val & HOLD_COUNTER) > 0;

			picintc(1 << dev->irq);

			break;
		case BUSM_PORT_CONFIG:
			dev->config_val = val;
			break;
		case BUSM_PORT_SIGNATURE:
			dev->sig_val = val;
			break;
		case BUSM_PORT_DATA:
			bm_log("ERROR: Unsupported write to port 0x%04x (value = 0x%02x)\n", port, val);
			break;
	}
    }
}


/* The emulator calls us with an update on the host mouse device. */
static int
bm_poll(int x, int y, int z, int b, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;
    uint8_t shift = 0xff;

    if (dev->type == MOUSE_TYPE_INPORT)
	shift = 0x07;

    if (!x && !y && !((b ^ dev->mouse_buttons_last) & shift)/* && (dev->type == MOUSE_TYPE_INPORT)*/)
	return(1);	/* State has not changed, do nothing. */

    /* change button staste MRL to LMR */
    dev->mouse_buttons = (uint8_t) (((b & 1) << 2) | ((b & 2) >> 1));
    if (dev->bn == 3)
	dev->mouse_buttons |= ((b & 4) >> 1);

    if (dev->type == MOUSE_TYPE_INPORT) {
	/* InPort mouse. */

	/* If the mouse has moved, set bit 6. */
	if (x || y)
		dev->mouse_buttons |= 0x40;

	/* Set bits 3-5 according to button state changes. */
	if ((b ^ dev->mouse_buttons_last) & (1 << 0))
		dev->mouse_buttons |= (1 << 5);
	if ((b ^ dev->mouse_buttons_last) & (1 << 4) && (dev->bn == 3))
		dev->mouse_buttons |= (1 << 4);
	if ((b ^ dev->mouse_buttons_last) & (1 << 2))
		dev->mouse_buttons |= (1 << 3);
    }

    dev->mouse_buttons_last = b;

    /* Clamp x and y to between -128 and 127 (int8_t range). */
    if (x > 127)  x = 127;
    if (x < -128)  x = -128;

    if (y > 127)  y = 127;
    if (y < -128)  y = -128;

    if (dev->timer_enabled) {		/* Timer interrupt mode. */
	/* Update delayed coordinates. */
	dev->mouse_delayed_dx += x;
	dev->mouse_delayed_dy += y;
    } else {				/* Data interrupt mode. */
	/* If the counters are not frozen, update them. */
	if (!dev->hold) {
		dev->current_x = (int8_t) x;
		dev->current_y = (int8_t) y;

		dev->current_b = dev->mouse_buttons;
	}

	/* Send interrupt. */
	if (dev->data_int) {
		picintc(1 << dev->irq);
		picint(1 << dev->irq);
		bm_log("DEBUG: Data Interrupt Fired...\n");
	}
    }
    return(0);
}


static void
bm_update_data(mouse_t *dev)
{
    int delta_x, delta_y;

    if (dev->mouse_delayed_dx > 127) {
	delta_x = 127;
	dev->mouse_delayed_dx -= 127;
    } else if (dev->mouse_delayed_dx < -128) {
	delta_x = -128;
	dev->mouse_delayed_dx += 128;
    } else {
	delta_x = dev->mouse_delayed_dx;
	dev->mouse_delayed_dx = 0;
    }

    if (dev->mouse_delayed_dy > 127) {
	delta_y = 127;
	dev->mouse_delayed_dy -= 127;
    } else if (dev->mouse_delayed_dy < -128) {
	delta_y = -128;
	dev->mouse_delayed_dy += 128;
    } else {
	delta_y = dev->mouse_delayed_dy;
	dev->mouse_delayed_dy = 0;
    }

    if (!dev->hold) {
	dev->current_x = (uint8_t) delta_x;
	dev->current_y = (uint8_t) delta_y;
	dev->current_b = dev->mouse_buttons;
    }
}


/* Called at 30hz */
static void
bm_timer(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    if (dev->type == MOUSE_TYPE_INPORT)
	dev->timer = ((int64_t) dev->period) * TIMER_USEC;
    else
	dev->timer = BUSM_PERIOD;

    if (dev->interrupts) {
	picintc(1 << dev->irq);
	picint(1 << dev->irq);
	bm_log("DEBUG: Timer Interrupt Fired...\n");
    }

    bm_update_data(dev);
}


/* Release all resources held by the device. */
static void
bm_close(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    if (dev)
	free(dev);
}


/* Initialize the device for use by the user. */
static void *
bm_init(const device_t *info)
{
    mouse_t *dev;

    dev = (mouse_t *)malloc(sizeof(mouse_t));
    memset(dev, 0x00, sizeof(mouse_t));

    dev->type = info->local;
    dev->model = device_get_config_int("model");
    dev->base = device_get_config_hex16("base");
    dev->irq = device_get_config_int("irq");
    dev->bn = device_get_config_int("buttons");
    mouse_set_buttons(dev->bn);

    io_sethandler(dev->base, 4,
		  bm_read, NULL, NULL, bm_write, NULL, NULL, dev);

    dev->mouse_delayed_dx = 0;
    dev->mouse_delayed_dy = 0;
    dev->mouse_buttons    = 0;
    dev->current_x =
    dev->current_y =
    dev->current_b = 0;

    if (dev->type == MOUSE_TYPE_INPORT) {
	dev->control_val        = 0;	/* the control port value */
	dev->mouse_buttons_last = 0;
	dev->period             = 0.0;	/* 30 Hz default */
	dev->timer              = 0LL;
	dev->timer_enabled      = 0;
    } else {
	dev->control_val   = 0x0f;	/* the control port value */
	dev->config_val    = 0x0e;	/* the config port value */
	dev->sig_val       = 0;		/* the signature port value */
	dev->timer         = BUSM_PERIOD;
	dev->timer_enabled = *TIMER_ALWAYS_ENABLED;
	dev->interrupts    = 1;
    }
    dev->data_int       = 0;
    dev->interrupts     = 0;		/* timer interrupts off */
    dev->command_val    = 0;		/* command byte */
    dev->toggle_counter = 0;		/* signature byte / IRQ bit toggle */
    dev->hold           = 0;		/* counter freeze */

    timer_add(bm_timer, &dev->timer, &dev->timer_enabled, dev);

    if (dev->type == MOUSE_TYPE_INPORT)
	bm_log("MS Inport BusMouse initialized\n");
    else
	bm_log("Standard MS/Logitech BusMouse initialized\n");

    return dev;
}


static const device_config_t bm_config[] = {
    {
	"base", "Address", CONFIG_HEX16, "", 0x23c,
	{
		{
			"0x230", 0x230
		},
		{
			"0x234", 0x234
		},
		{
			"0x238", 0x238
		},
		{
			"0x23C", 0x23c
		},
		{
			""
		}
	}
    },
    {
	"irq", "IRQ", CONFIG_SELECTION, "", 5, {
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
	"model", "Model", CONFIG_SELECTION, "", 0, {
		{
			"Old", 0
		},
		{
			"New", 1
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


const device_t mouse_logibus_device = {
    "Logitech Bus Mouse",
    DEVICE_ISA,
    MOUSE_TYPE_LOGIBUS,
    bm_init, bm_close, NULL,
    bm_poll, NULL, NULL,
    bm_config
};

const device_t mouse_msinport_device = {
    "Microsoft Bus Mouse (InPort)",
    DEVICE_ISA,
    MOUSE_TYPE_INPORT,
    bm_init, bm_close, NULL,
    bm_poll, NULL, NULL,
    bm_config
};
