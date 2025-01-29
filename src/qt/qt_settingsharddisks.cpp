/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Hard disk configuration UI module.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021 Joakim L. Gilje
 */
#include "qt_settingsharddisks.hpp"
#include "ui_qt_settingsharddisks.h"

extern "C" {
#include <86box/86box.h>
#include <86box/hdd.h>
}

#include <QStandardItemModel>

#include "qt_harddiskdialog.hpp"
#include "qt_harddrive_common.hpp"
#include "qt_settings_bus_tracking.hpp"
#include "qt_progsettings.hpp"

const int ColumnBus       = 0;
const int ColumnFilename  = 1;
const int ColumnCylinders = 2;
const int ColumnHeads     = 3;
const int ColumnSectors   = 4;
const int ColumnSize      = 5;
const int ColumnSpeed     = 6;

const int DataBus                = Qt::UserRole;
const int DataBusChannel         = Qt::UserRole + 1;
const int DataBusPrevious        = Qt::UserRole + 2;
const int DataBusChannelPrevious = Qt::UserRole + 3;

#if 0
static void
normalize_hd_list()
{
    hard_disk_t ihdd[HDD_NUM];
    int j;

    j = 0;
    memset(ihdd, 0x00, HDD_NUM * sizeof(hard_disk_t));

    for (uint8_t i = 0; i < HDD_NUM; i++) {
        if (temp_hdd[i].bus_type != HDD_BUS_DISABLED) {
            memcpy(&(ihdd[j]), &(temp_hdd[i]), sizeof(hard_disk_t));
            j++;
        }
    }

    memcpy(temp_hdd, ihdd, HDD_NUM * sizeof(hard_disk_t));
}
#endif

static QString
busChannelName(const QModelIndex &idx)
{
    return Harddrives::BusChannelName(idx.data(DataBus).toUInt(), idx.data(DataBusChannel).toUInt());
}

static void
addRow(QAbstractItemModel *model, hard_disk_t *hd)
{
    const QString userPath = usr_path;

    int row = model->rowCount();
    model->insertRow(row);

    QString busName = Harddrives::BusChannelName(hd->bus_type, hd->channel);
    model->setData(model->index(row, ColumnBus), busName);
    model->setData(model->index(row, ColumnBus), ProgSettings::loadIcon("/hard_disk.ico"), Qt::DecorationRole);
    model->setData(model->index(row, ColumnBus), hd->bus_type, DataBus);
    model->setData(model->index(row, ColumnBus), hd->bus_type, DataBusPrevious);
    model->setData(model->index(row, ColumnBus), hd->channel, DataBusChannel);
    model->setData(model->index(row, ColumnBus), hd->channel, DataBusChannelPrevious);
    Harddrives::busTrackClass->device_track(1, DEV_HDD, hd->bus_type, hd->channel);
    QString fileName = hd->fn;
    if (fileName.startsWith(userPath, Qt::CaseInsensitive)) {
        model->setData(model->index(row, ColumnFilename), fileName.mid(userPath.size()));
    } else {
        model->setData(model->index(row, ColumnFilename), fileName);
    }
    model->setData(model->index(row, ColumnFilename), fileName, Qt::UserRole);

    model->setData(model->index(row, ColumnCylinders), hd->tracks);
    model->setData(model->index(row, ColumnHeads), hd->hpc);
    model->setData(model->index(row, ColumnSectors), hd->spt);
    model->setData(model->index(row, ColumnSize), (hd->tracks * hd->hpc * hd->spt) >> 11);
    model->setData(model->index(row, ColumnSpeed), QObject::tr(hdd_preset_getname(hd->speed_preset)));
    model->setData(model->index(row, ColumnSpeed), hd->speed_preset, Qt::UserRole);
}

SettingsHarddisks::SettingsHarddisks(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsHarddisks)
{
    ui->setupUi(this);

    QAbstractItemModel *model = new QStandardItemModel(0, 7, this);
    model->setHeaderData(ColumnBus, Qt::Horizontal, tr("Bus"));
    model->setHeaderData(ColumnFilename, Qt::Horizontal, tr("File"));
    model->setHeaderData(ColumnCylinders, Qt::Horizontal, tr("C"));
    model->setHeaderData(ColumnHeads, Qt::Horizontal, tr("H"));
    model->setHeaderData(ColumnSectors, Qt::Horizontal, tr("S"));
    model->setHeaderData(ColumnSize, Qt::Horizontal, tr("MiB"));
    model->setHeaderData(ColumnSpeed, Qt::Horizontal, tr("Speed"));
    ui->tableView->setModel(model);

    for (int i = 0; i < HDD_NUM; i++) {
        if (hdd[i].bus_type > 0) {
            addRow(model, &hdd[i]);
        }
    }
    if (model->rowCount() == HDD_NUM) {
        ui->pushButtonNew->setEnabled(false);
        ui->pushButtonExisting->setEnabled(false);
    }
    ui->tableView->resizeColumnsToContents();
    ui->tableView->horizontalHeader()->setSectionResizeMode(ColumnFilename, QHeaderView::Stretch);

    auto *tableSelectionModel = ui->tableView->selectionModel();
    connect(tableSelectionModel, &QItemSelectionModel::currentRowChanged, this, &SettingsHarddisks::onTableRowChanged);
    onTableRowChanged(QModelIndex());

    Harddrives::populateBuses(ui->comboBoxBus->model());
    on_comboBoxBus_currentIndexChanged(0);
}

