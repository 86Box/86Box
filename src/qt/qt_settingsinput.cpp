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

#include "qt_settings_completer.hpp"
#include "qt_settingsinput.hpp"
#include "ui_qt_settingsinput.h"
#include "qt_mainwindow.hpp"
#include "qt_preferences.hpp"

#include "qt_models_common.hpp"
#include "qt_deviceconfig.hpp"
#include "qt_joystickconfiguration.hpp"
#include "qt_defs.hpp"

extern MainWindow *main_window;

joystick_state_t      org_joystick_state[GAMEPORT_MAX][MAX_JOYSTICKS];

SettingsInput::SettingsInput(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsInput)
{
    ui->setupUi(this);

    scKeyboard                      = new SettingsCompleter(ui->comboBoxKeyboard, nullptr);
    scMouse                         = new SettingsCompleter(ui->comboBoxMouse, nullptr);

    scJoystick0                     = new SettingsCompleter(ui->comboBoxJoystick0, nullptr);

    kbd_config_changed   = 0;
    mouse_config_changed = 0;

    for (int i = 0; i < GAMEPORT_MAX; i++) {
        for (int j = 0; j < MAX_JOYSTICKS; j++)
             memcpy(&(org_joystick_state[i][j]), &(joystick_state[i][j]), sizeof(joystick_state_t));
    }

    onCurrentMachineChanged(machine);
}

SettingsInput::~SettingsInput()
{
    delete scJoystick0;

    delete scMouse;
    delete scKeyboard;

    delete ui;
}

static int
has_joystick_state_changed(int gameport_nr, int joystick_nr)
{
    int has_changed = 0;

    has_changed |= (joystick_state[gameport_nr][joystick_nr].plat_joystick_nr !=
                    org_joystick_state[gameport_nr][joystick_nr].plat_joystick_nr);

    for (int axis_nr = 0; axis_nr < MAX_JOY_AXES; axis_nr++) {
        has_changed |= (joystick_state[gameport_nr][joystick_nr].axis_mapping[axis_nr] !=
                        org_joystick_state[gameport_nr][joystick_nr].axis_mapping[axis_nr]);
    }

    for (int button_nr = 0; button_nr < MAX_JOY_BUTTONS; button_nr++) {
        has_changed |= (joystick_state[gameport_nr][joystick_nr].button_mapping[button_nr] !=
                        org_joystick_state[gameport_nr][joystick_nr].button_mapping[button_nr]);
    }

    for (int pov_nr = 0; pov_nr < MAX_JOY_POVS; pov_nr++) {
        has_changed |= (joystick_state[gameport_nr][joystick_nr].pov_mapping[pov_nr][0] !=
                        org_joystick_state[gameport_nr][joystick_nr].pov_mapping[pov_nr][0]);
        has_changed |= (joystick_state[gameport_nr][joystick_nr].pov_mapping[pov_nr][1] !=
                        org_joystick_state[gameport_nr][joystick_nr].pov_mapping[pov_nr][1]);
    }

    return has_changed;
}

int
SettingsInput::changed()
{
    int has_changed = 0;

    has_changed |= (keyboard_type != ui->comboBoxKeyboard->currentData().toInt());
    has_changed |= kbd_config_changed;
    has_changed |= (mouse_type    != ui->comboBoxMouse->currentData().toInt());
    has_changed |= mouse_config_changed;

    has_changed |= (joystick_type[0] != ui->comboBoxJoystick0->currentData().toInt());

    for (int i = 0; i < 4; i++)
        has_changed |= has_joystick_state_changed(0, i);

    return has_changed ? (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET) : 0;
}

void
SettingsInput::restore()
{
}

void
SettingsInput::save()
{
    keyboard_type = ui->comboBoxKeyboard->currentData().toInt();
    mouse_type    = ui->comboBoxMouse->currentData().toInt();

    joystick_type[0] = ui->comboBoxJoystick0->currentData().toInt();
}

