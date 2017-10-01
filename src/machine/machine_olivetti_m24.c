/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../ibm.h"
#include "../cpu/cpu.h"
#include "../io.h"
#include "../nmi.h"
#include "../mem.h"
#include "../device.h"
#include "../nvr.h"
#include "../game/gameport.h"
#include "../keyboard_olim24.h"
#include "machine_common.h"
#include "machine_olivetti_m24.h"


static uint8_t olivetti_m24_read(uint16_t port, void *priv)
{
        switch (port)
        {
                case 0x66:
                return 0x00;
                case 0x67:
                return 0x20 | 0x40 | 0x0C;
        }
        return 0xff;
}


static void olivetti_m24_init(void)
{
        io_sethandler(0x0066, 0x0002, olivetti_m24_read, NULL, NULL, NULL, NULL, NULL, NULL);
}


void machine_olim24_init(void)
{
        machine_common_init();
	mem_add_bios();
        keyboard_olim24_init();
        nvr_init();
        olivetti_m24_init();
	nmi_init();
	if (joystick_type != 7)  device_add(&gameport_device);
}
