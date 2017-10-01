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
#include "../hdd/hdc.h"
#include "../hdd/hdc_ide.h"
#include "machine_common.h"
#include "machine_at.h"


void machine_at_init(void)
{
	AT = 1;

        machine_common_init();
	if (lpt_enabled)
		lpt2_remove();
	mem_add_bios();
        pit_set_out_func(&pit, 1, pit_refresh_timer_at);
        dma16_init();
        keyboard_at_init();
        nvr_init();
        pic2_init();
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
