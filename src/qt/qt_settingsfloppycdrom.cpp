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
#include <cstdint>

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
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/cdrom.h>
#include <86box/scsi_device.h>
#include <86box/scsi_tape.h>
#include <86box/fdd_audio.h>
}

#include "qt_settings_completer.hpp"

#include "qt_settingsfloppycdrom.hpp"
#include "ui_qt_settingsfloppycdrom.h"

#include "qt_models_common.hpp"
#include "qt_harddrive_common.hpp"
#include "qt_settings_bus_tracking.hpp"
#include "qt_preferences.hpp"
#include "qt_defs.hpp"

uint64_t               ifa[FDD_NUM] = { 0 };
int                    inc[CDROM_NUM] = { 0 };

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
SettingsFloppyCDROM::setCDROMBus(QAbstractItemModel *model, const QModelIndex &idx, uint8_t bus, uint32_t type, uint8_t channel)
{
    QIcon icon;

    switch (bus) {
        case CDROM_BUS_DISABLED:
            icon = cdrom_disabled_icon;
            break;
        case CDROM_BUS_ATAPI:
        case CDROM_BUS_SCSI:
        case CDROM_BUS_MITSUMI:
        case CDROM_BUS_PHILIPS:
        case CDROM_BUS_SONY:
        case CDROM_BUS_CM100:
        case CDROM_BUS_HITACHI:
        case CDROM_BUS_MKE:
        case CDROM_BUS_LPT:
            icon = cdrom_is_dvd(type) ? dvdrom_icon : cdrom_icon;
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
    if (speed > 72)
        model->setData(i, QObject::tr("Turbo"));
    else
        model->setData(i, QString("%1x").arg(speed));
    model->setData(i, speed, Qt::UserRole);
}

static QString
CDROMName(int type)
{
    char temp[512];
    cdrom_get_name(type, temp);
    QString name = QObject::tr((const char *) temp);
    int cd  = cdrom_get_speed(type);
    int dvd = cdrom_get_dvd_speed(type);
    int hd  = cdrom_get_hd_speed(type);
    QStringList parts;
    if (cd > 0 && cd != -1) parts << QString("%1x CD").arg(cd);
    if (dvd > 0 && dvd != -1) parts << QString("%1x DVD").arg(dvd);
    if (hd > 0 && hd != -1) {
        if (cdrom_drive_types[type].hd_type == 1)
            parts << QString("%1x HD DVD").arg(hd);
        else
            parts << QString("%1x Blu-ray").arg(hd);
    }
    if (!parts.isEmpty())
        name += " (" + parts.join(" / ") + ")";
    return name;
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
    Harddrives::widenPopup(ui->comboBoxChannel);

    scFloppyType                    = new SettingsCompleter(ui->comboBoxFloppyType, nullptr);
    scCDROMType                     = new SettingsCompleter(ui->comboBoxCDROMType, nullptr);

    floppy_disabled_icon = QIcon(":/settings/qt/icons/floppy_disabled.ico");
    floppy_525_icon      = QIcon(":/settings/qt/icons/floppy_525.ico");
    floppy_35_icon       = QIcon(":/settings/qt/icons/floppy_35.ico");

    auto *model = ui->comboBoxFloppyType->model();
    int   i     = 0;
    {
        Models::Batch floppyTypeRows(model);
        while (true) {
            QString name = tr(fdd_getname(i));
            if (name.isEmpty())
                break;

            floppyTypeRows.add(name, i);
            scFloppyType->addDevice(nullptr, name);
            ++i;
        }
    }

    model = new QStandardItemModel(0, 3, this);
    ui->treeViewFloppy->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, tr("Type"));
    model->setHeaderData(1, Qt::Horizontal, tr("Turbo"));
    model->setHeaderData(2, Qt::Horizontal, tr("Check BPB"));

    model->insertRows(0, FDD_NUM);
    /* Floppy drives category */
    for (int i = 0; i < FDD_NUM; i++) {
        auto idx  = model->index(i, 0);
        int  type = fdd_get_type(&drives[i]);
        setFloppyType(model, idx, type);
        model->setData(idx.siblingAtColumn(1), fdd_get_turbo(&drives[i]) > 0 ? tr("On") : tr("Off"));
        model->setData(idx.siblingAtColumn(2), fdd_get_check_bpb(&drives[i]) > 0 ? tr("On") : tr("Off"));

#ifndef DISABLE_FDD_AUDIO
        ifa[i] = fdd_get_audio_profile(&drives[i]);
#else
        ifa[i] = 0;
#endif
        Harddrives::busTrackClass->device_track(fdd_get_type(&drives[i]) ? 1 : 0, DEV_FDD, TAPE_BUS_FDC, i);
    }

    for (int i = 0; i < model->columnCount(); i++)
        ui->treeViewFloppy->resizeColumnToContents(i);

    connect(ui->treeViewFloppy->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &SettingsFloppyCDROM::onFloppyRowChanged);

#ifndef DISABLE_FDD_AUDIO
    ui->comboBoxFloppyAudio->setVisible(true);
    int profile_count = fdd_audio_get_profile_count();
    if (!profile_count) {
        /* If no profiles found, add "None" and disable the combobox */
        ui->comboBoxFloppyAudio->addItem(tr("None"), 0);
        ui->comboBoxFloppyAudio->setEnabled(false);
    } else
        for (int i = 0; i < profile_count; i++) {
            const char *name = fdd_audio_get_profile_name(i);
                ui->comboBoxFloppyAudio->addItem(tr(name), i);
        }
    ui->comboBoxFloppyAudio->setSizeAdjustPolicy(QComboBox::AdjustToContents);
#else
    ui->comboBoxFloppyAudio->setEnabled(false);
    ui->comboBoxFloppyAudio->setVisible(false);
#endif

    // Set initial selection and trigger the row changed event to update controls
    ui->treeViewFloppy->setCurrentIndex(model->index(0, 0));
    // Manually trigger the row changed event to ensure audio selection is updated
    onFloppyRowChanged(model->index(0, 0));

    cdrom_disabled_icon = QIcon(":/settings/qt/icons/cdrom_disabled.ico");
    cdrom_icon          = QIcon(":/settings/qt/icons/cdrom.ico");
    dvdrom_icon         = QIcon(":/settings/qt/icons/dvdrom.ico");

    Harddrives::populateCDROMBuses(ui->comboBoxBus->model());

    auto populate = [](QComboBox *box, int max, int turboVal) {
        for (int i = 1; i <= max; i++)
            box->addItem(QString("%1x").arg(i), i);
        box->addItem(tr("Turbo"), turboVal);
    };
    populate(ui->comboBoxCDSpeed, 72, 73);
    populate(ui->comboBoxDVDSpeed, 16, 17);
    populate(ui->comboBoxHDBDSpeed, 2, 3);

    /* Size speed comboboxes to fit "Turbo" only */
    auto sizeToTurbo = [](QComboBox *box) {
        QFontMetrics fm(box->font());
        int w = fm.horizontalAdvance(tr("Turbo")) + 30; /* padding for arrow */
        box->setMaximumWidth(w);
        box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    };
    sizeToTurbo(ui->comboBoxCDSpeed);
    sizeToTurbo(ui->comboBoxDVDSpeed);
    sizeToTurbo(ui->comboBoxHDBDSpeed);

    model = new QStandardItemModel(0, 3, this);
    ui->treeViewCDROM->setModel(model);
    model->setHeaderData(0, Qt::Horizontal, tr("Bus"));
    model->setHeaderData(1, Qt::Horizontal, tr("Speed"));
    model->setHeaderData(2, Qt::Horizontal, tr("Type"));
    model->insertRows(0, CDROM_NUM);
    for (int i = 0; i < CDROM_NUM; i++) {
        auto idx  = model->index(i, 0);
        int  type = cdrom_get_type(i);
        setCDROMBus(model, idx, cdrom[i].bus_type, type, cdrom[i].res);
        setCDROMType(model, idx.siblingAtColumn(2), type);
        int speed = cdrom_get_speed(type);
        if (speed == -1)
            setCDROMSpeed(model, idx.siblingAtColumn(1), cdrom[i].speed);
        else
            setCDROMSpeed(model, idx.siblingAtColumn(1), speed);
        model->setData(idx.siblingAtColumn(1), cdrom[i].dvd_speed, Qt::UserRole + 2);
        model->setData(idx.siblingAtColumn(1), cdrom[i].hd_speed, Qt::UserRole + 3);
        model->setData(idx.siblingAtColumn(1), cdrom[i].restricted_capacity, Qt::UserRole + 4);
        if (cdrom[i].bus_type == CDROM_BUS_HITACHI)
            Harddrives::busTrackClass->device_track(1, DEV_CDROM, cdrom[i].bus_type, cdrom[i].hitachi_channel);
        else if (cdrom[i].bus_type == CDROM_BUS_MKE)
            Harddrives::busTrackClass->device_track(1, DEV_CDROM, cdrom[i].bus_type, cdrom[i].mke_channel);
        else if (cdrom[i].bus_type == CDROM_BUS_ATAPI)
            Harddrives::busTrackClass->device_track(1, DEV_CDROM, cdrom[i].bus_type, cdrom[i].ide_channel);
        else if (cdrom[i].bus_type == CDROM_BUS_SCSI)
            Harddrives::busTrackClass->device_track(1, DEV_CDROM, cdrom[i].bus_type, cdrom[i].scsi_device_id);
        else if ((cdrom[i].bus_type == CDROM_BUS_MITSUMI) || (cdrom[i].bus_type == CDROM_BUS_PHILIPS) || (cdrom[i].bus_type == CDROM_BUS_CM100) || (cdrom[i].bus_type == CDROM_BUS_SONY))
            Harddrives::busTrackClass->device_track(1, DEV_CDROM, cdrom[i].bus_type, 0);
        else if (cdrom[i].bus_type == CDROM_BUS_LPT)
            Harddrives::busTrackClass->device_track(1, DEV_CDROM, cdrom[i].bus_type, cdrom[i].res);
        inc[i] = cdrom[i].no_check;
    }
    for (int i = 0; i < model->columnCount(); i++)
        ui->treeViewCDROM->resizeColumnToContents(i);

    connect(ui->treeViewCDROM->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &SettingsFloppyCDROM::onCDROMRowChanged);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(ui->checkBoxUnlimitedCapacity, &QCheckBox::checkStateChanged,
            this, &SettingsFloppyCDROM::onUnlimitedCapacityChanged);
#else
    connect(ui->checkBoxUnlimitedCapacity, &QCheckBox::stateChanged,
            this, &SettingsFloppyCDROM::onUnlimitedCapacityChanged);
#endif
    ui->treeViewCDROM->setCurrentIndex(model->index(0, 0));

    uint8_t bus_type = ui->comboBoxBus->currentData().toUInt();
    int     cdromIdx = ui->treeViewCDROM->selectionModel()->currentIndex().data().toInt();

    auto *modelType  = ui->comboBoxCDROMType->model();
    int   removeRows = modelType->rowCount();

    uint32_t j               = 0;
    int      selectedTypeRow = 0;
    int      eligibleRows    = 0;
    scCDROMType->removeRows();
    Models::Batch typeRows(modelType);
    while (cdrom_drive_types[j].bus_type != BUS_TYPE_NONE) {
        if (((bus_type == CDROM_BUS_CM100) || (bus_type == CDROM_BUS_SONY) || (bus_type == CDROM_BUS_PHILIPS) || (bus_type == CDROM_BUS_HITACHI) || (bus_type == CDROM_BUS_MKE) || (bus_type == CDROM_BUS_ATAPI) || (bus_type == CDROM_BUS_SCSI) ||
             (bus_type == CDROM_BUS_LPT)) &&
            ((cdrom_drive_types[j].bus_type == bus_type) ||
             ((cdrom_drive_types[j].bus_type == BUS_TYPE_IDE) && (bus_type == CDROM_BUS_LPT)) ||
             ((cdrom_drive_types[j].bus_type == BUS_TYPE_BOTH) && (bus_type != BUS_TYPE_MKE) && (bus_type != BUS_TYPE_HITACHI) && (bus_type != CDROM_BUS_PHILIPS) && (bus_type != CDROM_BUS_CM100) && (bus_type != CDROM_BUS_SONY)))) {
            QString name = CDROMName(j);
            typeRows.add(name, j);
            scCDROMType->addDevice(nullptr, name);
            if (cdrom[cdromIdx].type == j)
                selectedTypeRow = eligibleRows;
            ++eligibleRows;
        }
        ++j;
    }
    typeRows.commit();
    modelType->removeRows(0, removeRows);
    ui->comboBoxCDROMType->setEnabled(eligibleRows > 1);
    ui->comboBoxCDROMType->setCurrentIndex(-1);
    ui->comboBoxCDROMType->setCurrentIndex(selectedTypeRow);
    onCurrentMachineChanged(machine);
}

