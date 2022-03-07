/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Program settings UI module.
 *
 *
 *
 * Authors: Miran Grca <mgrca8@gmail.com>
 *          Cacodemon345
 *
 *      Copyright 2022 Miran Grca
 *      Copyright 2022 Cacodemon345
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "86box/hdd.h"
#include "qt_settings_bus_tracking.hpp"


SettingsBusTracking::SettingsBusTracking()
{
    int i;

    mfm_tracking = 0x0000000000000000ULL;
    esdi_tracking = 0x0000000000000000ULL;
    xta_tracking = 0x0000000000000000ULL;

    for (i = 0; i < 8; i++) {
        if (i < 4)
            ide_tracking[i] = 0x0000000000000000ULL;

        scsi_tracking[i] = 0x0000000000000000ULL;
    }
}


uint8_t
SettingsBusTracking::next_free_mfm_channel()
{
    if ((mfm_tracking & 0xff00ULL) && !(mfm_tracking & 0x00ffULL))
        return 1;

    if (!(mfm_tracking & 0xff00ULL) && (mfm_tracking & 0x00ffULL))
        return 0;

    return CHANNEL_NONE;
}


uint8_t
SettingsBusTracking::next_free_esdi_channel()
{
    if ((esdi_tracking & 0xff00ULL) && !(esdi_tracking & 0x00ffULL))
        return 1;

    if (!(esdi_tracking & 0xff00ULL) && (esdi_tracking & 0x00ffULL))
        return 0;

    return CHANNEL_NONE;
}


uint8_t
SettingsBusTracking::next_free_xta_channel()
{
    if ((xta_tracking & 0xff00ULL) && !(xta_tracking & 0x00ffULL))
        return 1;

    if (!(xta_tracking & 0xff00ULL) && (xta_tracking & 0x00ffULL))
        return 0;

    return CHANNEL_NONE;
}


uint8_t
SettingsBusTracking::next_free_ide_channel()
{
    int i, element;
    uint64_t mask;
    uint8_t ret = CHANNEL_NONE;

    for (i = 0; i < 32; i++) {
        element = ((i << 3) >> 6);
        mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (!(ide_tracking[element] & mask)) {
		ret = (uint8_t) i;
                break;
        }
    }

    return ret;
}


uint8_t
SettingsBusTracking::next_free_scsi_id()
{
    int i, element;
    uint64_t mask;
    uint8_t ret = CHANNEL_NONE;

    for (i = 0; i < 64; i++) {
        element = ((i << 3) >> 6);
        mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (!(scsi_tracking[element] & mask)) {
		ret = (uint8_t) i;
                break;
        }
    }

    return ret;
}


int
SettingsBusTracking::mfm_bus_full()
{
    int i;
    uint64_t mask;
    uint8_t count = 0;

    for (i = 0; i < 2; i++) {
        mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (mfm_tracking & mask)
                count++;
    }

    return (count == 2);
}


int
SettingsBusTracking::esdi_bus_full()
{
    int i;
    uint64_t mask;
    uint8_t count = 0;

    for (i = 0; i < 2; i++) {
        mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (esdi_tracking & mask)
                count++;
    }

    return (count == 2);
}


int
SettingsBusTracking::xta_bus_full()
{
    int i;
    uint64_t mask;
    uint8_t count = 0;

    for (i = 0; i < 2; i++) {
        mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (xta_tracking & mask)
                count++;
    }

    return (count == 2);
}


int
SettingsBusTracking::ide_bus_full()
{
    int i, element;
    uint64_t mask;
    uint8_t count = 0;

    for (i = 0; i < 32; i++) {
        element = ((i << 3) >> 6);
        mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (ide_tracking[element] & mask)
                count++;
    }

    return (count == 32);
}


int
SettingsBusTracking::scsi_bus_full()
{
    int i, element;
    uint64_t mask;
    uint8_t count = 0;

    for (i = 0; i < 64; i++) {
        element = ((i << 3) >> 6);
        mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (scsi_tracking[element] & mask)
                count++;
    }

    return (count == 64);
}


void
SettingsBusTracking::device_track(int set, uint8_t dev_type, int bus, int channel)
{
    int element;
    uint64_t mask;

    switch (bus) {
        case HDD_BUS_MFM:
            mask = ((uint64_t) dev_type) << ((uint64_t) ((channel << 3) & 0x3f));

            if (set)
                mfm_tracking |= mask;
            else
                mfm_tracking &= ~mask;
            break;

        case HDD_BUS_ESDI:
            mask = ((uint64_t) dev_type) << ((uint64_t) ((channel << 3) & 0x3f));

            if (set)
                esdi_tracking |= mask;
            else
                esdi_tracking &= ~mask;
            break;

        case HDD_BUS_XTA:
            mask = ((uint64_t) dev_type) << ((uint64_t) ((channel << 3) & 0x3f));

            if (set)
                xta_tracking |= mask;
            else
                xta_tracking &= ~mask;
            break;

        case HDD_BUS_IDE:
        case HDD_BUS_ATAPI:
            element = ((channel << 3) >> 6);
            mask = ((uint64_t) dev_type) << ((uint64_t) ((channel << 3) & 0x3f));

            if (set)
                ide_tracking[element] |= mask;
            else
                ide_tracking[element] &= ~mask;
            break;

        case HDD_BUS_SCSI:
            element = ((channel << 3) >> 6);
            mask = ((uint64_t) dev_type) << ((uint64_t) ((channel << 3) & 0x3f));

            if (set)
                scsi_tracking[element] |= mask;
            else
                scsi_tracking[element] &= ~mask;
            break;
    }
}
