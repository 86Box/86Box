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
#include "qt_settingsotherremovable.hpp"
#include "ui_qt_settingsotherremovable.h"

extern "C" {
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/scsi_device.h>
#include <86box/mo.h>
#include <86box/rdisk.h>
#include <86box/scsi_tape.h>
}

#include "qt_models_common.hpp"
#include "qt_harddrive_common.hpp"
#include "qt_settings_bus_tracking.hpp"
#include "qt_progsettings.hpp"

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
        case RDISK_BUS_ATAPI:
        case RDISK_BUS_SCSI:
            icon = ((type == RDISK_TYPE_ZIP_100) || (type == RDISK_TYPE_ZIP_250)) ? zip_icon : rdisk_icon;
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
SettingsOtherRemovable::setTapeBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint8_t channel)
{
    QIcon icon;
    switch (bus) {
        case TAPE_BUS_DISABLED:
            icon = tape_disabled_icon;
            break;
        case TAPE_BUS_SCSI:
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
        case RDISK_BUS_ATAPI:
        case RDISK_BUS_SCSI:
            icon = ((type == RDISK_TYPE_ZIP_100) || (type == RDISK_TYPE_ZIP_250)) ? zip_icon : rdisk_icon;
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

    mo_disabled_icon = QIcon(":/settings/qt/icons/mo_disabled.ico");
    mo_icon          = QIcon(":/settings/qt/icons/mo.ico");

    Harddrives::populateRemovableBuses(ui->comboBoxMOBus->model());
    ui->comboBoxMOBus->model()->removeRows(3, ui->comboBoxMOBus->model()->rowCount() - 3);
    auto *model = ui->comboBoxMOType->model();
    for (uint32_t i = 0; i < KNOWN_MO_DRIVE_TYPES; i++) {
        Models::AddEntry(model, moDriveTypeName(i), i);
    }

    model = new QStandardItemModel(0, 2, this);
    ui->tableViewMO->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, tr("Bus"));
    model->setHeaderData(1, Qt::Horizontal, tr("Type"));
    model->insertRows(0, MO_NUM);
    for (int i = 0; i < MO_NUM; i++) {
        auto idx = model->index(i, 0);
        setMOBus(model, idx, mo_drives[i].bus_type, mo_drives[i].res);
        setMOType(model, idx.siblingAtColumn(1), mo_drives[i].type);
        Harddrives::busTrackClass->device_track(1, DEV_MO, mo_drives[i].bus_type, mo_drives[i].bus_type == MO_BUS_ATAPI ? mo_drives[i].ide_channel : mo_drives[i].scsi_device_id);
    }
    ui->tableViewMO->resizeColumnsToContents();
    ui->tableViewMO->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    connect(ui->tableViewMO->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &SettingsOtherRemovable::onMORowChanged);
    ui->tableViewMO->setCurrentIndex(model->index(0, 0));

    rdisk_disabled_icon = QIcon(":/settings/qt/icons/rdisk_disabled.ico");
    rdisk_icon          = QIcon(":/settings/qt/icons/rdisk.ico");
    zip_icon            = QIcon(":/settings/qt/icons/zip.ico");

    Harddrives::populateRemovableBuses(ui->comboBoxRDiskBus->model());
    if ((ui->comboBoxRDiskBus->model()->rowCount() - 3) > 0)
        ui->comboBoxRDiskBus->model()->removeRows(3, ui->comboBoxRDiskBus->model()->rowCount() - 3);
    model = ui->comboBoxRDiskType->model();
    for (uint32_t i = 0; i < KNOWN_RDISK_DRIVE_TYPES; i++) {
        Models::AddEntry(model, rdiskDriveTypeName(i), i);
    }

    model = new QStandardItemModel(0, 2, this);
    ui->tableViewRDisk->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, tr("Bus"));
    model->setHeaderData(1, Qt::Horizontal, tr("Type"));
    model->insertRows(0, RDISK_NUM);
    for (int i = 0; i < RDISK_NUM; i++) {
        auto idx = model->index(i, 0);
        setRDiskBus(model, idx, rdisk_drives[i].bus_type, rdisk_drives[i].type, rdisk_drives[i].res);
        setRDiskType(model, idx.siblingAtColumn(1), rdisk_drives[i].bus_type, rdisk_drives[i].type);
        Harddrives::busTrackClass->device_track(1, DEV_MO, rdisk_drives[i].bus_type, rdisk_drives[i].bus_type == RDISK_BUS_ATAPI ? rdisk_drives[i].ide_channel : rdisk_drives[i].scsi_device_id);
    }
    ui->tableViewRDisk->resizeColumnsToContents();
    ui->tableViewRDisk->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    connect(ui->tableViewRDisk->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &SettingsOtherRemovable::onRDiskRowChanged);
    ui->tableViewRDisk->setCurrentIndex(model->index(0, 0));

    tape_disabled_icon = QIcon(":/settings/qt/icons/tape_disabled.ico");
    tape_icon          = QIcon(":/settings/qt/icons/tape.ico");

    /* Tape drives are SCSI-only, so just Disabled and SCSI options */
    auto *tapeBusModel = ui->comboBoxTapeBus->model();
    tapeBusModel->removeRows(0, tapeBusModel->rowCount());
    tapeBusModel->insertRows(0, 2);
    tapeBusModel->setData(tapeBusModel->index(0, 0), tr("Disabled"));
    tapeBusModel->setData(tapeBusModel->index(1, 0), "SCSI");
    tapeBusModel->setData(tapeBusModel->index(0, 0), TAPE_BUS_DISABLED, Qt::UserRole);
    tapeBusModel->setData(tapeBusModel->index(1, 0), TAPE_BUS_SCSI, Qt::UserRole);

    model = ui->comboBoxTapeType->model();
    for (uint32_t i = 0; i < KNOWN_TAPE_DRIVE_TYPES; i++) {
        Models::AddEntry(model, tapeDriveTypeName(i), i);
    }

    model = new QStandardItemModel(0, 2, this);
    ui->tableViewTape->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, tr("Bus"));
    model->setHeaderData(1, Qt::Horizontal, tr("Type"));
    model->insertRows(0, TAPE_NUM);
    for (int i = 0; i < TAPE_NUM; i++) {
        auto idx = model->index(i, 0);
        setTapeBus(model, idx, tape_drives[i].bus_type, tape_drives[i].res);
        setTapeType(model, idx.siblingAtColumn(1), tape_drives[i].type);
        Harddrives::busTrackClass->device_track(1, DEV_TAPE, tape_drives[i].bus_type, tape_drives[i].scsi_device_id);
    }
    ui->tableViewTape->resizeColumnsToContents();
    ui->tableViewTape->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    connect(ui->tableViewTape->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &SettingsOtherRemovable::onTapeRowChanged);
    ui->tableViewTape->setCurrentIndex(model->index(0, 0));
}

