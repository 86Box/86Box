/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Floppy/CD-ROM devices configuration UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021 Joakim L. Gilje
 */
#include "qt_settingsfloppycdrom.hpp"
#include "ui_qt_settingsfloppycdrom.h"

extern "C" {
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/cdrom.h>
#include <86box/fdd_audio.h>
}

#include "qt_models_common.hpp"
#include "qt_harddrive_common.hpp"
#include "qt_settings_bus_tracking.hpp"
#include "qt_progsettings.hpp"

void
SettingsFloppyCDROM::setFloppyType(QAbstractItemModel *model, const QModelIndex &idx, int type)
{
    QIcon icon;
    if (type == 0)
        icon = floppy_disabled_icon;
    else if (type >= 1 && type <= 6)
        icon = floppy_525_icon;
    else
        icon = floppy_35_icon;

    model->setData(idx, QObject::tr(fdd_getname(type)));
    model->setData(idx, type, Qt::UserRole);
    model->setData(idx, icon, Qt::DecorationRole);
}

void
SettingsFloppyCDROM::setCDROMBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint8_t channel)
{
    QIcon icon;

    switch (bus) {
        case CDROM_BUS_DISABLED:
            icon = cdrom_disabled_icon;
            break;
        case CDROM_BUS_ATAPI:
        case CDROM_BUS_SCSI:
        case CDROM_BUS_MITSUMI:
        case CDROM_BUS_MKE:
            icon = cdrom_icon;
            break;
    }

    auto i = idx.siblingAtColumn(0);
    model->setData(i, Harddrives::BusChannelName(bus, channel));
    model->setData(i, bus, Qt::UserRole);
    model->setData(i, channel, Qt::UserRole + 1);
    model->setData(i, icon, Qt::DecorationRole);
}

static void
setCDROMSpeed(QAbstractItemModel *model, const QModelIndex &idx, uint8_t speed)
{
    if (!speed)
        speed = 8;
    auto i = idx.siblingAtColumn(1);
    model->setData(i, QString("%1x").arg(speed));
    model->setData(i, speed, Qt::UserRole);
}

static QString
CDROMName(int type)
{
    char temp[512];
    cdrom_get_name(type, temp);
    return QObject::tr((const char *) temp);
}

static void
setCDROMType(QAbstractItemModel *model, const QModelIndex &idx, int type)
{
    auto i = idx.siblingAtColumn(2);
    if (idx.siblingAtColumn(0).data(Qt::UserRole).toUInt() == CDROM_BUS_DISABLED)
        model->setData(i, QCoreApplication::translate("", "None"));
    else if (idx.siblingAtColumn(0).data(Qt::UserRole).toUInt() != CDROM_BUS_MITSUMI)
        model->setData(i, CDROMName(type));
    model->setData(i, type, Qt::UserRole);
}

SettingsFloppyCDROM::SettingsFloppyCDROM(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsFloppyCDROM)
{
    ui->setupUi(this);

    floppy_disabled_icon = QIcon(":/settings/qt/icons/floppy_disabled.ico");
    floppy_525_icon      = QIcon(":/settings/qt/icons/floppy_525.ico");
    floppy_35_icon       = QIcon(":/settings/qt/icons/floppy_35.ico");

    auto *model = ui->comboBoxFloppyType->model();
    int   i     = 0;
    while (true) {
        QString name = tr(fdd_getname(i));
        if (name.isEmpty())
            break;

        Models::AddEntry(model, name, i);
        ++i;
    }

    model = new QStandardItemModel(0, 4, this);
    ui->tableViewFloppy->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, tr("Type"));
    model->setHeaderData(1, Qt::Horizontal, tr("Turbo"));
    model->setHeaderData(2, Qt::Horizontal, tr("Check BPB"));
    model->setHeaderData(3, Qt::Horizontal, tr("Audio"));

    model->insertRows(0, FDD_NUM);
    /* Floppy drives category */
    for (int i = 0; i < FDD_NUM; i++) {
        auto idx  = model->index(i, 0);
        int  type = fdd_get_type(i);
        setFloppyType(model, idx, type);
        model->setData(idx.siblingAtColumn(1), fdd_get_turbo(i) > 0 ? tr("On") : tr("Off"));
        model->setData(idx.siblingAtColumn(2), fdd_get_check_bpb(i) > 0 ? tr("On") : tr("Off"));

        int     prof = fdd_get_audio_profile(i);
        QString profName;

#ifndef DISABLE_FDD_AUDIO
        // Get the profile name from the configuration system
        const char *name = fdd_audio_get_profile_name(prof);
        if (name) {
            profName = QString(name);
        } else {
            profName = tr("None");
        }
#else
        profName = tr("None");
#endif

        auto audioIdx = model->index(i, 3);
        model->setData(audioIdx, profName);
        model->setData(audioIdx, prof, Qt::UserRole);
    }

    ui->tableViewFloppy->resizeColumnsToContents();
    ui->tableViewFloppy->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    connect(ui->tableViewFloppy->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &SettingsFloppyCDROM::onFloppyRowChanged);

