/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Slot 2 machines.
 *
 *          Slot 2 is quite a rare type of Slot. Used mostly by Pentium II & III Xeons
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/hwm.h>
#include <86box/spd.h>
#include <86box/video.h>
#include <86box/clock.h>
#include <86box/scsi.h>
#include "cpu.h"
#include <86box/machine.h>

/* i440GX */
int
machine_at_6gxu_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/6gxu/6gxu.f1c",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x0E, PCI_CARD_SCSI,        1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   2, 3, 0, 0);

    device_add(&i440gx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977EF | W83977_AMI | W83977_NO_NVR));
    device_add(&sst_flash_39sf020_device);
    if ((scsi_card_current[0] == SCSI_CARD_INTERNAL) && machine_get_scsi_device(machine))
        device_add(machine_get_scsi_device(machine));
    spd_register(SPD_TYPE_SDRAM, 0xF, 512);
    device_add(&w83782d_device);       /* fans: CPU, Power, System; temperatures: System, CPU, unused */
    hwm_values.temperatures[2] = 0;    /* unused */
    hwm_values.voltages[1]     = 1500; /* VGTL */

    return ret;
}

static const device_config_t s2dge_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "s2dge_rev16",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMIBIOS 6 (063100) - Revision 1.6",
                .internal_name = "s2dge_rev16",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/s2dge/2gu2241.rom", "" }
            },
            {
                .name          = "AMIBIOS 6 (063100) - Revision 1.0",
                .internal_name = "s2dge",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 262144,
                .files         = { "roms/machines/s2dge/2gu7301.rom", "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t s2dge_device = {
    .name          = "Supermicro S2DGE",
    .internal_name = "s2dge",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = s2dge_config
};

int
machine_at_s2dge_init(const machine_t *model)
{
    int         ret;
    const char *fn;

    if (!device_available(model->device))
        return 0;

    device_context(model->device);
    fn  = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000c0000, 262144, 0);
    device_context_restore();

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 0, 0);
    pci_register_slot(0x0F, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x10, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x14, PCI_CARD_SCSI,        4, 1, 2, 3);
    pci_register_slot(0x0D, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x0E, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 0, 0);

    device_add(&i440gx_device);
    device_add(&piix4e_device);
    device_add_params(&w83977_device, (void *) (W83977TF | W83977_AMI | W83977_NO_NVR));
    device_add(&intel_flash_bxt_device);
    if ((scsi_card_current[0] == SCSI_CARD_INTERNAL) && machine_get_scsi_device(machine))
        device_add(machine_get_scsi_device(machine));
    spd_register(SPD_TYPE_SDRAM, 0xF, 512);
    device_add(&w83781d_device);    /* fans: CPU1, CPU2, Thermal Control; temperatures: unused, CPU1, CPU2? */
    hwm_values.fans[1]         = 0; /* no CPU2 fan */
    hwm_values.temperatures[0] = 0; /* unused */
    hwm_values.temperatures[2] = 0; /* CPU2? */

    return ret;
}
