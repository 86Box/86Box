/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Mouse/Joystick configuration UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */
#include "qt_settings_completer.hpp"
#include "qt_preferences.hpp"
#include "qt_preferencesemulator.hpp"
#include "ui_qt_preferencesemulator.h"
#include "qt_mainwindow.hpp"
#include "ui_qt_mainwindow.h"
#include "qt_machinestatus.hpp"

#include <QDialog>
#include <QTranslator>
#include <QDebug>
#include <QKeySequence>
#include <QMessageBox>
#include <QTimer>
#include <string>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/gameport.h>
#include <86box/ui.h>
}

#include "qt_models_common.hpp"
#include "qt_deviceconfig.hpp"
#include "qt_joystickconfiguration.hpp"
#include "qt_keybind.hpp"

extern MainWindow *main_window;

PreferencesEmulator::PreferencesEmulator(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesEmulator)
{
    ui->setupUi(this);

    scLanguage = new SettingsCompleter(ui->comboBoxLanguage, nullptr);

    ui->comboBoxLanguage->setItemData(0, 0);
    for (int i = 1; i < Preferences::languages.length(); i++) {
        ui->comboBoxLanguage->addItem(Preferences::languages[i].second, i);
        scLanguage->addDevice(nullptr, Preferences::languages[i].second);

        if (i == lang_id)
            ui->comboBoxLanguage->setCurrentIndex(ui->comboBoxLanguage->findData(i));
    }
    ui->comboBoxLanguage->model()->sort(Qt::AscendingOrder);

    ui->openDirUsrPath->setChecked(open_dir_usr_path > 0);
    ui->checkBoxConfirmExit->setChecked(confirm_exit);
    ui->checkBoxConfirmSave->setChecked(confirm_save);
    ui->checkBoxConfirmHardReset->setChecked(confirm_reset);

    ui->radioButtonSystem->setChecked(color_scheme == 0);
    ui->radioButtonLight->setChecked(color_scheme == 1);
    ui->radioButtonDark->setChecked(color_scheme == 2);

#ifndef Q_OS_WINDOWS
    ui->groupBox->setHidden(true);
#endif
}

PreferencesEmulator::~PreferencesEmulator()
{
    delete scLanguage;

    delete ui;
}

void
PreferencesEmulator::save()
{
    auto size               = main_window->centralWidget()->size();
    lang_id                 = ui->comboBoxLanguage->currentData().toInt();
    open_dir_usr_path       = ui->openDirUsrPath->isChecked() ? 1 : 0;
    confirm_exit            = ui->checkBoxConfirmExit->isChecked() ? 1 : 0;
    confirm_save            = ui->checkBoxConfirmSave->isChecked() ? 1 : 0;
    confirm_reset           = ui->checkBoxConfirmHardReset->isChecked() ? 1 : 0;

    color_scheme = (ui->radioButtonSystem->isChecked()) ? 0 : (ui->radioButtonLight->isChecked() ? 1 : 2);

#ifdef Q_OS_WINDOWS
    extern void selectDarkMode();
    selectDarkMode();
#endif

    Preferences::loadTranslators(QCoreApplication::instance());
    Preferences::reloadStrings();
    update_mouse_msg();
    main_window->ui->retranslateUi(main_window);
    QString vmname(vm_name);
    if (vmname.at(vmname.size() - 1) == '"' || vmname.at(vmname.size() - 1) == '\'')
        vmname.truncate(vmname.size() - 1);
    main_window->setWindowTitle(QString("%1 - %2 %3").arg(vmname, EMU_NAME, EMU_VERSION_FULL));
    QString msg = main_window->status->getMessage();
    main_window->status.reset(new MachineStatus(main_window));
    main_window->refreshMediaMenu();
    main_window->status->message(msg);
    connect(main_window, &MainWindow::updateStatusBarTip, main_window->status.get(), &MachineStatus::updateTip);
    connect(main_window, &MainWindow::statusBarMessage, main_window->status.get(), &MachineStatus::message, Qt::QueuedConnection);
    QTimer::singleShot(200, [size] () {
        main_window->centralWidget()->setFixedSize(size);
        QApplication::processEvents();
        if (vid_resize == 1) {
            main_window->centralWidget()->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        }
    });
}

void
PreferencesEmulator::on_pushButtonLanguage_released()
{
    ui->comboBoxLanguage->setCurrentIndex(0);
}
