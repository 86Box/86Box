/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Common storage devices module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */
#include "qt_harddrive_common.hpp"

#include <cstdint>

extern "C" {
#include <86box/hdd.h>
#include <86box/scsi.h>
#include <86box/cdrom.h>
}

#include <QAbstractItemModel>
#include <QStandardItemModel>

void
Harddrives::populateBuses(QAbstractItemModel *model)
{
    model->removeRows(0, model->rowCount());
    model->insertRows(0, 6);

    model->setData(model->index(0, 0), "MFM/RLL");
    model->setData(model->index(1, 0), "XTA");
    model->setData(model->index(2, 0), "ESDI");
    model->setData(model->index(3, 0), "IDE");
    model->setData(model->index(4, 0), "ATAPI");
    model->setData(model->index(5, 0), "SCSI");

    model->setData(model->index(0, 0), HDD_BUS_MFM, Qt::UserRole);
    model->setData(model->index(1, 0), HDD_BUS_XTA, Qt::UserRole);
    model->setData(model->index(2, 0), HDD_BUS_ESDI, Qt::UserRole);
    model->setData(model->index(3, 0), HDD_BUS_IDE, Qt::UserRole);
    model->setData(model->index(4, 0), HDD_BUS_ATAPI, Qt::UserRole);
    model->setData(model->index(5, 0), HDD_BUS_SCSI, Qt::UserRole);
}

void
Harddrives::populateCDROMBuses(QAbstractItemModel *model)
{
    model->removeRows(0, model->rowCount());
#ifdef USE_CDROM_MITSUMI
    model->insertRows(0, 5);
#else
    model->insertRows(0, 4);
#endif

    model->setData(model->index(0, 0), QObject::tr("Disabled"));
    model->setData(model->index(1, 0), QObject::tr("ATAPI"));
    model->setData(model->index(2, 0), QObject::tr("SCSI"));
#ifdef USE_CDROM_MITSUMI
    model->setData(model->index(3, 0), QObject::tr("Mitsumi"));
    model->setData(model->index(4, 0), QObject::tr("Panasonic/MKE"));
#else
    model->setData(model->index(3, 0), QObject::tr("Panasonic/MKE"));
#endif

    model->setData(model->index(0, 0), HDD_BUS_DISABLED, Qt::UserRole);
    model->setData(model->index(1, 0), HDD_BUS_ATAPI, Qt::UserRole);
    model->setData(model->index(2, 0), HDD_BUS_SCSI, Qt::UserRole);
#ifdef USE_CDROM_MITSUMI
    model->setData(model->index(3, 0), CDROM_BUS_MITSUMI, Qt::UserRole);
    model->setData(model->index(4, 0), CDROM_BUS_MKE, Qt::UserRole);
#else
    model->setData(model->index(3, 0), CDROM_BUS_MKE, Qt::UserRole);
#endif
}

void
Harddrives::populateRemovableBuses(QAbstractItemModel *model)
{
    model->removeRows(0, model->rowCount());
    model->insertRows(0, 3);

    model->setData(model->index(0, 0), QObject::tr("Disabled"));
    model->setData(model->index(1, 0), QObject::tr("ATAPI"));
    model->setData(model->index(2, 0), QObject::tr("SCSI"));

    model->setData(model->index(0, 0), HDD_BUS_DISABLED, Qt::UserRole);
    model->setData(model->index(1, 0), HDD_BUS_ATAPI, Qt::UserRole);
    model->setData(model->index(2, 0), HDD_BUS_SCSI, Qt::UserRole);
}

void
Harddrives::populateSpeeds(QAbstractItemModel *model, int bus)
{
    int num_preset;

    switch (bus) {
        case HDD_BUS_ESDI:
        case HDD_BUS_IDE:
        case HDD_BUS_ATAPI:
        case HDD_BUS_SCSI:
            num_preset = hdd_preset_get_num();
            break;

        default:
            num_preset = 1;
    }

    model->removeRows(0, model->rowCount());
    model->insertRows(0, num_preset);

    for (int i = 0; i < num_preset; i++) {
        model->setData(model->index(i, 0), QObject::tr(hdd_preset_getname(i)));
        model->setData(model->index(i, 0), i, Qt::UserRole);
    }
}

