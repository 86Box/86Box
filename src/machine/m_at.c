/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Standard PC/AT implementation.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2025      Jasmine Iwanek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
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
#include <86box/nvr.h>
#include <86box/gameport.h>
#include <86box/ibm_5161.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/rom.h>
#include <86box/hdc.h>
#include <86box/port_6x.h>
#include <86box/machine.h>

void
machine_at_common_init_ex(const machine_t *model, int type)
{
    machine_common_init(model);

    refresh_at_enable = 1;
    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_at);
    pic2_init();
    dma16_init();

    if (!(type & 4))
        device_add(&port_6x_device);
    type &= 3;

    if (type == 1)
        device_add(&ibmat_nvr_device);
    else if (type == 0)
        device_add(&at_nvr_device);

    standalone_gameport_type = &gameport_device;
}

void
machine_at_common_init(const machine_t *model)
{
    machine_at_common_init_ex(model, 0);
}

void
machine_at_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add(&keyboard_at_device);
}

static void
machine_at_ibm_common_init(const machine_t *model)
{
    machine_at_common_init_ex(model, 1);

    device_add(&keyboard_at_device);

    mem_remap_top(384);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);
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

    device_add(&ide_isa_device);
}

void
machine_at_ibm_common_ide_init(const machine_t *model)
{
    machine_at_common_init_ex(model, 1);

    device_add(&ide_isa_device);
}

void
machine_at_ide_init(const machine_t *model)
{
    machine_at_init(model);

    device_add(&ide_isa_device);
}

void
machine_at_ps2_ide_init(const machine_t *model)
{
    machine_at_ps2_init(model);

    device_add(&ide_isa_device);
}

static const device_config_t ibmat_config[] = {
    // clang-format off
    {
        .name = "bios",
        .description = "BIOS Version",
        .type = CONFIG_BIOS,
        .default_string = "ibm5170_111585",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .bios = {
            { .name = "62X082x (11/15/85)", .internal_name = "ibm5170_111585", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 65536, .files = { "roms/machines/ibmat/BIOS_5170_15NOV85_U27.BIN", "roms/machines/ibmat/BIOS_5170_15NOV85_U47.BIN", "" } },

            { .name = "61X9266 (11/15/85) (Alt)", .internal_name = "ibm5170_111585_alt", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 65536, .files = { "roms/machines/ibmat/BIOS_5170_15NOV85_U27_61X9266.BIN", "roms/machines/ibmat/BIOS_5170_15NOV85_U47_61X9265.BIN", "" } },

            { .name = "648009x (06/10/85)", .internal_name = "ibm5170_061085", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 65536, .files = { "roms/machines/ibmat/BIOS_5170_10JUN85_U27.BIN", "roms/machines/ibmat/BIOS_5170_10JUN85_U47.BIN", "" } },

            { .name = "618102x (01/10/84)", .internal_name = "ibm5170_011084", .bios_type = BIOS_NORMAL,
              .files_no = 2, .local = 0, .size = 65536, .files = { "roms/machines/ibmat/BIOS_5170_10JAN84_U27.BIN", "roms/machines/ibmat/BIOS_5170_10JAN84_U47.BIN", "" } },
            { .files_no = 0 }
        },
    },
    {
        .name = "enable_5161",
        .description = "IBM 5161 Expansion Unit",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmat_device = {
    .name          = " IBM AT Devices",
    .internal_name = "ibmat_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmat_config
};

int
machine_at_ibm_init(const machine_t *model)
{
    int         ret = 0;
    uint8_t     enable_5161;
    const char *fn[2];

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    enable_5161  = machine_get_config_int("enable_5161");
    fn[0]        = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    fn[1]        = device_get_bios_file(model->device, device_get_config_bios("bios"), 1);
    ret          = bios_load_interleaved(fn[0], fn[1], 0x000f0000, 65536, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

/* IBM AT machines with custom BIOSes */
int
machine_at_ibmatquadtel_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ibmatquadtel/BIOS_30MAR90_U27_QUADTEL_ENH_286_BIOS_3.05.01_27256.BIN",
                                "roms/machines/ibmatquadtel/BIOS_30MAR90_U47_QUADTEL_ENH_286_BIOS_3.05.01_27256.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    return ret;
}

int
machine_at_ibmatami_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ibmatami/BIOS_5170_30APR89_U27_AMI_27256.BIN",
                                "roms/machines/ibmatami/BIOS_5170_30APR89_U47_AMI_27256.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    return ret;
}

int
machine_at_ibmatpx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ibmatpx/BIOS ROM - PhoenixBIOS A286 - Version 1.01 - Even.bin",
                                "roms/machines/ibmatpx/BIOS ROM - PhoenixBIOS A286 - Version 1.01 - Odd.bin",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    return ret;
}

static const device_config_t ibmxt286_config[] = {
    // clang-format off
    {
        .name = "enable_5161",
        .description = "IBM 5161 Expansion Unit",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ibmxt286_device = {
    .name          = "IBM XT Model 286 Devices",
    .internal_name = "ibmxt286_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ibmxt286_config
};

int
machine_at_ibmxt286_init(const machine_t *model)
{
    int     ret;
    uint8_t enable_5161;

    device_context(model->device);
    enable_5161  = machine_get_config_int("enable_5161");
    device_context_restore();

    ret = bios_load_interleaved("roms/machines/ibmxt286/bios_5162_21apr86_u34_78x7460_27256.bin",
                                "roms/machines/ibmxt286/bios_5162_21apr86_u35_78x7461_27256.bin",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    if (enable_5161)
        device_add(&ibm_5161_device);

    return ret;
}

int
machine_at_siemens_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/siemens/286BIOS.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 1);

    device_add(&keyboard_at_siemens_device);

    mem_remap_top(384);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_wellamerastar_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/wellamerastar/W_3.031_L.BIN",
                                "roms/machines/wellamerastar/W_3.031_H.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    return ret;
}

#ifdef USE_OPEN_AT
int
machine_at_openat_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/openat/bios.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_ibm_common_init(model);

    return ret;
}
#endif /* USE_OPEN_AT */
