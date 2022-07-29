/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Serial/Parallel ports configuration UI module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *      Copyright 2022 Cacodemon345
 *		Copyright 2022 Jasmine Iwanek
 *		Copyright 2021 Joakim L. Gilje
 */
#include "qt_settingsports.hpp"
#include "ui_qt_settingsports.h"

extern "C" {
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/lpt.h>
#include <86box/serial.h>
}

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

SettingsPorts::SettingsPorts(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsPorts)
{
    ui->setupUi(this);

    for (int i = 0; i < PARALLEL_MAX; i++) {
        auto* cbox = findChild<QComboBox*>(QString("comboBoxLpt%1").arg(i+1));
        auto* model = cbox->model();
        int c = 0;
        int selectedRow = 0;
        while (true) {
            const char* lptName = lpt_device_get_name(c);
            if (lptName == nullptr) {
                break;
            }

            Models::AddEntry(model, tr(lptName), c);
            if (c == lpt_ports[i].device) {
                selectedRow = c;
            }
            c++;
        }
        cbox->setCurrentIndex(selectedRow);

        auto* checkBox = findChild<QCheckBox*>(QString("checkBoxParallel%1").arg(i+1));
        checkBox->setChecked(lpt_ports[i].enabled > 0);
        cbox->setEnabled(lpt_ports[i].enabled > 0);
    }

    for (int i = 0; i < SERIAL_MAX; i++) {
        auto* checkBox = findChild<QCheckBox*>(QString("checkBoxSerial%1").arg(i+1));
        checkBox->setChecked(com_ports[i].enabled > 0);
    }
}

SettingsPorts::~SettingsPorts()
{
    delete ui;
}

void SettingsPorts::save() {
    for (int i = 0; i < PARALLEL_MAX; i++) {
        auto* cbox = findChild<QComboBox*>(QString("comboBoxLpt%1").arg(i+1));
        auto* checkBox = findChild<QCheckBox*>(QString("checkBoxParallel%1").arg(i+1));
        lpt_ports[i].device = cbox->currentData().toInt();
        lpt_ports[i].enabled = checkBox->isChecked() ? 1 : 0;
    }

    for (int i = 0; i < SERIAL_MAX; i++) {
        auto* checkBox = findChild<QCheckBox*>(QString("checkBoxSerial%1").arg(i+1));
        com_ports[i].enabled = checkBox->isChecked() ? 1 : 0;
    }
}

void SettingsPorts::on_checkBoxParallel1_stateChanged(int state) {
    ui->comboBoxLpt1->setEnabled(state == Qt::Checked);
}

void SettingsPorts::on_checkBoxParallel2_stateChanged(int state) {
    ui->comboBoxLpt2->setEnabled(state == Qt::Checked);
}

void SettingsPorts::on_checkBoxParallel3_stateChanged(int state) {
    ui->comboBoxLpt3->setEnabled(state == Qt::Checked);
}

void SettingsPorts::on_checkBoxParallel4_stateChanged(int state) {
    ui->comboBoxLpt4->setEnabled(state == Qt::Checked);
}
