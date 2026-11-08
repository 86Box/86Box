/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Other removable devices configuration UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021 Joakim L. Gilje
 */
#include <cstdint>
#include <cstdio>

extern "C" {
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/scsi_device.h>
#include <86box/mo.h>
#include <86box/hdd.h>
#include <86box/rdisk.h>
#include <86box/scsi_tape.h>
}

#include "qt_settings_completer.hpp"

#include "qt_settingsotherremovable.hpp"
#include "ui_qt_settingsotherremovable.h"

#include "qt_models_common.hpp"
#include "qt_harddrive_common.hpp"
#include "qt_settings_bus_tracking.hpp"
#include "qt_preferences.hpp"
#include "qt_defs.hpp"

static QString
moDriveTypeName(int i)
{
    return QString("%1 %2 %3").arg(mo_drive_types[i].vendor, mo_drive_types[i].model, mo_drive_types[i].revision);
}

static QString
rdiskDriveTypeName(int i)
{
    return QString("%1 %2 %3").arg(rdisk_drive_types[i].vendor, rdisk_drive_types[i].model, rdisk_drive_types[i].revision);
}

static QString
tapeDriveTypeName(int i)
{
    return QString("%1 %2 %3").arg(tape_drive_types[i].vendor, tape_drive_types[i].model, tape_drive_types[i].revision);
}

static bool
tapeTypeBusCompatible(int bus, uint32_t type)
{
    if (type >= KNOWN_TAPE_DRIVE_TYPES)
        return false;

    switch (tape_drive_types[type].drive_type) {
        case TAPE_DRIVE_TYPE_SCSI_ATAPI:
            return (bus == TAPE_BUS_ATAPI) || (bus == TAPE_BUS_SCSI);
        case TAPE_DRIVE_TYPE_FDC:
            return (bus == TAPE_BUS_FDC);
        case TAPE_DRIVE_TYPE_LPT:
            return (bus == TAPE_BUS_LPT);
        default:
            return false;
    }
}

static int
firstCompatibleTapeType(int bus)
{
    for (uint32_t i = 0; i < KNOWN_TAPE_DRIVE_TYPES; i++) {
        if (tapeTypeBusCompatible(bus, i))
            return (int) i;
    }

    return 0;
}

/* Whether a removable disk drive type belongs on a bus, from its drive_type in rdisk.h. */
static bool
rdiskTypeBusCompatible(int bus, uint32_t type)
{
    if (type >= KNOWN_RDISK_DRIVE_TYPES)
        return false;

    const int drive_type = rdisk_drive_types[type].drive_type;

    /* On bus LPT an ATAPI drive sits behind one of the IDE bridges. */
    if ((bus == RDISK_BUS_LPT) && rdisk_drive_type_supports_bus(drive_type, RDISK_BUS_ATAPI))
        return true;

    return rdisk_drive_type_supports_bus(drive_type, (uint8_t) bus) != 0;
}

static int
firstCompatibleRDiskType(int bus)
{
    for (uint32_t i = 0; i < KNOWN_RDISK_DRIVE_TYPES; i++) {
        if (rdiskTypeBusCompatible(bus, i))
            return (int) i;
    }

    return 0;
}

/* On bus LPT the list's channel is the port and, above it, the bridge. */
static bool
rdiskOwnChip(uint32_t type)
{
    return (type == RDISK_TYPE_ZIP_100) || (type == RDISK_TYPE_ZIP_100_PLUS) || (type == RDISK_TYPE_ZIP_250);
}

static QString
rdiskChannelName(uint8_t bus, uint32_t type, uint8_t channel)
{
    if ((bus == RDISK_BUS_LPT) && (channel != CHANNEL_NONE)) {
        if (rdiskOwnChip(type))
            return QString("LPT%1").arg((channel & 3) + 1);
        return QString("LPT%1 (%2)").arg((channel & 3) + 1).arg(Harddrives::lptBridgeName(channel >> 2));
    }

    return Harddrives::BusChannelName(bus, channel);
}

/* A Zip drive on bus LPT has its own bridge chip: of each port only the
   Shuttle EPAT entry can be chosen, and a selected other bridge of that
   port becomes it. Returns whether the selection changed. */
static bool
restrictRDiskBridges(QComboBox *cbox, int bus, uint32_t type)
{
    if ((bus != RDISK_BUS_LPT) || !rdiskOwnChip(type))
        return false;

    auto *model = qobject_cast<QStandardItemModel *>(cbox->model());
    for (int row = 0; row < model->rowCount(); row++) {
        auto *item = model->item(row);
        if (item && ((item->data(Qt::UserRole).toUInt() >> 2) != HDD_LPT_EPAT))
            item->setEnabled(false);
    }

    if (cbox->currentIndex() < 0)
        return false;
    const uint32_t channel = cbox->currentData().toUInt();
    if ((channel >> 2) == HDD_LPT_EPAT)
        return false;
    const auto match = model->match(model->index(0, 0), Qt::UserRole, (channel & 3) | (HDD_LPT_EPAT << 2));
    if (match.isEmpty())
        return false;
    cbox->setCurrentIndex(match.first().row());
    return true;
}

/* What the bus tracking counts: on bus LPT only the port. */
static int
rdiskTrackChannel(int bus, int channel)
{
    return (bus == RDISK_BUS_LPT) ? (channel & 3) : channel;
}