SettingsOtherRemovable::~SettingsOtherRemovable()
{
    delete ui;
}

void
SettingsOtherRemovable::save()
{
    const auto *model = ui->tableViewMO->model();
    for (uint8_t i = 0; i < MO_NUM; i++) {
        mo_drives[i].fp       = NULL;
        mo_drives[i].priv     = NULL;
        mo_drives[i].bus_type = model->index(i, 0).data(Qt::UserRole).toUInt();
        mo_drives[i].res      = model->index(i, 0).data(Qt::UserRole + 1).toUInt();
        mo_drives[i].type     = model->index(i, 1).data(Qt::UserRole).toUInt();
    }

    model = ui->tableViewRDisk->model();
    for (uint8_t i = 0; i < RDISK_NUM; i++) {
        rdisk_drives[i].fp       = NULL;
        rdisk_drives[i].priv     = NULL;
        rdisk_drives[i].bus_type = model->index(i, 0).data(Qt::UserRole).toUInt();
        rdisk_drives[i].res      = model->index(i, 0).data(Qt::UserRole + 1).toUInt();
        rdisk_drives[i].type     = model->index(i, 1).data(Qt::UserRole).toUInt();
    }

    model = ui->tableViewTape->model();
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
    enableCurrentlySelectedChannel_RDisk();
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
                                    ui->comboBoxRDiskBus->currentData().toInt(), Harddrives::busTrackClass);
    ui->comboBoxRDiskChannel->setCurrentIndex(selected);
    enableCurrentlySelectedChannel_RDisk();
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
        Harddrives::populateBusChannels(ui->comboBoxRDiskChannel->model(), bus, Harddrives::busTrackClass);
    }
}

