/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Other removable devices configuration UI module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *      Copyright 2021-2022 Cacodemon345
 *		Copyright 2021 Joakim L. Gilje
 */
#include "qt_settingsotherremovable.hpp"
#include "ui_qt_settingsotherremovable.h"

extern "C" {
#include <86box/timer.h>
#include <86box/scsi_device.h>
#include <86box/mo.h>
#include <86box/zip.h>
}

#include <QStandardItemModel>

#include "qt_models_common.hpp"
#include "qt_harddrive_common.hpp"
#include "qt_settings_bus_tracking.hpp"
#include "qt_progsettings.hpp"

static QString moDriveTypeName(int i) {
    return QString("%1 %2 %3").arg(mo_drive_types[i].vendor, mo_drive_types[i].model, mo_drive_types[i].revision);
}

static void setMOBus(QAbstractItemModel* model, const QModelIndex& idx, uint8_t bus, uint8_t channel) {
    QIcon icon;
    switch (bus) {
    case MO_BUS_DISABLED:
        icon = ProgSettings::loadIcon("/mo_disabled.ico");
        break;
    case MO_BUS_ATAPI:
    case MO_BUS_SCSI:
        icon = ProgSettings::loadIcon("/mo.ico");
        break;
    }

    auto i = idx.siblingAtColumn(0);
    model->setData(i, Harddrives::BusChannelName(bus, channel));
    model->setData(i, bus, Qt::UserRole);
    model->setData(i, channel, Qt::UserRole + 1);
    model->setData(i, icon, Qt::DecorationRole);
}

static void setMOType(QAbstractItemModel* model, const QModelIndex& idx, uint32_t type) {
    auto i = idx.siblingAtColumn(1);
    if (idx.siblingAtColumn(0).data(Qt::UserRole).toUInt() == MO_BUS_DISABLED) {
        model->setData(i, QCoreApplication::translate("", "None"));
    } else {
        model->setData(i, moDriveTypeName(type));
    }
    model->setData(i, type, Qt::UserRole);
}

static void setZIPBus(QAbstractItemModel* model, const QModelIndex& idx, uint8_t bus, uint8_t channel) {
    QIcon icon;
    switch (bus) {
    case ZIP_BUS_DISABLED:
        icon = ProgSettings::loadIcon("/zip_disabled.ico");
        break;
    case ZIP_BUS_ATAPI:
    case ZIP_BUS_SCSI:
        icon = ProgSettings::loadIcon("/zip.ico");
        break;
    }

    auto i = idx.siblingAtColumn(0);
    model->setData(i, Harddrives::BusChannelName(bus, channel));
    model->setData(i, bus, Qt::UserRole);
    model->setData(i, channel, Qt::UserRole + 1);
    model->setData(i, icon, Qt::DecorationRole);
}

static void setZIPType(QAbstractItemModel* model, const QModelIndex& idx, bool is250) {
    auto i = idx.siblingAtColumn(1);
    model->setData(i, is250 ? "ZIP 250" : "ZIP 100");
    model->setData(i, is250, Qt::UserRole);
}

SettingsOtherRemovable::SettingsOtherRemovable(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsOtherRemovable)
{
    ui->setupUi(this);

    Harddrives::populateRemovableBuses(ui->comboBoxMOBus->model());
    auto* model = ui->comboBoxMOType->model();
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
    ui->tableViewMO->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    connect(ui->tableViewMO->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &SettingsOtherRemovable::onMORowChanged);
    ui->tableViewMO->setCurrentIndex(model->index(0, 0));




    Harddrives::populateRemovableBuses(ui->comboBoxZIPBus->model());

    model = new QStandardItemModel(0, 2, this);
    ui->tableViewZIP->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, "Bus");
    model->setHeaderData(1, Qt::Horizontal, "Type");
    model->insertRows(0, ZIP_NUM);
    for (int i = 0; i < ZIP_NUM; i++) {
        auto idx = model->index(i, 0);
        setZIPBus(model, idx, zip_drives[i].bus_type, zip_drives[i].res);
        setZIPType(model, idx, zip_drives[i].is_250 > 0);
        Harddrives::busTrackClass->device_track(1, DEV_ZIP, zip_drives[i].bus_type, zip_drives[i].bus_type == ZIP_BUS_ATAPI ? zip_drives[i].ide_channel : zip_drives[i].scsi_device_id);
    }
    ui->tableViewZIP->resizeColumnsToContents();
    ui->tableViewZIP->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    connect(ui->tableViewZIP->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &SettingsOtherRemovable::onZIPRowChanged);
    ui->tableViewZIP->setCurrentIndex(model->index(0, 0));
}