SettingsFloppyCDROM::~SettingsFloppyCDROM()
{
    delete scCDROMType;
    delete scFloppyType;

    delete ui;
}

void
SettingsFloppyCDROM::onCurrentMachineChanged(int machineId)
{
    const bool fixed = machines[machineId].init == machine_ibm5140_init;
    auto *model = ui->treeViewFloppy->model();
    ui->comboBoxFloppyType->setEnabled(!fixed);
    ui->checkBoxTurboTimings->setEnabled(!fixed);
    for (int i = 0; i < FDD_NUM; i++) {
        ui->treeViewFloppy->setRowHidden(i, QModelIndex(), fixed && i >= 2);
        if (fixed) {
            const auto idx = model->index(i, 0);
            setFloppyType(model, idx, i < 2 ? fdd_get_from_internal_name((char *) "35_2dd") : 0);
            model->setData(idx.siblingAtColumn(1), tr("Off"));
        }
    }
    if (fixed)
        ui->treeViewFloppy->setCurrentIndex(model->index(0, 0));
    onFloppyRowChanged(ui->treeViewFloppy->currentIndex());
}

int
SettingsFloppyCDROM::changed()
{
    int has_changed  = 0;
    int soft_changed = 0;

    auto *model = ui->treeViewFloppy->model();
    for (int i = 0; i < FDD_NUM; i++) {
        has_changed  |= (fdd_get_type(&drives[i])          != model->index(i, 0).data(Qt::UserRole).toInt());
        has_changed  |= (fdd_get_turbo(&drives[i])         != (model->index(i, 1).data() == tr("On") ? 1 : 0));
        has_changed  |= (fdd_get_check_bpb(&drives[i])     != (model->index(i, 2).data() == tr("On") ? 1 : 0));
#ifndef DISABLE_FDD_AUDIO
        has_changed  |= (fdd_get_audio_profile(&drives[i]) != (int) (uint32_t) ifa[i]);
#endif
    }

    /* Removable devices category */
    model = ui->treeViewCDROM->model();
    for (int i = 0; i < CDROM_NUM; i++) {
        has_changed  |= (cdrom[i].bus_type        != model->index(i, 0).data(Qt::UserRole).toUInt());
        has_changed  |= (cdrom[i].res             != model->index(i, 0).data(Qt::UserRole + 1).toUInt());
        has_changed  |= cdrom[i].speed && (cdrom[i].speed != model->index(i, 1).data(Qt::UserRole).toUInt());
        has_changed  |= (cdrom_get_type(i)        != model->index(i, 2).data(Qt::UserRole).toInt());
        has_changed  |= (cdrom[i].restricted_capacity != model->index(i, 1).data(Qt::UserRole + 4).toUInt());
        soft_changed |= (cdrom[i].no_check        != inc[i]);
    }

    return has_changed ? (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET) :
                         (soft_changed ? SETTINGS_CHANGED : 0);
}

