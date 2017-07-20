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
 * Version:	@(#)mouse_serial.c	1.0.6	2017/06/19
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdlib.h>
#include "ibm.h"
#include "timer.h"
#include "serial.h"
#include "mouse.h"
#include "mouse_serial.h"


#ifdef WALTJE
#define SERMOUSE_TYPE_MSYSTEMS	1		/* Mouse Systems */
#define SERMOUSE_TYPE_MICROSOFT	2		/* Microsoft */
#define SERMOUSE_TYPE_LOGITECH	3		/* Logitech */


typedef struct mouse_serial_t {
    int8_t	port,
		type;
    int		pos,
		delay;
    int		oldb;
    SERIAL	*serial;
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

    switch(ms->type) {
	case SERMOUSE_TYPE_MICROSOFT:
		/* This identifies a two-button Microsoft Serial mouse. */
		serial_write_fifo(ms->serial, 'M', 1);
		break;

	case SERMOUSE_TYPE_LOGITECH:
		/* This identifies a two-button Logitech Serial mouse. */
		serial_write_fifo(ms->serial, 'M', 1);
		serial_write_fifo(ms->serial, '3', 1);
		break;

	default:
		/* No action needed. */
		break;
    }
}


static uint8_t
sermouse_poll(int x, int y, int z, int b, void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;
    uint8_t buff[16];
    int len;

    if (!x && !y && b == ms->oldb) return(1);

    ms->oldb = b;

    if (ms->type == SERMOUSE_TYPE_MSYSTEMS) y = -y;

    if (x>127) x = 127;
    if (y>127) y = 127;
    if (x<-128) x = -128;
    if (y<-128) y = -128;

    len = 0;
    switch(ms->type) {
	case SERMOUSE_TYPE_MSYSTEMS:
		buff[0] = 0x80;
		buff[0] |= (b&0x01) ? 0x00 : 0x04;	/* left button */
		buff[0] |= (b&0x02) ? 0x00 : 0x01;	/* middle button */
		buff[0] |= (b&0x04) ? 0x00 : 0x02;	/* right button */
		buff[1] = x;
		buff[2] = y;
		buff[3] = x;				/* same as byte 1 */
		buff[4] = y;				/* same as byte 2 */
		len = 5;
		break;

	case SERMOUSE_TYPE_MICROSOFT:
		buff[0] = 0x40;
		buff[0] |= (((y>>6)&03)<<2);
		buff[0] |= ((x>>6)&03);
		if (b&0x01) buff[0] |= 0x20;
		if (b&0x02) buff[0] |= 0x10;
		buff[1] = x & 0x3F;
		buff[2] = y & 0x3F;
		len = 3;
		break;

	case SERMOUSE_TYPE_LOGITECH:
		buff[0] = 0x40;
		buff[0] |= (((y>>6)&03)<<2);
		buff[0] |= ((x>>6)&03);
		if (b&0x01) buff[0] |= 0x20;
		if (b&0x02) buff[0] |= 0x10;
		buff[1] = x & 0x3F;
		buff[2] = y & 0x3F;
		if (b&0x04) {
			buff[3] = 0x20;
			len = 4;
		} else {
			len = 3;
		}
		break;
    }

#if 0
    pclog("Mouse_Serial(%d): [", ms->type);
    for (b=0; b<len; b++) pclog(" %02X", buff[b]);
    pclog(" ] (%d)\n", len);
#endif

    /* Send the packet to the bottom-half of the attached port. */
    for (b=0; b<len; b++)
	serial_write_fifo(ms->serial, buff[b], 1);

    return(0);
}


static void
sermouse_close(void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;

    /* Detach serial port from the mouse. */
    serial_attach(ms->port, NULL, NULL);

    free(ms);
}


static void *
sermouse_init(int type)
{
    mouse_serial_t *ms = (mouse_serial_t *)malloc(sizeof(mouse_serial_t));
    memset(ms, 0x00, sizeof(mouse_serial_t));
    ms->port = SERMOUSE_PORT;
    ms->type = type;

    /* Attach a serial port to the mouse. */
    ms->serial = serial_attach(ms->port, sermouse_callback, ms);

    timer_add(sermouse_timer, &ms->delay, &ms->delay, ms);

    return(ms);
}


static void *
sermouse_init_microsoft(void)
{
    return(sermouse_init(SERMOUSE_TYPE_MICROSOFT));
}


static void *
sermouse_init_logitech(void)
{
    return(sermouse_init(SERMOUSE_TYPE_LOGITECH));
}


static void *
sermouse_init_msystems(void)
{
    return(sermouse_init(SERMOUSE_TYPE_MSYSTEMS));
}


mouse_t mouse_msystems = {
    "Mouse Systems Mouse (serial)",
    "mssystems",
    MOUSE_TYPE_MSYSTEMS,
    sermouse_init_msystems,
    sermouse_close,
    sermouse_poll
};


mouse_t mouse_serial_microsoft = {
    "Microsoft 2-button mouse (serial)",
    "msserial",
    MOUSE_TYPE_SERIAL,
    sermouse_init_microsoft,
    sermouse_close,
    sermouse_poll
};


mouse_t mouse_serial_logitech = {
    "Logitech 3-button mouse (serial)",
    "lserial",
    MOUSE_TYPE_SERIAL | MOUSE_TYPE_3BUTTON,
    sermouse_init_logitech,
    sermouse_close,
    sermouse_poll
};


#else


typedef struct mouse_serial_t
{
        int mousepos, mousedelay;
        int oldb;
	int type;
        SERIAL *serial;
} mouse_serial_t;

uint8_t mouse_serial_poll(int x, int y, int z, int b, void *p)
{
        mouse_serial_t *mouse = (mouse_serial_t *)p;
        SERIAL *serial = mouse->serial;        
        uint8_t mousedat[4];

        if (!(serial->ier & 1))
                return 0xff;
        if (!x && !y && b == mouse->oldb)
                return 0xff;

        mouse->oldb = b;
        if (x>127) x=127;
        if (y>127) y=127;
        if (x<-128) x=-128;
        if (y<-128) y=-128;

        /*Use Microsoft format*/
        mousedat[0]=0x40;
        mousedat[0]|=(((y>>6)&3)<<2);
        mousedat[0]|=((x>>6)&3);
        if (b&1) mousedat[0]|=0x20;
        if (b&2) mousedat[0]|=0x10;
        mousedat[1]=x&0x3F;
        mousedat[2]=y&0x3F;
        
        if (!(serial->mctrl & 0x10))
        {
                serial_write_fifo(mouse->serial, mousedat[0]);
                serial_write_fifo(mouse->serial, mousedat[1]);
                serial_write_fifo(mouse->serial, mousedat[2]);
		if ((b&0x04) && mouse->type)
		{
                	serial_write_fifo(mouse->serial, 0x20);
		}
        }

	return 0;
}

uint8_t mouse_serial_msystems_poll(int x, int y, int z, int b, void *p)
{
        mouse_serial_t *mouse = (mouse_serial_t *)p;
        SERIAL *serial = mouse->serial;        
        uint8_t mousedat[4];

        if (!(serial->ier & 1))
                return 0xff;
        if (!x && !y && b == mouse->oldb)
                return 0xff;

	y = -y;

        mouse->oldb = b;
        if (x>127) x=127;
        if (y>127) y=127;
        if (x<-128) x=-128;
        if (y<-128) y=-128;

        /*Use Mouse Systems format*/
	mousedat[0] = 0x80;
	mousedat[0] |= (b&0x01) ? 0x00 : 0x04;	/* left button */
	mousedat[0] |= (b&0x02) ? 0x00 : 0x01;	/* middle button */
	mousedat[0] |= (b&0x04) ? 0x00 : 0x02;	/* right button */
	mousedat[1] = x;
	mousedat[2] = y;
	mousedat[3] = x;			/* same as byte 1 */
	mousedat[4] = y;			/* same as byte 2 */

        if (!(serial->mctrl & 0x10))
        {
                serial_write_fifo(mouse->serial, mousedat[0]);
                serial_write_fifo(mouse->serial, mousedat[1]);
                serial_write_fifo(mouse->serial, mousedat[2]);
                serial_write_fifo(mouse->serial, mousedat[3]);
                serial_write_fifo(mouse->serial, mousedat[4]);
        }

	return 0;
}

void mouse_serial_rcr(struct SERIAL *serial, void *p)
{
        mouse_serial_t *mouse = (mouse_serial_t *)p;
        
        mouse->mousepos = -1;
        mouse->mousedelay = 5000 * (1 << TIMER_SHIFT);
}
        
void mousecallback(void *p)
{
        mouse_serial_t *mouse = (mouse_serial_t *)p;

	mouse->mousedelay = 0;
        if (mouse->mousepos == -1)
        {
                mouse->mousepos = 0;
		if (mouse->type < 2)
		{
	                serial_write_fifo(mouse->serial, 'M');
			if (mouse->type == 1)
			{
		                serial_write_fifo(mouse->serial, '3');
			}
		}
        }
}

void *mouse_serial_common_init(int type)
{
        mouse_serial_t *mouse = (mouse_serial_t *)malloc(sizeof(mouse_serial_t));
        memset(mouse, 0, sizeof(mouse_serial_t));

        mouse->serial = &serial1;
        serial1.rcr_callback = mouse_serial_rcr;
        serial1.rcr_callback_p = mouse;
        timer_add(mousecallback, &mouse->mousedelay, &mouse->mousedelay, mouse);

	mouse->type = type;
        
        return mouse;
}

void *mouse_serial_init()
{
	return mouse_serial_common_init(0);
}

void *mouse_serial_logitech_init()
{
	return mouse_serial_common_init(1);
}

void *mouse_serial_msystems_init()
{
	return mouse_serial_common_init(2);
}

void mouse_serial_close(void *p)
{
        mouse_serial_t *mouse = (mouse_serial_t *)p;
        
        free(mouse);
        
        serial1.rcr_callback = NULL;
}

mouse_t mouse_serial_microsoft =
{
        "Microsoft 2-button mouse (serial)",
	"msserial",
        MOUSE_TYPE_SERIAL,
        mouse_serial_init,
        mouse_serial_close,
        mouse_serial_poll
};

mouse_t mouse_serial_logitech =
{
        "Logitech 3-button mouse (serial)",
	"lserial",
        MOUSE_TYPE_SERIAL | MOUSE_TYPE_3BUTTON,
        mouse_serial_logitech_init,
        mouse_serial_close,
        mouse_serial_poll
};

mouse_t mouse_msystems =
{
	"Mouse Systems Mouse (serial)",
	"mssystems",
	MOUSE_TYPE_MSYSTEMS,
        mouse_serial_msystems_init,
        mouse_serial_close,
        mouse_serial_msystems_poll
};


#endif