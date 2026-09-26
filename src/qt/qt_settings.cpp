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
#include <86box/hdc.h>
#include <86box/sound.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/hdc_ide.h>
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
#include "qt_models_common.hpp"
#include "qt_settings_bus_tracking.hpp"

#include <QDebug>
#include <QComboBox>
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
    ui->setupUi(this);
    auto *model = new SettingsModel(this);
    ui->listView->setModel(model);

    Settings::settings = this;

    Harddrives::busTrackClass = new SettingsBusTracking;

    /* Only the Machine page is built now. The others are built when first
       shown, or all at once on OK, so opening the dialog does not build
       every device list of every page. */
    machine            = new SettingsMachine(this);
    display            = nullptr;
    input              = nullptr;
    sound              = nullptr;
    network            = nullptr;
    ports              = nullptr;
    storageControllers = nullptr;
    harddisks          = nullptr;
    floppyCdrom        = nullptr;
    otherRemovable     = nullptr;
    otherPeripherals   = nullptr;

    ui->stackedWidget->addWidget(machine);
    for (int i = PAGE_DISPLAY; i < PAGE_COUNT; i++)
        ui->stackedWidget->addWidget(new QWidget(this));

    connect(ui->listView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &previous) {
                ensurePage(current.row());
                ui->stackedWidget->setCurrentIndex(current.row());
                ui->headerIcon->setPixmap(qvariant_cast<QIcon>(ui->listView->model()->data(current, Qt::DecorationRole)).pixmap(QSize(16, 16)));
                ui->headerLabel->setText(ui->listView->model()->data(current, Qt::DisplayRole).toString());
            });

    ui->listView->setCurrentIndex(model->index(0, 0));
}

/* A page takes the place of its placeholder in the stack. */
void
Settings::placePage(int index, QWidget *page)
{
    QWidget *placeholder = ui->stackedWidget->widget(index);

    ui->stackedWidget->insertWidget(index, page);
    ui->stackedWidget->removeWidget(placeholder);
    delete placeholder;
}

/* Build the page at a list index if it is not built yet, with the pages it
   depends on: Display before Input (Input asks it about the light pen), and
   Ports before the three drive pages, which are built together: they share
   the bus channel tracking, where Ports marks the parallel ports in use. A
   page built after the machine was changed is brought up to that machine. */
void
Settings::ensurePage(int index)
{
    const int machineId = machine->currentMachineId();

    switch (index) {
        case PAGE_DISPLAY:
        case PAGE_INPUT:
            if (display == nullptr) {
                display = new SettingsDisplay(this);
                placePage(PAGE_DISPLAY, display);
                connect(machine, &SettingsMachine::currentMachineChanged, display,
                        &SettingsDisplay::onCurrentMachineChanged);
            }
            if (input == nullptr) {
                input = new SettingsInput(this);
                placePage(PAGE_INPUT, input);
                connect(machine, &SettingsMachine::currentMachineChanged, input,
                        &SettingsInput::onCurrentMachineChanged);
                if (machineId != ::machine)
                    display->onCurrentMachineChanged(machineId); /* and Input with it */
            }
            break;

        case PAGE_SOUND:
            if (sound == nullptr) {
                sound = new SettingsSound(this);
                placePage(PAGE_SOUND, sound);
                connect(machine, &SettingsMachine::currentMachineChanged, sound,
                        &SettingsSound::onCurrentMachineChanged);
                if (machineId != ::machine)
                    sound->onCurrentMachineChanged(machineId);
            }
            break;

        case PAGE_NETWORK:
            if (network == nullptr) {
                network = new SettingsNetwork(this);
                placePage(PAGE_NETWORK, network);
                connect(machine, &SettingsMachine::currentMachineChanged, network,
                        &SettingsNetwork::onCurrentMachineChanged);
                if (machineId != ::machine)
                    network->onCurrentMachineChanged(machineId);
            }
            break;

        case PAGE_PORTS:
            if (ports == nullptr) {
                ports = new SettingsPorts(this);
                placePage(PAGE_PORTS, ports);
                connect(machine, &SettingsMachine::currentMachineChanged, ports,
                        &SettingsPorts::onCurrentMachineChanged);
                if (machineId != ::machine)
                    ports->onCurrentMachineChanged(machineId);
            }
            break;

        case PAGE_STORAGE:
            if (storageControllers == nullptr) {
                storageControllers = new SettingsStorageControllers(this);
                placePage(PAGE_STORAGE, storageControllers);
                connect(machine, &SettingsMachine::currentMachineChanged, storageControllers,
                        &SettingsStorageControllers::onCurrentMachineChanged);
                if (machineId != ::machine)
                    storageControllers->onCurrentMachineChanged(machineId);
            }
            break;

        case PAGE_HARDDISKS:
        case PAGE_FLOPPYCDROM:
        case PAGE_REMOVABLE:
            ensurePage(PAGE_PORTS);
            if (harddisks == nullptr) {
                harddisks      = new SettingsHarddisks(this);
                floppyCdrom    = new SettingsFloppyCDROM(this);
                otherRemovable = new SettingsOtherRemovable(this);
                placePage(PAGE_HARDDISKS, harddisks);
                placePage(PAGE_FLOPPYCDROM, floppyCdrom);
                placePage(PAGE_REMOVABLE, otherRemovable);

                connect(machine, &SettingsMachine::currentMachineChanged, floppyCdrom,
                        &SettingsFloppyCDROM::onCurrentMachineChanged);
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

                if (machineId != ::machine)
                    floppyCdrom->onCurrentMachineChanged(machineId);
            }
            break;

        case PAGE_OTHER:
            if (otherPeripherals == nullptr) {
                otherPeripherals = new SettingsOtherPeripherals(this);
                placePage(PAGE_OTHER, otherPeripherals);
                connect(machine, &SettingsMachine::currentMachineChanged, otherPeripherals,
                        &SettingsOtherPeripherals::onCurrentMachineChanged);
                if (machineId != ::machine)
                    otherPeripherals->onCurrentMachineChanged(machineId);
            }
            break;

        default:
            break;
    }
}

