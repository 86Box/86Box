#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/nmi.h>
#include <86box/nvr.h>
#include <86box/gameport.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/lpt.h>
#include <86box/rom.h>
#include <86box/hdc.h>
#include <86box/port_6x.h>
#include <86box/video.h>
#include <86box/machine.h>
#include "cpu.h"
#include <86box/plat_unused.h>

#include <86box/m_xt_xi8088.h>

typedef struct xi8088_t {
    uint8_t turbo;

    int turbo_setting;
    int bios_128kb;
} xi8088_t;

static xi8088_t xi8088;

uint8_t
xi8088_turbo_get(void)
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
        c   = cpu;
        cpu = 0; /* 8088/4.77 */
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
xi8088_init(UNUSED(const device_t *info))
{
    xi8088.turbo         = 1;
    xi8088.turbo_setting = device_get_config_int("turbo_setting");
    xi8088.bios_128kb    = device_get_config_int("bios_128kb");

    mem_set_mem_state(0x0a0000, 0x20000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    mem_set_mem_state(0x0c0000, 0x08000, device_get_config_int("umb_c0000h_c7fff") ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state(0x0c8000, 0x08000, device_get_config_int("umb_c8000h_cffff") ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state(0x0d0000, 0x08000, device_get_config_int("umb_d0000h_d7fff") ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state(0x0d8000, 0x08000, device_get_config_int("umb_d8000h_dffff") ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state(0x0e0000, 0x08000, device_get_config_int("umb_e0000h_e7fff") ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state(0x0e8000, 0x08000, device_get_config_int("umb_e8000h_effff") ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTANY | MEM_WRITE_EXTANY));
    mem_set_mem_state(0x0f0000, 0x10000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    return &xi8088;
}

static const device_config_t xi8088_config[] = {
  // clang-format off
    {
        .name = "turbo_setting",
        .description = "Turbo",
        .type = CONFIG_SELECTION,
        .selection = {
            {
                .description = "Always at selected speed",
                .value = 0
            },
            {
                .description = "BIOS setting + Hotkeys (off during POST)",
                .value = 1
            }
        },
        .default_int = 0
    },
    {
        .name = "bios_128kb",
        .description = "BIOS size",
        .type = CONFIG_SELECTION,
        .selection = {
            {
                .description = "64 kB starting from F0000",
                .value = 0
            },
            {
                .description = "128 kB starting from E0000 (address MSB inverted, last 64KB first)",
                .value = 1
            }
        },
        .default_int = 1
    },
    {
        .name = "umb_c0000h_c7fff",
        .description = "Map C0000-C7FFF as UMB",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "umb_c8000h_cffff",
        .description = "Map C8000-CFFFF as UMB",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "umb_d0000h_d7fff",
        .description = "Map D0000-D7FFF as UMB",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "umb_d8000h_dffff",
        .description = "Map D8000-DFFFF as UMB",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "umb_e0000h_e7fff",
        .description = "Map E0000-E7FFF as UMB",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "umb_e8000h_effff",
        .description = "Map E8000-EFFFF as UMB",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t xi8088_device = {
    .name          = "Xi8088",
    .internal_name = "xi8088",
    .flags         = 0,
    .local         = 0,
    .init          = xi8088_init,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = xi8088_config
};

int
machine_xt_xi8088_init(const machine_t *model)
{
    int ret;

    if (bios_only) {
        ret = bios_load_linear_inverted("roms/machines/xi8088/bios-xi8088-128k.bin",
                                        0x000e0000, 131072, 0);
        ret |= bios_load_linear("roms/machines/xi8088/bios-xi8088.bin",
                                0x000f0000, 65536, 0);
    } else {
        device_add(&xi8088_device);

        if (xi8088_bios_128kb()) {
            ret = bios_load_linear_inverted("roms/machines/xi8088/bios-xi8088-128k.bin",
                                            0x000e0000, 131072, 0);
        } else {
            ret = bios_load_linear("roms/machines/xi8088/bios-xi8088.bin",
                                   0x000f0000, 65536, 0);
        }
    }

    if (bios_only || !ret)
        return ret;

    machine_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&keyboard_ps2_xi8088_device);
    device_add(&port_6x_xi8088_device);
    nmi_init();
    device_add(&ibmat_nvr_device);
    pic2_init();
    standalone_gameport_type = &gameport_device;
    device_add(&sst_flash_39sf010_device);

    return ret;
}
