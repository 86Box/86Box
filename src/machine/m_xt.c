#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../nmi.h"
#include "../pit.h"
#include "../mem.h"
#include "../device.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../game/gameport.h"
#include "../keyboard.h"
#include "machine.h"


static void
machine_xt_common_init(const machine_t *model)
{
    machine_common_init(model);

    pit_set_out_func(&pit, 1, pit_refresh_timer_xt);

    device_add(&fdc_xt_device);
    nmi_init();
    if (joystick_type != 7)
	device_add(&gameport_device);
}


void
machine_pc_init(const machine_t *model)
{
    machine_xt_common_init(model);

    device_add(&keyboard_pc_device);
}


void
machine_pc82_init(const machine_t *model)
{
    machine_xt_common_init(model);

    device_add(&keyboard_pc82_device);
}


void
machine_xt_init(const machine_t *model)
{
    machine_xt_common_init(model);

    device_add(&keyboard_xt_device);
}


void
machine_xt86_init(const machine_t *model)
{
    machine_xt_common_init(model);

    device_add(&keyboard_xt86_device);
}