void
SettingsOtherRemovable::on_comboBoxMOBus_activated(int)
{
    auto i = ui->tableViewMO->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_MO, ui->tableViewMO->model()->data(i, Qt::UserRole).toInt(), ui->tableViewMO->model()->data(i, Qt::UserRole + 1).toInt());
    ui->comboBoxMOChannel->setCurrentIndex(ui->comboBoxMOBus->currentData().toUInt() == MO_BUS_ATAPI ? Harddrives::busTrackClass->next_free_ide_channel() : Harddrives::busTrackClass->next_free_scsi_id());
    ui->tableViewMO->model()->data(i, Qt::UserRole + 1);
    setMOBus(ui->tableViewMO->model(),
             ui->tableViewMO->selectionModel()->currentIndex(),
             ui->comboBoxMOBus->currentData().toUInt(),
             ui->comboBoxMOChannel->currentData().toUInt());
    setMOType(ui->tableViewMO->model(),
              ui->tableViewMO->selectionModel()->currentIndex(),
              ui->comboBoxMOType->currentData().toUInt());
    ui->tableViewMO->resizeColumnsToContents();
    ui->tableViewMO->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    Harddrives::busTrackClass->device_track(1, DEV_MO, ui->tableViewMO->model()->data(i, Qt::UserRole).toInt(), ui->tableViewMO->model()->data(i, Qt::UserRole + 1).toInt());
    emit moChannelChanged();
}

void
SettingsOtherRemovable::on_comboBoxRDiskBus_activated(int)
{
    auto i = ui->tableViewRDisk->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_RDISK, ui->tableViewRDisk->model()->data(i, Qt::UserRole).toInt(), ui->tableViewRDisk->model()->data(i, Qt::UserRole + 1).toInt());
    ui->comboBoxRDiskChannel->setCurrentIndex(ui->comboBoxRDiskBus->currentData().toUInt() == RDISK_BUS_ATAPI ? Harddrives::busTrackClass->next_free_ide_channel() : Harddrives::busTrackClass->next_free_scsi_id());
    ui->tableViewRDisk->model()->data(i, Qt::UserRole + 1);
    setRDiskBus(ui->tableViewRDisk->model(),
                ui->tableViewRDisk->selectionModel()->currentIndex(),
                ui->comboBoxRDiskBus->currentData().toUInt(),
                ui->comboBoxRDiskType->currentData().toUInt(),
                ui->comboBoxRDiskChannel->currentData().toUInt());
    setRDiskType(ui->tableViewRDisk->model(),
                 ui->tableViewRDisk->selectionModel()->currentIndex(),
                 ui->comboBoxRDiskBus->currentData().toUInt(),
                 ui->comboBoxRDiskType->currentData().toUInt());
    ui->tableViewRDisk->resizeColumnsToContents();
    ui->tableViewRDisk->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    Harddrives::busTrackClass->device_track(1, DEV_RDISK, ui->tableViewRDisk->model()->data(i, Qt::UserRole).toInt(), ui->tableViewRDisk->model()->data(i, Qt::UserRole + 1).toInt());
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
}
void
SettingsOtherRemovable::on_comboBoxMOChannel_activated(int)
{
    auto i = ui->tableViewMO->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_MO, ui->tableViewMO->model()->data(i, Qt::UserRole).toInt(), ui->tableViewMO->model()->data(i, Qt::UserRole + 1).toInt());
    setMOBus(ui->tableViewMO->model(),
             ui->tableViewMO->selectionModel()->currentIndex(),
             ui->comboBoxMOBus->currentData().toUInt(),
             ui->comboBoxMOChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_MO, ui->tableViewMO->model()->data(i, Qt::UserRole).toInt(), ui->tableViewMO->model()->data(i, Qt::UserRole + 1).toInt());
    emit moChannelChanged();
}

