/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Intel ICH2 Machine Configuration.
 *
 * Authors:	Tiseno100
 *
 *		Copyright 2021 Tiseno100.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/spd.h>
#include <86box/machine.h>

/*

Intel ICH2 Setup:

northbridge: Selects the Northbridge (815 for i815EP / 845 for i845).
lan: Adds the Internal LAN Controller if the board supports it.
pci_slots: The amount of PCI Slots registered by the BIOS.

*/
void
intel_ich2_setup(int northbridge, int lan, int pci_slots, const machine_t *model)
{
    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);

    /* Proper Devices */
    pci_register_bus_slot(0, 0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0); /* North Bridge */
    pci_register_bus_slot(0, 0x1f, PCI_CARD_SOUTHBRIDGE, 1, 2, 8, 4); /* ICH2 LPC */

    /* Bridges */
    pci_register_bus_slot(0, 0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4); /* AGP Bridge */
    pci_register_bus_slot(0, 0x1e, PCI_CARD_BRIDGE,      1, 2, 3, 4); /*  ICH2 Hub  */

    /* Internal LAN */
    if(lan) /* ICH2 LAN */
    pci_register_bus_slot(1, 0x08, PCI_CARD_NETWORK,     5, 6, 7, 8);

    /* ICH2 HUB Bridge Bus Masters(The PCI Slots) */
    intel_ich2_pci_slot_number(pci_slots);

    /* Intel i8xx Northbridge */
    if(northbridge == 815) {               /*    Intel i815EP    */
    device_add(&intel_gmch_device);
    }
    else if(northbridge == 845) {          /*  Intel i845 SDRAM  */
    device_add(&intel_mch_p4_device);
    }
    else pclog("Intel ICH2: Incorrect Northbridge added!\n");

    /* SPD Setup */
    spd_register(SPD_TYPE_SDRAM, 7, 512);

    if(lan)
        device_add(&intel_ich2_device); /* Intel ICH2 */
    else
        device_add(&intel_ich2_no_lan_device); /* Intel ICH2 Without LAN */

}
