/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Mouse/Joystick configuration UI module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *
 *		Copyright 2021 Joakim L. Gilje
 */
#include "qt_settingsinput.hpp"
#include "ui_qt_settingsinput.h"

#include <QDebug>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mouse.h>
#include <86box/gameport.h>
}

#include "qt_models_common.hpp"
#include "qt_deviceconfig.hpp"
#include "qt_joystickconfiguration.hpp"

SettingsInput::SettingsInput(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsInput)
{
    ui->setupUi(this);

    onCurrentMachineChanged(machine);
}

SettingsInput::~SettingsInput()
{
    delete ui;
}

void SettingsInput::save() {
    mouse_type = ui->comboBoxMouse->currentData().toInt();
    joystick_type = ui->comboBoxJoystick->currentData().toInt();
}

void SettingsInput::onCurrentMachineChanged(int machineId) {
    // win_settings_video_proc, WM_INITDIALOG
    this->machineId = machineId;

    auto* mouseModel = ui->comboBoxMouse->model();
    auto removeRows = mouseModel->rowCount();

    int selectedRow = 0;
    for (int i = 0; i < mouse_get_ndev(); ++i) {
        const auto* dev = mouse_get_device(i);
        if ((i == MOUSE_TYPE_INTERNAL) && (machine_has_flags(machineId, MACHINE_MOUSE) == 0)) {
            continue;
        }

        if (device_is_valid(dev, machineId) == 0) {
            continue;
        }

        QString name = DeviceConfig::DeviceName(dev, mouse_get_internal_name(i), 0);
        int row = mouseModel->rowCount();
        mouseModel->insertRow(row);
        auto idx = mouseModel->index(row, 0);

        mouseModel->setData(idx, name, Qt::DisplayRole);
        mouseModel->setData(idx, i, Qt::UserRole);

        if (i == mouse_type) {
            selectedRow = row - removeRows;
        }
    }
    mouseModel->removeRows(0, removeRows);
    ui->comboBoxMouse->setCurrentIndex(selectedRow);


    int i = 0;
    char* joyName = joystick_get_name(i);
    auto* joystickModel = ui->comboBoxJoystick->model();
    removeRows = joystickModel->rowCount();
    selectedRow = 0;
    while (joyName) {
        int row = Models::AddEntry(joystickModel, tr(joyName).toUtf8().data(), i);
        if (i == joystick_type) {
            selectedRow = row - removeRows;
        }

        ++i;
        joyName = joystick_get_name(i);
    }
    joystickModel->removeRows(0, removeRows);
    ui->comboBoxJoystick->setCurrentIndex(selectedRow);
}

void SettingsInput::on_comboBoxMouse_currentIndexChanged(int index) {
    int mouseId = ui->comboBoxMouse->currentData().toInt();
    ui->pushButtonConfigureMouse->setEnabled(mouse_has_config(mouseId) > 0);
}


void SettingsInput::on_comboBoxJoystick_currentIndexChanged(int index) {
    int joystickId = ui->comboBoxJoystick->currentData().toInt();
    for (int i = 0; i < 4; ++i) {
        auto* btn = findChild<QPushButton*>(QString("pushButtonJoystick%1").arg(i+1));
        if (btn == nullptr) {
            continue;
        }
        btn->setEnabled(joystick_get_max_joysticks(joystickId) > i);
    }
}

void SettingsInput::on_pushButtonConfigureMouse_clicked() {
    int mouseId = ui->comboBoxMouse->currentData().toInt();
    DeviceConfig::ConfigureDevice(mouse_get_device(mouseId), 0, qobject_cast<Settings*>(Settings::settings));
}

static int get_axis(JoystickConfiguration& jc, int axis, int joystick_nr) {
    int axis_sel = jc.selectedAxis(axis);
    int nr_axes = plat_joystick_state[joystick_state[joystick_nr].plat_joystick_nr - 1].nr_axes;
    int nr_povs = plat_joystick_state[joystick_state[joystick_nr].plat_joystick_nr - 1].nr_povs;

    if (axis_sel < nr_axes) {
        return axis_sel;
    }

    axis_sel -= nr_axes;
    if (axis_sel < nr_povs * 2) {
        if (axis_sel & 1)
            return POV_Y | (axis_sel >> 1);
        else
            return POV_X | (axis_sel >> 1);
    }
    axis_sel -= nr_povs;

    return SLIDER | (axis_sel >> 1);
}

static int get_pov(JoystickConfiguration& jc, int pov, int joystick_nr) {
    int pov_sel = jc.selectedPov(pov);
    int nr_povs = plat_joystick_state[joystick_state[joystick_nr].plat_joystick_nr-1].nr_povs*2;

    if (pov_sel < nr_povs)
    {
        if (pov_sel & 1)
            return POV_Y | (pov_sel >> 1);
        else
            return POV_X | (pov_sel >> 1);
    }

    return pov_sel - nr_povs;
}

static void updateJoystickConfig(int type, int joystick_nr, QWidget* parent) {
    JoystickConfiguration jc(type, joystick_nr, parent);
    switch (jc.exec()) {
    case QDialog::Rejected:
        return;
    case QDialog::Accepted:
        break;
    }

    joystick_state[joystick_nr].plat_joystick_nr = jc.selectedDevice();
    if (joystick_state[joystick_nr].plat_joystick_nr) {
        for (int c = 0; c < joystick_get_axis_count(type); c++) {
            joystick_state[joystick_nr].axis_mapping[c] = get_axis(jc, c, joystick_nr);
        }
        for (int c = 0; c < joystick_get_button_count(type); c++) {
            joystick_state[joystick_nr].button_mapping[c] = jc.selectedButton(c);
        }
        for (int c = 0; c < joystick_get_button_count(type); c++) {
            joystick_state[joystick_nr].pov_mapping[c][0] = get_pov(jc, c, joystick_nr);
            joystick_state[joystick_nr].pov_mapping[c][1] = get_pov(jc, c, joystick_nr);
        }
    }
}

void SettingsInput::on_pushButtonJoystick1_clicked() {
    updateJoystickConfig(ui->comboBoxJoystick->currentData().toInt(), 0, this);
}

void SettingsInput::on_pushButtonJoystick2_clicked() {
    updateJoystickConfig(ui->comboBoxJoystick->currentData().toInt(), 1, this);
}

void SettingsInput::on_pushButtonJoystick3_clicked() {
    updateJoystickConfig(ui->comboBoxJoystick->currentData().toInt(), 2, this);
}

void SettingsInput::on_pushButtonJoystick4_clicked() {
    updateJoystickConfig(ui->comboBoxJoystick->currentData().toInt(), 3, this);
}