#ifndef DISABLE_FDD_AUDIO
    ui->comboBoxFloppyAudio->setVisible(true);
    int profile_count = fdd_audio_get_profile_count();
    for (int i = 0; i < profile_count; i++) {
        const char *name = fdd_audio_get_profile_name(i);
        if (name) {
            ui->comboBoxFloppyAudio->addItem(name, i);
        }
    }
    ui->comboBoxFloppyAudio->setSizeAdjustPolicy(QComboBox::AdjustToContents);
#else
    ui->comboBoxFloppyAudio->setVisible(false);
#endif

    // Set initial selection and trigger the row changed event to update controls
    ui->tableViewFloppy->setCurrentIndex(model->index(0, 0));
    // Manually trigger the row changed event to ensure audio selection is updated
    onFloppyRowChanged(model->index(0, 0));

    cdrom_disabled_icon = QIcon(":/settings/qt/icons/cdrom_disabled.ico");
    cdrom_icon          = QIcon(":/settings/qt/icons/cdrom.ico");

    Harddrives::populateCDROMBuses(ui->comboBoxBus->model());
    model = ui->comboBoxSpeed->model();
    for (int i = 0; i < 72; i++)
        Models::AddEntry(model, QString("%1x").arg(i + 1), i + 1);

    model = new QStandardItemModel(0, 3, this);
    ui->tableViewCDROM->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, tr("Bus"));
    model->setHeaderData(1, Qt::Horizontal, tr("Speed"));
    model->setHeaderData(2, Qt::Horizontal, tr("Type"));
    model->insertRows(0, CDROM_NUM);
    for (int i = 0; i < CDROM_NUM; i++) {
        auto idx  = model->index(i, 0);
        int  type = cdrom_get_type(i);
        setCDROMBus(model, idx, cdrom[i].bus_type, cdrom[i].res);
        setCDROMType(model, idx.siblingAtColumn(2), type);
        int speed = cdrom_get_speed(type);
        if (speed == -1)
            setCDROMSpeed(model, idx.siblingAtColumn(1), cdrom[i].speed);
        else
            setCDROMSpeed(model, idx.siblingAtColumn(1), speed);
        if (cdrom[i].bus_type == CDROM_BUS_MKE)
            Harddrives::busTrackClass->device_track(1, DEV_CDROM, cdrom[i].bus_type, cdrom[i].mke_channel);
        else if (cdrom[i].bus_type == CDROM_BUS_ATAPI)
            Harddrives::busTrackClass->device_track(1, DEV_CDROM, cdrom[i].bus_type, cdrom[i].ide_channel);
        else if (cdrom[i].bus_type == CDROM_BUS_SCSI)
            Harddrives::busTrackClass->device_track(1, DEV_CDROM, cdrom[i].bus_type, cdrom[i].scsi_device_id);
        else if (cdrom[i].bus_type == CDROM_BUS_MITSUMI)
            Harddrives::busTrackClass->device_track(1, DEV_CDROM, cdrom[i].bus_type, 0);
    }
    ui->tableViewCDROM->resizeColumnsToContents();
    ui->tableViewCDROM->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    connect(ui->tableViewCDROM->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &SettingsFloppyCDROM::onCDROMRowChanged);
    ui->tableViewCDROM->setCurrentIndex(model->index(0, 0));

    uint8_t bus_type = ui->comboBoxBus->currentData().toUInt();
    int     cdromIdx = ui->tableViewCDROM->selectionModel()->currentIndex().data().toInt();

    auto *modelType  = ui->comboBoxCDROMType->model();
    int   removeRows = modelType->rowCount();

    uint32_t j               = 0;
    int      selectedTypeRow = 0;
    int      eligibleRows    = 0;
    while (cdrom_drive_types[j].bus_type != BUS_TYPE_NONE) {
        if (((bus_type == CDROM_BUS_MKE) || (bus_type == CDROM_BUS_ATAPI) || (bus_type == CDROM_BUS_SCSI)) && ((cdrom_drive_types[j].bus_type == bus_type) || ((cdrom_drive_types[j].bus_type == BUS_TYPE_BOTH) && (bus_type != BUS_TYPE_MKE)))) {
            QString name = CDROMName(j);
            Models::AddEntry(modelType, name, j);
            if (cdrom[cdromIdx].type == j)
                selectedTypeRow = eligibleRows;
            ++eligibleRows;
        }
        ++j;
    }
    modelType->removeRows(0, removeRows);
    ui->comboBoxCDROMType->setEnabled(eligibleRows > 1);
    ui->comboBoxCDROMType->setCurrentIndex(-1);
    ui->comboBoxCDROMType->setCurrentIndex(selectedTypeRow);
}

