/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Program settings UI module.
 *
 * Authors: Miran Grca <mgrca8@gmail.com>
 *          Cacodemon345
 *
 *          Copyright 2022 Miran Grca
 *          Copyright 2022 Cacodemon345
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "86box/hdd.h"
#include "86box/scsi.h"
#include "86box/cdrom.h"
#include "qt_settings_bus_tracking.hpp"

SettingsBusTracking::SettingsBusTracking()
{
    mitsumi_tracking = false;

    mke_tracking  = 0x0000000000000000ULL;
    mfm_tracking  = 0x0000000000000000ULL;
    esdi_tracking = 0x0000000000000000ULL;
    xta_tracking  = 0x0000000000000000ULL;

    for (uint8_t i = 0; i < 4; i++)
        ide_tracking[i] = 0x0000000000000000ULL;

    for (uint8_t i = 0; i < 32; i++)
        scsi_tracking[i] = 0x0000000000000000ULL;
}

uint8_t
SettingsBusTracking::next_free_mke_channel()
{
    uint64_t mask;
    uint8_t  ret = CHANNEL_NONE;

    for (uint8_t i = 0; i < 4; i++) {
        mask    = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (!(mke_tracking & mask)) {
            ret = (uint8_t) i;
            break;
        }
    }

    return ret;
}

uint8_t
SettingsBusTracking::next_free_mfm_channel()
{
    uint64_t mask;
    uint8_t  ret = CHANNEL_NONE;

    for (uint8_t i = 0; i < 2; i++) {
        mask    = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (!(mfm_tracking & mask)) {
            ret = (uint8_t) i;
            break;
        }
    }

    return ret;
}

uint8_t
SettingsBusTracking::next_free_esdi_channel()
{
    uint64_t mask;
    uint8_t  ret = CHANNEL_NONE;

    for (uint8_t i = 0; i < 2; i++) {
        mask    = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (!(esdi_tracking & mask)) {
            ret = (uint8_t) i;
            break;
        }
    }

    return ret;
}

uint8_t
SettingsBusTracking::next_free_xta_channel()
{
    uint64_t mask;
    uint8_t  ret = CHANNEL_NONE;

    for (uint8_t i = 0; i < 2; i++) {
        mask    = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (!(xta_tracking & mask)) {
            ret = (uint8_t) i;
            break;
        }
    }

    return ret;
}

uint8_t
SettingsBusTracking::next_free_ide_channel()
{
    int      element;
    uint64_t mask;
    uint8_t  ret = CHANNEL_NONE;

    for (uint8_t i = 0; i < 32; i++) {
        element = ((i << 3) >> 6);
        mask    = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

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
    int      element;
    uint64_t mask;
    uint8_t  ret = CHANNEL_NONE;

    for (uint8_t i = 0; i < (SCSI_BUS_MAX * SCSI_ID_MAX); i++) {
        element = ((i << 3) >> 6);
        mask    = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

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
    uint64_t mask;
    uint8_t  count = 0;

    for (uint8_t i = 0; i < 2; i++) {
        mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (mfm_tracking & mask)
            count++;
    }

    return (count == 2);
}

int
SettingsBusTracking::esdi_bus_full()
{
    uint64_t mask;
    uint8_t  count = 0;

    for (uint8_t i = 0; i < 2; i++) {
        mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (esdi_tracking & mask)
            count++;
    }

    return (count == 2);
}

int
SettingsBusTracking::xta_bus_full()
{
    uint64_t mask;
    uint8_t  count = 0;

    for (uint8_t i = 0; i < 2; i++) {
        mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (xta_tracking & mask)
            count++;
    }

    return (count == 2);
}

int
SettingsBusTracking::ide_bus_full()
{
    int      element;
    uint64_t mask;
    uint8_t  count = 0;

    for (uint8_t i = 0; i < 32; i++) {
        element = ((i << 3) >> 6);
        mask    = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (ide_tracking[element] & mask)
            count++;
    }

    return (count == 32);
}

int
SettingsBusTracking::scsi_bus_full()
{
    int      element;
    uint64_t mask;
    uint8_t  count = 0;

    for (uint8_t i = 0; i < (SCSI_BUS_MAX * SCSI_ID_MAX); i++) {
        element = ((i << 3) >> 6);
        mask    = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));

        if (scsi_tracking[element] & mask)
            count++;
    }

    return (count == 64);
}

QList<int> SettingsBusTracking::busChannelsInUse(const int bus) {

    QList<int> channelsInUse;
    int        element;
    uint64_t   mask;
    switch (bus) {
        case CDROM_BUS_MKE:
            for (uint8_t i = 0; i < 4; i++) {
                mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));
                if (mke_tracking & mask)
                    channelsInUse.append(i);
            }
            break;
        case CDROM_BUS_MITSUMI:
            if (mitsumi_tracking)
                channelsInUse.append(0);
            break;
        case HDD_BUS_MFM:
            for (uint8_t i = 0; i < 2; i++) {
                mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));
                if (mfm_tracking & mask)
                    channelsInUse.append(i);
            }
            break;
        case HDD_BUS_ESDI:
            for (uint8_t i = 0; i < 2; i++) {
                mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));
                if (esdi_tracking & mask)
                    channelsInUse.append(i);
            }
            break;
        case HDD_BUS_XTA:
            for (uint8_t i = 0; i < 2; i++) {
                mask = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));
                if (xta_tracking & mask)
                    channelsInUse.append(i);
            }
            break;
        case HDD_BUS_IDE:
            for (uint8_t i = 0; i < 32; i++) {
                element = ((i << 3) >> 6);
                mask = ((uint64_t) 0xffULL) << ((uint64_t) ((i << 3) & 0x3f));
                if (ide_tracking[element] & mask)
                    channelsInUse.append(i);
            }
            break;
        case HDD_BUS_ATAPI:
            for (uint8_t i = 0; i < 32; i++) {
                element = ((i << 3) >> 6);
                mask = ((uint64_t) 0xffULL) << ((uint64_t) ((i << 3) & 0x3f));
                if (ide_tracking[element] & mask)
                    channelsInUse.append(i);
            }
            break;
        case HDD_BUS_SCSI:
            for (uint8_t i = 0; i < (SCSI_BUS_MAX * SCSI_ID_MAX); i++) {
                element = ((i << 3) >> 6);
                mask    = 0xffULL << ((uint64_t) ((i << 3) & 0x3f));
                if (scsi_tracking[element] & mask)
                    channelsInUse.append(i);
            }
            break;
        default:
            break;
    }

    return channelsInUse;
}

void
SettingsBusTracking::device_track(int set, uint8_t dev_type, int bus, int channel)
{
    int      element;
    uint64_t mask;

    switch (bus) {
        case CDROM_BUS_MKE:
            mask = ((uint64_t) dev_type) << ((uint64_t) ((channel << 3) & 0x3f));

            if (set)
                mke_tracking |= mask;
            else
                mke_tracking &= ~mask;
            break;
        case CDROM_BUS_MITSUMI:
            mitsumi_tracking = set;
            break;
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
            mask    = ((uint64_t) dev_type) << ((uint64_t) ((channel << 3) & 0x3f));

            if (set)
                ide_tracking[element] |= mask;
            else
                ide_tracking[element] &= ~mask;
            break;

        case HDD_BUS_SCSI:
            element = ((channel << 3) >> 6);
            mask    = ((uint64_t) dev_type) << ((uint64_t) ((channel << 3) & 0x3f));

            if (set)
                scsi_tracking[element] |= mask;
            else
                scsi_tracking[element] &= ~mask;
            break;
    }
}
