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
 *		Based on the 86Box Serial Mouse driver as a framework.
 *
 * Version:	@(#)mouse_serial.c	1.0.3	2017/05/07
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdlib.h>
#include "ibm.h"
#include "timer.h"
#include "serial.h"
#include "mouse.h"
#include "mouse_serial.h"


typedef struct mouse_serial_t {
    int		port;
    int		pos,
		delay;
    int		oldb;
    SERIAL	*serial;
	int 	is_ms_format;
} mouse_serial_t;


/* Callback from serial driver: RTS was toggled. */
static void
sermouse_callback(void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;

    /* Start a timer to wake us up in a little while. */
    ms->pos = -1;
    ms->delay = 5000 * (1 << TIMER_SHIFT);
}


/* Callback timer expired, now send our "mouse ID" to the serial port. */
static void
sermouse_timer(void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;

    ms->delay = 0;
    if (ms->pos == -1) 
	{
		ms->pos = 0;

		/* This identifies a two-button Microsoft Serial mouse. */
		serial_write_fifo(ms->serial, 'M');
    }
}


static uint8_t
sermouse_poll(int x, int y, int z, int b, void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;
    uint8_t data[3];

    if (!x && !y && b == ms->oldb) return(1);

    ms->oldb = b;
    if (x>127) x = 127;
    if (y>127) y = 127;
    if (x<-128) x = -128;
    if (y<-128) y = -128;

    /* Use Microsoft format. */
    data[0] = 0x40;
    data[0] |= (((y>>6)&3)<<2);
    data[0] |= ((x>>6)&3);
    if (b&1) data[0] |= 0x20;
    if (b&2) data[0] |= 0x10;
    data[1] = x & 0x3F;
    data[2] = y & 0x3F;

    /* Send the packet to the bottom-half of the attached port. */
#if 0
    pclog("Mouse_Serial: data %02X %02X %02X\n", data[0], data[1], data[2]);
#endif
    serial_write_fifo(ms->serial, data[0]);
    serial_write_fifo(ms->serial, data[1]);
    serial_write_fifo(ms->serial, data[2]);

    return(0);
}


static void *
sermouse_init(void)
{
    mouse_serial_t *ms = (mouse_serial_t *)malloc(sizeof(mouse_serial_t));
    memset(ms, 0x00, sizeof(mouse_serial_t));
    ms->port = SERMOUSE_PORT;

    /* Attach a serial port to the mouse. */
    ms->serial = serial_attach(ms->port, sermouse_callback, ms);

    timer_add(sermouse_timer, &ms->delay, &ms->delay, ms);

    return(ms);
}


static void
sermouse_close(void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;

    /* Detach serial port from the mouse. */
    serial_attach(ms->port, NULL, NULL);

    free(ms);
}


mouse_t mouse_serial_microsoft = {
    "Microsoft 2-button mouse (serial)",
    "msserial",
    MOUSE_TYPE_SERIAL,
    sermouse_init,
    sermouse_close,
    sermouse_poll
};

static uint8_t
mssystems_mouse_poll(int x, int y, int z, int b, void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;
    uint8_t data[5];
	
    if (!x && !y && b == ms->oldb) return(1);

    ms->oldb = b;
	
	y=-y;
	
    if (x>127) x = 127;
    if (y>127) y = 127;
    if (x<-128) x = -128;
    if (y<-128) y = -128;

	data[0] = 0x80 | ((((b & 0x04) >> 1) + ((b & 0x02) << 1) + (b & 0x01)) ^ 0x07);
	data[1] = x;
	data[2] = y;
	data[3] = 0;
	data[4] = 0;
	
	pclog("Mouse_Systems_Serial: data %02X %02X %02X\n", data[0], data[1], data[2]);
	
    serial_write_fifo(ms->serial, data[0]);
    serial_write_fifo(ms->serial, data[1]);
    serial_write_fifo(ms->serial, data[2]);
	serial_write_fifo(ms->serial, data[3]);  
	serial_write_fifo(ms->serial, data[4]);
	
	return(0);
}

static void *
mssystems_mouse_init(void)
{
    mouse_serial_t *ms = (mouse_serial_t *)malloc(sizeof(mouse_serial_t));
    memset(ms, 0x00, sizeof(mouse_serial_t));
    ms->port = SERMOUSE_PORT;
	
    /* Attach a serial port to the mouse. */
    ms->serial = serial_attach(ms->port, sermouse_callback, ms);

    timer_add(sermouse_timer, &ms->delay, &ms->delay, ms);

    return(ms);
}

mouse_t mouse_msystems = {
    "Mouse Systems Mouse (serial)",
    "mssystems",
    MOUSE_TYPE_MSYSTEMS,
    mssystems_mouse_init,
    sermouse_close,
    mssystems_mouse_poll
};