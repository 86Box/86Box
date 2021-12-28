#include "qt_settingsharddisks.hpp"
#include "ui_qt_settingsharddisks.h"

extern "C" {
#include <86box/86box.h>
#include <86box/hdd.h>
}

#include <QStandardItemModel>

#include "qt_harddiskdialog.hpp"
#include "qt_harddrive_common.hpp"
#include "qt_progsettings.hpp"

const int ColumnBus         = 0;
const int ColumnFilename    = 1;
const int ColumnCylinders   = 2;
const int ColumnHeads       = 3;
const int ColumnSectors     = 4;
const int ColumnSize        = 5;

const int DataBus           = Qt::UserRole;
const int DataBusChannel    = Qt::UserRole + 1;

/*
static void
normalize_hd_list()
{
    hard_disk_t ihdd[HDD_NUM];
    int i, j;

    j = 0;
    memset(ihdd, 0x00, HDD_NUM * sizeof(hard_disk_t));

    for (i = 0; i < HDD_NUM; i++) {
        if (temp_hdd[i].bus != HDD_BUS_DISABLED) {
            memcpy(&(ihdd[j]), &(temp_hdd[i]), sizeof(hard_disk_t));
            j++;
        }
    }

    memcpy(temp_hdd, ihdd, HDD_NUM * sizeof(hard_disk_t));
}
*/

static QString busChannelName(const QModelIndex& idx) {
    return Harddrives::BusChannelName(idx.data(DataBus).toUInt(), idx.data(DataBusChannel).toUInt());
}

static void addRow(QAbstractItemModel* model, hard_disk_t* hd) {
    const QString userPath = usr_path;

    int row = model->rowCount();
    model->insertRow(row);

    QString busName = Harddrives::BusChannelName(hd->bus, hd->channel);
    model->setData(model->index(row, ColumnBus), busName);
    model->setData(model->index(row, ColumnBus), QIcon(ProgSettings::getIconSetPath() + "/hard_disk.ico"), Qt::DecorationRole);
    model->setData(model->index(row, ColumnBus), hd->bus, DataBus);
    model->setData(model->index(row, ColumnBus), hd->channel, DataBusChannel);
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
}

SettingsHarddisks::SettingsHarddisks(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsHarddisks)
{
    ui->setupUi(this);

    QAbstractItemModel* model = new QStandardItemModel(0, 6, this);
    model->setHeaderData(ColumnBus, Qt::Horizontal, "Bus");
    model->setHeaderData(ColumnFilename, Qt::Horizontal, "File");
    model->setHeaderData(ColumnCylinders, Qt::Horizontal, "C");
    model->setHeaderData(ColumnHeads, Qt::Horizontal, "H");
    model->setHeaderData(ColumnSectors, Qt::Horizontal, "S");
    model->setHeaderData(ColumnSize, Qt::Horizontal, "MiB");
    ui->tableView->setModel(model);

    for (int i = 0; i < HDD_NUM; i++) {
        if (hdd[i].bus > 0) {
            addRow(model, &hdd[i]);
        }
    }
    ui->tableView->resizeColumnsToContents();
    ui->tableView->horizontalHeader()->setSectionResizeMode(ColumnFilename, QHeaderView::Stretch);

    auto* tableSelectionModel = ui->tableView->selectionModel();
    connect(tableSelectionModel, &QItemSelectionModel::currentRowChanged, this, &SettingsHarddisks::onTableRowChanged);
    onTableRowChanged(QModelIndex());

    Harddrives::populateBuses(ui->comboBoxBus->model());
    on_comboBoxBus_currentIndexChanged(0);
}

SettingsHarddisks::~SettingsHarddisks()
{
    delete ui;
}

