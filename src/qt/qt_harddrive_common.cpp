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
#include <algorithm>
#include <cstdint>

#include "qt_settings_completer.hpp"
#include "qt_harddrive_common.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/hdd.h>
#include <86box/device.h>
#include <86box/scsi.h>
#include <86box/cdrom.h>
#include <86box/scsi_device.h>
#include <86box/scsi_tape.h>
#include <86box/lpt.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/sound.h>
#include <86box/machine.h>
}

#include "qt_settings.hpp"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QComboBox>
#include <QScrollBar>
#include <QTimer>
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
    bus_owner_t    owners[(IDE_BUS_MAX > SCSI_BUS_MAX) ? IDE_BUS_MAX : SCSI_BUS_MAX];
    ide_conflict_t conflicts[IDE_CONFLICTS_MAX];
    int            conflictCount = 0;
    int            owned         = 0;
    const bool  is_ide  = (bus == HDD_BUS_IDE) || (bus == HDD_BUS_ATAPI);
    const bool  is_scsi = (bus == HDD_BUS_SCSI);
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
            owned   = idePlan(owners, conflicts, &conflictCount);
            busRows = owned * 2;
            busesToCheck.append(HDD_BUS_ATAPI);
            busesToCheck.append(HDD_BUS_IDE);
            break;
        case HDD_BUS_ATAPI:
            owned   = idePlan(owners, conflicts, &conflictCount);
            busRows = owned * 2;
            busesToCheck.append(HDD_BUS_IDE);
            busesToCheck.append(HDD_BUS_ATAPI);
            break;
        case HDD_BUS_SCSI:
            shifter         = 4;
            orer            = 15;
            /* The buses something has, at least the first. */
            owned           = scsiPlan(owners);
            busRows         = ((owned > 0) ? owned : 1) * SCSI_ID_MAX;
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
    } else if (is_scsi) {
        for (const int channel : channelsInUse) {
            if ((channel < (SCSI_BUS_MAX * SCSI_ID_MAX)) && (channel >= busRows))
                busRows = (channel | 15) + 1;
        }
    }

    model->insertRows(0, busRows);
    for (int i = 0; i < busRows; ++i) {
        auto idx = model->index(i, 0);
        if (bus == TAPE_BUS_LPT)
            model->setData(idx, QString("LPT%1").arg(i + 1));
        else if (is_ide)
            model->setData(idx, QString("%1:%2 %3").arg(i >> 1).arg(i & 1).arg(ownerName(i >> 1, owners, owned, conflicts, conflictCount)));
        else if (is_scsi)
            model->setData(idx, QString("%1:%2 %3").arg(i >> 4).arg(i & 15, 2, 10, QChar('0')).arg(ownerName(i >> 4, owners, owned)));
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
            if (is_scsi && (((i >> 4) >= owned) || (!owners[i >> 4].onboard && (owners[i >> 4].device == nullptr))))
                enabled = false;
            channelItem->setEnabled(enabled);
        }
    }

    /* Then, not to be picked, each controller left without its channels. */
    if (is_ide) {
        auto *standard = qobject_cast<QStandardItemModel *>(model);

        for (int i = 0; (standard != nullptr) && (i < conflictCount); i++) {
            auto *item = new QStandardItem(conflictText(i, owners, owned, conflicts, conflictCount));

            item->setData(CHANNEL_NONE, Qt::UserRole);
            item->setEnabled(false);
            standard->appendRow(item);
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
            bus_owner_t owners[IDE_BUS_MAX];
            const int   owned = idePlan(owners);

            busName = QString("%1 %2:%3 %4").arg((bus == HDD_BUS_IDE) ? "IDE" : "ATAPI")
                          .arg(channel >> 1).arg(channel & 1)
                          .arg(ownerName(channel >> 1, owners, owned));
            break;
        }
        case HDD_BUS_SCSI: {
            bus_owner_t owners[SCSI_BUS_MAX];
            const int   owned = scsiPlan(owners);

            busName = QString("SCSI %1:%2 %3").arg(channel >> 4).arg(channel & 15, 2, 10, QChar('0'))
                          .arg(ownerName(channel >> 4, owners, owned));
            break;
        }
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
Harddrives::idePlan(bus_owner_t *owners, ide_conflict_t *conflicts, int *conflictCount)
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

    return ide_plan(owners, mach, hdc, snd, conflicts, conflictCount);
}

/* The owner of each SCSI bus for what the settings hold now, as for IDE.
   Returns the number of buses taken. */