void
SettingsFloppyCDROM::restore()
{
}

void
SettingsFloppyCDROM::save(int soft)
{
    if (soft) {
        for (int i = 0; i < CDROM_NUM; i++)
            cdrom[i].no_check = inc[i];
        return;
    }

    auto *model = ui->treeViewFloppy->model();
    for (int i = 0; i < FDD_NUM; i++) {
        fdd_set_type(&drives[i], model->index(i, 0).data(Qt::UserRole).toInt());
        fdd_set_turbo(&drives[i], model->index(i, 1).data() == tr("On") ? 1 : 0);
        fdd_set_check_bpb(&drives[i], model->index(i, 2).data() == tr("On") ? 1 : 0);
#ifndef DISABLE_FDD_AUDIO
        fdd_set_audio_profile(&drives[i], ifa[i]);
#endif
    }

    /* Removable devices category */
    model = ui->treeViewCDROM->model();
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
        cdrom[i].dvd_speed   = model->index(i, 1).data(Qt::UserRole + 2).toUInt();
        cdrom[i].hd_speed    = model->index(i, 1).data(Qt::UserRole + 3).toUInt();
        cdrom[i].no_check    = inc[i];
        cdrom[i].restricted_capacity = model->index(i, 1).data(Qt::UserRole + 4).toUInt();
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

    int prof = ifa[current.row()];

#ifndef DISABLE_FDD_AUDIO
    // Rebuild audio profile combo box based on drive type
    ui->comboBoxFloppyAudio->clear();

    int profile_count = fdd_audio_get_profile_count();
    if ((!profile_count) || (type == 0)) {
        ui->comboBoxFloppyAudio->addItem(tr("None"), 0);
        ui->comboBoxFloppyAudio->setCurrentIndex(0);
        ui->comboBoxFloppyAudio->setEnabled(false);

        // Update the model to reflect "None" profile
        ifa[current.row()] = 0;
        return;
    }

    ui->comboBoxFloppyAudio->setEnabled(true);

    // Get drive type's track count to determine 40-track vs 80-track
    int  drive_max_tracks = fdd_get_type_max_track(type);
    bool is_40_track      = (drive_max_tracks <= 43);

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
                    ui->comboBoxFloppyAudio->addItem(tr(name), i);
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
        ifa[current.row()] = 0;
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

    ui->checkBoxErrorCheck->setChecked(!inc[current.row()]);

    ui->comboBoxBus->setCurrentIndex(-1);
    auto *model = ui->comboBoxBus->model();
    auto  match = model->match(model->index(0, 0), Qt::UserRole, bus);
    if (!match.isEmpty())
        ui->comboBoxBus->setCurrentIndex(match.first().row());

    model = ui->comboBoxChannel->model();
    match = model->match(model->index(0, 0), Qt::UserRole, channel);
    if (!match.isEmpty())
        ui->comboBoxChannel->setCurrentIndex(match.first().row());

    /* Unlimited capacity */
    ui->checkBoxUnlimitedCapacity->setChecked(!current.siblingAtColumn(1).data(Qt::UserRole + 4).toUInt());

    /* CD / DVD / HD speeds */
    auto setSpeed = [](QComboBox *box, int fixed, int userVal, int turboThreshold) {
        bool selectable = (fixed == -1);
        box->setEnabled(selectable);
        int val = selectable ? userVal : fixed;
        if (selectable && val >= turboThreshold)
            val = box->itemData(box->count() - 1).toInt(); /* Turbo item */
        int idx = box->findData(val);
        if (idx >= 0) box->setCurrentIndex(idx);
    };
    int userCd  = current.siblingAtColumn(1).data(Qt::UserRole).toUInt();
    int userDvd = current.siblingAtColumn(1).data(Qt::UserRole + 2).toUInt();
    int userHd  = current.siblingAtColumn(1).data(Qt::UserRole + 3).toUInt();
    setSpeed(ui->comboBoxCDSpeed, cdrom_get_speed(type), userCd, 73);
    setSpeed(ui->comboBoxDVDSpeed, cdrom_get_dvd_speed(type), userDvd, 17);
    setSpeed(ui->comboBoxHDBDSpeed, cdrom_get_hd_speed(type), userHd, 3);

    auto *modelType  = ui->comboBoxCDROMType->model();
    int   removeRows = modelType->rowCount();

    uint32_t j               = 0;
    int      selectedTypeRow = 0;
    int      eligibleRows    = 0;
    scCDROMType->removeRows();
    Models::Batch typeRows(modelType);
    while (cdrom_drive_types[j].bus_type != BUS_TYPE_NONE) {
        if (((bus == CDROM_BUS_CM100) || (bus == CDROM_BUS_SONY) || (bus == CDROM_BUS_PHILIPS) || (bus == CDROM_BUS_HITACHI) || (bus == CDROM_BUS_MKE) || (bus == CDROM_BUS_ATAPI) || (bus == CDROM_BUS_SCSI) ||
             (bus == CDROM_BUS_LPT)) &&
            ((cdrom_drive_types[j].bus_type == bus) ||
             ((cdrom_drive_types[j].bus_type == BUS_TYPE_IDE) && (bus == CDROM_BUS_LPT)) ||
             ((cdrom_drive_types[j].bus_type == BUS_TYPE_BOTH) && (bus != BUS_TYPE_MKE) && (bus != BUS_TYPE_HITACHI) && (bus != CDROM_BUS_PHILIPS) && (bus != CDROM_BUS_CM100) && (bus != CDROM_BUS_SONY)))) {
            QString name = CDROMName(j);
            typeRows.add(name, j);
            scCDROMType->addDevice(nullptr, name);
            if (type == j)
                selectedTypeRow = eligibleRows;
            ++eligibleRows;
        }
        ++j;
    }
    typeRows.commit();
    modelType->removeRows(0, removeRows);
    ui->comboBoxCDROMType->setEnabled(eligibleRows > 1);
    ui->comboBoxCDROMType->setCurrentIndex(-1);
    ui->comboBoxCDROMType->setCurrentIndex(selectedTypeRow);

    ui->checkBoxErrorCheck->setEnabled((bus == CDROM_BUS_DISABLED) ? false : true);

    enableCurrentlySelectedChannel();
}

