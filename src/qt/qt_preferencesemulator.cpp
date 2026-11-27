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
#include "qt_preferencesemulator.hpp"
#include "ui_qt_preferencesemulator.h"

extern "C" {
#include <86box/86box.h>
}

PreferencesEmulator::PreferencesEmulator(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesEmulator)
{
    ui->setupUi(this);

    ui->openDirUsrPath->setChecked(open_dir_usr_path > 0);
    ui->checkBoxAutoPause->setChecked(do_auto_pause);
    ui->checkBoxAutoDialogPause->setChecked(do_auto_dialog_pause);
    ui->checkBoxConfirmExit->setChecked(confirm_exit);
    ui->checkBoxConfirmSave->setChecked(confirm_save);
    ui->checkBoxConfirmHardReset->setChecked(confirm_reset);
    ui->checkBoxCHDPrecache->setChecked(chd_precache_level > 0);
}

PreferencesEmulator::~PreferencesEmulator()
{
    delete ui;
}

void
PreferencesEmulator::save()
{
    open_dir_usr_path       = ui->openDirUsrPath->isChecked() ? 1 : 0;
    do_auto_pause           = ui->checkBoxAutoPause->isChecked() ? 1 : 0;
    do_auto_dialog_pause    = ui->checkBoxAutoDialogPause->isChecked() ? 1 : 0;
    confirm_exit            = ui->checkBoxConfirmExit->isChecked() ? 1 : 0;
    confirm_save            = ui->checkBoxConfirmSave->isChecked() ? 1 : 0;
    confirm_reset           = ui->checkBoxConfirmHardReset->isChecked() ? 1 : 0;
    chd_precache_level      = ui->checkBoxCHDPrecache->isChecked() ? 1 : 0;
}
