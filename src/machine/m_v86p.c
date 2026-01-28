/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Victor V86P portable computer emulation.
 *
 * Authors: Lubomir Rintel, <lkundrak@v3.sk>
 *
 *          Copyright 2021 Lubomir Rintel.
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
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/keyboard.h>
#include <86box/chipset.h>
#include <86box/sio.h>
#include <86box/video.h>

static const device_config_t v86p_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "v86p_122089",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "12/20/89",
                .internal_name = "v86p_122089",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/v86p/INTEL8086AWD_BIOS_S3.1_V86P_122089_Even.rom",
                                   "roms/machines/v86p/INTEL8086AWD_BIOS_S3.1_V86P_122089_Odd.rom",
                                   "" }
            },
            {
                .name          = "09/04/89",
                .internal_name = "v86p_090489",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 2,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/v86p/INTEL8086AWD_BIOS_S3.1_V86P_090489_Even.rom",
                                   "roms/machines/v86p/INTEL8086AWD_BIOS_S3.1_V86P_090489_Odd.rom",
                                   "" }
            },
            {
                .name          = "09/04/89 (Alt)",
                .internal_name = "v86p_jvernet",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 1,
                .size          = 65536,
                .files         = { "roms/machines/v86p/V86P.ROM",
                                   "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t v86p_device = {
    .name          = "Victor V86P",
    .internal_name = "v86p_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = v86p_config
};

int
machine_v86p_init(const machine_t *model)
{
    int ret      = 0;
    int files_no = 0;
    int local    = 0;
    const char  *fn1, *fn2;

    /* No ROMs available. */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    files_no = device_get_bios_num_files(model->device, device_get_config_bios("bios"));
    local    = device_get_bios_local(model->device, device_get_config_bios("bios"));
    fn1      = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);

    if (files_no > 1)
    {
        fn2 = device_get_bios_file(model->device, device_get_config_bios("bios"), 1);
        ret = bios_load_interleavedr(fn1, fn2, 0x000f8000, 65536, 0);
    }
    else
        ret = bios_load_linear(fn1, 0x000f0000, 65536, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    if (local > 0)
        video_load_font("roms/machines/v86p/V86P.FON", FONT_FORMAT_PC1512_T1000, LOAD_FONT_NO_OFFSET);
    else
        video_load_font("roms/machines/v86p/v86pfont.rom", FONT_FORMAT_PC1512_T1000, LOAD_FONT_NO_OFFSET);

    machine_common_init(model);

    device_add(&ct_82c100_device);
    device_add(&f82c606_device);

    device_add(&kbc_xt_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_xt_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&f82c425_video_device);

    if (hdc_current[0] <= HDC_INTERNAL)
        device_add(&st506_xt_victor_v86p_device);

    return ret;
}