void
SettingsFloppyCDROM::on_checkBoxTurboTimings_stateChanged(int arg1)
{
    auto idx = ui->treeViewFloppy->selectionModel()->currentIndex();
    ui->treeViewFloppy->model()->setData(idx.siblingAtColumn(1), arg1 == Qt::Checked ? tr("On") : tr("Off"));
}

void
SettingsFloppyCDROM::on_checkBoxCheckBPB_stateChanged(int arg1)
{
    auto idx = ui->treeViewFloppy->selectionModel()->currentIndex();
    ui->treeViewFloppy->model()->setData(idx.siblingAtColumn(2), arg1 == Qt::Checked ? tr("On") : tr("Off"));
}

void
SettingsFloppyCDROM::on_checkBoxErrorCheck_stateChanged(int arg1)
{
    auto idx = ui->treeViewFloppy->selectionModel()->currentIndex();
    inc[idx.row()] = (arg1 != Qt::Checked);
}

void
SettingsFloppyCDROM::on_comboBoxFloppyType_activated(int index)
{
    auto currentIndex = ui->treeViewFloppy->selectionModel()->currentIndex();
    auto typeIndex    = currentIndex.siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_FDD, TAPE_BUS_FDC, currentIndex.row());
    setFloppyType(ui->treeViewFloppy->model(), typeIndex, index);
    Harddrives::busTrackClass->device_track(index ? 1 : 0, DEV_FDD, TAPE_BUS_FDC, currentIndex.row());
    ui->treeViewFloppy->resizeColumnToContents(0);

    // Trigger row changed to rebuild audio profile list
    onFloppyRowChanged(currentIndex);
}

