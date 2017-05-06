#include <stdlib.h>
#include "ibm.h"
#include "mouse.h"
#include "pic.h"
#include "serial.h"
#include "timer.h"


typedef struct mouse_serial_t {
    int		pos,
		delay;
    int		oldb;
    SERIAL	*serial;
} mouse_serial_t;


static void
sermouse_rcr(SERIAL *serial, void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;
        
    ms->pos = -1;
    ms->delay = 5000 * (1 << TIMER_SHIFT);
}


static void
sermouse_timer(void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;

    ms->delay = 0;
    if (ms->pos == -1) {
	ms->pos = 0;
	serial_write_fifo(ms->serial, 'M');
    }
}


static uint8_t
sermouse_poll(int x, int y, int z, int b, void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;
    SERIAL *sp = ms->serial;        
    uint8_t mousedat[3];

    if (!(sp->ier & 1)) return(1);

    if (!x && !y && b == ms->oldb) return(1);

    ms->oldb = b;
    if (x>127) x = 127;
    if (y>127) y = 127;
    if (x<-128) x = -128;
    if (y<-128) y = -128;

    /* Use Microsoft format. */
    mousedat[0]	= 0x40;
    mousedat[0] |= (((y>>6)&3)<<2);
    mousedat[0] |= ((x>>6)&3);
    if (b&1) mousedat[0] |= 0x20;
    if (b&2) mousedat[0] |= 0x10;
    mousedat[1] = x & 0x3F;
    mousedat[2] = y & 0x3F;

    /* FIXME: we should check in serial_write_fifo, not here! --FvK */
    if (! (sp->mctrl & 0x10)) {
#if 0
	pclog("Serial data %02X %02X %02X\n",
		mousedat[0], mousedat[1], mousedat[2]);
#endif
	serial_write_fifo(ms->serial, mousedat[0]);
	serial_write_fifo(ms->serial, mousedat[1]);
	serial_write_fifo(ms->serial, mousedat[2]);
    }

    return(0);
}


static void *
sermouse_init(void)
{
    mouse_serial_t *ms = (mouse_serial_t *)malloc(sizeof(mouse_serial_t));
    memset(ms, 0x00, sizeof(mouse_serial_t));

    /* Attach a serial port to the mouse. */
#if 1
    ms->serial = serial_attach(0, sermouse_rcr, ms);
#else
    ms->serial = &serial1;
    serial1.rcr_callback = sermouse_rcr;
    serial1.rcr_callback_p = ms;
#endif

    timer_add(sermouse_timer, &ms->delay, &ms->delay, ms);

    return(ms);
}


static void
sermouse_close(void *priv)
{
    mouse_serial_t *ms = (mouse_serial_t *)priv;

    /* Detach serial port from the mouse. */
#if 1
    serial_attach(0, NULL, NULL);
#else
    serial1.rcr_callback = NULL;
#endif

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
