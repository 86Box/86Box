#include "qt_harddrive_common.hpp"

#include <cstdint>

extern "C" {
#include <86box/hdd.h>
}

#include <QAbstractItemModel>

void Harddrives::populateBuses(QAbstractItemModel *model) {
    model->removeRows(0, model->rowCount());
    model->insertRows(0, 6);
    model->setData(model->index(0, 0), "MFM/RLL");
    model->setData(model->index(1, 0), "XT IDE");
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

void Harddrives::populateRemovableBuses(QAbstractItemModel *model) {
    model->removeRows(0, model->rowCount());
    model->insertRows(0, 3);
    model->setData(model->index(0, 0), QObject::tr("Disabled"));
    model->setData(model->index(1, 0), QObject::tr("ATAPI"));
    model->setData(model->index(2, 0), QObject::tr("SCSI"));

    model->setData(model->index(0, 0), HDD_BUS_DISABLED, Qt::UserRole);
    model->setData(model->index(1, 0), HDD_BUS_ATAPI, Qt::UserRole);
    model->setData(model->index(2, 0), HDD_BUS_SCSI, Qt::UserRole);
}

void Harddrives::populateBusChannels(QAbstractItemModel *model, int bus) {
    model->removeRows(0, model->rowCount());

    int busRows = 0;
    int shifter = 1;
    int orer = 1;
    int subChannelWidth = 1;
    switch (bus) {
    case HDD_BUS_MFM:
    case HDD_BUS_XTA:
    case HDD_BUS_ESDI:
        busRows = 2;
        break;
    case HDD_BUS_IDE:
    case HDD_BUS_ATAPI:
        busRows = 8;
        break;
    case HDD_BUS_SCSI:
        shifter = 4;
        orer = 15;
        busRows = 64;
        subChannelWidth = 2;
        break;
    }

    model->insertRows(0, busRows);
    for (int i = 0; i < busRows; ++i) {
        auto idx = model->index(i, 0);
        model->setData(idx, QString("%1:%2").arg(i >> shifter).arg(i & orer, subChannelWidth, 10, QChar('0')));
        model->setData(idx, ((i >> shifter) << shifter) | (i & orer), Qt::UserRole);
    }
}

QString Harddrives::BusChannelName(uint8_t bus, uint8_t channel) {
    QString busName;
    switch(bus) {
    case HDD_BUS_DISABLED:
        busName = QString(QObject::tr("Disabled"));
        break;
    case HDD_BUS_MFM:
        busName = QString("MFM/RLL (%1:%2)").arg(channel >> 1).arg(channel & 1);
        break;
    case HDD_BUS_XTA:
        busName = QString("XT IDE (%1:%2)").arg(channel >> 1).arg(channel & 1);
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
    }

    return busName;
}
