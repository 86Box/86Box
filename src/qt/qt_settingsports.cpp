/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Serial/Parallel ports configuration UI module.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2022 Cacodemon345
 *          Copyright 2022 Jasmine Iwanek
 *          Copyright 2021 Joakim L. Gilje
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
#include <86box/serial_passthrough.h>
}

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

SettingsPorts::SettingsPorts(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsPorts)
{
    ui->setupUi(this);
    onCurrentMachineChanged(machine);
}

SettingsPorts::~SettingsPorts()
{
    delete ui;
}

void
SettingsPorts::save()
{
    for (int i = 0; i < PARALLEL_MAX; i++) {
        auto *cbox     = findChild<QComboBox *>(QString("comboBoxLpt%1").arg(i + 1));
        auto *checkBox       = findChild<QCheckBox *>(QString("checkBoxParallel%1").arg(i + 1));
        if (cbox != NULL)
            lpt_ports[i].device  = cbox->currentData().toInt();
        if (checkBox != NULL)
            lpt_ports[i].enabled = checkBox->isChecked() ? 1 : 0;
    }

    for (int i = 0; i < SERIAL_MAX; i++) {
        auto *checkBox     = findChild<QCheckBox *>(QString("checkBoxSerial%1").arg(i + 1));
        auto *checkBoxPass = findChild<QCheckBox *>(QString("checkBoxSerialPassThru%1").arg(i + 1));
        if (checkBox != NULL)
            com_ports[i].enabled = checkBox->isChecked() ? 1 : 0;
        if (checkBoxPass != NULL)
            serial_passthrough_enabled[i] = checkBoxPass->isChecked();
    }
}

void
SettingsPorts::onCurrentMachineChanged(int machineId)
{
    this->machineId = machineId;

    for (int i = 0; i < PARALLEL_MAX; i++) {
        auto *cbox            = findChild<QComboBox *>(QString("comboBoxLpt%1").arg(i + 1));
        auto *model           = cbox->model();
        const auto removeRows = model->rowCount();
        int   c               = 0;
        int   selectedRow     = 0;
        while (true) {
            const char *lptName = lpt_device_get_name(c);
            if (lptName == nullptr) {
                break;
            }

            int row = Models::AddEntry(model, tr(lptName), c);
            if (c == lpt_ports[i].device) {
                selectedRow = row - removeRows;
            }
            c++;
        }
        model->removeRows(0, removeRows);
        cbox->setEnabled(model->rowCount() > 0);
        cbox->setCurrentIndex(-1);
        cbox->setCurrentIndex(selectedRow);

        auto *checkBox = findChild<QCheckBox *>(QString("checkBoxParallel%1").arg(i + 1));
        if (checkBox != NULL)
            checkBox->setChecked(lpt_ports[i].enabled > 0);
        if (cbox != NULL)
            cbox->setEnabled(lpt_ports[i].enabled > 0);
    }

    for (int i = 0; i < SERIAL_MAX; i++) {
        auto *checkBox     = findChild<QCheckBox *>(QString("checkBoxSerial%1").arg(i + 1));
        auto *checkBoxPass = findChild<QCheckBox *>(QString("checkBoxSerialPassThru%1").arg(i + 1));
        auto *buttonPass   = findChild<QPushButton *>(QString("pushButtonSerialPassThru%1").arg(i + 1));
        if (checkBox != NULL)
            checkBox->setChecked(com_ports[i].enabled > 0);
        if (checkBoxPass != NULL) {
            checkBoxPass->setEnabled(com_ports[i].enabled > 0);
            checkBoxPass->setChecked(serial_passthrough_enabled[i]);
            buttonPass->setEnabled((com_ports[i].enabled > 0) && serial_passthrough_enabled[i]);
        }
    }
}

void
SettingsPorts::on_checkBoxParallel1_stateChanged(int state)
{
    ui->comboBoxLpt1->setEnabled(state == Qt::Checked);
}

void
SettingsPorts::on_checkBoxParallel2_stateChanged(int state)
{
    ui->comboBoxLpt2->setEnabled(state == Qt::Checked);
}

void
SettingsPorts::on_checkBoxParallel3_stateChanged(int state)
{
    ui->comboBoxLpt3->setEnabled(state == Qt::Checked);
}

void
SettingsPorts::on_checkBoxParallel4_stateChanged(int state)
{
    ui->comboBoxLpt4->setEnabled(state == Qt::Checked);
}

void
SettingsPorts::on_checkBoxSerial1_stateChanged(int state)
{
    ui->checkBoxSerialPassThru1->setEnabled(state == Qt::Checked);
    ui->pushButtonSerialPassThru1->setEnabled((state == Qt::Checked) && ui->checkBoxSerialPassThru1->isChecked());
}