static uint8_t
rdiskListChannel(const rdisk_drive_t *drv)
{
    return (drv->bus_type == RDISK_BUS_LPT) ? (uint8_t) (drv->res | (drv->lpt_bridge << 2)) : drv->res;
}

static uint8_t
nextFreeRDiskChannel(SettingsBusTracking *sbt, int bus)
{
    switch (bus) {
        case RDISK_BUS_IDE:
        case RDISK_BUS_ATAPI:
            return sbt->next_free_ide_channel();
        case RDISK_BUS_LPT: {
            /* The port's EPAT entry. */
            const uint8_t port = sbt->next_free_lpt_port();
            return (port == CHANNEL_NONE) ? port : (uint8_t) ((port * HDD_LPT_BRIDGES) + HDD_LPT_EPAT);
        }
        default:
            return sbt->next_free_scsi_id();
    }
}

QIcon
SettingsOtherRemovable::rdiskIconForType(uint32_t type) const
{
    switch (type) {
        case RDISK_TYPE_ZIP_100:
        case RDISK_TYPE_ZIP_100_PLUS:
        case RDISK_TYPE_ZIP_250:
        case RDISK_TYPE_ZIP_750:
            return zip_icon;
        case RDISK_TYPE_JAZ_1GB:
        case RDISK_TYPE_JAZ_2GB:
            return jaz_icon;
        case RDISK_TYPE_SYJET_1_5GB:
        case RDISK_TYPE_SYJET_S:
        case RDISK_TYPE_SPARQ_1GB:
        case RDISK_TYPE_SQ5200C:
        case RDISK_TYPE_SQ3105S:
        case RDISK_TYPE_EZ230S:
        case RDISK_TYPE_EZ135A:
        case RDISK_TYPE_EZ230A:
        case RDISK_TYPE_SQ3105A:
        case RDISK_TYPE_SQ3270A:
        case RDISK_TYPE_EZ135S:
        case RDISK_TYPE_SQ3270S:
        case RDISK_TYPE_SQ5110C:
        case RDISK_TYPE_SQ555:
            return syquest_icon;
        default:
            return rdisk_icon;
    }
}

void
SettingsOtherRemovable::setMOBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint8_t channel)
{
    QIcon icon;
    switch (bus) {
        case MO_BUS_DISABLED:
            icon = mo_disabled_icon;
            break;
        case MO_BUS_ATAPI:
        case MO_BUS_SCSI:
            icon = mo_icon;
            break;

        default:
            break;
    }

    auto i = idx.siblingAtColumn(0);
    model->setData(i, Harddrives::BusChannelName(bus, channel));
    model->setData(i, bus, Qt::UserRole);
    model->setData(i, channel, Qt::UserRole + 1);
    model->setData(i, icon, Qt::DecorationRole);
}

void
SettingsOtherRemovable::setRDiskBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint32_t type, uint8_t channel)
{
    QIcon icon;
    switch (bus) {
        case RDISK_BUS_DISABLED:
            icon = rdisk_disabled_icon;
            break;
        case RDISK_BUS_IDE:
        case RDISK_BUS_ATAPI:
        case RDISK_BUS_SCSI:
        case RDISK_BUS_LPT:
            icon = rdiskIconForType(type);
            break;

        default:
            break;
    }

    auto i = idx.siblingAtColumn(0);
    model->setData(i, rdiskChannelName(bus, type, channel));
    model->setData(i, bus, Qt::UserRole);
    model->setData(i, channel, Qt::UserRole + 1);
    model->setData(i, icon, Qt::DecorationRole);
}

void
SettingsOtherRemovable::setTapeBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint8_t channel)
{
    QIcon icon;
    switch (bus) {
        case TAPE_BUS_DISABLED:
            icon = tape_disabled_icon;
            break;
        case TAPE_BUS_ATAPI:
        case TAPE_BUS_SCSI:
        case TAPE_BUS_FDC:
        case TAPE_BUS_LPT:
            icon = tape_icon;
            break;

        default:
            break;
    }

    auto i = idx.siblingAtColumn(0);
    model->setData(i, Harddrives::BusChannelName(bus, channel));
    model->setData(i, bus, Qt::UserRole);
    model->setData(i, channel, Qt::UserRole + 1);
    model->setData(i, icon, Qt::DecorationRole);
}

static void
setTapeType(QAbstractItemModel *model, const QModelIndex &idx, uint32_t type)
{
    auto i = idx.siblingAtColumn(1);
    if (idx.siblingAtColumn(0).data(Qt::UserRole).toUInt() == TAPE_BUS_DISABLED)
        model->setData(i, QCoreApplication::translate("", "None"));
    else
        model->setData(i, tapeDriveTypeName(type));
    model->setData(i, type, Qt::UserRole);
}

static void
setMOType(QAbstractItemModel *model, const QModelIndex &idx, uint32_t type)
{
    auto i = idx.siblingAtColumn(1);
    if (idx.siblingAtColumn(0).data(Qt::UserRole).toUInt() == MO_BUS_DISABLED)
        model->setData(i, QCoreApplication::translate("", "None"));
    else
        model->setData(i, moDriveTypeName(type));
    model->setData(i, type, Qt::UserRole);
}

