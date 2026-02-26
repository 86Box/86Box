/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 4/5 machines.
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
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/nvr.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/sio.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/machine.h>

/* OPTi 597 */
int
machine_at_pci56001_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pci56001/AWARD_ISA_PCI_586_non_PNP_SN_013870745_1994.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x10, PCI_CARD_NORTHBRIDGE, 0,  0,  0,  0);
    pci_register_slot(0x11, PCI_CARD_NORMAL,      1,  2,  3,  4);
    pci_register_slot(0x12, PCI_CARD_NORMAL,      5,  6,  7,  8);
    pci_register_slot(0x13, PCI_CARD_NORMAL,      9,  10, 11, 12);
    pci_register_slot(0x14, PCI_CARD_NORMAL,      13, 14, 15, 16);

    device_add(&opti5x7_pci_device);
    device_add(&opti822_device);
    device_add(&sst_flash_29ee010_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* VLSI SuperCore */
int
machine_at_celebris5xx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/celebris5xx/CELEBRIS.ROM",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x08, PCI_CARD_IDE,         4, 1, 2, 3); /* Onboard */
    pci_register_slot(0x09, PCI_CARD_VIDEO,       4, 1, 2, 3); /* Onboard */
    pci_register_slot(0x0B, PCI_CARD_NORMAL,      1, 3, 2, 1); /* Slot 01 */
    pci_register_slot(0x0C, PCI_CARD_NORMAL,      2, 1, 3, 2); /* Slot 02 */

    device_add(&vl82c59x_device);
    device_add(&intel_flash_bxt_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&ide_cmd640_pci_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}