SettingsFloppyCDROM::~SettingsFloppyCDROM()
{
    delete ui;
}

void
SettingsFloppyCDROM::save()
{
    auto *model = ui->tableViewFloppy->model();
    for (int i = 0; i < FDD_NUM; i++) {
        fdd_set_type(i, model->index(i, 0).data(Qt::UserRole).toInt());
        fdd_set_turbo(i, model->index(i, 1).data() == tr("On") ? 1 : 0);
        fdd_set_check_bpb(i, model->index(i, 2).data() == tr("On") ? 1 : 0);
#ifndef DISABLE_FDD_AUDIO
        fdd_set_audio_profile(i, model->index(i, 3).data(Qt::UserRole).toInt());
#endif
    }

    /* Removable devices category */
    model = ui->tableViewCDROM->model();
    for (int i = 0; i < CDROM_NUM; i++) {
        cdrom[i].priv        = NULL;
        cdrom[i].ops         = NULL;
        cdrom[i].local       = NULL;
        cdrom[i].insert      = NULL;
        cdrom[i].close       = NULL;
        cdrom[i].get_volume  = NULL;
        cdrom[i].get_channel = NULL;
        cdrom[i].bus_type    = model->index(i, 0).data(Qt::UserRole).toUInt();
        cdrom[i].res         = model->index(i, 0).data(Qt::UserRole + 1).toUInt();
        cdrom[i].speed       = model->index(i, 1).data(Qt::UserRole).toUInt();
        cdrom_set_type(i, model->index(i, 2).data(Qt::UserRole).toInt());
    }

#ifdef DISABLE_FDD_AUDIO
    fdd_sounds_enabled = 0;
#else
    fdd_sounds_enabled = 1;
#endif
}

void
SettingsFloppyCDROM::onFloppyRowChanged(const QModelIndex &current)
{
    int type = current.siblingAtColumn(0).data(Qt::UserRole).toInt();
    ui->comboBoxFloppyType->setCurrentIndex(type);
    ui->checkBoxTurboTimings->setChecked(current.siblingAtColumn(1).data() == tr("On"));
    ui->checkBoxCheckBPB->setChecked(current.siblingAtColumn(2).data() == tr("On"));

    int prof = current.siblingAtColumn(3).data(Qt::UserRole).toInt();

#ifndef DISABLE_FDD_AUDIO
    // Rebuild audio profile combo box based on drive type
    ui->comboBoxFloppyAudio->clear();

    if (type == 0) {
        ui->comboBoxFloppyAudio->addItem(tr("None"), 0);
        ui->comboBoxFloppyAudio->setCurrentIndex(0);
        ui->comboBoxFloppyAudio->setEnabled(false);

        // Update the model to reflect "None" profile
        auto audioIdx = current.siblingAtColumn(3);
        ui->tableViewFloppy->model()->setData(audioIdx, tr("None"));
        ui->tableViewFloppy->model()->setData(audioIdx, 0, Qt::UserRole);
        return;
    }

    ui->comboBoxFloppyAudio->setEnabled(true);

    // Get drive type's track count to determine 40-track vs 80-track
    int  drive_max_tracks = fdd_get_type_max_track(type);
    bool is_40_track      = (drive_max_tracks <= 43);

    int profile_count       = fdd_audio_get_profile_count();
    int currentProfileIndex = -1;
    int comboIndex          = 0;

    for (int i = 0; i < profile_count; i++) {
        const char *name = fdd_audio_get_profile_name(i);
        if (name) {
            const fdd_audio_profile_config_t *profile = fdd_audio_get_profile(i);
            if (profile) {
                // Only show profiles that match the drive type's track count
                if (profile->total_tracks == 0 || 
                    (is_40_track && profile->total_tracks == 40) || 
                    (!is_40_track && profile->total_tracks == 80)) {
                    ui->comboBoxFloppyAudio->addItem(name, i);
                    if (i == prof) {
                        currentProfileIndex = comboIndex;
                    }
                    comboIndex++;
                }
            }
        }
    }

    // If current profile is not compatible, select "None" (profile 0)
    if (currentProfileIndex == -1) {
        currentProfileIndex = ui->comboBoxFloppyAudio->findData(0);
        // Update the model to reflect "None" profile
        auto audioIdx = current.siblingAtColumn(3);
        ui->tableViewFloppy->model()->setData(audioIdx, tr("None"));
        ui->tableViewFloppy->model()->setData(audioIdx, 0, Qt::UserRole);
    }

    ui->comboBoxFloppyAudio->setCurrentIndex(currentProfileIndex);
#else
    int comboIndex = ui->comboBoxFloppyAudio->findData(prof);
    ui->comboBoxFloppyAudio->setCurrentIndex(comboIndex);
#endif
}