void SettingsHarddisks::save() {
    memset(hdd, 0, sizeof(hdd));

    auto* model = ui->tableView->model();
    int rows = model->rowCount();
    for (int i = 0; i < rows; ++i) {
        auto idx = model->index(i, ColumnBus);
        hdd[i].bus = idx.data(DataBus).toUInt();
        hdd[i].channel = idx.data(DataBusChannel).toUInt();
        hdd[i].tracks = idx.siblingAtColumn(ColumnCylinders).data().toUInt();
        hdd[i].hpc = idx.siblingAtColumn(ColumnHeads).data().toUInt();
        hdd[i].spt = idx.siblingAtColumn(ColumnSectors).data().toUInt();

        QByteArray fileName = idx.siblingAtColumn(ColumnFilename).data(Qt::UserRole).toString().toUtf8();
        strncpy(hdd[i].fn, fileName.data(), sizeof(hdd[i].fn));
        hdd[i].priv = nullptr;
    }
}

void SettingsHarddisks::on_comboBoxBus_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }

    auto idx = ui->tableView->selectionModel()->currentIndex();
    if (idx.isValid()) {
        auto* model = ui->tableView->model();
        auto col = idx.siblingAtColumn(ColumnBus);
        model->setData(col, ui->comboBoxBus->currentData(Qt::UserRole), DataBus);
        model->setData(col, busChannelName(col), Qt::DisplayRole);
    }

    Harddrives::populateBusChannels(ui->comboBoxChannel->model(), ui->comboBoxBus->currentData().toInt());
}

void SettingsHarddisks::on_comboBoxChannel_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }

    auto idx = ui->tableView->selectionModel()->currentIndex();
    if (idx.isValid()) {
        auto* model = ui->tableView->model();
        auto col = idx.siblingAtColumn(ColumnBus);
        model->setData(col, ui->comboBoxChannel->currentData(Qt::UserRole), DataBusChannel);
        model->setData(col, busChannelName(col), Qt::DisplayRole);
    }
}

void SettingsHarddisks::onTableRowChanged(const QModelIndex &current) {
    bool hidden = !current.isValid();
    ui->labelBus->setHidden(hidden);
    ui->labelChannel->setHidden(hidden);
    ui->comboBoxBus->setHidden(hidden);
    ui->comboBoxChannel->setHidden(hidden);

    uint32_t bus = current.siblingAtColumn(ColumnBus).data(DataBus).toUInt();
    uint32_t busChannel = current.siblingAtColumn(ColumnBus).data(DataBusChannel).toUInt();

    auto* model = ui->comboBoxBus->model();
    auto match = model->match(model->index(0, 0), Qt::UserRole, bus);
    if (! match.isEmpty()) {
        ui->comboBoxBus->setCurrentIndex(match.first().row());
    }
    model = ui->comboBoxChannel->model();
    match = model->match(model->index(0, 0), Qt::UserRole, busChannel);
    if (! match.isEmpty()) {
        ui->comboBoxChannel->setCurrentIndex(match.first().row());
    }
}

static void addDriveFromDialog(Ui::SettingsHarddisks* ui, const HarddiskDialog& dlg) {
    QByteArray fn = dlg.fileName().toUtf8();

    hard_disk_t hd;
    hd.bus = dlg.bus();
    hd.channel = dlg.channel();
    hd.tracks = dlg.cylinders();
    hd.hpc = dlg.heads();
    hd.spt = dlg.sectors();
    strncpy(hd.fn, fn.data(), sizeof(hd.fn));

    addRow(ui->tableView->model(), &hd);
    ui->tableView->resizeColumnsToContents();
    ui->tableView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
}

void SettingsHarddisks::on_pushButtonNew_clicked() {
    HarddiskDialog dialog(false, this);
    switch (dialog.exec()) {
    case QDialog::Accepted:
        addDriveFromDialog(ui, dialog);
        break;
    }
}


void SettingsHarddisks::on_pushButtonExisting_clicked() {
    HarddiskDialog dialog(true, this);
    switch (dialog.exec()) {
    case QDialog::Accepted:
        addDriveFromDialog(ui, dialog);
        break;
    }
}

void SettingsHarddisks::on_pushButtonRemove_clicked() {
    auto idx = ui->tableView->selectionModel()->currentIndex();
    if (! idx.isValid()) {
        return;
    }

    auto* model = ui->tableView->model();
    model->removeRow(idx.row());
}

