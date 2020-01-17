#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../timer.h"
#include "../pic.h"
#include "../pit.h"
#include "../dma.h"
#include "../mem.h"
#include "../device.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../nmi.h"
#include "../nvr.h"
#include "../game/gameport.h"
#include "../keyboard.h"
#include "../lpt.h"
#include "../rom.h"
#include "../disk/hdc.h"
#include "../video/video.h"
#include "machine.h"
#include "../cpu/cpu.h"

#include "m_xt_xi8088.h"

typedef struct xi8088_t
{
        uint8_t turbo;

        int turbo_setting;
        int bios_128kb;
} xi8088_t;


static xi8088_t		xi8088;


uint8_t
xi8088_turbo_get()
{
    return xi8088.turbo;
}


void
xi8088_turbo_set(uint8_t value)
{
    int c;

    if (!xi8088.turbo_setting)
	return;

    xi8088.turbo = value;
    if (!value) {
	c = cpu;
	cpu = 0;	/* 8088/4.77 */
	cpu_set();
	cpu = c;
    } else
	cpu_set();
}


int
xi8088_bios_128kb(void)
{
    return xi8088.bios_128kb;
}


static void *
xi8088_init(const device_t *info)
{
    /* even though the bios by default turns the turbo off when controlling by hotkeys, pcem always starts at full speed */
    xi8088.turbo = 1;
    xi8088.turbo_setting = device_get_config_int("turbo_setting");
    xi8088.bios_128kb = device_get_config_int("bios_128kb");

    return &xi8088;
}


static const device_config_t xi8088_config[] =
{
        {
                .name = "turbo_setting",
                .description = "Turbo",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "Always at selected speed",
                                .value = 0
                        },
                        {
                                .description = "Hotkeys (starts off)",
                                .value = 1
                        }
                },
                .default_int = 0
        },
        {
                .name = "bios_128kb",
                .description = "BIOS size",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "64KB",
                                .value = 0
                        },
                        {
                                .description = "128KB",
                                .value = 1
                        }
                },
                .default_int = 1
        },
        {
                .type = -1
        }
};


const device_t xi8088_device =
{
        "Xi8088",
        0,
        0,
        xi8088_init,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        xi8088_config
};


const device_t *
xi8088_get_device(void)
{
    return &xi8088_device;
}


int
machine_xt_xi8088_init(const machine_t *model)
{
    int ret;

    if (bios_only) {
	ret = bios_load_linear(L"roms/machines/xi8088/bios-xi8088.bin",
			       0x000f0000, 65536, 0);
    } else {
	device_add(&xi8088_device);

	if (xi8088_bios_128kb()) {
		ret = bios_load_linear_inverted(L"roms/machines/xi8088/bios-xi8088.bin",
						0x000e0000, 131072, 0);
	} else {
		ret = bios_load_linear(L"roms/machines/xi8088/bios-xi8088.bin",
				       0x000f0000, 65536, 0);
	}
    }

    if (bios_only || !ret)
	return ret;

    /* TODO: set UMBs? See if PCem always sets when we have > 640KB ram and avoids conflicts when a peripheral uses the same memory space */
    machine_common_init(model);
    device_add(&fdc_at_device);
    device_add(&keyboard_ps2_xi8088_device);
    nmi_init();
    device_add(&ibmat_nvr_device);
    pic2_init();
    if (joystick_type != JOYSTICK_TYPE_NONE)
	device_add(&gameport_device);

    return ret;
}