void
SettingsFloppyCDROM::onCDROMRowChanged(const QModelIndex &current)
{
    uint8_t  bus     = current.siblingAtColumn(0).data(Qt::UserRole).toUInt();
    uint8_t  channel = current.siblingAtColumn(0).data(Qt::UserRole + 1).toUInt();
    uint32_t type    = current.siblingAtColumn(2).data(Qt::UserRole).toUInt();

    ui->comboBoxBus->setCurrentIndex(-1);
    auto *model = ui->comboBoxBus->model();
    auto  match = model->match(model->index(0, 0), Qt::UserRole, bus);
    if (!match.isEmpty())
        ui->comboBoxBus->setCurrentIndex(match.first().row());

    model = ui->comboBoxChannel->model();
    match = model->match(model->index(0, 0), Qt::UserRole, channel);
    if (!match.isEmpty())
        ui->comboBoxChannel->setCurrentIndex(match.first().row());

    int speed = cdrom_get_speed(type);
    if (speed == -1) {
        speed = current.siblingAtColumn(1).data(Qt::UserRole).toUInt();
        ui->comboBoxSpeed->setEnabled((bus == CDROM_BUS_DISABLED) ? false : true);
    } else
        ui->comboBoxSpeed->setEnabled(false);
    ui->comboBoxSpeed->setCurrentIndex(speed == 0 ? 7 : speed - 1);

    auto *modelType  = ui->comboBoxCDROMType->model();
    int   removeRows = modelType->rowCount();

    uint32_t j               = 0;
    int      selectedTypeRow = 0;
    int      eligibleRows    = 0;
    while (cdrom_drive_types[j].bus_type != BUS_TYPE_NONE) {
        if (((bus == CDROM_BUS_MKE) || (bus == CDROM_BUS_ATAPI) || (bus == CDROM_BUS_SCSI)) && ((cdrom_drive_types[j].bus_type == bus) || ((cdrom_drive_types[j].bus_type == BUS_TYPE_BOTH) && (bus != BUS_TYPE_MKE)))) {
            QString name = CDROMName(j);
            Models::AddEntry(modelType, name, j);
            if (type == j)
                selectedTypeRow = eligibleRows;
            ++eligibleRows;
        }
        ++j;
    }
    modelType->removeRows(0, removeRows);
    ui->comboBoxCDROMType->setEnabled(eligibleRows > 1);
    ui->comboBoxCDROMType->setCurrentIndex(-1);
    ui->comboBoxCDROMType->setCurrentIndex(selectedTypeRow);

    enableCurrentlySelectedChannel();
}

void
SettingsFloppyCDROM::on_checkBoxTurboTimings_stateChanged(int arg1)
{
    auto idx = ui->tableViewFloppy->selectionModel()->currentIndex();
    ui->tableViewFloppy->model()->setData(idx.siblingAtColumn(1), arg1 == Qt::Checked ? tr("On") : tr("Off"));
}

void
SettingsFloppyCDROM::on_checkBoxCheckBPB_stateChanged(int arg1)
{
    auto idx = ui->tableViewFloppy->selectionModel()->currentIndex();
    ui->tableViewFloppy->model()->setData(idx.siblingAtColumn(2), arg1 == Qt::Checked ? tr("On") : tr("Off"));
}