void
SettingsFloppyCDROM::on_comboBoxFloppyAudio_activated(int)
{
    auto    idx  = ui->treeViewFloppy->selectionModel()->currentIndex();
    int     prof = ui->comboBoxFloppyAudio->currentData().toInt();

#ifndef DISABLE_FDD_AUDIO
    ifa[idx.row()] = prof;
#else
    ifa[i] = 0;
#endif
}

/* The machine, disk controllers and sound cards chosen on other pages
   decide who has each IDE channel: bring the names up to date. */
void
SettingsFloppyCDROM::showEvent(QShowEvent *event)
{
    Harddrives::refreshBusNames(ui->treeViewCDROM->model());
    reloadBusChannels();
    QWidget::showEvent(event);
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
        ui->comboBoxChannel->setEnabled(((bus == CDROM_BUS_MITSUMI) || (bus == CDROM_BUS_PHILIPS) || (bus == CDROM_BUS_CM100) || (bus == CDROM_BUS_SONY)) ? 0 : enabled);        ui->comboBoxCDROMType->setEnabled((bus == CDROM_BUS_MITSUMI) ? 0 : enabled);
        ui->checkBoxErrorCheck->setEnabled(((bus == CDROM_BUS_MITSUMI) || (bus == CDROM_BUS_PHILIPS) || (bus == CDROM_BUS_CM100) || (bus == CDROM_BUS_SONY)) ? 0 : enabled);

        Harddrives::populateBusChannels(ui->comboBoxChannel->model(), bus, Harddrives::busTrackClass);
    }
}