SettingsOtherRemovable::~SettingsOtherRemovable()
{
    delete ui;
}

void SettingsOtherRemovable::save() {
    auto* model = ui->tableViewMO->model();
    for (int i = 0; i < MO_NUM; i++) {
        mo_drives[i].f = NULL;
        mo_drives[i].priv = NULL;
        mo_drives[i].bus_type = model->index(i, 0).data(Qt::UserRole).toUInt();
        mo_drives[i].res = model->index(i, 0).data(Qt::UserRole + 1).toUInt();
        mo_drives[i].type = model->index(i, 1).data(Qt::UserRole).toUInt();
    }

    model = ui->tableViewZIP->model();
    for (int i = 0; i < ZIP_NUM; i++) {
        zip_drives[i].f = NULL;
        zip_drives[i].priv = NULL;
        zip_drives[i].bus_type = model->index(i, 0).data(Qt::UserRole).toUInt();
        zip_drives[i].res = model->index(i, 0).data(Qt::UserRole + 1).toUInt();
        zip_drives[i].is_250 = model->index(i, 1).data(Qt::UserRole).toBool() ? 1 : 0;
    }
}

void SettingsOtherRemovable::onMORowChanged(const QModelIndex &current) {
    uint8_t bus = current.siblingAtColumn(0).data(Qt::UserRole).toUInt();
    uint8_t channel = current.siblingAtColumn(0).data(Qt::UserRole + 1).toUInt();
    uint8_t type = current.siblingAtColumn(1).data(Qt::UserRole).toUInt();

    ui->comboBoxMOBus->setCurrentIndex(-1);
    auto* model = ui->comboBoxMOBus->model();
    auto match = model->match(model->index(0, 0), Qt::UserRole, bus);
    if (! match.isEmpty()) {
        ui->comboBoxMOBus->setCurrentIndex(match.first().row());
    }

    model = ui->comboBoxMOChannel->model();
    match = model->match(model->index(0, 0), Qt::UserRole, channel);
    if (! match.isEmpty()) {
        ui->comboBoxMOChannel->setCurrentIndex(match.first().row());
    }
    ui->comboBoxMOType->setCurrentIndex(type);
}

void SettingsOtherRemovable::onZIPRowChanged(const QModelIndex &current) {
    uint8_t bus = current.siblingAtColumn(0).data(Qt::UserRole).toUInt();
    uint8_t channel = current.siblingAtColumn(0).data(Qt::UserRole + 1).toUInt();
    bool is250 = current.siblingAtColumn(1).data(Qt::UserRole).toBool();

    ui->comboBoxZIPBus->setCurrentIndex(-1);
    auto* model = ui->comboBoxZIPBus->model();
    auto match = model->match(model->index(0, 0), Qt::UserRole, bus);
    if (! match.isEmpty()) {
        ui->comboBoxZIPBus->setCurrentIndex(match.first().row());
    }

    model = ui->comboBoxZIPChannel->model();
    match = model->match(model->index(0, 0), Qt::UserRole, channel);
    if (! match.isEmpty()) {
        ui->comboBoxZIPChannel->setCurrentIndex(match.first().row());
    }
    ui->checkBoxZIP250->setChecked(is250);
}

void SettingsOtherRemovable::on_comboBoxMOBus_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }

    int bus = ui->comboBoxMOBus->currentData().toInt();
    bool enabled = (bus != MO_BUS_DISABLED);
    ui->comboBoxMOChannel->setEnabled(enabled);
    ui->comboBoxMOType->setEnabled(enabled);
    Harddrives::populateBusChannels(ui->comboBoxMOChannel->model(), bus);
}