void
SettingsOtherRemovable::setRDiskType(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint32_t type)
{
    QIcon icon;
    switch (bus) {
        case RDISK_BUS_DISABLED:
            icon = rdisk_disabled_icon;
            break;
        case RDISK_BUS_IDE:
        case RDISK_BUS_ATAPI:
        case RDISK_BUS_SCSI:
        case RDISK_BUS_LPT:
            icon = rdiskIconForType(type);
            break;

        default:
            break;
    }

    auto i = idx.siblingAtColumn(0);
    model->setData(i, icon, Qt::DecorationRole);

    i = idx.siblingAtColumn(1);
    if (idx.siblingAtColumn(0).data(Qt::UserRole).toUInt() == RDISK_BUS_DISABLED)
        model->setData(i, QCoreApplication::translate("", "None"));
    else
        model->setData(i, rdiskDriveTypeName(type));
    model->setData(i, type, Qt::UserRole);
}

SettingsOtherRemovable::SettingsOtherRemovable(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsOtherRemovable)
{
    ui->setupUi(this);
    Harddrives::widenPopup(ui->comboBoxMOChannel);
    Harddrives::widenPopup(ui->comboBoxRDiskChannel);
    Harddrives::widenPopup(ui->comboBoxTapeChannel);

    scMOType                        = new SettingsCompleter(ui->comboBoxMOType, nullptr);
    scRDiskType                     = new SettingsCompleter(ui->comboBoxRDiskType, nullptr);

    scTapeType                      = new SettingsCompleter(ui->comboBoxTapeType, nullptr);

    mo_disabled_icon = QIcon(":/settings/qt/icons/mo_disabled.ico");
    mo_icon          = QIcon(":/settings/qt/icons/mo.ico");

    Harddrives::populateRemovableBuses(ui->comboBoxMOBus->model());
    ui->comboBoxMOBus->model()->removeRows(3, ui->comboBoxMOBus->model()->rowCount() - 3);
    auto *model = ui->comboBoxMOType->model();
    {
        Models::Batch typeRows(model);
        for (uint32_t i = 0; i < KNOWN_MO_DRIVE_TYPES; i++) {
            typeRows.add(moDriveTypeName(i), i);
            scMOType->addDevice(nullptr, moDriveTypeName(i));
        }
    }

    model = new QStandardItemModel(0, 2, this);
    ui->treeViewMO->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, tr("Bus"));
    model->setHeaderData(1, Qt::Horizontal, tr("Type"));
    model->insertRows(0, MO_NUM);
    for (int i = 0; i < MO_NUM; i++) {
        auto idx = model->index(i, 0);
        setMOBus(model, idx, mo_drives[i].bus_type, mo_drives[i].res);
        setMOType(model, idx.siblingAtColumn(1), mo_drives[i].type);
        Harddrives::busTrackClass->device_track(1, DEV_MO, mo_drives[i].bus_type, mo_drives[i].bus_type == MO_BUS_ATAPI ? mo_drives[i].ide_channel : mo_drives[i].scsi_device_id);
    }

    for (int i = 0; i < model->columnCount(); i++)
        ui->treeViewMO->resizeColumnToContents(i);

    connect(ui->treeViewMO->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &SettingsOtherRemovable::onMORowChanged);
    ui->treeViewMO->setCurrentIndex(model->index(0, 0));

    rdisk_disabled_icon = QIcon(":/settings/qt/icons/rdisk_disabled.ico");
    rdisk_icon          = QIcon(":/settings/qt/icons/rdisk.ico");
    zip_icon            = QIcon(":/settings/qt/icons/zip.ico");
    jaz_icon            = QIcon(":/settings/qt/icons/jaz.ico");
    syquest_icon        = QIcon(":/settings/qt/icons/syquest.ico");

    Harddrives::populateRemovableBuses(ui->comboBoxRDiskBus->model());
    if ((ui->comboBoxRDiskBus->model()->rowCount() - 4) > 0)
        ui->comboBoxRDiskBus->model()->removeRows(4, ui->comboBoxRDiskBus->model()->rowCount() - 4);
    {
        /* Native ATA cartridge drives (the SyQuest EZ135A and EZ230A). */
        auto *busModel = ui->comboBoxRDiskBus->model();
        int   row      = busModel->rowCount();
        busModel->insertRows(row, 1);
        busModel->setData(busModel->index(row, 0), "IDE");
        busModel->setData(busModel->index(row, 0), RDISK_BUS_IDE, Qt::UserRole);
    }
    model = ui->comboBoxRDiskType->model();
    {
        Models::Batch typeRows(model);
        for (uint32_t i = 0; i < KNOWN_RDISK_DRIVE_TYPES; i++) {
            typeRows.add(rdiskDriveTypeName(i), i);
            scRDiskType->addDevice(nullptr, rdiskDriveTypeName(i));
        }
    }

    model = new QStandardItemModel(0, 2, this);
    ui->treeViewRDisk->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, tr("Bus"));
    model->setHeaderData(1, Qt::Horizontal, tr("Type"));
    model->insertRows(0, RDISK_NUM);
    for (int i = 0; i < RDISK_NUM; i++) {
        auto idx = model->index(i, 0);
        setRDiskBus(model, idx, rdisk_drives[i].bus_type, rdisk_drives[i].type, rdiskListChannel(&rdisk_drives[i]));
        setRDiskType(model, idx.siblingAtColumn(1), rdisk_drives[i].bus_type, rdisk_drives[i].type);
        Harddrives::busTrackClass->device_track(1, DEV_RDISK, rdisk_drives[i].bus_type,
                                                ((rdisk_drives[i].bus_type == RDISK_BUS_ATAPI) || (rdisk_drives[i].bus_type == RDISK_BUS_IDE)) ?
                                                rdisk_drives[i].ide_channel : rdisk_drives[i].scsi_device_id);
    }


    for (int i = 0; i < model->columnCount(); i++)
        ui->treeViewRDisk->resizeColumnToContents(i);

    connect(ui->treeViewRDisk->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &SettingsOtherRemovable::onRDiskRowChanged);
    ui->treeViewRDisk->setCurrentIndex(model->index(0, 0));

    tape_disabled_icon = QIcon(":/settings/qt/icons/tape_disabled.ico");
    tape_icon          = QIcon(":/settings/qt/icons/tape.ico");

    Harddrives::populateRemovableBuses(ui->comboBoxTapeBus->model());
    {
        auto *busModel = ui->comboBoxTapeBus->model();
        int   row      = busModel->rowCount();
        busModel->insertRows(row, 1);
        busModel->setData(busModel->index(row, 0), "FDC");
        busModel->setData(busModel->index(row, 0), TAPE_BUS_FDC, Qt::UserRole);
    }
    model = ui->comboBoxTapeType->model();
    {
        Models::Batch typeRows(model);
        for (uint32_t i = 0; i < KNOWN_TAPE_DRIVE_TYPES; i++) {
            typeRows.add(tapeDriveTypeName(i), i);
            scTapeType->addDevice(nullptr, tapeDriveTypeName(i));
        }
    }

    model = new QStandardItemModel(0, 2, this);
    ui->treeViewTape->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, tr("Bus"));
    model->setHeaderData(1, Qt::Horizontal, tr("Type"));
    model->insertRows(0, TAPE_NUM);
    for (int i = 0; i < TAPE_NUM; i++) {
        auto idx = model->index(i, 0);
        setTapeBus(model, idx, tape_drives[i].bus_type, tape_drives[i].res);
        setTapeType(model, idx.siblingAtColumn(1), tape_drives[i].type);
        Harddrives::busTrackClass->device_track(1, DEV_TAPE, tape_drives[i].bus_type, tape_drives[i].res);
    }

    for (int i = 0; i < model->columnCount(); i++)
        ui->treeViewTape->resizeColumnToContents(i);

    connect(ui->treeViewTape->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &SettingsOtherRemovable::onTapeRowChanged);
    ui->treeViewTape->setCurrentIndex(model->index(0, 0));
}