void
SettingsPorts::on_checkBoxSerial2_stateChanged(int state)
{
    ui->checkBoxSerialPassThru2->setEnabled(state == Qt::Checked);
    ui->pushButtonSerialPassThru2->setEnabled((state == Qt::Checked) && ui->checkBoxSerialPassThru2->isChecked());
}

void
SettingsPorts::on_checkBoxSerial3_stateChanged(int state)
{
    ui->checkBoxSerialPassThru3->setEnabled(state == Qt::Checked);
    ui->pushButtonSerialPassThru3->setEnabled((state == Qt::Checked) && ui->checkBoxSerialPassThru3->isChecked());
}

void
SettingsPorts::on_checkBoxSerial4_stateChanged(int state)
{
    ui->checkBoxSerialPassThru4->setEnabled(state == Qt::Checked);
    ui->pushButtonSerialPassThru4->setEnabled((state == Qt::Checked) && ui->checkBoxSerialPassThru4->isChecked());
}

#if 0
void
SettingsPorts::on_checkBoxSerial5_stateChanged(int state)
{
    ui->checkBoxSerialPassThru5->setEnabled(state == Qt::Checked);
    ui->pushButtonSerialPassThru5->setEnabled((state == Qt::Checked) && ui->checkBoxSerialPassThru5->isChecked());
}

void
SettingsPorts::on_checkBoxSerial6_stateChanged(int state)
{
    ui->checkBoxSerialPassThru6->setEnabled(state == Qt::Checked);
    ui->pushButtonSerialPassThru6->setEnabled((state == Qt::Checked) && ui->checkBoxSerialPassThru6->isChecked());
}

void
SettingsPorts::on_checkBoxSerial7_stateChanged(int state)
{
    ui->checkBoxSerialPassThru7->setEnabled(state == Qt::Checked);
    ui->pushButtonSerialPassThru7->setEnabled((state == Qt::Checked) && ui->checkBoxSerialPassThru7->isChecked());
}
#endif

void
SettingsPorts::on_checkBoxSerialPassThru1_stateChanged(int state)
{
    ui->pushButtonSerialPassThru1->setEnabled(state == Qt::Checked);
}

void
SettingsPorts::on_checkBoxSerialPassThru2_stateChanged(int state)
{
    ui->pushButtonSerialPassThru2->setEnabled(state == Qt::Checked);
}

void
SettingsPorts::on_checkBoxSerialPassThru3_stateChanged(int state)
{
    ui->pushButtonSerialPassThru3->setEnabled(state == Qt::Checked);
}

void
SettingsPorts::on_checkBoxSerialPassThru4_stateChanged(int state)
{
    ui->pushButtonSerialPassThru4->setEnabled(state == Qt::Checked);
}

#if 0
void
SettingsPorts::on_checkBoxSerialPassThru5_stateChanged(int state)
{
    ui->pushButtonSerialPassThru5->setEnabled(state == Qt::Checked);
}

void
SettingsPorts::on_checkBoxSerialPassThru6_stateChanged(int state)
{
    ui->pushButtonSerialPassThru6->setEnabled(state == Qt::Checked);
}

void
SettingsPorts::on_checkBoxSerialPassThru7_stateChanged(int state)
{
    ui->pushButtonSerialPassThru7->setEnabled(state == Qt::Checked);
}
#endif

void
SettingsPorts::on_pushButtonSerialPassThru1_clicked()
{
    DeviceConfig::ConfigureDevice(&serial_passthrough_device, 1, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsPorts::on_pushButtonSerialPassThru2_clicked()
{
    DeviceConfig::ConfigureDevice(&serial_passthrough_device, 2, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsPorts::on_pushButtonSerialPassThru3_clicked()
{
    DeviceConfig::ConfigureDevice(&serial_passthrough_device, 3, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsPorts::on_pushButtonSerialPassThru4_clicked()
{
    DeviceConfig::ConfigureDevice(&serial_passthrough_device, 4, qobject_cast<Settings *>(Settings::settings));
}

#if 0
void
SettingsPorts::on_pushButtonSerialPassThru5_clicked()
{
    DeviceConfig::ConfigureDevice(&serial_passthrough_device, 5, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsPorts::on_pushButtonSerialPassThru6_clicked()
{
    DeviceConfig::ConfigureDevice(&serial_passthrough_device, 6, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsPorts::on_pushButtonSerialPassThru7_clicked()
{
    DeviceConfig::ConfigureDevice(&serial_passthrough_device, 7, qobject_cast<Settings *>(Settings::settings));
}
#endif
