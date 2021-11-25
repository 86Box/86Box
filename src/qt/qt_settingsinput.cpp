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

#include "qt_deviceconfig.hpp"

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

    const auto* machine = &machines[machineId];
    auto* mouseModel = ui->comboBoxMouse->model();
    auto removeRows = mouseModel->rowCount();

    int selectedRow = 0;
    for (int i = 0; i < mouse_get_ndev(); ++i) {
        const auto* dev = mouse_get_device(i);
        if ((i == MOUSE_TYPE_INTERNAL) && !(machines[machineId].flags & MACHINE_MOUSE)) {
            continue;
        }

        if (device_is_valid(dev, machine->flags) == 0) {
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
        int row = joystickModel->rowCount();
        joystickModel->insertRow(row);
        auto idx = joystickModel->index(row, 0);

        joystickModel->setData(idx, joyName, Qt::DisplayRole);
        joystickModel->setData(idx, i, Qt::UserRole);

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
    DeviceConfig::ConfigureDevice(mouse_get_device(mouseId));
}
