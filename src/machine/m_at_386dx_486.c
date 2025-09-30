/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of 386DX/486 machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/dma.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/sio.h>
#include <86box/hdc.h>
#include <86box/port_6x.h>
#include <86box/port_92.h>
#include <86box/video.h>
#include <86box/flash.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/hwm.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>
#include <86box/sound.h>

/* ALi M1429G */
int
machine_at_exp4349_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/exp4349/biosdump.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&ali1429g_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* OPTi 495SX */
int
machine_at_c747_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/c747/486-C747 Tandon.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    /* The EFAR chipset is a rebrand of the OPTi 495SX. */
    device_add(&opti495sx_device);

    /*
       No idea what KBC it actually has but this produces the
       desired behavior: command A9 does absolutely nothing.
     */
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add_params(&um866x_device, (void *) (UM82C862F | UM866X_IDE_PRI));

    return ret;
}

static const device_config_t opti495_ami_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "ami495",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMI 060692",
                .internal_name = "ami495",
                .bios_type     = BIOS_NORMAL, 
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ami495/opt495sx.ami", "" }
            },
            {
                .name          = "MR BIOS V1.60",
                .internal_name = "mr495",
                .bios_type     = BIOS_NORMAL, 
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/ami495/opt495sx.mr", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t opti495_ami_device = {
    .name          = "DataExpert SX495",
    .internal_name = "opti495_ami_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = opti495_ami_config
};

int
machine_at_opti495_ami_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000f0000, 65536, 0);

    machine_at_common_init(model);

    device_add(&opti495sx_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}