SettingsOtherRemovable::~SettingsOtherRemovable()
{
    delete scTapeType;

    delete scMOType;
    delete scRDiskType;

    delete ui;
}

int
SettingsOtherRemovable::changed()
{
    int has_changed = 0;

    const auto *model = ui->treeViewMO->model();
    for (uint8_t i = 0; i < MO_NUM; i++) {
        has_changed |= (mo_drives[i].bus_type != model->index(i, 0).data(Qt::UserRole).toUInt());
        has_changed |= (mo_drives[i].res      != model->index(i, 0).data(Qt::UserRole + 1).toUInt());
        has_changed |= (mo_drives[i].type     != model->index(i, 1).data(Qt::UserRole).toUInt());
    }

    model = ui->treeViewRDisk->model();
    for (uint8_t i = 0; i < RDISK_NUM; i++) {
        has_changed |= (rdisk_drives[i].bus_type != model->index(i, 0).data(Qt::UserRole).toUInt());
        has_changed |= (rdiskListChannel(&rdisk_drives[i]) != model->index(i, 0).data(Qt::UserRole + 1).toUInt());
        has_changed |= (rdisk_drives[i].type     != model->index(i, 1).data(Qt::UserRole).toUInt());
    }

    model = ui->treeViewTape->model();
    for (uint8_t i = 0; i < TAPE_NUM; i++) {
        has_changed |= (tape_drives[i].bus_type != model->index(i, 0).data(Qt::UserRole).toUInt());
        has_changed |= (tape_drives[i].res      != model->index(i, 0).data(Qt::UserRole + 1).toUInt());
        has_changed |= (tape_drives[i].type     != model->index(i, 1).data(Qt::UserRole).toUInt());
    }

    return has_changed ? (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET) : 0;
}

void
SettingsOtherRemovable::restore()
{
}

void
SettingsOtherRemovable::save(int soft)
{
    if (soft)
        return;

    const auto *model = ui->treeViewMO->model();
    for (uint8_t i = 0; i < MO_NUM; i++) {
        mo_drives[i].fp       = NULL;
        mo_drives[i].priv     = NULL;
        mo_drives[i].bus_type = model->index(i, 0).data(Qt::UserRole).toUInt();
        mo_drives[i].res      = model->index(i, 0).data(Qt::UserRole + 1).toUInt();
        mo_drives[i].type     = model->index(i, 1).data(Qt::UserRole).toUInt();
    }

    model = ui->treeViewRDisk->model();
    for (uint8_t i = 0; i < RDISK_NUM; i++) {
        rdisk_drives[i].fp       = NULL;
        rdisk_drives[i].priv     = NULL;
        rdisk_drives[i].bus_type = model->index(i, 0).data(Qt::UserRole).toUInt();
        rdisk_drives[i].res      = model->index(i, 0).data(Qt::UserRole + 1).toUInt();
        rdisk_drives[i].type     = model->index(i, 1).data(Qt::UserRole).toUInt();
        rdisk_drives[i].lpt_bridge = HDD_LPT_EPAT;
        if (rdisk_drives[i].bus_type == RDISK_BUS_LPT) {
            if (!rdiskOwnChip(rdisk_drives[i].type))
                rdisk_drives[i].lpt_bridge = rdisk_drives[i].res >> 2;
            rdisk_drives[i].res &= 3;
        }
    }

    model = ui->treeViewTape->model();
    for (uint8_t i = 0; i < TAPE_NUM; i++) {
        tape_drives[i].fp       = NULL;
        tape_drives[i].priv     = NULL;
        tape_drives[i].bus_type = model->index(i, 0).data(Qt::UserRole).toUInt();
        tape_drives[i].res      = model->index(i, 0).data(Qt::UserRole + 1).toUInt();
        tape_drives[i].type     = model->index(i, 1).data(Qt::UserRole).toUInt();
    }
}

