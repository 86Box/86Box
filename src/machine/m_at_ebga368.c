/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of VIA EBGA368 Based Single Board Computers.
 *
 *		Note: 86Box doesn't emulate all the components a SBC may have.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *         	Tiseno100
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2021 Tiseno100.
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
#include "cpu.h"
#include <86box/machine.h>

int
machine_at_arb9673_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/arb9673/W9673.v12",
			   0x00080000, 524288, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 2, 3, 4, 1);
    pci_register_slot(0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    device_add(&via_vt8601_device);
    device_add(&via_vt82c686b_device);
    device_add(&via_vt82c686_sio_device);
    device_add(&via_vt82c686_hwm_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&sst_flash_39sf040_device);
    spd_register(SPD_TYPE_SDRAM, 0xf, 32);

	
    return ret;
}
