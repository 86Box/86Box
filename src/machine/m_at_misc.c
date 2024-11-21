/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Miscellaneous, Fake, Hypervisor machines.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2019 Miran Grca.
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
#include <86box/clock.h>
#include <86box/sound.h>

int
machine_at_vpc2007_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vpc2007/13500.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    is_vpc = 1;

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&i440bx_no_agp_device);
    device_add(&piix4e_device);
    device_add(&w83977f_370_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&intel_flash_bxt_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256); /* real VPC provides invalid SPD data */

    return ret;
}

int
machine_at_vmware_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vmware/BIOS440.ROM",
                           0x00080000, 524288, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init_ex(model, 2);
    is_vpc = 0;

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0); /* Onboard */
	pci_register_slot(0x01, PCI_CARD_AGPBRIDGE, 1, 2, 0, 0); /* Onboard */
	pci_register_slot(0x04, PCI_CARD_SOUTHBRIDGE, 1, 2, 3, 4); /* Onboard */
	pci_register_slot(0x0F, PCI_CARD_NORMAL, 1, 2, 3, 4); /* Slot 01 */
	pci_register_slot(0x10, PCI_CARD_NORMAL, 2, 3, 4, 1); /* Slot 02 */
	pci_register_slot(0x11, PCI_CARD_NORMAL, 3, 4, 1, 2); /* Slot 03 */
	pci_register_slot(0x12, PCI_CARD_NORMAL, 4, 1, 2, 3); /* Slot 04 */
	pci_register_slot(0x13, PCI_CARD_NORMAL, 4, 1, 2, 3); /* Slot 05 */
	pci_register_slot(0x14, PCI_CARD_NORMAL, 2, 3, 4, 1); /* Slot 06 */
	pci_register_slot(0x15, PCI_CARD_NORMAL, 3, 4, 1, 2); /* Slot 07 */
	pci_register_slot(0x16, PCI_CARD_NORMAL, 4, 1, 2, 3); /* Slot 08 */
	pci_register_slot(0x17, PCI_CARD_NORMAL, 1, 2, 3, 4); /* Slot 09 */
	pci_register_slot(0x18, PCI_CARD_NORMAL, 2, 3, 4, 1); /* Slot 0A */
	pci_register_slot(0x19, PCI_CARD_NORMAL, 3, 4, 1, 2); /* Slot 0B */
	pci_register_slot(0x1A, PCI_CARD_NORMAL, 4, 1, 2, 3); /* Slot 0C */
	pci_register_slot(0x1B, PCI_CARD_NORMAL, 2, 3, 4, 1); /* Slot 0D */
	pci_register_slot(0x1C, PCI_CARD_NORMAL, 3, 4, 1, 2); /* Slot 0E */
	pci_register_slot(0x1D, PCI_CARD_NORMAL, 2, 3, 4, 1); /* Slot 0F */
	pci_register_slot(0x1E, PCI_CARD_NORMAL, 4, 1, 2, 3); /* Slot 10 */
	pci_register_slot(0x1F, PCI_CARD_NORMAL, 1, 2, 3, 4); /* Slot 11 */

    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&pc87332_device);
    device_add(&keyboard_ps2_device);
    device_add(&winbond_flash_w29c040_device);
	device_add(ics9xxx_get(ICS9250_08));
    device_add(&as99127f_device);
	hwm_values.voltages[4] = hwm_values.voltages[5];
	spd_register(SPD_TYPE_SDRAM, 0x7, 256); /* real VPC provides invalid SPD data */
    
    return ret;
}