void
SettingsOtherRemovable::onMORowChanged(const QModelIndex &current)
{
    uint8_t bus     = current.siblingAtColumn(0).data(Qt::UserRole).toUInt();
    uint8_t channel = current.siblingAtColumn(0).data(Qt::UserRole + 1).toUInt();
    uint8_t type    = current.siblingAtColumn(1).data(Qt::UserRole).toUInt();

    ui->comboBoxMOBus->setCurrentIndex(-1);
    const auto *model = ui->comboBoxMOBus->model();
    auto        match = model->match(model->index(0, 0), Qt::UserRole, bus);
    if (!match.isEmpty())
        ui->comboBoxMOBus->setCurrentIndex(match.first().row());

    model = ui->comboBoxMOChannel->model();
    match = model->match(model->index(0, 0), Qt::UserRole, channel);
    if (!match.isEmpty())
        ui->comboBoxMOChannel->setCurrentIndex(match.first().row());
    ui->comboBoxMOType->setCurrentIndex(type);
    enableCurrentlySelectedChannel_MO();
}

void
SettingsOtherRemovable::onRDiskRowChanged(const QModelIndex &current)
{
    uint8_t bus     = current.siblingAtColumn(0).data(Qt::UserRole).toUInt();
    uint8_t channel = current.siblingAtColumn(0).data(Qt::UserRole + 1).toUInt();
    uint8_t type    = current.siblingAtColumn(1).data(Qt::UserRole).toUInt();

    ui->comboBoxRDiskBus->setCurrentIndex(-1);
    const auto *model = ui->comboBoxRDiskBus->model();
    auto        match = model->match(model->index(0, 0), Qt::UserRole, bus);
    if (!match.isEmpty())
        ui->comboBoxRDiskBus->setCurrentIndex(match.first().row());

    model = ui->comboBoxRDiskChannel->model();
    match = model->match(model->index(0, 0), Qt::UserRole, channel);
    if (!match.isEmpty())
        ui->comboBoxRDiskChannel->setCurrentIndex(match.first().row());
    ui->comboBoxRDiskType->setCurrentIndex(type);
    updateRDiskTypeCombo();
    enableCurrentlySelectedChannel_RDisk();
    restrictRDiskBridges(ui->comboBoxRDiskChannel, bus, type);
}

/* The machine, disk controllers and sound cards chosen on other pages
   decide who has each IDE channel: bring the names up to date. */
void
SettingsOtherRemovable::showEvent(QShowEvent *event)
{
    Harddrives::refreshBusNames(ui->treeViewMO->model());
    /* As Harddrives::refreshBusNames(), with the bridge of a drive on LPT. */
    auto *model = ui->treeViewRDisk->model();
    for (int row = 0; row < model->rowCount(); row++) {
        const auto idx = model->index(row, 0);

        model->setData(idx, rdiskChannelName(idx.data(Qt::UserRole).toUInt(), idx.siblingAtColumn(1).data(Qt::UserRole).toUInt(),
                                             idx.data(Qt::UserRole + 1).toUInt()));
    }
    Harddrives::refreshBusNames(ui->treeViewTape->model());
    reloadBusChannels_MO();
    reloadBusChannels_RDisk();
    reloadBusChannels_Tape();
    QWidget::showEvent(event);
}

void
SettingsOtherRemovable::reloadBusChannels_MO()
{
    auto selected = ui->comboBoxMOChannel->currentIndex();
    Harddrives::populateBusChannels(ui->comboBoxMOChannel->model(),
                                    ui->comboBoxMOBus->currentData().toInt(), Harddrives::busTrackClass);
    ui->comboBoxMOChannel->setCurrentIndex(selected);
    enableCurrentlySelectedChannel_MO();
}

void
SettingsOtherRemovable::reloadBusChannels_RDisk()
{
    auto selected = ui->comboBoxRDiskChannel->currentIndex();
    Harddrives::populateBusChannels(ui->comboBoxRDiskChannel->model(),
                                    ui->comboBoxRDiskBus->currentData().toInt(), Harddrives::busTrackClass,
                                    Harddrives::LptBridgesHdd);
    ui->comboBoxRDiskChannel->setCurrentIndex(selected);
    enableCurrentlySelectedChannel_RDisk();
    restrictRDiskBridges(ui->comboBoxRDiskChannel, ui->comboBoxRDiskBus->currentData().toInt(),
                         ui->comboBoxRDiskType->currentData().toUInt());
}