void
SettingsFloppyCDROM::on_comboBoxBus_activated(int)
{
    int type = ui->comboBoxCDROMType->currentData().toUInt();
    auto current = ui->treeViewCDROM->selectionModel()->currentIndex();
    auto    i        = ui->treeViewCDROM->selectionModel()->currentIndex().siblingAtColumn(0);
    uint8_t bus_type = ui->comboBoxBus->currentData().toUInt();
    int     cdromIdx = ui->treeViewCDROM->selectionModel()->currentIndex().data().toInt();

    Harddrives::busTrackClass->device_track(0, DEV_CDROM, ui->treeViewCDROM->model()->data(i, Qt::UserRole).toInt(), ui->treeViewCDROM->model()->data(i, Qt::UserRole + 1).toInt());
    if (bus_type == CDROM_BUS_HITACHI)
        ui->comboBoxChannel->setCurrentIndex(Harddrives::busTrackClass->next_free_hitachi_channel());
    else if (bus_type == CDROM_BUS_MKE)
        ui->comboBoxChannel->setCurrentIndex(Harddrives::busTrackClass->next_free_mke_channel());
    else if (bus_type == CDROM_BUS_ATAPI)
        ui->comboBoxChannel->setCurrentIndex(Harddrives::busTrackClass->next_free_ide_channel());
    else if (bus_type == CDROM_BUS_SCSI)
        ui->comboBoxChannel->setCurrentIndex(Harddrives::busTrackClass->next_free_scsi_id());
    else if (bus_type == CDROM_BUS_LPT)
        ui->comboBoxChannel->setCurrentIndex(Harddrives::busTrackClass->next_free_lpt_port());
    else if ((bus_type == CDROM_BUS_MITSUMI) || (bus_type == CDROM_BUS_PHILIPS) || (bus_type == CDROM_BUS_CM100) || (bus_type == CDROM_BUS_SONY))
        ui->comboBoxChannel->setCurrentIndex(0);

    setCDROMBus(ui->treeViewCDROM->model(),
                ui->treeViewCDROM->selectionModel()->currentIndex(),
                bus_type, cdrom[cdromIdx].type,
                ui->comboBoxChannel->currentData().toUInt());
    Harddrives::busTrackClass->device_track(1, DEV_CDROM, ui->treeViewCDROM->model()->data(i, Qt::UserRole).toInt(), ui->treeViewCDROM->model()->data(i, Qt::UserRole + 1).toInt());

    auto *modelType  = ui->comboBoxCDROMType->model();
    int   removeRows = modelType->rowCount();

    uint32_t j               = 0;
    int      selectedTypeRow = 0;
    int      eligibleRows    = 0;
    scCDROMType->removeRows();
    Models::Batch typeRows(modelType);
    while (cdrom_drive_types[j].bus_type != BUS_TYPE_NONE) {
        if (((bus_type == CDROM_BUS_CM100) || (bus_type == CDROM_BUS_SONY) || (bus_type == CDROM_BUS_PHILIPS) || (bus_type == CDROM_BUS_HITACHI) || (bus_type == CDROM_BUS_MKE) || (bus_type == CDROM_BUS_ATAPI) || (bus_type == CDROM_BUS_SCSI) ||
             (bus_type == CDROM_BUS_LPT)) &&
            ((cdrom_drive_types[j].bus_type == bus_type) ||
             ((cdrom_drive_types[j].bus_type == BUS_TYPE_IDE) && (bus_type == CDROM_BUS_LPT)) ||
             ((cdrom_drive_types[j].bus_type == BUS_TYPE_BOTH) && (bus_type != BUS_TYPE_MKE) && (bus_type != BUS_TYPE_HITACHI) && (bus_type != CDROM_BUS_PHILIPS) && (bus_type != CDROM_BUS_CM100) && (bus_type != CDROM_BUS_SONY)))) {
            QString name = CDROMName(j);
            typeRows.add(name, j);
            scCDROMType->addDevice(nullptr, name);
            if (cdrom[cdromIdx].type == j)
                selectedTypeRow = eligibleRows;
            ++eligibleRows;
        }
        ++j;
    }
    typeRows.commit();
    modelType->removeRows(0, removeRows);
    ui->comboBoxCDROMType->setEnabled(eligibleRows > 1);
    ui->comboBoxCDROMType->setCurrentIndex(-1);
    ui->comboBoxCDROMType->setCurrentIndex(selectedTypeRow);

    setCDROMType(ui->treeViewCDROM->model(),
                 ui->treeViewCDROM->selectionModel()->currentIndex(),
                 ui->comboBoxCDROMType->currentData().toUInt());

    int speed = cdrom_get_speed(ui->comboBoxCDROMType->currentData().toUInt());
    if ((speed == -1) && (bus_type != CDROM_BUS_MITSUMI)) {
        speed = cdrom[current.row()].speed;
    } else {
        if (bus_type == CDROM_BUS_MITSUMI)
            speed = 2;
    }
    /* Unlimited capacity */
    ui->checkBoxUnlimitedCapacity->setChecked(!current.siblingAtColumn(1).data(Qt::UserRole + 4).toUInt());

    /* CD / DVD / HD speeds */
    auto setSpeed = [](QComboBox *box, int fixed, int userVal, int turboThreshold) {
        bool selectable = (fixed == -1);
        box->setEnabled(selectable);
        int val = selectable ? userVal : fixed;
        if (selectable && val >= turboThreshold)
            val = box->itemData(box->count() - 1).toInt(); /* Turbo item */
        int idx = box->findData(val);
        if (idx >= 0) box->setCurrentIndex(idx);
    };
    int userCd  = current.siblingAtColumn(1).data(Qt::UserRole).toUInt();
    int userDvd = current.siblingAtColumn(1).data(Qt::UserRole + 2).toUInt();
    int userHd  = current.siblingAtColumn(1).data(Qt::UserRole + 3).toUInt();
    setSpeed(ui->comboBoxCDSpeed, cdrom_get_speed(type), userCd, 73);
    setSpeed(ui->comboBoxDVDSpeed, cdrom_get_dvd_speed(type), userDvd, 17);
    setSpeed(ui->comboBoxHDBDSpeed, cdrom_get_hd_speed(type), userHd, 3);
    setCDROMSpeed(ui->treeViewCDROM->model(),
                  ui->treeViewCDROM->selectionModel()->currentIndex(),
                  speed);
    /* Persist effective speeds into the model */
    auto cur = ui->treeViewCDROM->selectionModel()->currentIndex();
    ui->treeViewCDROM->model()->setData(cur.siblingAtColumn(1), ui->comboBoxDVDSpeed->currentData().toUInt(), Qt::UserRole + 2);
    ui->treeViewCDROM->model()->setData(cur.siblingAtColumn(1), ui->comboBoxHDBDSpeed->currentData().toUInt(), Qt::UserRole + 3);
    for (int i = 0; i < ui->treeViewCDROM->model()->columnCount(); i++)
        ui->treeViewCDROM->resizeColumnToContents(i);
    ui->checkBoxErrorCheck->setEnabled(bus_type != CDROM_BUS_DISABLED);
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
    int type = ui->comboBoxCDROMType->currentData().toUInt();
    auto i = ui->treeViewCDROM->selectionModel()->currentIndex().siblingAtColumn(0);
    Harddrives::busTrackClass->device_track(0, DEV_CDROM, ui->treeViewCDROM->model()->data(i, Qt::UserRole).toInt(), ui->treeViewCDROM->model()->data(i, Qt::UserRole + 1).toInt());
    setCDROMBus(ui->treeViewCDROM->model(),
                ui->treeViewCDROM->selectionModel()->currentIndex(),
                ui->comboBoxBus->currentData().toUInt(), type,
                ui->comboBoxChannel->currentData().toUInt());
    ui->treeViewCDROM->resizeColumnToContents(0);
    Harddrives::busTrackClass->device_track(1, DEV_CDROM, ui->treeViewCDROM->model()->data(i, Qt::UserRole).toInt(), ui->treeViewCDROM->model()->data(i, Qt::UserRole + 1).toInt());
    emit cdromChannelChanged();
}