void
SettingsFloppyCDROM::on_comboBoxFloppyType_activated(int index)
{
    auto currentIndex = ui->tableViewFloppy->selectionModel()->currentIndex();
    auto typeIndex    = currentIndex.siblingAtColumn(0);
    setFloppyType(ui->tableViewFloppy->model(), typeIndex, index);

    // Trigger row changed to rebuild audio profile list
    onFloppyRowChanged(currentIndex);
}

void
SettingsFloppyCDROM::on_comboBoxFloppyAudio_activated(int)
{
    auto    idx  = ui->tableViewFloppy->selectionModel()->currentIndex();
    int     prof = ui->comboBoxFloppyAudio->currentData().toInt();
    QString profName;

#ifndef DISABLE_FDD_AUDIO
    // Get the profile name from the configuration system
    const char *name = fdd_audio_get_profile_name(prof);
    if (name) {
        profName = name;
    } else {
        profName = tr("None");
    }
    if (prof > 0) {
        load_profile_samples(prof);
    }
#else
    profName = tr("None");
#endif

    auto audioIdx = idx.siblingAtColumn(3);
    ui->tableViewFloppy->model()->setData(audioIdx, profName);
    ui->tableViewFloppy->model()->setData(audioIdx, prof, Qt::UserRole);
}

void
SettingsFloppyCDROM::reloadBusChannels()
{
    auto selected = ui->comboBoxChannel->currentIndex();
    Harddrives::populateBusChannels(ui->comboBoxChannel->model(), ui->comboBoxBus->currentData().toInt(), Harddrives::busTrackClass);
    ui->comboBoxChannel->setCurrentIndex(selected);
    enableCurrentlySelectedChannel();
}

void
SettingsFloppyCDROM::on_comboBoxBus_currentIndexChanged(int index)
{
    if (index >= 0) {
        int  bus     = ui->comboBoxBus->currentData().toInt();
        bool enabled = (bus != CDROM_BUS_DISABLED);
        ui->comboBoxChannel->setEnabled((bus == CDROM_BUS_MITSUMI) ? 0 : enabled);
        ui->comboBoxSpeed->setEnabled((bus == CDROM_BUS_MITSUMI) ? 0 : enabled);
        ui->comboBoxCDROMType->setEnabled((bus == CDROM_BUS_MITSUMI) ? 0 : enabled);

        Harddrives::populateBusChannels(ui->comboBoxChannel->model(), bus, Harddrives::busTrackClass);
    }
}

void
SettingsFloppyCDROM::on_comboBoxSpeed_activated(int index)
{
    auto idx = ui->tableViewCDROM->selectionModel()->currentIndex();
    setCDROMSpeed(ui->tableViewCDROM->model(), idx.siblingAtColumn(1), index + 1);
}