void
SettingsInput::onCurrentMachineChanged(int machineId)
{
    // win_settings_video_proc, WM_INITDIALOG
    this->machineId = machineId;

    scKeyboard->removeRows();
    scMouse->removeRows();

    scJoystick0->removeRows();

    auto *keyboardModel = ui->comboBoxKeyboard->model();
    auto  removeRows    = keyboardModel->rowCount();

    int selectedRow = 0;

    int c           = 0;
    int has_int_kbd = !!machine_has_flags(machineId, MACHINE_KEYBOARD);

    for (int i = 0; i < keyboard_get_ndev(); ++i) {
        const auto *dev  = keyboard_get_device(i);
        int         ikbd = (i == KEYBOARD_TYPE_INTERNAL);

        int pc5086_filter = (strstr(keyboard_get_internal_name(i), "ps") && machines[machineId].init == machine_xt_pc5086_init);

        if ((ikbd != has_int_kbd) || !device_is_valid(dev, machineId) || pc5086_filter)
            continue;

        QString name = DeviceConfig::DeviceName(dev, keyboard_get_internal_name(i), 0);
        int     row  = keyboardModel->rowCount();
        keyboardModel->insertRow(row);
        auto idx = keyboardModel->index(row, 0);

        keyboardModel->setData(idx, name, Qt::DisplayRole);
        keyboardModel->setData(idx, i, Qt::UserRole);

        scKeyboard->addDevice(nullptr, name);

        if (i == keyboard_type)
            selectedRow = row - removeRows;

        c++;
    }
    keyboardModel->removeRows(0, removeRows);
    ui->comboBoxKeyboard->setCurrentIndex(-1);
    ui->comboBoxKeyboard->setCurrentIndex(selectedRow);

    if ((c == 1) || has_int_kbd)
        ui->comboBoxKeyboard->setEnabled(false);
    else
        ui->comboBoxKeyboard->setEnabled(true);

    auto *mouseModel = ui->comboBoxMouse->model();
    removeRows       = mouseModel->rowCount();

    selectedRow = 0;
    for (int i = 0; i < mouse_get_ndev(); ++i) {
        const auto *dev = mouse_get_device(i);
        if ((i == MOUSE_TYPE_INTERNAL) && (machine_has_flags(machineId, MACHINE_MOUSE) == 0))
            continue;

        if (device_is_valid(dev, machineId) == 0)
            continue;

        QString name = DeviceConfig::DeviceName(dev, mouse_get_internal_name(i), 0);
        int     row  = mouseModel->rowCount();
        mouseModel->insertRow(row);
        auto idx = mouseModel->index(row, 0);

        mouseModel->setData(idx, name, Qt::DisplayRole);
        mouseModel->setData(idx, i, Qt::UserRole);

        scMouse->addDevice(nullptr, name);

        if (i == mouse_type)
            selectedRow = row - removeRows;
    }
    mouseModel->removeRows(0, removeRows);
    ui->comboBoxMouse->setCurrentIndex(-1);
    ui->comboBoxMouse->setCurrentIndex(selectedRow);

    // Joysticks
    int         i             = 0;
    const char *joyName       = joystick_get_name(i);
    auto       *joystickModel = ui->comboBoxJoystick0->model();
    removeRows                = joystickModel->rowCount();
    selectedRow               = 0;
    while (joyName) {
        int row = Models::AddEntry(joystickModel, tr(joyName).toUtf8().data(), i);
        scJoystick0->addDevice(nullptr, tr(joyName));
        if (i == joystick_type[0])
            selectedRow = row - removeRows;

        ++i;
        joyName = joystick_get_name(i);
    }
    joystickModel->removeRows(0, removeRows);
    ui->comboBoxJoystick0->setCurrentIndex(selectedRow);
}

void
SettingsInput::on_comboBoxKeyboard_currentIndexChanged(int index)
{
    if (index < 0)
        return;
    int keyboardId = ui->comboBoxKeyboard->currentData().toInt();
    ui->pushButtonConfigureKeyboard->setEnabled(keyboard_has_config(keyboardId) > 0);
}

void
SettingsInput::on_comboBoxMouse_currentIndexChanged(int index)
{
    if (index < 0)
        return;
    int mouseId = ui->comboBoxMouse->currentData().toInt();
    ui->pushButtonConfigureMouse->setEnabled(mouse_has_config(mouseId) > 0);
}