int
Harddrives::scsiPlan(bus_owner_t *owners)
{
    int mach = machine;
    int snd[SOUND_CARD_MAX];
    int scsi[SCSI_CARD_MAX];

    for (int i = 0; i < SOUND_CARD_MAX; i++)
        snd[i] = sound_card_current[i];
    for (int i = 0; i < SCSI_CARD_MAX; i++)
        scsi[i] = scsi_card_current[i];

    if (Settings::settings != nullptr) {
        mach = Settings::settings->currentMachine();
        for (int i = 0; i < SOUND_CARD_MAX; i++)
            snd[i] = Settings::settings->currentSoundCard(i);
        for (int i = 0; i < SCSI_CARD_MAX; i++)
            scsi[i] = Settings::settings->currentScsiCard(i);
    }

    return scsi_plan(owners, mach, snd, scsi);
}

/* A device's label: its short name, with "(Onboard)" for a chip on the
   machine's board; where there are two or more of a card, among the
   owners and those left without boards, each is numbered by its slot
   order, #1 first. */
static QString
deviceLabel(const device_t *dev, int instance, int onboard, const bus_owner_t *owners, int count,
            const ide_conflict_t *conflicts, int conflictCount)
{
    const QString name = QString::fromUtf8((dev->short_name != nullptr) ? dev->short_name : dev->name);

    if (onboard)
        return QObject::tr("%1 (Onboard)").arg(name);

    QList<int> instances;
    for (int i = 0; i < count; i++) {
        if ((owners[i].device == dev) && !instances.contains(owners[i].instance))
            instances.append(owners[i].instance);
    }
    for (int i = 0; i < conflictCount; i++) {
        if ((conflicts[i].device == dev) && !instances.contains(conflicts[i].instance))
            instances.append(conflicts[i].instance);
    }
    if (instances.size() > 1) {
        std::sort(instances.begin(), instances.end());
        return QString("%1 #%2").arg(name).arg(instances.indexOf(instance) + 1);
    }

    return name;
}

/* The label for an IDE board or SCSI bus, of the count given: the owner's
   label, or "Onboard" for the chipset's own IDE. */
QString
Harddrives::ownerName(int bus, const bus_owner_t *owners, int count, const ide_conflict_t *conflicts, int conflictCount)
{
    if ((bus < 0) || (bus >= count))
        return QObject::tr("(none)");

    const bus_owner_t &owner = owners[bus];

    if (owner.device == nullptr)
        return owner.onboard ? QObject::tr("Onboard") : QObject::tr("(none)");

    return deviceLabel(owner.device, owner.instance, owner.onboard, owners, count, conflicts, conflictCount);
}

/* What a controller left without its IDE channels says: the boards
   another device has, or that no pair was free. */
QString
Harddrives::conflictText(int index, const bus_owner_t *owners, int count, const ide_conflict_t *conflicts, int conflictCount)
{
    const ide_conflict_t &c     = conflicts[index];
    const QString         label = deviceLabel(c.device, c.instance, c.onboard, owners, count, conflicts, conflictCount);

    if (c.lost == 0)
        return QObject::tr("%1: no free channels").arg(label);

    QStringList boards;
    for (int board = 0; board < IDE_BUS_MAX; board++) {
        if (c.lost & (1 << board))
            boards.append(QString::number(board));
    }

    return QObject::tr("%1: channels %2 in use").arg(label, boards.join(", "));
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

/* A channel box keeps its width on the page, and its list opens as wide as
   its longest entry: the owners' names are not cut short. */
void
Harddrives::widenPopup(QComboBox *cbox)
{
    auto *view  = cbox->view();
    auto *timer = new QTimer(cbox);

    view->setTextElideMode(Qt::ElideNone);
    timer->setSingleShot(true);
    timer->setInterval(0);
    QObject::connect(timer, &QTimer::timeout, cbox, [view]() {
        view->setMinimumWidth(view->sizeHintForColumn(0) + view->verticalScrollBar()->sizeHint().width() + (2 * view->frameWidth()));
    });

    /* Once the list has changed, however many rows went in. */
    const auto *model = cbox->model();
    QObject::connect(model, &QAbstractItemModel::rowsInserted, timer, qOverload<>(&QTimer::start));
    QObject::connect(model, &QAbstractItemModel::dataChanged, timer, qOverload<>(&QTimer::start));
    QObject::connect(model, &QAbstractItemModel::modelReset, timer, qOverload<>(&QTimer::start));
}