void
SettingsOtherRemovable::on_comboBoxMOBus_currentIndexChanged(int index)
{
    if (index >= 0) {
        int  bus     = ui->comboBoxMOBus->currentData().toInt();
        bool enabled = (bus != MO_BUS_DISABLED);
        ui->comboBoxMOChannel->setEnabled(enabled);
        ui->comboBoxMOType->setEnabled(enabled);
        Harddrives::populateBusChannels(ui->comboBoxMOChannel->model(), bus, Harddrives::busTrackClass);
    }
}

void
SettingsOtherRemovable::on_comboBoxRDiskBus_currentIndexChanged(int index)
{
    if (index >= 0) {
        int  bus     = ui->comboBoxRDiskBus->currentData().toInt();
        bool enabled = (bus != RDISK_BUS_DISABLED);
        ui->comboBoxRDiskChannel->setEnabled(enabled);
        ui->comboBoxRDiskType->setEnabled(enabled);
        Harddrives::populateBusChannels(ui->comboBoxRDiskChannel->model(), bus, Harddrives::busTrackClass,
                                        Harddrives::LptBridgesHdd);
        restrictRDiskBridges(ui->comboBoxRDiskChannel, bus, ui->comboBoxRDiskType->currentData().toUInt());
    }
    updateRDiskTypeCombo();
}

void
SettingsOtherRemovable::updateRDiskTypeCombo()
{
    int   bus   = ui->comboBoxRDiskBus->currentData().toInt();
    auto *model = qobject_cast<QStandardItemModel *>(ui->comboBoxRDiskType->model());

    for (int i = 0; i < model->rowCount(); i++) {
        auto *item = model->item(i);
        if (item)
            item->setEnabled(rdiskTypeBusCompatible(bus, item->data(Qt::UserRole).toUInt()));
    }
}

void
SettingsOtherRemovable::on_comboBoxMOBus_activated(int)
{
    auto i = ui->treeViewMO->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_MO, ui->treeViewMO->model()->data(i, Qt::UserRole).toInt(), ui->treeViewMO->model()->data(i, Qt::UserRole + 1).toInt());
    ui->comboBoxMOChannel->setCurrentIndex(ui->comboBoxMOBus->currentData().toUInt() == MO_BUS_ATAPI ? Harddrives::busTrackClass->next_free_ide_channel() : Harddrives::busTrackClass->next_free_scsi_id());
    ui->treeViewMO->model()->data(i, Qt::UserRole + 1);
    setMOBus(ui->treeViewMO->model(),
             ui->treeViewMO->selectionModel()->currentIndex(),
             ui->comboBoxMOBus->currentData().toUInt(),
             ui->comboBoxMOChannel->currentData().toUInt());
    setMOType(ui->treeViewMO->model(),
              ui->treeViewMO->selectionModel()->currentIndex(),
              ui->comboBoxMOType->currentData().toUInt());
    ui->treeViewMO->resizeColumnToContents(0);
    Harddrives::busTrackClass->device_track(1, DEV_MO, ui->treeViewMO->model()->data(i, Qt::UserRole).toInt(), ui->treeViewMO->model()->data(i, Qt::UserRole + 1).toInt());
    emit moChannelChanged();
}

void
SettingsOtherRemovable::on_comboBoxRDiskBus_activated(int)
{
    auto i = ui->treeViewRDisk->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_RDISK, ui->treeViewRDisk->model()->data(i, Qt::UserRole).toInt(), rdiskTrackChannel(ui->treeViewRDisk->model()->data(i, Qt::UserRole).toInt(), ui->treeViewRDisk->model()->data(i, Qt::UserRole + 1).toInt()));
    uint8_t next_free = nextFreeRDiskChannel(Harddrives::busTrackClass, ui->comboBoxRDiskBus->currentData().toInt());
    ui->comboBoxRDiskChannel->setCurrentIndex(next_free == CHANNEL_NONE ? -1 : next_free);
    ui->treeViewRDisk->model()->data(i, Qt::UserRole + 1);

    /* A drive type that does not belong on the new bus (its drive_type in rdisk.h) is replaced by
       the first one that does.  The type is left alone when the bus is disabled. */
    int      new_bus = ui->comboBoxRDiskBus->currentData().toInt();
    uint32_t type    = ui->comboBoxRDiskType->currentData().toUInt();
    if ((new_bus != RDISK_BUS_DISABLED) && !rdiskTypeBusCompatible(new_bus, type))
        type = firstCompatibleRDiskType(new_bus);
    ui->comboBoxRDiskType->setCurrentIndex(type);
    restrictRDiskBridges(ui->comboBoxRDiskChannel, new_bus, type);
    setRDiskBus(ui->treeViewRDisk->model(),
                ui->treeViewRDisk->selectionModel()->currentIndex(),
                ui->comboBoxRDiskBus->currentData().toUInt(),
                ui->comboBoxRDiskType->currentData().toUInt(),
                ui->comboBoxRDiskChannel->currentData().toUInt());
    setRDiskType(ui->treeViewRDisk->model(),
                 ui->treeViewRDisk->selectionModel()->currentIndex(),
                 ui->comboBoxRDiskBus->currentData().toUInt(),
                 ui->comboBoxRDiskType->currentData().toUInt());
    ui->treeViewRDisk->resizeColumnToContents(0);
    Harddrives::busTrackClass->device_track(1, DEV_RDISK, ui->treeViewRDisk->model()->data(i, Qt::UserRole).toInt(), rdiskTrackChannel(ui->treeViewRDisk->model()->data(i, Qt::UserRole).toInt(), ui->treeViewRDisk->model()->data(i, Qt::UserRole + 1).toInt()));
    emit rdiskChannelChanged();
}