void SettingsOtherRemovable::on_comboBoxMOBus_activated(int) {
    auto i = ui->tableViewMO->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_MO, ui->tableViewMO->model()->data(i, Qt::UserRole).toInt(), ui->tableViewMO->model()->data(i, Qt::UserRole + 1).toInt());
    ui->comboBoxMOChannel->setCurrentIndex(ui->comboBoxMOBus->currentData().toUInt() == MO_BUS_ATAPI ? Harddrives::busTrackClass->next_free_ide_channel() : Harddrives::busTrackClass->next_free_scsi_id());
    ui->tableViewMO->model()->data(i, Qt::UserRole + 1);
    setMOBus(
        ui->tableViewMO->model(),
        ui->tableViewMO->selectionModel()->currentIndex(),
        ui->comboBoxMOBus->currentData().toUInt(),
        ui->comboBoxMOChannel->currentData().toUInt());
    setMOType(
        ui->tableViewMO->model(),
        ui->tableViewMO->selectionModel()->currentIndex(),
        ui->comboBoxMOType->currentData().toUInt());
    ui->tableViewMO->resizeColumnsToContents();
    ui->tableViewMO->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    Harddrives::busTrackClass->device_track(1, DEV_MO, ui->tableViewMO->model()->data(i, Qt::UserRole).toInt(), ui->tableViewMO->model()->data(i, Qt::UserRole + 1).toInt());
}

void SettingsOtherRemovable::on_comboBoxMOChannel_activated(int) {
    auto i = ui->tableViewMO->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_MO, ui->tableViewMO->model()->data(i, Qt::UserRole).toInt(), ui->tableViewMO->model()->data(i, Qt::UserRole + 1).toInt());
    setMOBus(
        ui->tableViewMO->model(),
        ui->tableViewMO->selectionModel()->currentIndex(),
        ui->comboBoxMOBus->currentData().toUInt(),
        ui->comboBoxMOChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_MO, ui->tableViewMO->model()->data(i, Qt::UserRole).toInt(), ui->tableViewMO->model()->data(i, Qt::UserRole + 1).toInt());
}

void SettingsOtherRemovable::on_comboBoxMOType_activated(int) {
    setMOType(
        ui->tableViewMO->model(),
        ui->tableViewMO->selectionModel()->currentIndex(),
        ui->comboBoxMOType->currentData().toUInt());
    ui->tableViewMO->resizeColumnsToContents();
    ui->tableViewMO->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
}

void SettingsOtherRemovable::on_comboBoxZIPBus_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }

    int bus = ui->comboBoxZIPBus->currentData().toInt();
    bool enabled = (bus != ZIP_BUS_DISABLED);
    ui->comboBoxZIPChannel->setEnabled(enabled);
    ui->checkBoxZIP250->setEnabled(enabled);
    Harddrives::populateBusChannels(ui->comboBoxZIPChannel->model(), bus);
}

void SettingsOtherRemovable::on_comboBoxZIPBus_activated(int) {
    auto i = ui->tableViewZIP->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_ZIP, ui->tableViewZIP->model()->data(i, Qt::UserRole).toInt(), ui->tableViewZIP->model()->data(i, Qt::UserRole + 1).toInt());
    ui->comboBoxZIPChannel->setCurrentIndex(ui->comboBoxZIPBus->currentData().toUInt() == ZIP_BUS_ATAPI ? Harddrives::busTrackClass->next_free_ide_channel() : Harddrives::busTrackClass->next_free_scsi_id());
    setZIPBus(
        ui->tableViewZIP->model(),
        ui->tableViewZIP->selectionModel()->currentIndex(),
        ui->comboBoxZIPBus->currentData().toUInt(),
        ui->comboBoxZIPChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_ZIP, ui->tableViewZIP->model()->data(i, Qt::UserRole).toInt(), ui->tableViewZIP->model()->data(i, Qt::UserRole + 1).toInt());
}

void SettingsOtherRemovable::on_comboBoxZIPChannel_activated(int) {
    auto i = ui->tableViewZIP->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_ZIP, ui->tableViewZIP->model()->data(i, Qt::UserRole).toInt(), ui->tableViewZIP->model()->data(i, Qt::UserRole + 1).toInt());
    setZIPBus(
        ui->tableViewZIP->model(),
        ui->tableViewZIP->selectionModel()->currentIndex(),
        ui->comboBoxZIPBus->currentData().toUInt(),
        ui->comboBoxZIPChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_ZIP, ui->tableViewZIP->model()->data(i, Qt::UserRole).toInt(), ui->tableViewZIP->model()->data(i, Qt::UserRole + 1).toInt());
}

void SettingsOtherRemovable::on_checkBoxZIP250_stateChanged(int state) {
    setZIPType(
        ui->tableViewZIP->model(),
        ui->tableViewZIP->selectionModel()->currentIndex(),
        state == Qt::Checked);
}
