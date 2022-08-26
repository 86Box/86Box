/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel ICH2 based Motherboards
 *
 *
 *
 * Authors: Tiseno100,
 *
 *          Copyright 2022 Tiseno100.
 *
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
#include <86box/hwm.h>
#include <86box/sio.h>
#include <86box/spd.h>
#include <86box/clock.h>
#include "cpu.h"
#include <86box/machine.h>

/*
 * ASUS CUSL2-C
 *
 * North Bridge: Intel 815EP
 * Super I/O: ITE IT8702
 * BIOS: Award Medallion 6.0
 * Notes: None
 */
int
machine_at_cusl2c_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/cusl2c/1014c.001",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_bus_slot(0, 0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_bus_slot(0, 0x01, PCI_CARD_AGPBRIDGE,   1, 2, 0, 0);
    pci_register_bus_slot(0, 0x1e, PCI_CARD_BRIDGE,      0, 0, 0, 0);
    pci_register_bus_slot(0, 0x1f, PCI_CARD_SOUTHBRIDGE, 1, 2, 8, 4);
    pci_register_bus_slot(1, 0x00, PCI_CARD_AGP,         1, 2, 0, 0);
    pci_register_bus_slot(2, 0x0a, PCI_CARD_NORMAL,      7, 8, 5, 6);
    pci_register_bus_slot(2, 0x0b, PCI_CARD_NORMAL,      8, 5, 6, 7);
    pci_register_bus_slot(2, 0x0c, PCI_CARD_NORMAL,      5, 6, 7, 8);
    pci_register_bus_slot(2, 0x0d, PCI_CARD_NORMAL,      6, 7, 8, 5);
    pci_register_bus_slot(2, 0x0e, PCI_CARD_NORMAL,      3, 4, 1, 2);

    device_add(&intel_815ep_device);       /* Intel 815EP MCH */
    device_add(&intel_ich2_device);        /* Intel ICH2 */
    device_add(&it8702_device);            /* ITE IT8702 */
    device_add(&sst_flash_49lf002_device); /* SST 2Mbit Firmware Hub */
    device_add(&as99127f_device);          /* ASUS Hardware Monitor */
    ics9xxx_get(ICS9150_08);               /* ICS Clock Chip */
    intel_815ep_spd_init();                /* SPD */

    return ret;
}

/*
 * Biostar M6TSL
 *
 * North Bridge: Intel 815E
 * Super I/O: National Semiconductor NSC366 (PC87366)
 * BIOS: Award BIOS 6.00PG
 * Notes: No integrated ESS Solo & GPU
 */
int
machine_at_m6tsl_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m6tsl/tsl0425b.bin",
                           0x00080000, 524288, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_bus_slot(0, 0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_bus_slot(0, 0x01, PCI_CARD_AGPBRIDGE,   1, 2, 0, 0);
    pci_register_bus_slot(0, 0x1e, PCI_CARD_BRIDGE,      0, 0, 0, 0);
    pci_register_bus_slot(0, 0x1f, PCI_CARD_SOUTHBRIDGE, 1, 2, 8, 4);
    pci_register_bus_slot(1, 0x01, PCI_CARD_AGP,         1, 2, 3, 4);
    pci_register_bus_slot(2, 0x04, PCI_CARD_NORMAL,      3, 4, 5, 6);
    pci_register_bus_slot(2, 0x05, PCI_CARD_NORMAL,      4, 5, 6, 7);
    pci_register_bus_slot(2, 0x06, PCI_CARD_NORMAL,      5, 6, 7, 8);
    pci_register_bus_slot(2, 0x07, PCI_CARD_NORMAL,      6, 7, 8, 1);

    device_add(&intel_815ep_device);       /* Intel 815EP MCH */
    device_add(&intel_ich2_device);        /* Intel ICH2 */
    device_add(&nsc366_device);            /* National Semiconductor NSC366 */
    device_add(&sst_flash_49lf004_device); /* SST 4Mbit Firmware Hub */
//    device_add(ics9xxx_get(ICS9250_08)); /* ICS Clock Chip */
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

/*
 * Biostar M6TSS
 *
 * North Bridge: Intel 815EP
 * Super I/O: National Semiconductor NSC366 (PC87366)
 * BIOS: AwardBIOS 6.00PG
 * Notes:
 */
int
machine_at_m6tss_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/m6tss/tss0518b.bin",
                           0x00080000, 524288, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_bus_slot(0, 0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_bus_slot(0, 0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    pci_register_bus_slot(0, 0x1e, PCI_CARD_BRIDGE,      0, 0, 0, 0);
    pci_register_bus_slot(0, 0x1f, PCI_CARD_SOUTHBRIDGE, 1, 2, 8, 4);
    pci_register_bus_slot(1, 0x01, PCI_CARD_AGP,         1, 2, 3, 4);
    pci_register_bus_slot(2, 0x03, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_bus_slot(2, 0x04, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_bus_slot(2, 0x05, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_bus_slot(2, 0x06, PCI_CARD_NORMAL,      4, 1, 2, 3); // 0x0a
    pci_register_bus_slot(2, 0x07, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&intel_815ep_device);       /* Intel 815EP MCH */
    device_add(&intel_ich2_device);        /* Intel ICH2 */
    device_add(&nsc366_device);            /* National Semiconductor NSC366 */
    device_add(&sst_flash_49lf004_device); /* SST 4Mbit Firmware Hub */
    device_add(ics9xxx_get(ICS9250_08));   /* ICS Clock Chip */
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}

/*
 * Tyan Tomcat 815T (S2080)
 *
 * North Bridge: Intel 815EP
 * Super I/O: National Semiconductor NSC366 (PC87366)
 * BIOS: AMIBIOS 7 (AMI Home BIOS Fork)
 * Notes: None
 */
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
    pci_register_bus_slot(1, 0x01, PCI_CARD_AGP,         1, 2, 3, 4);
    pci_register_bus_slot(2, 0x04, PCI_CARD_NORMAL,      2, 3, 4, 5);
    pci_register_bus_slot(2, 0x05, PCI_CARD_NORMAL,      3, 4, 5, 6);
    pci_register_bus_slot(2, 0x06, PCI_CARD_NORMAL,      4, 5, 6, 7);
    pci_register_bus_slot(2, 0x07, PCI_CARD_NORMAL,      5, 6, 7, 8);

    device_add(&intel_815ep_device);       /* Intel 815EP MCH */
    device_add(&intel_ich2_device);        /* Intel ICH2 */
    device_add(&nsc366_device);            /* National Semiconductor NSC366 */
    device_add(&sst_flash_49lf004_device); /* SST 4Mbit Firmware Hub */
    spd_register(SPD_TYPE_SDRAM, 0x7, 512);

    return ret;
}
