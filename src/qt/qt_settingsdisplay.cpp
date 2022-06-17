/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Display settings UI module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *
 *		Copyright 2021 Joakim L. Gilje
 */
#include "qt_settingsdisplay.hpp"
#include "ui_qt_settingsdisplay.h"

#include <QDebug>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/video.h>
}

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

SettingsDisplay::SettingsDisplay(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsDisplay)
{
    ui->setupUi(this);

    onCurrentMachineChanged(machine);
}

SettingsDisplay::~SettingsDisplay()
{
    delete ui;
}

void SettingsDisplay::save() {
    gfxcard = ui->comboBoxVideo->currentData().toInt();
    voodoo_enabled = ui->checkBoxVoodoo->isChecked() ? 1 : 0;
    ibm8514_enabled = ui->checkBox8514->isChecked() ? 1 : 0;
    xga_enabled = ui->checkBoxXga->isChecked() ? 1 : 0;
}

void SettingsDisplay::onCurrentMachineChanged(int machineId) {
    // win_settings_video_proc, WM_INITDIALOG
    this->machineId = machineId;

    auto* model = ui->comboBoxVideo->model();
    auto removeRows = model->rowCount();

    int c = 0;
    int selectedRow = 0;
    while (true) {
        /* Skip "internal" if machine doesn't have it. */
        if ((c == 1) && (machine_has_flags(machineId, MACHINE_VIDEO) == 0)) {
            c++;
            continue;
        }

        const device_t* video_dev = video_card_getdevice(c);
        QString name = DeviceConfig::DeviceName(video_dev, video_get_internal_name(c), 1);
        if (name.isEmpty()) {
            break;
        }

        if (video_card_available(c) &&
            device_is_valid(video_dev, machineId)) {
            int row = Models::AddEntry(model, name, c);
            if (c == gfxcard) {
                selectedRow = row - removeRows;
            }
        }

        c++;
    }
    model->removeRows(0, removeRows);

    if (machine_has_flags(machineId, MACHINE_VIDEO_ONLY) > 0) {
        ui->comboBoxVideo->setEnabled(false);
        selectedRow = 1;
    } else {
        ui->comboBoxVideo->setEnabled(true);
    }
    ui->comboBoxVideo->setCurrentIndex(selectedRow);
}

void SettingsDisplay::on_pushButtonConfigure_clicked() {
    auto* device = video_card_getdevice(ui->comboBoxVideo->currentData().toInt());
    DeviceConfig::ConfigureDevice(device, 0, qobject_cast<Settings*>(Settings::settings));
}

void SettingsDisplay::on_pushButtonConfigureVoodoo_clicked() {
    DeviceConfig::ConfigureDevice(&voodoo_device, 0, qobject_cast<Settings*>(Settings::settings));
}

void SettingsDisplay::on_comboBoxVideo_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }
    int videoCard = ui->comboBoxVideo->currentData().toInt();
    ui->pushButtonConfigure->setEnabled(video_card_has_config(videoCard) > 0);

    bool machineHasPci = machine_has_bus(machineId, MACHINE_BUS_PCI) > 0;
    ui->checkBoxVoodoo->setEnabled(machineHasPci);
    if (machineHasPci) {
        ui->checkBoxVoodoo->setChecked(voodoo_enabled);
    }
    ui->pushButtonConfigureVoodoo->setEnabled(machineHasPci && ui->checkBoxVoodoo->isChecked());

    bool hasIsa16 = machine_has_bus(machineId, MACHINE_BUS_ISA16) > 0;
    bool has_MCA = machine_has_bus(machineId, MACHINE_BUS_MCA) > 0;
    ui->checkBox8514->setEnabled(hasIsa16 || has_MCA);
    if (hasIsa16 || has_MCA) {
        ui->checkBox8514->setChecked(ibm8514_enabled);
    }

    ui->checkBoxXga->setEnabled(hasIsa16 || has_MCA);
    if (hasIsa16 || has_MCA)
        ui->checkBoxXga->setChecked(xga_enabled);
}

void SettingsDisplay::on_checkBoxVoodoo_stateChanged(int state) {
    ui->pushButtonConfigureVoodoo->setEnabled(state == Qt::Checked);
}