void
SettingsOtherRemovable::enableCurrentlySelectedChannel_MO()
{
    const auto *item_model = qobject_cast<QStandardItemModel *>(ui->comboBoxMOChannel->model());
    const auto  index      = ui->comboBoxMOChannel->currentIndex();
    auto       *item       = item_model->item(index);
    if (item)
        item->setEnabled(true);
}

void
SettingsOtherRemovable::enableCurrentlySelectedChannel_RDisk()
{
    const auto *item_model = qobject_cast<QStandardItemModel *>(ui->comboBoxRDiskChannel->model());
    const auto  index      = ui->comboBoxRDiskChannel->currentIndex();
    auto       *item       = item_model->item(index);
    if (item)
        item->setEnabled(true);

    /* On bus LPT the drive may change its bridge and keep its port. */
    if ((index >= 0) && (ui->comboBoxRDiskBus->currentData().toInt() == RDISK_BUS_LPT)) {
        const int first = index - (index % HDD_LPT_BRIDGES);

        for (int i = first; i < (first + HDD_LPT_BRIDGES); i++) {
            auto *other = item_model->item(i);
            if (other && Harddrives::lptPortEnabled(first / HDD_LPT_BRIDGES))
                other->setEnabled(true);
        }
    }
}
void
SettingsOtherRemovable::on_comboBoxMOChannel_activated(int)
{
    auto i = ui->treeViewMO->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_MO, ui->treeViewMO->model()->data(i, Qt::UserRole).toInt(), ui->treeViewMO->model()->data(i, Qt::UserRole + 1).toInt());
    setMOBus(ui->treeViewMO->model(),
             ui->treeViewMO->selectionModel()->currentIndex(),
             ui->comboBoxMOBus->currentData().toUInt(),
             ui->comboBoxMOChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_MO, ui->treeViewMO->model()->data(i, Qt::UserRole).toInt(), ui->treeViewMO->model()->data(i, Qt::UserRole + 1).toInt());
    ui->treeViewMO->resizeColumnToContents(0);
    emit moChannelChanged();
}

void
SettingsOtherRemovable::on_comboBoxRDiskChannel_activated(int)
{
    auto i = ui->treeViewRDisk->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_RDISK, ui->treeViewRDisk->model()->data(i, Qt::UserRole).toInt(), rdiskTrackChannel(ui->treeViewRDisk->model()->data(i, Qt::UserRole).toInt(), ui->treeViewRDisk->model()->data(i, Qt::UserRole + 1).toInt()));
    setRDiskBus(ui->treeViewRDisk->model(),
                ui->treeViewRDisk->selectionModel()->currentIndex(),
                ui->comboBoxRDiskBus->currentData().toUInt(),
                ui->comboBoxRDiskType->currentData().toUInt(),
                ui->comboBoxRDiskChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_RDISK, ui->treeViewRDisk->model()->data(i, Qt::UserRole).toInt(),
                                            rdiskTrackChannel(ui->treeViewRDisk->model()->data(i, Qt::UserRole).toInt(),
                                                              ui->treeViewRDisk->model()->data(i, Qt::UserRole + 1).toInt()));
    ui->treeViewRDisk->resizeColumnToContents(0);
    emit rdiskChannelChanged();
}

void
SettingsOtherRemovable::on_comboBoxMOType_activated(int)
{
    setMOType(ui->treeViewMO->model(),
              ui->treeViewMO->selectionModel()->currentIndex(),
              ui->comboBoxMOType->currentData().toUInt());
    ui->treeViewMO->resizeColumnToContents(0);
}

void
SettingsOtherRemovable::on_comboBoxRDiskType_activated(int)
{
    setRDiskType(ui->treeViewRDisk->model(),
                 ui->treeViewRDisk->selectionModel()->currentIndex(),
                 ui->comboBoxRDiskBus->currentData().toUInt(),
                 ui->comboBoxRDiskType->currentData().toUInt());
    ui->treeViewRDisk->resizeColumnToContents(1);

    /* On bus LPT the type decides which bridges can be chosen, and how the
       list names the channel. */
    if (ui->comboBoxRDiskBus->currentData().toInt() == RDISK_BUS_LPT) {
        reloadBusChannels_RDisk();
        on_comboBoxRDiskChannel_activated(ui->comboBoxRDiskChannel->currentIndex());
    }
}

void
SettingsOtherRemovable::onTapeRowChanged(const QModelIndex &current)
{
    uint8_t bus     = current.siblingAtColumn(0).data(Qt::UserRole).toUInt();
    uint8_t channel = current.siblingAtColumn(0).data(Qt::UserRole + 1).toUInt();
    uint8_t type    = current.siblingAtColumn(1).data(Qt::UserRole).toUInt();

    ui->comboBoxTapeBus->setCurrentIndex(-1);
    const auto *model = ui->comboBoxTapeBus->model();
    auto        match = model->match(model->index(0, 0), Qt::UserRole, bus);
    if (!match.isEmpty())
        ui->comboBoxTapeBus->setCurrentIndex(match.first().row());

    model = ui->comboBoxTapeChannel->model();
    match = model->match(model->index(0, 0), Qt::UserRole, channel);
    if (!match.isEmpty())
        ui->comboBoxTapeChannel->setCurrentIndex(match.first().row());
    ui->comboBoxTapeType->setCurrentIndex(type);
    updateTapeTypeCombo();
    enableCurrentlySelectedChannel_Tape();
}

