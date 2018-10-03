#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../pic.h"
#include "../pit.h"
#include "../dma.h"
#include "../mem.h"
#include "../device.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../nvr.h"
#include "../game/gameport.h"
#include "../keyboard.h"
#include "../lpt.h"
#include "../disk/hdc.h"
#include "machine.h"


void
machine_at_common_init(const machine_t *model)
{
    machine_common_init(model);

    pit_set_out_func(&pit, 1, pit_refresh_timer_at);
    pic2_init();
    dma16_init();

    if (lpt_enabled)
	lpt2_remove();

    device_add(&at_nvr_device);

    if (joystick_type != 7)
	device_add(&gameport_device);
}


void
machine_at_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add(&keyboard_at_device);
}


void
machine_at_ps2_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add(&keyboard_ps2_device);
}


void
machine_at_common_ide_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add(&ide_isa_2ch_opt_device);
}


void
machine_at_ide_init(const machine_t *model)
{
    machine_at_init(model);

    device_add(&ide_isa_2ch_opt_device);
}


void
machine_at_ps2_ide_init(const machine_t *model)
{
    machine_at_ps2_init(model);

    device_add(&ide_isa_2ch_opt_device);
}


void
machine_at_top_remap_init(const machine_t *model)
{
    machine_at_init(model);

    mem_remap_top_384k();
}


void
machine_at_ide_top_remap_init(const machine_t *model)
{
    machine_at_ide_init(model);

    mem_remap_top_384k();
}


void
machine_at_ibm_init(const machine_t *model)
{
    machine_at_top_remap_init(model);

    device_add(&fdc_at_device);
}