void
SettingsOtherRemovable::on_comboBoxRDiskChannel_activated(int)
{
    auto i = ui->tableViewRDisk->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_RDISK, ui->tableViewRDisk->model()->data(i, Qt::UserRole).toInt(), ui->tableViewRDisk->model()->data(i, Qt::UserRole + 1).toInt());
    setRDiskBus(ui->tableViewRDisk->model(),
                ui->tableViewRDisk->selectionModel()->currentIndex(),
                ui->comboBoxRDiskBus->currentData().toUInt(),
                ui->comboBoxRDiskType->currentData().toUInt(),
                ui->comboBoxRDiskChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_RDISK, ui->tableViewRDisk->model()->data(i, Qt::UserRole).toInt(),
                                            ui->tableViewRDisk->model()->data(i, Qt::UserRole + 1).toInt());
    emit rdiskChannelChanged();
}

void
SettingsOtherRemovable::on_comboBoxMOType_activated(int)
{
    setMOType(ui->tableViewMO->model(),
              ui->tableViewMO->selectionModel()->currentIndex(),
              ui->comboBoxMOType->currentData().toUInt());
    ui->tableViewMO->resizeColumnsToContents();
    ui->tableViewMO->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
}

void
SettingsOtherRemovable::on_comboBoxRDiskType_activated(int)
{
    setRDiskType(ui->tableViewRDisk->model(),
                 ui->tableViewRDisk->selectionModel()->currentIndex(),
                 ui->comboBoxRDiskBus->currentData().toUInt(),
                 ui->comboBoxRDiskType->currentData().toUInt());
    ui->tableViewRDisk->resizeColumnsToContents();
    ui->tableViewRDisk->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
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
}

void
SettingsOtherRemovable::on_comboBoxTapeBus_activated(int)
{
    auto i = ui->tableViewTape->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_TAPE, ui->tableViewTape->model()->data(i, Qt::UserRole).toInt(), ui->tableViewTape->model()->data(i, Qt::UserRole + 1).toInt());
    ui->comboBoxTapeChannel->setCurrentIndex(Harddrives::busTrackClass->next_free_scsi_id());
    ui->tableViewTape->model()->data(i, Qt::UserRole + 1);
    setTapeBus(ui->tableViewTape->model(),
               ui->tableViewTape->selectionModel()->currentIndex(),
               ui->comboBoxTapeBus->currentData().toUInt(),
               ui->comboBoxTapeChannel->currentData().toUInt());
    setTapeType(ui->tableViewTape->model(),
                ui->tableViewTape->selectionModel()->currentIndex(),
                ui->comboBoxTapeType->currentData().toUInt());
    ui->tableViewTape->resizeColumnsToContents();
    ui->tableViewTape->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    Harddrives::busTrackClass->device_track(1, DEV_TAPE, ui->tableViewTape->model()->data(i, Qt::UserRole).toInt(), ui->tableViewTape->model()->data(i, Qt::UserRole + 1).toInt());
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
    auto i = ui->tableViewTape->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_TAPE, ui->tableViewTape->model()->data(i, Qt::UserRole).toInt(), ui->tableViewTape->model()->data(i, Qt::UserRole + 1).toInt());
    setTapeBus(ui->tableViewTape->model(),
               ui->tableViewTape->selectionModel()->currentIndex(),
               ui->comboBoxTapeBus->currentData().toUInt(),
               ui->comboBoxTapeChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_TAPE, ui->tableViewTape->model()->data(i, Qt::UserRole).toInt(), ui->tableViewTape->model()->data(i, Qt::UserRole + 1).toInt());
    emit tapeChannelChanged();
}

void
SettingsOtherRemovable::on_comboBoxTapeType_activated(int)
{
    setTapeType(ui->tableViewTape->model(),
                ui->tableViewTape->selectionModel()->currentIndex(),
                ui->comboBoxTapeType->currentData().toUInt());
    ui->tableViewTape->resizeColumnsToContents();
    ui->tableViewTape->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
}
