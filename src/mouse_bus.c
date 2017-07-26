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
 *		Based on an early driver for MINIX 1.5.
 *		Based on the 86Box PS/2 mouse driver as a framework.
 *
 * Version:	@(#)mouse_bus.c	1.0.6	2017/07/24
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		TheCollector1995,
 *		Copyright 1989-2017 Fred N. van Kempen, TheCollector1995.
 */
#include <stdio.h>
#include <stdlib.h>
#include "ibm.h"
#include "io.h"
#include "pic.h"
#include "timer.h"
#include "mouse.h"
#include "mouse_bus.h"
#include "plat_mouse.h"

#define BUS_MOUSE_IRQ  5
#define IRQ_MASK ((1<<5) >> BUS_MOUSE_IRQ)

// MS Inport Bus Mouse Adapter
#define INP_PORT_CONTROL     0x023C
#define INP_PORT_DATA        0x023D
#define INP_PORT_SIGNATURE   0x023E
#define INP_PORT_CONFIG      0x023F

#define INP_CTRL_READ_BUTTONS 0x00
#define INP_CTRL_READ_X       0x01
#define INP_CTRL_READ_Y       0x02
#define INP_CTRL_COMMAND      0x07
#define INP_CTRL_RAISE_IRQ    0x16
#define INP_CTRL_RESET        0x80

#define INP_HOLD_COUNTER      (1 << 5)
#define INP_ENABLE_IRQ        (1 << 0)

// MS/Logictech Standard Bus Mouse Adapter
#define BUSM_PORT_DATA        0x023C
#define BUSM_PORT_SIGNATURE   0x023D
#define BUSM_PORT_CONTROL     0x023E
#define BUSM_PORT_CONFIG      0x023F

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
typedef struct mouse_bus_t
{
	int irq;
	int timer_index;
	int x_delay;
	int y_delay;
	uint8_t mouse_buttons;
	uint8_t mouse_buttons_last;
	uint8_t x, y, but;
	uint8_t command_val;
	uint8_t control_val;
	uint8_t config_val;
	uint8_t sig_val;
	uint16_t toggle_counter;
	int interrupts;
	int is_inport;
} mouse_bus_t;


/* Handle a WRITE operation to one of our registers. */
static void busmouse_write(uint16_t port, uint8_t val, void *priv)
{
    mouse_bus_t *busmouse = (mouse_bus_t *)priv;

	switch (port)
	{
		case BUSM_PORT_CONTROL:
		busmouse->control_val = val | 0x0f;
		busmouse->interrupts = (val & DISABLE_IRQ) == 0;
		picintc(1 << busmouse->irq);
		break;
		
		case BUSM_PORT_CONFIG:
		busmouse->config_val = val;
		break;
		
		case BUSM_PORT_SIGNATURE:
		busmouse->sig_val = val;
		break;
		
		case BUSM_PORT_DATA:
		break;
	}
}


/* Handle a READ operation from one of our registers. */
static uint8_t busmouse_read(uint16_t port, void *priv)
{
    mouse_bus_t *busmouse = (mouse_bus_t *)priv;
    uint8_t r = 0;
	
	switch (port)
	{
		case BUSM_PORT_CONTROL:
		r = busmouse->control_val;
        /* This is to allow the driver to see which IRQ the card has "jumpered"
		only happens if IRQ's are enabled */
		busmouse->control_val |= 0x0f;
		
		busmouse->control_val &= ~IRQ_MASK;
		busmouse->toggle_counter = (busmouse->toggle_counter + 1) & 0x7ff;
		break;
		
		case BUSM_PORT_DATA:
		switch (busmouse->control_val & 0x60)
		{
			case READ_X_LOW:
			r = busmouse->x & 0x0f;
			break;
			
			case READ_X_HIGH:
			r = (busmouse->x >> 4) & 0x0f;
			break;
			
			case READ_Y_LOW:
			r = busmouse->y & 0x0f;
			break;
			
			case READ_Y_HIGH:
			r = ((busmouse->but ^ 7) << 5) | ((busmouse->y >> 4) & 0x0f);
			break;
			
			default:
			break;
		}
		break;
		
		case BUSM_PORT_CONFIG:
		r = busmouse->config_val;
		break;
		
		case BUSM_PORT_SIGNATURE:
		r = busmouse->sig_val;
		break;
	}
	
    return(r);
}

static void inport_write(uint16_t port, uint8_t val, void *priv)
{
	mouse_bus_t *inport = (mouse_bus_t *)priv;
	
	switch (port)
	{
		case INP_PORT_CONTROL:
		switch (val)
		{
			case INP_CTRL_RESET:
			inport->control_val = 0;
			inport->command_val = 0;
			break;
			
			case INP_CTRL_COMMAND:
			case INP_CTRL_READ_BUTTONS:
			case INP_CTRL_READ_X:
			case INP_CTRL_READ_Y:
			inport->command_val = val;
			break;
			
			case 0x87:
			inport->control_val = 0;
			inport->command_val = 0x07;
			break;
		}
		break;
		
		case INP_PORT_DATA:
		picintc(1 << inport->irq);
		if (val == INP_CTRL_RAISE_IRQ)
		{
			picint(1 << inport->irq);
		}
		else
		{
			switch (inport->command_val)
			{
				case INP_CTRL_COMMAND:
				inport->control_val = val;
				inport->interrupts = (val & INP_ENABLE_IRQ) > 0;
				break;
				
				default:
				break;
			}
		}
		break;
		
		case INP_PORT_SIGNATURE:
		case INP_PORT_CONFIG:
		break;
	}
}

static uint8_t inport_read(uint16_t port, void *priv)
{
    mouse_bus_t *inport = (mouse_bus_t *)priv;
    uint8_t r = 0;
	
	switch (port)
	{
		case INP_PORT_CONTROL:
		r = inport->control_val;
		break;
		
		case INP_PORT_DATA:
		switch (inport->command_val)
		{
			case INP_CTRL_READ_BUTTONS:
			r = inport->but;
			r |= 0x40;
			break;
			
			case INP_CTRL_READ_X:
			r = inport->x;
			break;
			
			case INP_CTRL_READ_Y:
			r = inport->y;
			break;
			
			case INP_CTRL_COMMAND:
			r = inport->control_val;
			break;
		}
		break;
		
		case INP_PORT_SIGNATURE:
		if (!inport->toggle_counter)
		{
			r = 0xde;
		}
		else
		{
			r = 0x12;
		}
		inport->toggle_counter ^= 1;
		break;
		
		case INP_PORT_CONFIG:
		break;
	}
	
	return(r);
}

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

/* Called at 30hz */
void busmouse_timer_handler(void *priv)
{
	mouse_bus_t *busmouse = (mouse_bus_t *)priv;
	
	busmouse->timer_index += ((1000000.0 / 30.0) * TIMER_USEC);
	
	/* The controller updates the data on every interrupt
	We just don't copy it to the current_X if the 'hold' bit is set */
	busmouse_update_mouse_data(busmouse);
}

/* The emulator calls us with an update on the host mouse device. */
static uint8_t busmouse_poll(int x, int y, int z, int b, void *priv)
{
    mouse_bus_t *busmouse = (mouse_bus_t *)priv;

    /* Return early if nothing to do. */
    if (!x && !y && !z && (busmouse->mouse_buttons == b)) return(1);

#if 1
    pclog("BUSMOUSE: poll(%d,%d,%d, %02x)\n", x, y, z, b);
#endif
	
	// scale down the motion
	if ((x < -1) || (x > 1))
		x /= 2;
	if ((y < -1) || (y > 1))
		y /= 2;
	
	if (x > 127) x =127;
	if (y > 127) y =127;
	if (x < -128) x = -128;
	if (y < -128) y = -128;
	
	busmouse->x_delay += x;
	busmouse->y_delay += y;
	
	busmouse->mouse_buttons = (uint8_t)(((b & 1) << 2) |
                              ((b & 4) >> 1) | ((b & 2) >> 1));
	
	if (busmouse->is_inport)
	{
		if ((busmouse->mouse_buttons & (1<<2)) ||
			((busmouse->mouse_buttons_last & (1<<2)) && !(busmouse->mouse_buttons & (1<<2))))
		  busmouse->mouse_buttons |= (1<<5);
		if ((busmouse->mouse_buttons & (1<<1)) ||
			((busmouse->mouse_buttons_last & (1<<1)) && !(busmouse->mouse_buttons & (1<<1))))
		  busmouse->mouse_buttons |= (1<<4);
		if ((busmouse->mouse_buttons & (1<<0)) ||
			((busmouse->mouse_buttons_last & (1<<0)) && !(busmouse->mouse_buttons & (1<<0))))
		  busmouse->mouse_buttons |= (1<<3);
		busmouse->mouse_buttons_last = busmouse->mouse_buttons;
	}

	picint(1 << busmouse->irq);
	
    return(0);
}

/* Release all resources held by the device. */
static void busmouse_close(void *priv)
{
    mouse_bus_t *busmouse = (mouse_bus_t *)priv;

    /* Release our I/O range. */
    io_removehandler(0x023C, 0x0004, busmouse_read, NULL, NULL, busmouse_write, NULL, NULL, busmouse);

    free(busmouse);
}

/* Initialize the device for use by the user. */
static void *busmouse_init(void)
{
    mouse_bus_t *busmouse;
 
    busmouse = (mouse_bus_t *)malloc(sizeof(mouse_bus_t));
    memset(busmouse, 0x00, sizeof(mouse_bus_t));
 
    busmouse->is_inport = 0;
    busmouse->irq = BUS_MOUSE_IRQ;
   
    /* Initialize registers. */
    busmouse->control_val = 0x1f; /* The control port value */
    busmouse->config_val = 0x0e; /* The config port value */
 
    /* Common. */
    busmouse->command_val = 0;
    busmouse->toggle_counter = 0;
    busmouse->interrupts = 0;
 
    /* Request an I/O range. */
    io_sethandler(0x023C, 0x0004, busmouse_read, NULL, NULL, busmouse_write, NULL, NULL, busmouse);
    timer_add(busmouse_timer_handler, &busmouse->timer_index, TIMER_ALWAYS_ENABLED, busmouse);
   
    /* Return our private data to the I/O layer. */
    return(busmouse);
}
 
/* Initialize the device for use by the user. */
static void *inport_init(void)
{
    mouse_bus_t *inport;
 
    inport = (mouse_bus_t *)malloc(sizeof(mouse_bus_t));
    memset(inport, 0x00, sizeof(mouse_bus_t));
 
    inport->is_inport = 1;
    inport->irq = BUS_MOUSE_IRQ;   
 
    /* Initialize registers. */
    inport->control_val = 0x00; /* The control port value */
    inport->config_val = 0x00; /* The config port value */
 
    /* Common. */
    inport->command_val = 0;
    inport->toggle_counter = 0;
    inport->interrupts = 0;
 
    /* Request an I/O range. */
    io_sethandler(0x023C, 0x0004, inport_read, NULL, NULL, inport_write, NULL, NULL, inport);
    timer_add(busmouse_timer_handler, &inport->timer_index, TIMER_ALWAYS_ENABLED, inport);
 
    /* Return our private data to the I/O layer. */
    return(inport);
}

mouse_t mouse_bus = 
{
    "Bus Mouse",
    "msbus",
    MOUSE_TYPE_BUS,
    busmouse_init,
    busmouse_close,
    busmouse_poll
};

mouse_t mouse_inport = 
{
    "InPort Mouse",
    "inport",
    MOUSE_TYPE_INPORT,
    inport_init,
    busmouse_close,
    busmouse_poll
};