/* OK builds every page not built yet before anything is checked or saved,
   as when all were built on opening: each page sees the selected machine
   and drops from its selection what that machine cannot take, and whatever
   else it corrects in the saved configuration. */
void
Settings::ensureAllPages()
{
    for (int i = PAGE_DISPLAY; i < PAGE_COUNT; i++)
        ensurePage(i);
}

Settings::~Settings()
{
    delete ui;
    /* The device lists are read again the next time: ROMs may come or go
       in between. */
    Models::ClearDevices();
    delete Harddrives::busTrackClass;
    Harddrives::busTrackClass = nullptr;
    Settings::settings        = nullptr;
}

int
Settings::currentMachine() const
{
    return machine->findChild<QComboBox *>("comboBoxMachine")->currentData().toInt();
}

int
Settings::currentHdc(int i) const
{
    return (storageControllers != nullptr) ? storageControllers->hdcCard(i) : hdc_current[i];
}

int
Settings::currentSoundCard(int i) const
{
    return (sound != nullptr) ? sound->soundCard(i) : sound_card_current[i];
}

int
Settings::currentScsiCard(int i) const
{
    return (storageControllers != nullptr) ? storageControllers->scsiCard(i) : scsi_card_current[i];
}

void
Settings::save(int soft)
{
    ensureAllPages();

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

    ensureAllPages();
    /* A controller that cannot have the IDE channels it needs (two cards
       that can only use the legacy ports, say) will not work: ask. */
    ide_owner_t    owners[IDE_BUS_MAX];
    ide_conflict_t conflicts[IDE_CONFLICTS_MAX];
    int            conflictCount = 0;
    const int      owned         = Harddrives::idePlan(owners, conflicts, &conflictCount);

    if (conflictCount > 0) {
        QStringList lines;
        for (int i = 0; i < conflictCount; i++)
            lines.append(Harddrives::conflictText(i, owners, owned, conflicts, conflictCount));

        QMessageBox box(QMessageBox::Icon::Warning, tr("IDE Conflict"),
                        tr("These IDE controllers need channels that another device already has, and will not work:") +
                            QString("\n\n%1\n\n").arg(lines.join("\n")) +
                            tr("Do you want to save the configuration anyway?"),
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setDefaultButton(QMessageBox::No);
        if (box.exec() != QMessageBox::Yes)
            return;
    }

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
                                tr("Do you want to save the settings?"),
                                QMessageBox::Save | QMessageBox::Cancel, this);
        QCheckBox  *chkbox = new QCheckBox(tr("Don't show this message again"));
        questionbox.setCheckBox(chkbox);
        questionbox.setInformativeText(tr("This will hard reset the emulated machine."));
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
