#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../ibm.h"
#include "../nmi.h"
#include "../pit.h"
#include "../mem.h"
#include "../device.h"
#include "../bugger.h"
#include "../game/gameport.h"
#include "../keyboard_xt.h"
#include "machine.h"


void
machine_xt_init(machine_t *model)
{
        machine_common_init(model);

	mem_add_bios();

        pit_set_out_func(&pit, 1, pit_refresh_timer_xt);

        keyboard_xt_init();
	nmi_init();
	if (joystick_type != 7)
		device_add(&gameport_device);

	if (bugger_enabled)
		bugger_init();
}