void
SettingsOtherRemovable::reloadBusChannels_Tape()
{
    auto selected = ui->comboBoxTapeChannel->currentIndex();
    Harddrives::populateBusChannels(ui->comboBoxTapeChannel->model(),
                                    ui->comboBoxTapeBus->currentData().toInt(), Harddrives::busTrackClass);
    ui->comboBoxTapeChannel->setCurrentIndex(selected);
    enableCurrentlySelectedChannel_Tape();
}

void
SettingsOtherRemovable::on_comboBoxTapeBus_currentIndexChanged(int index)
{
    if (index >= 0) {
        int  bus     = ui->comboBoxTapeBus->currentData().toInt();
        bool enabled = (bus != TAPE_BUS_DISABLED);
        ui->comboBoxTapeChannel->setEnabled(enabled);
        ui->comboBoxTapeType->setEnabled(enabled);
        Harddrives::populateBusChannels(ui->comboBoxTapeChannel->model(), bus, Harddrives::busTrackClass);
    }
    updateTapeTypeCombo();
}

void
SettingsOtherRemovable::updateTapeTypeCombo()
{
    int  bus = ui->comboBoxTapeBus->currentData().toInt();
    auto *model = qobject_cast<QStandardItemModel *>(ui->comboBoxTapeType->model());

    for (int i = 0; i < model->rowCount(); i++) {
        auto *item = model->item(i);
        if (item)
            item->setEnabled(tapeTypeBusCompatible(bus, item->data(Qt::UserRole).toUInt()));
    }
}

static uint8_t
nextFreeTapeChannel(SettingsBusTracking *sbt, int bus)
{
    switch (bus) {
        case TAPE_BUS_ATAPI:
            return sbt->next_free_ide_channel();
        case TAPE_BUS_SCSI:
            return sbt->next_free_scsi_id();
        case TAPE_BUS_FDC:
            return sbt->next_free_fdc_unit();
        case TAPE_BUS_LPT:
            return sbt->next_free_lpt_port();
        default:
            return 0;
    }
}

void
SettingsOtherRemovable::on_comboBoxTapeBus_activated(int)
{
    auto i = ui->treeViewTape->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_TAPE, ui->treeViewTape->model()->data(i, Qt::UserRole).toInt(), ui->treeViewTape->model()->data(i, Qt::UserRole + 1).toInt());
    uint8_t next_free = nextFreeTapeChannel(Harddrives::busTrackClass, ui->comboBoxTapeBus->currentData().toInt());
    ui->comboBoxTapeChannel->setCurrentIndex(next_free == CHANNEL_NONE ? -1 : next_free);
    ui->treeViewTape->model()->data(i, Qt::UserRole + 1);

    uint32_t type = ui->comboBoxTapeType->currentData().toUInt();
    if (!tapeTypeBusCompatible(ui->comboBoxTapeBus->currentData().toInt(), type))
        type = firstCompatibleTapeType(ui->comboBoxTapeBus->currentData().toInt());
    ui->comboBoxTapeType->setCurrentIndex(type);

    setTapeBus(ui->treeViewTape->model(),
               ui->treeViewTape->selectionModel()->currentIndex(),
               ui->comboBoxTapeBus->currentData().toUInt(),
               ui->comboBoxTapeChannel->currentData().toUInt());
    setTapeType(ui->treeViewTape->model(),
                ui->treeViewTape->selectionModel()->currentIndex(),
                ui->comboBoxTapeType->currentData().toUInt());
    ui->treeViewTape->resizeColumnToContents(0);
    Harddrives::busTrackClass->device_track(1, DEV_TAPE, ui->treeViewTape->model()->data(i, Qt::UserRole).toInt(), ui->treeViewTape->model()->data(i, Qt::UserRole + 1).toInt());
    emit tapeChannelChanged();
}

void
SettingsOtherRemovable::enableCurrentlySelectedChannel_Tape()
{
    const auto *item_model = qobject_cast<QStandardItemModel *>(ui->comboBoxTapeChannel->model());
    const auto  index      = ui->comboBoxTapeChannel->currentIndex();
    auto       *item       = item_model->item(index);
    if (item)
        item->setEnabled(true);
}

void
SettingsOtherRemovable::on_comboBoxTapeChannel_activated(int)
{
    auto i = ui->treeViewTape->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_TAPE, ui->treeViewTape->model()->data(i, Qt::UserRole).toInt(), ui->treeViewTape->model()->data(i, Qt::UserRole + 1).toInt());
    setTapeBus(ui->treeViewTape->model(),
               ui->treeViewTape->selectionModel()->currentIndex(),
               ui->comboBoxTapeBus->currentData().toUInt(),
               ui->comboBoxTapeChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_TAPE, ui->treeViewTape->model()->data(i, Qt::UserRole).toInt(), ui->treeViewTape->model()->data(i, Qt::UserRole + 1).toInt());
    ui->treeViewTape->resizeColumnToContents(0);
    emit tapeChannelChanged();
}

void
SettingsOtherRemovable::on_comboBoxTapeType_activated(int)
{
    setTapeType(ui->treeViewTape->model(),
                ui->treeViewTape->selectionModel()->currentIndex(),
                ui->comboBoxTapeType->currentData().toUInt());
    ui->treeViewTape->resizeColumnToContents(1);
}
