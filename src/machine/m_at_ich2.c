/*
 * Intel ICH2 based Motherboards
 *
 * Authors:	Tiseno100,
 *
 * Copyright 2022 Tiseno100.
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
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/spd.h>
#include "cpu.h"
#include <86box/machine.h>


int
machine_at_s2080_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/s2080/2080V110.ROM",
			   0x00080000, 524288, 0);

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_bus_slot(0, 0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_bus_slot(0, 0x01, PCI_CARD_AGPBRIDGE,   1, 2, 0, 0);
    pci_register_bus_slot(0, 0x1e, PCI_CARD_BRIDGE,      0, 0, 0, 0);
    pci_register_bus_slot(0, 0x1f, PCI_CARD_SOUTHBRIDGE, 1, 2, 8, 4);
    pci_register_bus_slot(2, 0x04, PCI_CARD_NORMAL,      2, 3, 4, 5);
    pci_register_bus_slot(2, 0x05, PCI_CARD_NORMAL,      3, 4, 5, 6);
    pci_register_bus_slot(2, 0x06, PCI_CARD_NORMAL,      4, 5, 6, 7);
    pci_register_bus_slot(2, 0x07, PCI_CARD_NORMAL,      5, 6, 7, 8);
    pci_register_bus_slot(2, 0x0a, PCI_CARD_NORMAL,      6, 7, 8, 2);
    pci_register_bus_slot(2, 0x09, PCI_CARD_NORMAL,      7, 8, 2, 3);

    device_add(&intel_mch_device); /* Intel i815EP MCH */
    device_add(&intel_ich2_device); /* Intel ICH2 */
    device_add(&nsc366_device); /* National Semiconductor NSC366 */
    device_add(&sst_flash_39sf040_device);
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);
    return ret;
}