void
SettingsFloppyCDROM::on_comboBoxBus_activated(int)
{
    auto    i        = ui->tableViewCDROM->selectionModel()->currentIndex().siblingAtColumn(0);
    uint8_t bus_type = ui->comboBoxBus->currentData().toUInt();
    int     cdromIdx = ui->tableViewCDROM->selectionModel()->currentIndex().data().toInt();

    Harddrives::busTrackClass->device_track(0, DEV_CDROM, ui->tableViewCDROM->model()->data(i, Qt::UserRole).toInt(), ui->tableViewCDROM->model()->data(i, Qt::UserRole + 1).toInt());
    if (bus_type == CDROM_BUS_MKE)
        ui->comboBoxChannel->setCurrentIndex(Harddrives::busTrackClass->next_free_mke_channel());
    else if (bus_type == CDROM_BUS_ATAPI)
        ui->comboBoxChannel->setCurrentIndex(Harddrives::busTrackClass->next_free_ide_channel());
    else if (bus_type == CDROM_BUS_SCSI)
        ui->comboBoxChannel->setCurrentIndex(Harddrives::busTrackClass->next_free_scsi_id());
    else if (bus_type == CDROM_BUS_MITSUMI)
        ui->comboBoxChannel->setCurrentIndex(0);

    setCDROMBus(ui->tableViewCDROM->model(),
                ui->tableViewCDROM->selectionModel()->currentIndex(),
                bus_type,
                ui->comboBoxChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_CDROM, ui->tableViewCDROM->model()->data(i, Qt::UserRole).toInt(), ui->tableViewCDROM->model()->data(i, Qt::UserRole + 1).toInt());

    auto *modelType  = ui->comboBoxCDROMType->model();
    int   removeRows = modelType->rowCount();

    uint32_t j               = 0;
    int      selectedTypeRow = 0;
    int      eligibleRows    = 0;
    while (cdrom_drive_types[j].bus_type != BUS_TYPE_NONE) {
        if (((bus_type == CDROM_BUS_MKE) || (bus_type == CDROM_BUS_ATAPI) || (bus_type == CDROM_BUS_SCSI)) && ((cdrom_drive_types[j].bus_type == bus_type) || ((cdrom_drive_types[j].bus_type == BUS_TYPE_BOTH) && (bus_type != BUS_TYPE_MKE)))) {
            QString name = CDROMName(j);
            Models::AddEntry(modelType, name, j);
            if (cdrom[cdromIdx].type == j)
                selectedTypeRow = eligibleRows;
            ++eligibleRows;
        }
        ++j;
    }
    modelType->removeRows(0, removeRows);
    ui->comboBoxCDROMType->setEnabled(eligibleRows > 1);
    ui->comboBoxCDROMType->setCurrentIndex(-1);
    ui->comboBoxCDROMType->setCurrentIndex(selectedTypeRow);

    setCDROMType(ui->tableViewCDROM->model(),
                 ui->tableViewCDROM->selectionModel()->currentIndex(),
                 ui->comboBoxCDROMType->currentData().toUInt());

    int speed = cdrom_get_speed(ui->comboBoxCDROMType->currentData().toUInt());
    if ((speed == -1) && (bus_type != CDROM_BUS_MITSUMI)) {
        speed = ui->comboBoxSpeed->currentData().toUInt();
        ui->comboBoxSpeed->setEnabled(bus_type != CDROM_BUS_DISABLED);
    } else {
        ui->comboBoxSpeed->setEnabled(false);
        if (bus_type == CDROM_BUS_MITSUMI) // temp hack
            speed = 0;
    }
    ui->comboBoxSpeed->setCurrentIndex(speed == 0 ? 7 : speed - 1);
    setCDROMSpeed(ui->tableViewCDROM->model(),
                  ui->tableViewCDROM->selectionModel()->currentIndex(),
                  speed);
    emit cdromChannelChanged();
}

void
SettingsFloppyCDROM::enableCurrentlySelectedChannel()
{
    const auto *item_model = qobject_cast<QStandardItemModel *>(ui->comboBoxChannel->model());
    const auto  index      = ui->comboBoxChannel->currentIndex();
    auto       *item       = item_model->item(index);
    if (item)
        item->setEnabled(true);
}

void
SettingsFloppyCDROM::on_comboBoxChannel_activated(int)
{
    auto i = ui->tableViewCDROM->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_CDROM, ui->tableViewCDROM->model()->data(i, Qt::UserRole).toInt(), ui->tableViewCDROM->model()->data(i, Qt::UserRole + 1).toInt());
    setCDROMBus(ui->tableViewCDROM->model(),
                ui->tableViewCDROM->selectionModel()->currentIndex(),
                ui->comboBoxBus->currentData().toUInt(),
                ui->comboBoxChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_CDROM, ui->tableViewCDROM->model()->data(i, Qt::UserRole).toInt(), ui->tableViewCDROM->model()->data(i, Qt::UserRole + 1).toInt());
    emit cdromChannelChanged();
}

void
SettingsFloppyCDROM::on_comboBoxCDROMType_activated(int)
{
    int type = ui->comboBoxCDROMType->currentData().toUInt();

    setCDROMType(ui->tableViewCDROM->model(),
                 ui->tableViewCDROM->selectionModel()->currentIndex(),
                 type);
    ui->tableViewCDROM->resizeColumnsToContents();
    ui->tableViewCDROM->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    int speed = cdrom_get_speed(type);
    if (speed == -1) {
        speed = ui->comboBoxSpeed->currentData().toUInt();
        ui->comboBoxSpeed->setEnabled(true);
    } else
        ui->comboBoxSpeed->setEnabled(false);
    ui->comboBoxSpeed->setCurrentIndex(speed == 0 ? 7 : speed - 1);

    auto idx = ui->tableViewCDROM->selectionModel()->currentIndex();
    setCDROMSpeed(ui->tableViewCDROM->model(), idx.siblingAtColumn(1), speed);
}
