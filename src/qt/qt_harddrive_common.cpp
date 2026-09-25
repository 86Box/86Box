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
#include <cstdint>

#include "qt_settings_completer.hpp"
#include "qt_harddrive_common.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/hdd.h>
#include <86box/scsi.h>
#include <86box/cdrom.h>
#include <86box/scsi_device.h>
#include <86box/scsi_tape.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/sound.h>
#include <86box/machine.h>
}

#include "qt_settings.hpp"

#include <QAbstractItemModel>
#include <QStandardItemModel>

void
Harddrives::populateBuses(QAbstractItemModel *model)
{
    model->removeRows(0, model->rowCount());
    model->insertRows(0, 6);

    model->setData(model->index(0, 0), "ST-506/ST-412 (MFM/RLL)");
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
    model->insertRows(0, 8);

    model->setData(model->index(0, 0), QObject::tr("Disabled"));
    model->setData(model->index(1, 0), "ATAPI");
    model->setData(model->index(2, 0), "SCSI");
    model->setData(model->index(3, 0), "Mitsumi");
    model->setData(model->index(4, 0), "Panasonic/MKE");
    model->setData(model->index(5, 0), "LPT");
    model->setData(model->index(6, 0), "Hitachi");
    model->setData(model->index(7, 0), "Philips/LMS");

    model->setData(model->index(0, 0), HDD_BUS_DISABLED, Qt::UserRole);
    model->setData(model->index(1, 0), HDD_BUS_ATAPI, Qt::UserRole);
    model->setData(model->index(2, 0), HDD_BUS_SCSI, Qt::UserRole);
    model->setData(model->index(3, 0), CDROM_BUS_MITSUMI, Qt::UserRole);
    model->setData(model->index(4, 0), CDROM_BUS_MKE, Qt::UserRole);
    model->setData(model->index(5, 0), CDROM_BUS_LPT, Qt::UserRole);
    model->setData(model->index(6, 0), CDROM_BUS_HITACHI, Qt::UserRole);
    model->setData(model->index(7, 0), CDROM_BUS_PHILIPS, Qt::UserRole);
}

void
Harddrives::populateRemovableBuses(QAbstractItemModel *model)
{
    model->removeRows(0, model->rowCount());
    model->insertRows(0, 4);

    model->setData(model->index(0, 0), QObject::tr("Disabled"));
    model->setData(model->index(1, 0), "ATAPI");
    model->setData(model->index(2, 0), "SCSI");
    model->setData(model->index(3, 0), "LPT");

    model->setData(model->index(0, 0), HDD_BUS_DISABLED, Qt::UserRole);
    model->setData(model->index(1, 0), HDD_BUS_ATAPI, Qt::UserRole);
    model->setData(model->index(2, 0), HDD_BUS_SCSI, Qt::UserRole);
    model->setData(model->index(3, 0), HDD_BUS_LPT, Qt::UserRole);
}

void
Harddrives::populateSpeeds(QAbstractItemModel *model, SettingsCompleter *sc, int bus)
{
    int num_preset;

    sc->removeRows();

    switch (bus) {
        case HDD_BUS_MFM:
        case HDD_BUS_XTA:
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

        sc->addDevice(nullptr, QObject::tr(hdd_preset_getname(i)));
    }
}

