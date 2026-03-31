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
#include "qt_preferencesinput.hpp"
#include "ui_qt_preferencesinput.h"
#include "qt_mainwindow.hpp"

#include <QDialog>
#include <QTranslator>
#include <QDebug>
#include <QKeySequence>
#include <QMessageBox>
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

PreferencesInput::PreferencesInput(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesInput)
{
    ui->setupUi(this);

    mouseSensitivity = mouse_sensitivity;

    ui->horizontalSlider->setValue(mouse_sensitivity * 100.);
    ui->checkBoxMultimediaKeys->setChecked(inhibit_multimedia_keys);

#ifndef Q_OS_WINDOWS
    ui->checkBoxMultimediaKeys->setHidden(true);
#endif
}

PreferencesInput::~PreferencesInput()
{
    delete ui;
}

void
PreferencesInput::save()
{
    inhibit_multimedia_keys = ui->checkBoxMultimediaKeys->isChecked() ? 1 : 0;
    mouse_sensitivity       = mouseSensitivity;
}

void
PreferencesInput::on_horizontalSlider_valueChanged(int value)
{
    mouseSensitivity = (double) value / 100.;
}

void
PreferencesInput::on_pushButton_2_clicked()
{
    mouseSensitivity = 1.0;
    ui->horizontalSlider->setValue(100);
}
