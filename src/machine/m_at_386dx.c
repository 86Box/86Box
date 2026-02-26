/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of 386DX machines.
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
#include <86box/vid_cga.h>
#include <86box/vid_cga_comp.h>
#include <86box/flash.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/hwm.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>
#include <86box/sound.h>

/* ISA */
uint8_t
machine_compaq_p1_handler(void)
{
    return machine_generic_p1_handler() | (hasfpu ? 0x00 : 0x04);
}

static const device_config_t deskpro386_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "deskpro386",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "September 1986",
                .internal_name = "deskpro386_09_1986",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/deskpro386/1986-09-04-HI.json.bin", "" }
            },
            {
                .name          = "May 1988",
                .internal_name = "deskpro386",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/deskpro386/1988-05-10.json.bin", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t deskpro386_device = {
    .name          = "Compaq Deskpro 386",
    .internal_name = "deskpro386_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = deskpro386_config
};

int
machine_at_deskpro386_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linearr(fn, 0x000f8000, 65536, 0);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    video_reset(gfxcard[0]);

    device_add(&compaq_386_device);

    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_portableiii386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linearr("roms/machines/portableiii/P.2 Combined.bin",
                            0x000f0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    video_reset(gfxcard[0]);

    if (hdc_current[0] == HDC_INTERNAL)
        device_add(&ide_isa_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(&compaq_plasma_device);

    device_add(&compaq_386_device);

    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_micronics386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/micronics386/386-Micronics-09-00021-EVEN.BIN",
                                "roms/machines/micronics386/386-Micronics-09-00021-ODD.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_init(model);

    device_add(&port_92_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_micronics386px_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/micronics386/386-Micronics-09-00021-LO.BIN",
                                "roms/machines/micronics386/386-Micronics-09-00021-HI.BIN",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_init(model);

    device_add(&port_92_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* ACC 2168 */
int
machine_at_acc386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acc386/acc386.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&acc2168_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* C&T 386/AT */
int
machine_at_ecs386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/ecs386/AMI BIOS for ECS-386_32 motherboard - L chip.bin",
                                "roms/machines/ecs386/AMI BIOS for ECS-386_32 motherboard - H chip.bin",
                                0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&cs8230_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_spc6000a_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved("roms/machines/spc6000a/3c80.u27",
                                "roms/machines/spc6000a/9f80.u26",
                                0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&cs8230_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_tandy4000_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tandy4000/BIOS Tandy 4000 v1.03.01.bin",
                           0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&cs8230_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* ALi M1429 */
int
machine_at_ecs386v_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ecs386v/PANDA_386V.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&ali1429_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* OPTi 391 */
static const device_config_t dataexpert386wb_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "dataexpert386wb",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios = {
            {
                .name          = "AMIBIOS 050591",
                .internal_name = "dataexpert386wb_050591",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/dataexpert386wb/386-OPTI-386WB-10.BIN", "" }
            },
            {
                .name          = "MR BIOS V1.26",
                .internal_name = "dataexpert386wb",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/dataexpert386wb/st0386-wb-ver2-0-618f078c738cb397184464.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t dataexpert386wb_device = {
    .name          = "DataExpert 386C",
    .internal_name = "dataexpert386wb_device",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = dataexpert386wb_config
};

int
machine_at_dataexpert386wb_init(const machine_t *model)
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

    device_add(&opti391_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* OPTi 495SLC */
int
machine_at_opti495_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/award495/opt495s.awa",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&opti495slc_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* SiS 310 */
int
machine_at_asus3863364k_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/asus3863364k/am27c512dip28-64b53c26be3d8160533563.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&rabbit_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_asus386_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/asus386/ASUS_ISA-386C_BIOS.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&rabbit_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}
