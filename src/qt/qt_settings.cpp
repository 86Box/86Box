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
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 */
#include <cstdint>
#include <cstdio>

#include <QApplication>

#include "qt_defs.hpp"
#include "qt_settings.hpp"
#include "ui_qt_settings.h"
#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/keyboard.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/hdd.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/midi.h>
}

#include <QStandardItemModel>

#include "qt_settings_completer.hpp"

#include "qt_harddiskdialog.hpp"

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

#include "qt_preferences.hpp"

#include "qt_harddrive_common.hpp"
#include "qt_settings_bus_tracking.hpp"

#include <QDebug>
#include <QMessageBox>
#include <QCheckBox>
#include <QStyle>

#include <dirent.h>
#include <unistd.h>

extern MainWindow *main_window;

class SettingsModel : public QAbstractListModel {
public:
    SettingsModel(QObject *parent)
        : QAbstractListModel(parent)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        fontHeight = QFontMetrics(qApp->font()).height();
#else
        fontHeight = QApplication::fontMetrics().height();
#endif
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
            return QIcon(QString(":/settings/qt/icons/%1.ico").arg(page_icons[index.row()]));
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
    uint32_t base = plat_get_ticks();
    uint32_t next;
    QString measurement = QString("start = %1\n").arg(base);

    ui->setupUi(this);
    next = plat_get_ticks(); measurement += QString("setupUi = %1\n").arg(next - base); base = next;
    auto *model = new SettingsModel(this);
    ui->listView->setModel(model);
    next = plat_get_ticks(); measurement += QString("model = %1\n").arg(next - base); base = next;

    Harddrives::busTrackClass = new SettingsBusTracking; next = plat_get_ticks(); measurement += QString("busTrack = %1\n").arg(next - base); base = next;
    machine                   = new SettingsMachine(this); next = plat_get_ticks(); measurement += QString("machine = %1\n").arg(next - base); base = next;
    display                   = new SettingsDisplay(this); next = plat_get_ticks(); measurement += QString("display = %1\n").arg(next - base); base = next;
    input                     = new SettingsInput(this); next = plat_get_ticks(); measurement += QString("input = %1\n").arg(next - base); base = next;
    sound                     = new SettingsSound(this); next = plat_get_ticks(); measurement += QString("sound = %1\n").arg(next - base); base = next;
    network                   = new SettingsNetwork(this); next = plat_get_ticks(); measurement += QString("network = %1\n").arg(next - base); base = next;
    ports                     = new SettingsPorts(this); next = plat_get_ticks(); measurement += QString("ports = %1\n").arg(next - base); base = next;
    storageControllers        = new SettingsStorageControllers(this); next = plat_get_ticks(); measurement += QString("storage = %1\n").arg(next - base); base = next;
    harddisks                 = new SettingsHarddisks(this); next = plat_get_ticks(); measurement += QString("hdd = %1\n").arg(next - base); base = next;
    floppyCdrom               = new SettingsFloppyCDROM(this); next = plat_get_ticks(); measurement += QString("floppycdrom = %1\n").arg(next - base); base = next;
    otherRemovable            = new SettingsOtherRemovable(this); next = plat_get_ticks(); measurement += QString("removable = %1\n").arg(next - base); base = next;
    otherPeripherals          = new SettingsOtherPeripherals(this); next = plat_get_ticks(); measurement += QString("peripherals = %1\n").arg(next - base); base = next;

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
     next = plat_get_ticks(); measurement += QString("widgets = %1\n").arg(next - base); base = next;

    connect(machine, &SettingsMachine::currentMachineChanged, display,
            &SettingsDisplay::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, input,
            &SettingsInput::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, sound,
            &SettingsSound::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, network,
            &SettingsNetwork::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, ports,
            &SettingsPorts::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, storageControllers,
            &SettingsStorageControllers::onCurrentMachineChanged);
    connect(machine, &SettingsMachine::currentMachineChanged, otherPeripherals,
            &SettingsOtherPeripherals::onCurrentMachineChanged);
    connect(floppyCdrom, &SettingsFloppyCDROM::cdromChannelChanged, harddisks,
            &SettingsHarddisks::reloadBusChannels);
    connect(floppyCdrom, &SettingsFloppyCDROM::cdromChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_MO);
    connect(floppyCdrom, &SettingsFloppyCDROM::cdromChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_RDisk);
    connect(harddisks, &SettingsHarddisks::driveChannelChanged, floppyCdrom,
            &SettingsFloppyCDROM::reloadBusChannels);
    connect(harddisks, &SettingsHarddisks::driveChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_MO);
    connect(harddisks, &SettingsHarddisks::driveChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_RDisk);
    connect(otherRemovable, &SettingsOtherRemovable::moChannelChanged, harddisks,
            &SettingsHarddisks::reloadBusChannels);
    connect(otherRemovable, &SettingsOtherRemovable::moChannelChanged, floppyCdrom,
            &SettingsFloppyCDROM::reloadBusChannels);
    connect(otherRemovable, &SettingsOtherRemovable::moChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_RDisk);
    connect(otherRemovable, &SettingsOtherRemovable::rdiskChannelChanged, harddisks,
            &SettingsHarddisks::reloadBusChannels);
    connect(otherRemovable, &SettingsOtherRemovable::rdiskChannelChanged, floppyCdrom,
            &SettingsFloppyCDROM::reloadBusChannels);
    connect(otherRemovable, &SettingsOtherRemovable::rdiskChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_MO);
    connect(harddisks, &SettingsHarddisks::driveChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_Tape);
    connect(otherRemovable, &SettingsOtherRemovable::tapeChannelChanged, harddisks,
            &SettingsHarddisks::reloadBusChannels);
    connect(otherRemovable, &SettingsOtherRemovable::tapeChannelChanged, floppyCdrom,
            &SettingsFloppyCDROM::reloadBusChannels);
    connect(otherRemovable, &SettingsOtherRemovable::tapeChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_MO);
    connect(otherRemovable, &SettingsOtherRemovable::tapeChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_RDisk);
    connect(otherRemovable, &SettingsOtherRemovable::moChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_Tape);
    connect(otherRemovable, &SettingsOtherRemovable::rdiskChannelChanged, otherRemovable,
            &SettingsOtherRemovable::reloadBusChannels_Tape);