SettingsHarddisks::~SettingsHarddisks()
{
    delete ui;
}

void
SettingsHarddisks::save()
{
    memset(hdd, 0, sizeof(hdd));

    auto *model = ui->tableView->model();
    int   rows  = model->rowCount();
    for (int i = 0; i < rows; ++i) {
        auto idx            = model->index(i, ColumnBus);
        hdd[i].bus_type     = idx.data(DataBus).toUInt();
        hdd[i].channel      = idx.data(DataBusChannel).toUInt();
        hdd[i].tracks       = idx.siblingAtColumn(ColumnCylinders).data().toUInt();
        hdd[i].hpc          = idx.siblingAtColumn(ColumnHeads).data().toUInt();
        hdd[i].spt          = idx.siblingAtColumn(ColumnSectors).data().toUInt();
        hdd[i].speed_preset = idx.siblingAtColumn(ColumnSpeed).data(Qt::UserRole).toUInt();

        QByteArray fileName = idx.siblingAtColumn(ColumnFilename).data(Qt::UserRole).toString().toUtf8();
        strncpy(hdd[i].fn, fileName.data(), sizeof(hdd[i].fn) - 1);
        hdd[i].priv = nullptr;
    }
}

void SettingsHarddisks::reloadBusChannels() {
    const auto selected = ui->comboBoxChannel->currentIndex();
    Harddrives::populateBusChannels(ui->comboBoxChannel->model(), ui->comboBoxBus->currentData().toInt(), Harddrives::busTrackClass);
    ui->comboBoxChannel->setCurrentIndex(selected);
    enableCurrentlySelectedChannel();
}

void
SettingsHarddisks::on_comboBoxBus_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }

    buschangeinprogress = true;
    auto idx            = ui->tableView->selectionModel()->currentIndex();
    if (idx.isValid()) {
        auto *model = ui->tableView->model();
        auto  col   = idx.siblingAtColumn(ColumnBus);
        model->setData(col, ui->comboBoxBus->currentData(Qt::UserRole), DataBus);
        model->setData(col, busChannelName(col), Qt::DisplayRole);
        Harddrives::busTrackClass->device_track(0, DEV_HDD, model->data(col, DataBusPrevious).toInt(), model->data(col, DataBusChannelPrevious).toInt());
        model->setData(col, ui->comboBoxBus->currentData(Qt::UserRole), DataBusPrevious);
    }

    Harddrives::populateBusChannels(ui->comboBoxChannel->model(), ui->comboBoxBus->currentData().toInt(), Harddrives::busTrackClass);
    Harddrives::populateSpeeds(ui->comboBoxSpeed->model(), ui->comboBoxBus->currentData().toInt());
    int chanIdx = 0;

    switch (ui->comboBoxBus->currentData().toInt()) {
        case HDD_BUS_MFM:
            chanIdx = (Harddrives::busTrackClass->next_free_mfm_channel());
            break;
        case HDD_BUS_XTA:
            chanIdx = (Harddrives::busTrackClass->next_free_xta_channel());
            break;
        case HDD_BUS_ESDI:
            chanIdx = (Harddrives::busTrackClass->next_free_esdi_channel());
            break;
        case HDD_BUS_ATAPI:
        case HDD_BUS_IDE:
            chanIdx = (Harddrives::busTrackClass->next_free_ide_channel());
            break;
        case HDD_BUS_SCSI:
            chanIdx = (Harddrives::busTrackClass->next_free_scsi_id());
            break;
    }

    if (idx.isValid()) {
        auto *model = ui->tableView->model();
        auto  col   = idx.siblingAtColumn(ColumnBus);
        model->setData(col, chanIdx, DataBusChannelPrevious);
    }
    ui->comboBoxChannel->setCurrentIndex(chanIdx);
    buschangeinprogress = false;
}

void
SettingsHarddisks::on_comboBoxChannel_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }

    auto idx = ui->tableView->selectionModel()->currentIndex();
    if (idx.isValid()) {
        auto *model = ui->tableView->model();
        auto  col   = idx.siblingAtColumn(ColumnBus);
        model->setData(col, ui->comboBoxChannel->currentData(Qt::UserRole), DataBusChannel);
        model->setData(col, busChannelName(col), Qt::DisplayRole);
        if (!buschangeinprogress)
            Harddrives::busTrackClass->device_track(0, DEV_HDD, model->data(col, DataBus).toInt(), model->data(col, DataBusChannelPrevious).toUInt());
        Harddrives::busTrackClass->device_track(1, DEV_HDD, model->data(col, DataBus).toInt(), model->data(col, DataBusChannel).toUInt());
        model->setData(col, ui->comboBoxChannel->currentData(Qt::UserRole), DataBusChannelPrevious);
        emit driveChannelChanged();
    }
}