void
SettingsFloppyCDROM::on_comboBoxCDROMType_activated(int)
{
    int type = ui->comboBoxCDROMType->currentData().toUInt();
    auto current = ui->treeViewCDROM->selectionModel()->currentIndex();

    setCDROMType(ui->treeViewCDROM->model(),
                 current,
                 type);
    ui->treeViewCDROM->resizeColumnToContents(2);

    int speed = cdrom_get_speed(type);
    if (speed == -1) {
        speed = cdrom[current.row()].speed;
    }
    /* Unlimited capacity */
    ui->checkBoxUnlimitedCapacity->setChecked(!current.siblingAtColumn(1).data(Qt::UserRole + 4).toUInt());

    /* CD / DVD / HD speeds */
    auto setSpeed = [](QComboBox *box, int fixed, int userVal, int turboThreshold) {
        bool selectable = (fixed == -1);
        box->setEnabled(selectable);
        int val = selectable ? userVal : fixed;
        if (selectable && val >= turboThreshold)
            val = box->itemData(box->count() - 1).toInt(); /* Turbo item */
        int idx = box->findData(val);
        if (idx >= 0) box->setCurrentIndex(idx);
    };
    int userCd  = current.siblingAtColumn(1).data(Qt::UserRole).toUInt();
    int userDvd = current.siblingAtColumn(1).data(Qt::UserRole + 2).toUInt();
    int userHd  = current.siblingAtColumn(1).data(Qt::UserRole + 3).toUInt();
    setSpeed(ui->comboBoxCDSpeed, cdrom_get_speed(type), userCd, 73);
    setSpeed(ui->comboBoxDVDSpeed, cdrom_get_dvd_speed(type), userDvd, 17);
    setSpeed(ui->comboBoxHDBDSpeed, cdrom_get_hd_speed(type), userHd, 3);

    auto idx = ui->treeViewCDROM->selectionModel()->currentIndex();
    setCDROMSpeed(ui->treeViewCDROM->model(), idx.siblingAtColumn(1), speed);
    /* Persist effective speeds into the model */
    ui->treeViewCDROM->model()->setData(idx.siblingAtColumn(1), ui->comboBoxDVDSpeed->currentData().toUInt(), Qt::UserRole + 2);
    ui->treeViewCDROM->model()->setData(idx.siblingAtColumn(1), ui->comboBoxHDBDSpeed->currentData().toUInt(), Qt::UserRole + 3);

    setCDROMBus(ui->treeViewCDROM->model(),
                ui->treeViewCDROM->selectionModel()->currentIndex(),
                ui->comboBoxBus->currentData().toUInt(), type,
                ui->comboBoxChannel->currentData().toUInt());
}