    connect(ui->listView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &previous) {
                ui->stackedWidget->setCurrentIndex(current.row());
                ui->headerIcon->setPixmap(qvariant_cast<QIcon>(ui->listView->model()->data(current, Qt::DecorationRole)).pixmap(QSize(16, 16)));
                ui->headerLabel->setText(ui->listView->model()->data(current, Qt::DisplayRole).toString());
            });

    ui->listView->setCurrentIndex(model->index(0, 0));

    Settings::settings = this;

    next = plat_get_ticks(); measurement += QString("connections = %1").arg(next - base);
    QMessageBox msgBox(QMessageBox::Icon::Information, QString(""), measurement);
    msgBox.exec();
}

Settings::~Settings()
{
    delete ui;
    delete Harddrives::busTrackClass;
    Harddrives::busTrackClass = nullptr;
    Settings::settings        = nullptr;
}

void
Settings::save(int soft)
{
    machine->save(soft);
    display->save(soft);
    input->save(soft);
    sound->save(soft);
    network->save(soft);
    ports->save(soft);
    storageControllers->save(soft);
    harddisks->save(soft);
    floppyCdrom->save(soft);
    otherRemovable->save(soft);
    otherPeripherals->save(soft);
}

void
Settings::accept()
{
    int changed = 0;

    changed |= machine->changed();
    changed |= display->changed();
    changed |= input->changed();
    changed |= sound->changed();
    changed |= network->changed();
    changed |= ports->changed();
    changed |= storageControllers->changed();
    changed |= harddisks->changed();
    changed |= floppyCdrom->changed();
    changed |= otherRemovable->changed();
    changed |= otherPeripherals->changed();

    if ((changed & SETTINGS_REQUIRE_HARD_RESET) && confirm_save && !settings_only) {
        QMessageBox questionbox(QMessageBox::Icon::Question, "86Box",
                                QStringLiteral("%1\n\n%2").arg(tr("Do you want to save the settings?"), tr("This will hard reset the emulated machine.")),
                                QMessageBox::Save | QMessageBox::Cancel, this);
        QCheckBox  *chkbox = new QCheckBox(tr("Don't show this message again"));
        questionbox.setCheckBox(chkbox);
        chkbox->setChecked(!confirm_save);
        QObject::connect(chkbox, &QCheckBox::CHECK_STATE_CHANGED, [](int state) { confirm_save = (state == Qt::CheckState::Unchecked); });
        questionbox.exec();
        if (questionbox.result() == QMessageBox::Cancel) {
            confirm_save = true;
            return;
        }
    } else if (changed && !(changed & SETTINGS_REQUIRE_HARD_RESET) && !settings_only) {
        save(1);
        config_changed = 2;
        main_window->emitVmmSignal();
        lpt_devices_reset();
        serial_devices_reset();
        midi_config_changed();

        video_copy = (video_grayscale || invert_display) ? video_transform_copy : memcpy;
        config_save();
        reset_screen_size();
        device_force_redraw();
        for (int i = 0; i < MONITORS_NUM; i++) {
            if (monitors[i].target_buffer)
                video_force_resize_set_monitor(1, i);
        }

        /* Reject so the main window does nothing. */
        QDialog::reject();
        return;
    } else if (!changed && !settings_only) {
        QDialog::reject();
        return;
    }

    QDialog::accept();
}

static int
plat_path_is_empty(char *path)
{
    int n            = 0;
    DIR *dir         = opendir(path);
    struct dirent *d;

    if (dir == NULL)
        /* Not a directory or doesn't exist. */
        return 1;

    while ((d = readdir(dir)) != NULL) {
        if (++n > 2)
            break;
    }

    closedir(dir);

    return (n <= 2);
}

void
Settings::reject()
{
    if (plat_path_is_empty(usr_path))
        rmdir(usr_path);

    QDialog::reject();
}
