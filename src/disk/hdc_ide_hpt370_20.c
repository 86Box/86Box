/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ABIT BE6-II revision 2.0 HighPoint HPT370 RAID variant.
 */
#include <stdint.h>
#include <86box/device.h>

#include "hdc_ide_hpt370.h"

const device_t ide_hpt370_20_ter_qua_onboard_device = {
    .name          = "HighPoint HPT370 (Tertiary and Quaternary) On-Board, 64K ROM BAR",
    .internal_name = "ide_hpt370_20_ter_qua_onboard",
    .flags         = DEVICE_PCI,
    .local         = HPT370_ROM_BAR_64K,
    .init          = hpt370_init,
    .close         = hpt370_close,
    .reset         = hpt370_reset
};
