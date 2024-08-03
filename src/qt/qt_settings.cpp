/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Program settings UI module.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 */
#include "qt_settings.hpp"
#include "ui_qt_settings.h"

#include "qt_settingsmachine.hpp"
#include "qt_settingsdisplay.hpp"
#include "qt_settingsinput.hpp"
#include "qt_settingssound.hpp"
#include "qt_settingsnetwork.hpp"
#include "qt_settingsports.hpp"
#include "qt_settingsstoragecontrollers.hpp"
#include "qt_settingsharddisks.hpp"
#include "qt_settingsfloppycdrom.hpp"
#include "qt_settingsotherremovable.hpp"
#include "qt_settingsotherperipherals.hpp"

#include "qt_progsettings.hpp"
#include "qt_harddrive_common.hpp"
#include "qt_settings_bus_tracking.hpp"

extern "C" {
#include <86box/86box.h>
}

#include <QDebug>
#include <QMessageBox>
#include <QCheckBox>
#include <QApplication>
#include <QStyle>

class SettingsModel : public QAbstractListModel {
public:
    SettingsModel(QObject *parent)
        : QAbstractListModel(parent)
    {
        fontHeight = QApplication::fontMetrics().height();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;

private:
    QStringList pages = {
        "Machine",
        "Display",
        "Input devices",
        "Sound",
        "Network",
        "Ports (COM & LPT)",
        "Storage controllers",
        "Hard disks",
        "Floppy & CD-ROM drives",
        "Other removable devices",
        "Other peripherals",
    };
    QStringList page_icons = {
        "machine",
        "display",
        "input_devices",
        "sound",
        "network",
        "ports",
        "storage_controllers",
        "hard_disk",
        "floppy_and_cdrom_drives",
        "other_removable_devices",
        "other_peripherals",
    };
    int fontHeight;
};

QVariant
SettingsModel::data(const QModelIndex &index, int role) const
{
    Q_ASSERT(checkIndex(index, QAbstractItemModel::CheckIndexOption::IndexIsValid | QAbstractItemModel::CheckIndexOption::ParentIsInvalid));

    switch (role) {
        case Qt::DisplayRole:
            return tr(pages.at(index.row()).toUtf8().data());
        case Qt::DecorationRole:
            return QIcon(QString("%1/%2.ico").arg(ProgSettings::getIconSetPath(), page_icons[index.row()]));
        case Qt::SizeHintRole:
            return QSize(-1, fontHeight * 2);
        default:
            return {};
    }
}

int
SettingsModel::rowCount(const QModelIndex &parent) const
{
    (void) parent;
    return pages.size();
}

Settings *Settings::settings = nullptr;
;
Settings::Settings(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Settings)
{
    ui->setupUi(this);
    auto *model = new SettingsModel(this);
    ui->listView->setModel(model);

    Harddrives::busTrackClass = new SettingsBusTracking;
    machine                   = new SettingsMachine(this);
    display                   = new SettingsDisplay(this);
    input                     = new SettingsInput(this);
    sound                     = new SettingsSound(this);
    network                   = new SettingsNetwork(this);
    ports                     = new SettingsPorts(this);
    storageControllers        = new SettingsStorageControllers(this);
    harddisks                 = new SettingsHarddisks(this);
    floppyCdrom               = new SettingsFloppyCDROM(this);
    otherRemovable            = new SettingsOtherRemovable(this);
    otherPeripherals          = new SettingsOtherPeripherals(this);

    ui->stackedWidget->addWidget(machine);
    ui->stackedWidget->addWidget(display);
    ui->stackedWidget->addWidget(input);
    ui->stackedWidget->addWidget(sound);
    ui->stackedWidget->addWidget(network);
    ui->stackedWidget->addWidget(ports);
    ui->stackedWidget->addWidget(storageControllers);
    ui->stackedWidget->addWidget(harddisks);
    ui->stackedWidget->addWidget(floppyCdrom);
    ui->stackedWidget->addWidget(otherRemovable);
    ui->stackedWidget->addWidget(otherPeripherals);

    connect(machine, &SettingsMachine::currentMachineChanged, display,
            &SettingsDisplay::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, input,
            &SettingsInput::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, sound,
            &SettingsSound::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, network,
            &SettingsNetwork::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, storageControllers,
            &SettingsStorageControllers::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, otherPeripherals,
            &SettingsOtherPeripherals::onCurrentMachineChanged);
    connect(floppyCdrom, &SettingsFloppyCDROM::cdromChannelChanged, harddisks,
            &SettingsHarddisks::reloadBusChannels);
    connect(floppyCdrom, &SettingsFloppyCDROM::cdromChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_MO);
    connect(floppyCdrom, &SettingsFloppyCDROM::cdromChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_ZIP);
    connect(harddisks, &SettingsHarddisks::driveChannelChanged, floppyCdrom,
            &SettingsFloppyCDROM::reloadBusChannels);
    connect(harddisks, &SettingsHarddisks::driveChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_MO);
    connect(harddisks, &SettingsHarddisks::driveChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_ZIP);
    connect(otherRemovable, &SettingsOtherRemovable::moChannelChanged, harddisks,
            &SettingsHarddisks::reloadBusChannels);
    connect(otherRemovable, &SettingsOtherRemovable::moChannelChanged, floppyCdrom,
            &SettingsFloppyCDROM::reloadBusChannels);
    connect(otherRemovable, &SettingsOtherRemovable::moChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_ZIP);
    connect(otherRemovable, &SettingsOtherRemovable::zipChannelChanged, harddisks,
            &SettingsHarddisks::reloadBusChannels);
    connect(otherRemovable, &SettingsOtherRemovable::zipChannelChanged, floppyCdrom,
            &SettingsFloppyCDROM::reloadBusChannels);
    connect(otherRemovable, &SettingsOtherRemovable::zipChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_MO);

    connect(ui->listView->selectionModel(), &QItemSelectionModel::currentChanged, this,
           [this](const QModelIndex &current, const QModelIndex &previous) {
                  ui->stackedWidget->setCurrentIndex(current.row()); });

    ui->listView->setCurrentIndex(model->index(0, 0));

    Settings::settings = this;
}

Settings::~Settings()
{
    delete ui;
    delete Harddrives::busTrackClass;
    Harddrives::busTrackClass = nullptr;
    Settings::settings        = nullptr;
}

void
Settings::save()
{
    machine->save();
    display->save();
    input->save();
    sound->save();
    network->save();
    ports->save();
    storageControllers->save();
    harddisks->save();
    floppyCdrom->save();
    otherRemovable->save();
    otherPeripherals->save();
}

void
Settings::accept()
{
    if (confirm_save && !settings_only) {
        QMessageBox questionbox(QMessageBox::Icon::Question, "86Box",
                                QStringLiteral("%1\n\n%2").arg(tr("Do you want to save the settings?"),
                                tr("This will hard reset the emulated machine.")),
                                QMessageBox::Save | QMessageBox::Cancel, this);
        QCheckBox  *chkbox = new QCheckBox(tr("Don't show this message again"));
        questionbox.setCheckBox(chkbox);
        chkbox->setChecked(!confirm_save);
        QObject::connect(chkbox, &QCheckBox::stateChanged, [](int state) {
                         confirm_save = (state == Qt::CheckState::Unchecked); });
        questionbox.exec();
        if (questionbox.result() == QMessageBox::Cancel) {
            confirm_save = true;
            return;
        }
    }
    QDialog::accept();
}