void
SettingsFloppyCDROM::on_comboBoxCDSpeed_activated(int)
{
    auto idx = ui->treeViewCDROM->selectionModel()->currentIndex();
    int speed = ui->comboBoxCDSpeed->currentData().toUInt();
    setCDROMSpeed(ui->treeViewCDROM->model(), idx, speed);
}

void
SettingsFloppyCDROM::on_comboBoxDVDSpeed_activated(int)
{
    auto idx = ui->treeViewCDROM->selectionModel()->currentIndex();
    int speed = ui->comboBoxDVDSpeed->currentData().toUInt();
    ui->treeViewCDROM->model()->setData(idx.siblingAtColumn(1), speed, Qt::UserRole + 2);
}

void
SettingsFloppyCDROM::on_comboBoxHDBDSpeed_activated(int)
{
    auto idx = ui->treeViewCDROM->selectionModel()->currentIndex();
    int speed = ui->comboBoxHDBDSpeed->currentData().toUInt();
    ui->treeViewCDROM->model()->setData(idx.siblingAtColumn(1), speed, Qt::UserRole + 3);
}

void
SettingsFloppyCDROM::onUnlimitedCapacityChanged(
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    Qt::CheckState state
#else
    int state
#endif
)
{
    const QModelIndex current = ui->treeViewCDROM->selectionModel()->currentIndex();
    if (!current.isValid())
        return;

    /* Store the inverse of the checkbox (1 = restricted, 0 = unlimited). */
    ui->treeViewCDROM->model()->setData(current.siblingAtColumn(1),
        (state == Qt::Unchecked) ? 1 : 0, Qt::UserRole + 4);
}
