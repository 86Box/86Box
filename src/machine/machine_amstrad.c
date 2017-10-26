#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../ibm.h"
#include "../io.h"
#include "../nmi.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../nvr.h"
#include "../game/gameport.h"
#include "../keyboard.h"
#include "../keyboard_amstrad.h"
#include "../mouse.h"
#include "../lpt.h"
#include "../floppy/floppy.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "machine.h"


typedef struct {
    int	oldb;
} mouse_amstrad_t;


static uint8_t amstrad_dead;
static uint8_t mousex, mousey;


static uint8_t
amstrad_read(uint16_t port, void *priv)
{
    pclog("amstrad_read: %04X\n", port);

    switch (port) {
	case 0x379:
		return(7);

	case 0x37a:
		if (romset == ROM_PC1512) return(0x20);
		if (romset == ROM_PC200)  return(0x80);
		return(0);

	case 0xdead:
		return(amstrad_dead);
    }

    return(0xff);
}


static void
amstrad_write(uint16_t port, uint8_t val, void *priv)
{
    switch (port) {
	case 0xdead:
		amstrad_dead = val;
		break;
    }
}


static void
amstrad_mouse_write(uint16_t addr, uint8_t val, void *priv)
{
    if (addr == 0x78)
	mousex = 0;
      else
	mousey = 0;
}


static uint8_t
amstrad_mouse_read(uint16_t addr, void *priv)
{
    if (addr == 0x78)
	return(mousex);

    return(mousey);
}


static uint8_t
amstrad_mouse_poll(int x, int y, int z, int b, void *priv)
{
    mouse_amstrad_t *ms = (mouse_amstrad_t *)priv;

    mousex += x;
    mousey -= y;

    if ((b & 1) && !(ms->oldb & 1))
	keyboard_send(0x7e);
    if ((b & 2) && !(ms->oldb & 2))
	keyboard_send(0x7d);
    if (!(b & 1) && (ms->oldb & 1))
	keyboard_send(0xfe);
    if (!(b & 2) && (ms->oldb & 2))
	keyboard_send(0xfd);

    ms->oldb = b;

    return(0);
}


static void *
amstrad_mouse_init(mouse_t *info)
{
    mouse_amstrad_t *ms = (mouse_amstrad_t *)malloc(sizeof(mouse_amstrad_t));

    memset(ms, 0x00, sizeof(mouse_amstrad_t));

    return(ms);
}


static void
amstrad_mouse_close(void *priv)
{
    mouse_amstrad_t *ms = (mouse_amstrad_t *)priv;

    free(ms);
}


mouse_t mouse_amstrad = {
    "Amstrad mouse",
    "amstrad",
    MOUSE_TYPE_AMSTRAD,
    amstrad_mouse_init,
    amstrad_mouse_close,
    amstrad_mouse_poll
};


static void
amstrad_init(void)
{
    lpt2_remove_ams();

    io_sethandler(0x0078, 1,
		  amstrad_mouse_read, NULL, NULL,
		  amstrad_mouse_write, NULL, NULL, NULL);

    io_sethandler(0x007a, 1,
		  amstrad_mouse_read, NULL, NULL,
		  amstrad_mouse_write, NULL, NULL, NULL);

    io_sethandler(0x0379, 2,
		  amstrad_read, NULL, NULL,
		  NULL, NULL, NULL, NULL);

    io_sethandler(0xdead, 1,
		  amstrad_read, NULL, NULL,
		  amstrad_write, NULL, NULL, NULL);
}


void
machine_amstrad_init(machine_t *model)
{
    machine_common_init(model);

    amstrad_init();
    keyboard_amstrad_init();

    /* FIXME: make sure this is correct? */
    nvr_at_init(1);

    nmi_init();
    fdc_set_dskchg_activelow();
    if (joystick_type != 7)
	device_add(&gameport_device);
}