void
Harddrives::populateBusChannels(QAbstractItemModel *model, int bus, SettingsBusTracking *sbt)
{
    model->removeRows(0, model->rowCount());

    int busRows         = 0;
    int shifter         = 1;
    int orer            = 1;
    int subChannelWidth = 1;
    QList<int> busesToCheck;
    QList<int> channelsInUse;
    switch (bus) {
        case HDD_BUS_MFM:
            busRows = 2;
            busesToCheck.append(HDD_BUS_MFM);
            break;
        case HDD_BUS_XTA:
            busRows = 2;
            busesToCheck.append(HDD_BUS_XTA);
            break;
        case HDD_BUS_ESDI:
            busRows = 2;
            busesToCheck.append(HDD_BUS_ESDI);
            break;
        case HDD_BUS_IDE:
            busRows = 8;
            busesToCheck.append(HDD_BUS_ATAPI);
            busesToCheck.append(HDD_BUS_IDE);
            break;
        case HDD_BUS_ATAPI:
            busRows = 8;
            busesToCheck.append(HDD_BUS_IDE);
            busesToCheck.append(HDD_BUS_ATAPI);
            break;
        case HDD_BUS_SCSI:
            shifter         = 4;
            orer            = 15;
            busRows         = /*64*/ SCSI_BUS_MAX * SCSI_ID_MAX;
            subChannelWidth = 2;
            busesToCheck.append(HDD_BUS_SCSI);
            break;
        case CDROM_BUS_MKE:
            shifter         = 2;
            orer            = 3;
            busRows         = 4;
            busesToCheck.append(CDROM_BUS_MKE);
            break;
        default:
            break;
    }
    if(sbt != nullptr && !busesToCheck.empty()) {
        for (auto const &checkBus : busesToCheck) {
            channelsInUse.append(sbt->busChannelsInUse(checkBus));
        }
    }

    model->insertRows(0, busRows);
    for (int i = 0; i < busRows; ++i) {
        auto idx = model->index(i, 0);
        model->setData(idx, QString("%1:%2").arg(i >> shifter).arg(i & orer, subChannelWidth, 10, QChar('0')));
        model->setData(idx, ((i >> shifter) << shifter) | (i & orer), Qt::UserRole);
        const auto *channelModel = qobject_cast<QStandardItemModel*>(model);
        auto *channelItem = channelModel->item(i);
        if(channelItem) {
            channelItem->setEnabled(!channelsInUse.contains(i));
        }
    }
}

QString
Harddrives::BusChannelName(uint8_t bus, uint8_t channel)
{
    QString busName;
    switch (bus) {
        case HDD_BUS_DISABLED:
            busName = QString(QObject::tr("Disabled"));
            break;
        case HDD_BUS_MFM:
            busName = QString("MFM/RLL (%1:%2)").arg(channel >> 1).arg(channel & 1);
            break;
        case HDD_BUS_XTA:
            busName = QString("XTA (%1:%2)").arg(channel >> 1).arg(channel & 1);
            break;
        case HDD_BUS_ESDI:
            busName = QString("ESDI (%1:%2)").arg(channel >> 1).arg(channel & 1);
            break;
        case HDD_BUS_IDE:
            busName = QString("IDE (%1:%2)").arg(channel >> 1).arg(channel & 1);
            break;
        case HDD_BUS_ATAPI:
            busName = QString("ATAPI (%1:%2)").arg(channel >> 1).arg(channel & 1);
            break;
        case HDD_BUS_SCSI:
            busName = QString("SCSI (%1:%2)").arg(channel >> 4).arg(channel & 15, 2, 10, QChar('0'));
            break;
        case CDROM_BUS_MITSUMI:
            busName = QString("Mitsumi");
            break;
        case CDROM_BUS_MKE:
            busName = QString("Panasonic/MKE (%1:%2)").arg(channel >> 2).arg(channel & 3);
            break;
    }

    return busName;
}