void
Harddrives::populateBusChannels(QAbstractItemModel *model, int bus, SettingsBusTracking *sbt)
{
    model->removeRows(0, model->rowCount());

    int        busRows         = 0;
    int        shifter         = 1;
    int        orer            = 1;
    int        subChannelWidth = 1;
    QList<int> busesToCheck;
    QList<int> channelsInUse;
    ide_owner_t owners[IDE_BUS_MAX];
    const bool is_ide = (bus == HDD_BUS_IDE) || (bus == HDD_BUS_ATAPI);
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
            busRows = idePlan(owners) * 2;
            busesToCheck.append(HDD_BUS_ATAPI);
            busesToCheck.append(HDD_BUS_IDE);
            break;
        case HDD_BUS_ATAPI:
            busRows = idePlan(owners) * 2;
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
        case CDROM_BUS_HITACHI:
            shifter = 2;
            orer    = 3;
            busRows = 4;
            busesToCheck.append(CDROM_BUS_HITACHI);
            break;
        case CDROM_BUS_MKE:
            shifter = 2;
            orer    = 3;
            busRows = 4;
            busesToCheck.append(CDROM_BUS_MKE);
            break;
        case TAPE_BUS_FDC:
            busRows = 4;
            busesToCheck.append(TAPE_BUS_FDC);
            break;
        case TAPE_BUS_LPT:
            shifter = 0;
            orer    = 0;
            busRows = 4;
            busesToCheck.append(TAPE_BUS_LPT);
            break;
        default:
            break;
    }
    if (sbt != nullptr && !busesToCheck.empty()) {
        for (auto const &checkBus : busesToCheck) {
            channelsInUse.append(sbt->busChannelsInUse(checkBus));
        }
    }

    /* A drive left on a board with no controller any more still has its
       channel listed, so that it shows where it is. */
    if (is_ide) {
        for (const int channel : channelsInUse) {
            if ((channel < IDE_DRIVES_MAX) && (channel >= busRows))
                busRows = (channel | 1) + 1;
        }
    }

    model->insertRows(0, busRows);
    for (int i = 0; i < busRows; ++i) {
        auto idx = model->index(i, 0);
        if (bus == TAPE_BUS_LPT)
            model->setData(idx, QString("LPT%1").arg(i + 1));
        else if (is_ide)
            model->setData(idx, QString("%1:%2 %3").arg(i >> 1).arg(i & 1).arg(ideOwnerName(i >> 1, owners)));
        else
            model->setData(idx, QString("%1:%2").arg(i >> shifter).arg(i & orer, subChannelWidth, 10, QChar('0')));
        model->setData(idx, ((i >> shifter) << shifter) | (i & orer), Qt::UserRole);
        const auto *channelModel = qobject_cast<QStandardItemModel *>(model);
        auto       *channelItem  = channelModel->item(i);
        if (channelItem) {
            bool enabled = !channelsInUse.contains(i);
            if ((bus == TAPE_BUS_LPT) && (i < PARALLEL_MAX) && !lpt_ports[i].enabled)
                enabled = false;
            if (is_ide && !owners[i >> 1].onboard && (owners[i >> 1].device == nullptr))
                enabled = false;
            channelItem->setEnabled(enabled);
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
        case HDD_BUS_ATAPI: {
            ide_owner_t owners[IDE_BUS_MAX];

            idePlan(owners);
            busName = QString("%1 %2:%3 %4").arg((bus == HDD_BUS_IDE) ? "IDE" : "ATAPI")
                          .arg(channel >> 1).arg(channel & 1)
                          .arg(((channel >> 1) < IDE_BUS_MAX) ? ideOwnerName(channel >> 1, owners) : QObject::tr("(none)"));
            break;
        }
        case HDD_BUS_SCSI:
            busName = QString("SCSI (%1:%2)").arg(channel >> 4).arg(channel & 15, 2, 10, QChar('0'));
            break;
        case CDROM_BUS_PHILIPS:
            busName = QString("Philips/LMS");
            break;
        case CDROM_BUS_MITSUMI:
            busName = QString("Mitsumi");
            break;
        case CDROM_BUS_HITACHI:
            busName = QString("Hitachi (%1:%2)").arg(channel >> 2).arg(channel & 3);
            break;
        case CDROM_BUS_MKE:
            busName = QString("Panasonic/MKE (%1:%2)").arg(channel >> 2).arg(channel & 3);
            break;
        case TAPE_BUS_FDC:
            busName = QString("FDC (%1:%2)").arg(channel >> 1).arg(channel & 1);
            break;
        case TAPE_BUS_LPT:
            busName = QString("LPT%1").arg(channel + 1);
            break;
    }

    return busName;
}

/* The owner of each IDE board for what the settings hold now: the machine,
   disk controllers and sound cards selected, or those saved for a page not
   opened yet. Returns the number of boards to show. */
int
Harddrives::idePlan(ide_owner_t *owners)
{
    int mach = machine;
    int hdc[HDC_MAX];
    int snd[SOUND_CARD_MAX];

    for (int i = 0; i < HDC_MAX; i++)
        hdc[i] = hdc_current[i];
    for (int i = 0; i < SOUND_CARD_MAX; i++)
        snd[i] = sound_card_current[i];

    if (Settings::settings != nullptr) {
        mach = Settings::settings->currentMachine();
        for (int i = 0; i < HDC_MAX; i++)
            hdc[i] = Settings::settings->currentHdc(i);
        for (int i = 0; i < SOUND_CARD_MAX; i++)
            snd[i] = Settings::settings->currentSoundCard(i);
    }

    return ide_plan(owners, mach, hdc, snd);
}

/* The label for an IDE board: "Onboard" for the chipset's own IDE, with the
   chip's short name for one on the board, a card's short name otherwise,
   numbered where two of the same card have boards. */
QString
Harddrives::ideOwnerName(int board, const ide_owner_t *owners)
{
    const ide_owner_t &owner = owners[board];

    if (!owner.onboard && (owner.device == nullptr))
        return QObject::tr("(none)");

    QString name;
    if (owner.device != nullptr)
        name = QString::fromUtf8((owner.device->short_name != nullptr) ? owner.device->short_name : owner.device->name);

    if (owner.onboard)
        return name.isEmpty() ? QObject::tr("Onboard") : QObject::tr("Onboard %1").arg(name);

    for (int i = 0; i < IDE_BUS_MAX; i++) {
        if ((owners[i].device == owner.device) && (owners[i].instance != owner.instance))
            return QString("%1 #%2").arg(name).arg(owner.instance);
    }

    return name;
}

/* Name each drive's bus and channel again, from the bus and channel kept
   with it in column 0: who has an IDE channel may have changed. */
void
Harddrives::refreshBusNames(QAbstractItemModel *model)
{
    for (int row = 0; row < model->rowCount(); row++) {
        const auto idx = model->index(row, 0);

        model->setData(idx, BusChannelName(idx.data(Qt::UserRole).toUInt(), idx.data(Qt::UserRole + 1).toUInt()));
    }
}
