#include <stdlib.h>
#include "../ibm.h"

#include "../cpu/cpu.h"
#include "../device.h"
#include "../disc.h"
#include "../fdd.h"
#include "../fdc.h"
#include "../gameport.h"
#include "../io.h"
#include "../keyboard.h"
#include "../keyboard_amstrad.h"
#include "../lpt.h"
#include "../mem.h"
#include "../mouse.h"
#include "../nmi.h"
#include "../nvr.h"

#include "machine_common.h"


static uint8_t amstrad_dead;


static uint8_t amstrad_read(uint16_t port, void *priv)
{
        pclog("amstrad_read : %04X\n",port);
        switch (port)
        {
                case 0x379:
                return 7;
                case 0x37a:
                if (romset == ROM_PC1512) return 0x20;
                if (romset == ROM_PC200)  return 0x80;
                return 0;
                case 0xdead:
                return amstrad_dead;
        }
        return 0xff;
}


static void amstrad_write(uint16_t port, uint8_t val, void *priv)
{
        switch (port)
        {
                case 0xdead:
                amstrad_dead = val;
                break;
        }
}


static uint8_t mousex, mousey;
static void amstrad_mouse_write(uint16_t addr, uint8_t val, void *p)
{
        if (addr == 0x78)
                mousex = 0;
        else
                mousey = 0;
}

static uint8_t amstrad_mouse_read(uint16_t addr, void *p)
{
        if (addr == 0x78)
                return mousex;
        return mousey;
}

typedef struct mouse_amstrad_t
{
        int oldb;
} mouse_amstrad_t;

static uint8_t mouse_amstrad_poll(int x, int y, int z, int b, void *p)
{
        mouse_amstrad_t *mouse = (mouse_amstrad_t *)p;
        
        mousex += x;
        mousey -= y;

        if ((b & 1) && !(mouse->oldb & 1))
                keyboard_send(0x7e);
        if ((b & 2) && !(mouse->oldb & 2))
                keyboard_send(0x7d);
        if (!(b & 1) && (mouse->oldb & 1))
                keyboard_send(0xfe);
        if (!(b & 2) && (mouse->oldb & 2))
                keyboard_send(0xfd);
        
        mouse->oldb = b;

	return(0);
}


static void *mouse_amstrad_init(void)
{
        mouse_amstrad_t *mouse = (mouse_amstrad_t *)malloc(sizeof(mouse_amstrad_t));
        memset(mouse, 0, sizeof(mouse_amstrad_t));
                
        return mouse;
}


static void mouse_amstrad_close(void *p)
{
        mouse_amstrad_t *mouse = (mouse_amstrad_t *)p;
        
        free(mouse);
}


mouse_t mouse_amstrad =
{
        "Amstrad mouse",
        "amstrad",
        MOUSE_TYPE_AMSTRAD,
        mouse_amstrad_init,
        mouse_amstrad_close,
        mouse_amstrad_poll
};


static void amstrad_init(void)
{
        lpt2_remove_ams();
        
        io_sethandler(0x0078, 0x0001, amstrad_mouse_read, NULL, NULL, amstrad_mouse_write, NULL, NULL,  NULL);
        io_sethandler(0x007a, 0x0001, amstrad_mouse_read, NULL, NULL, amstrad_mouse_write, NULL, NULL,  NULL);
        io_sethandler(0x0379, 0x0002, amstrad_read,       NULL, NULL, NULL,                NULL, NULL,  NULL);
        io_sethandler(0xdead, 0x0001, amstrad_read,       NULL, NULL, amstrad_write,       NULL, NULL,  NULL);
}


void machine_amstrad_init(void)
{
        AMSTRAD = 1;

        machine_common_init();
	mem_add_bios();
        amstrad_init();
        keyboard_amstrad_init();
        nvr_init();
	nmi_init();
	fdc_set_dskchg_activelow();
	if (joystick_type != 7)
		device_add(&gameport_device);
}
