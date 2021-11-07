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
dimm_type: Selects the type of Memory the board has (1 for DDR / 0 for SDRAM).
dimm_slots: Selects the amount of DIMM slots the board has from 1 to 4.

*/
void
intel_ich2_setup(int northbridge, int lan, int pci_slots, int dimm_type, int dimm_slots, const machine_t *model)
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

    /* Intel i8xx Northbridge Setup */
    int max_dimm_size = 512; /* Maximum DIMM size supported by the Northbridge. */
    switch(northbridge)
    {
        case 815:                                 /* Intel i815EP */
            device_add(&intel_gmch_device);
        break;

        case 845:                                 /*  Intel i845  */
                                                  /*  Note: That is a Pentium 4 chipset which 86Box doesn't emulate. Used only for testing. */
        if(dimm_type)
            device_add(&intel_mch_p4_ddr_device); /*   DDR i845   */
        else
            device_add(&intel_mch_p4_device);     /*  SDRAM i845  */

        max_dimm_size = 1024;
        break;

        default:
            pclog("Intel ICH2-SETUP: Unimplemented Northbridge 'i%d' was given! Northbridge won't be added.\n", northbridge);
        break;
    }

    if(lan)
        device_add(&intel_ich2_device); /* Intel ICH2 */
    else
        device_add(&intel_ich2_no_lan_device); /* Intel ICH2 Without LAN */

    /* SPD Setup */
    if((dimm_slots > 0) && (dimm_slots <= 4))
    {
        uint8_t dimm_slot_amount = 0;

        for(int i = 0; i < dimm_slots; i++)
            dimm_slot_amount |= (1 << i);
            

        spd_register(dimm_type ? SPD_TYPE_DDR : SPD_TYPE_SDRAM, dimm_slot_amount, max_dimm_size);
    }
    else pclog("Intel ICH2-SETUP: Incorrect amount of DIMM's (%d) were given! SPD won't be added.\n", dimm_slots);
}
