#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../ibm.h"
#include "../pic.h"
#include "../pit.h"
#include "../dma.h"
#include "../mem.h"
#include "../device.h"
#include "../nvr.h"
#include "../bugger.h"
#include "../game/gameport.h"
#include "../keyboard_at.h"
#include "../lpt.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "machine_common.h"
#include "machine_at.h"


void machine_at_init(void)
{
	AT = 1;

        machine_common_init();
        pit_set_out_func(&pit, 1, pit_refresh_timer_at);
        pic2_init();
        dma16_init();

	if (lpt_enabled)
		lpt2_remove();

        nvr_at_init(8);

        keyboard_at_init();

	if (joystick_type != 7)
		device_add(&gameport_device);

	if (bugger_enabled)
		bugger_init();
}

void machine_at_ide_init(void)
{
	machine_at_init();

	ide_init();
}

void machine_at_top_remap_init(void)
{
	machine_at_init();

	mem_remap_top_384k();
}

void machine_at_ide_top_remap_init(void)
{
	machine_at_ide_init();

	mem_remap_top_384k();
}