void
SettingsHarddisks::enableCurrentlySelectedChannel()
{
    const auto *item_model = qobject_cast<QStandardItemModel*>(ui->comboBoxChannel->model());
    const auto index = ui->comboBoxChannel->currentIndex();
    auto *item = item_model->item(index);
    if(item) {
        item->setEnabled(true);
    }
}

void
SettingsHarddisks::on_comboBoxSpeed_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }

    auto idx = ui->tableView->selectionModel()->currentIndex();
    if (idx.isValid()) {
        auto *model = ui->tableView->model();
        auto  col   = idx.siblingAtColumn(ColumnSpeed);
        model->setData(col, ui->comboBoxSpeed->currentData(Qt::UserRole), Qt::UserRole);
        model->setData(col, QObject::tr(hdd_preset_getname(ui->comboBoxSpeed->currentData(Qt::UserRole).toUInt())));
    }
}

void
SettingsHarddisks::onTableRowChanged(const QModelIndex &current)
{
    bool hidden = !current.isValid();
    ui->labelBus->setHidden(hidden);
    ui->labelChannel->setHidden(hidden);
    ui->labelSpeed->setHidden(hidden);
    ui->comboBoxBus->setHidden(hidden);
    ui->comboBoxChannel->setHidden(hidden);
    ui->comboBoxSpeed->setHidden(hidden);

    uint32_t bus        = current.siblingAtColumn(ColumnBus).data(DataBus).toUInt();
    uint32_t busChannel = current.siblingAtColumn(ColumnBus).data(DataBusChannel).toUInt();
    uint32_t speed      = current.siblingAtColumn(ColumnSpeed).data(Qt::UserRole).toUInt();

    auto *model = ui->comboBoxBus->model();
    auto  match = model->match(model->index(0, 0), Qt::UserRole, bus);
    if (!match.isEmpty()) {
        ui->comboBoxBus->setCurrentIndex(match.first().row());
    }
    model = ui->comboBoxChannel->model();
    match = model->match(model->index(0, 0), Qt::UserRole, busChannel);
    if (!match.isEmpty()) {
        ui->comboBoxChannel->setCurrentIndex(match.first().row());
    }

    model = ui->comboBoxSpeed->model();
    match = model->match(model->index(0, 0), Qt::UserRole, speed);
    if (!match.isEmpty()) {
        ui->comboBoxSpeed->setCurrentIndex(match.first().row());
    }
    reloadBusChannels();
}

static void
addDriveFromDialog(Ui::SettingsHarddisks *ui, const HarddiskDialog &dlg)
{
    QByteArray fn = dlg.fileName().toUtf8();

    hard_disk_t hd;
    memset(&hd, 0, sizeof(hd));

    hd.bus_type = dlg.bus();
    hd.channel  = dlg.channel();
    hd.tracks   = dlg.cylinders();
    hd.hpc      = dlg.heads();
    hd.spt      = dlg.sectors();
    strncpy(hd.fn, fn.data(), sizeof(hd.fn) - 1);
    hd.speed_preset = dlg.speed();

    addRow(ui->tableView->model(), &hd);
    ui->tableView->resizeColumnsToContents();
    ui->tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    if (ui->tableView->model()->rowCount() == HDD_NUM) {
        ui->pushButtonNew->setEnabled(false);
        ui->pushButtonExisting->setEnabled(false);
    }
}

void
SettingsHarddisks::on_pushButtonNew_clicked()
{
    HarddiskDialog dialog(false, this);
    switch (dialog.exec()) {
        case QDialog::Accepted:
            addDriveFromDialog(ui, dialog);
            reloadBusChannels();
            break;
    }
}

void
SettingsHarddisks::on_pushButtonExisting_clicked()
{
    HarddiskDialog dialog(true, this);
    switch (dialog.exec()) {
        case QDialog::Accepted:
            addDriveFromDialog(ui, dialog);
            reloadBusChannels();
            break;
    }
}

void
SettingsHarddisks::on_pushButtonRemove_clicked()
{
    auto idx = ui->tableView->selectionModel()->currentIndex();
    if (!idx.isValid()) {
        return;
    }

    auto *model = ui->tableView->model();
    const auto  col   = idx.siblingAtColumn(ColumnBus);
    Harddrives::busTrackClass->device_track(0, DEV_HDD, model->data(col, DataBus).toInt(), model->data(col, DataBusChannel).toInt());
    model->removeRow(idx.row());
    ui->pushButtonNew->setEnabled(true);
    ui->pushButtonExisting->setEnabled(true);
}