void
SettingsInput::on_comboBoxJoystick0_currentIndexChanged(int index)
{
    int joystickId = ui->comboBoxJoystick0->currentData().toInt();
    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        auto *btn = findChild<QPushButton *>(QString("pushButtonJoystick0%1").arg(i + 1));
        if (btn == nullptr)
            continue;

        btn->setEnabled(joystick_get_max_joysticks(joystickId) > i);
    }
}

void
SettingsInput::on_pushButtonConfigureKeyboard_clicked()
{
    int keyboardId = ui->comboBoxKeyboard->currentData().toInt();
    kbd_config_changed |= DeviceConfig::ConfigureDevice(keyboard_get_device(keyboardId));
}

void
SettingsInput::on_pushButtonConfigureMouse_clicked()
{
    int mouseId = ui->comboBoxMouse->currentData().toInt();
    mouse_config_changed |= DeviceConfig::ConfigureDevice(mouse_get_device(mouseId));
}

static int
get_axis(JoystickConfiguration &jc, uint8_t gameport_nr, int joystick_nr, int axis)
{
    int axis_sel = jc.selectedAxis(axis);
    int nr_axes  = plat_joystick_state[joystick_state[gameport_nr][joystick_nr].plat_joystick_nr - 1].nr_axes;

    if (axis_sel < nr_axes)
        return axis_sel;

    axis_sel -= nr_axes;
    if (axis_sel & 1)
        return POV_Y | (axis_sel >> 1);
    else
        return POV_X | (axis_sel >> 1);
}

static int
get_pov(JoystickConfiguration &jc, uint8_t gameport_nr, int joystick_nr, int pov)
{
    int pov_sel = jc.selectedPov(pov);
    int nr_povs = plat_joystick_state[joystick_state[gameport_nr][joystick_nr].plat_joystick_nr - 1].nr_povs * 2;

    if (pov_sel < nr_povs) {
        if (pov_sel & 1)
            return POV_Y | (pov_sel >> 1);
        else
            return POV_X | (pov_sel >> 1);
    }

    return pov_sel - nr_povs;
}

static void
updateJoystickConfig(int type, uint8_t gameport_nr, int joystick_nr, QWidget *parent)
{
    JoystickConfiguration jc(type, gameport_nr, joystick_nr, parent);
    switch (jc.exec()) {
        case QDialog::Rejected:
            return;
        case QDialog::Accepted:
            break;
    }

    joystick_state[gameport_nr][joystick_nr].plat_joystick_nr = jc.selectedDevice();
    if (joystick_state[gameport_nr][joystick_nr].plat_joystick_nr) {
        for (int axis_nr = 0; axis_nr < joystick_get_axis_count(type); axis_nr++)
            joystick_state[gameport_nr][joystick_nr].axis_mapping[axis_nr] = get_axis(jc, gameport_nr, joystick_nr, axis_nr);

        for (int button_nr = 0; button_nr < joystick_get_button_count(type); button_nr++)
            joystick_state[gameport_nr][joystick_nr].button_mapping[button_nr] = jc.selectedButton(button_nr);

        for (int pov_nr = 0; pov_nr < joystick_get_pov_count(type); pov_nr++) {
            joystick_state[gameport_nr][joystick_nr].pov_mapping[pov_nr][0] = get_pov(jc, gameport_nr, joystick_nr, pov_nr * 2); // X Axis
            joystick_state[gameport_nr][joystick_nr].pov_mapping[pov_nr][1] = get_pov(jc, gameport_nr, joystick_nr, pov_nr * 2 + 1); // Y Axis
        }
    }
}

void
SettingsInput::on_pushButtonJoystick01_clicked()
{
    updateJoystickConfig(ui->comboBoxJoystick0->currentData().toInt(), 0, 0, this);
}

void
SettingsInput::on_pushButtonJoystick02_clicked()
{
    updateJoystickConfig(ui->comboBoxJoystick0->currentData().toInt(), 0, 1, this);
}

void
SettingsInput::on_pushButtonJoystick03_clicked()
{
    updateJoystickConfig(ui->comboBoxJoystick0->currentData().toInt(), 0, 2, this);
}

void
SettingsInput::on_pushButtonJoystick04_clicked()
{
    updateJoystickConfig(ui->comboBoxJoystick0->currentData().toInt(), 0, 3, this);
}
