/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Other peripherals configuration UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Jasmine Iwanek <jriwanek@gmail.com>
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2025 Jasmine Iwanek
 */
#include <cstdint>
#include <cstdio>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/isamem.h>
#include <86box/isarom.h>
#include <86box/isartc.h>
#include <86box/unittester.h>
#include <86box/novell_cardkey.h>
}

#include "qt_settings_completer.hpp"

#include "qt_settingsotherperipherals.hpp"
#include "ui_qt_settingsotherperipherals.h"

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

#include "qt_defs.hpp"

SettingsOtherPeripherals::SettingsOtherPeripherals(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsOtherPeripherals)
{
    ui->setupUi(this);

    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        scIsaMemCard[i] = new SettingsCompleter(findChild<QComboBox *>(QString("comboBoxIsaMemCard%1").arg(i + 1)), nullptr);
        isamem_cfg_changed[i] = 0;
    }

    for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
        scIsaRomCard[i] = new SettingsCompleter(findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1)), nullptr);
        isarom_cfg_changed[i] = 0;
    }

    scRTC           = new SettingsCompleter(ui->comboBoxRTC, nullptr);
    isartc_cfg_changed         = 0;

    unittester_cfg_changed     = 0;
    novell_keycard_cfg_changed = 0;

    onCurrentMachineChanged(machine);
}

SettingsOtherPeripherals::~SettingsOtherPeripherals()
{
    for (uint8_t i = 0; i < ISAMEM_MAX; ++i)
        delete scIsaMemCard[i];

    for (uint8_t i = 0; i < ISAROM_MAX; ++i)
        delete scIsaRomCard[i];

    delete scRTC;

    delete ui;
}

void
SettingsOtherPeripherals::onCurrentMachineChanged(int machineId)
{
    this->machineId = machineId;

    bool machineHasIsa = (machine_has_bus(machineId, MACHINE_BUS_ISA) > 0);

    ui->pushButtonConfigureRTC->setEnabled(machineHasIsa);
    ui->comboBoxRTC->setEnabled(machineHasIsa);
    ui->checkBoxISABugger->setEnabled(machineHasIsa);
    ui->pushButtonConfigureUT->setEnabled(unittester_enabled > 0);
    ui->checkBoxKeyCard->setEnabled(machineHasIsa);
    ui->pushButtonConfigureKeyCard->setEnabled(novell_keycard_enabled > 0);

    ui->checkBoxISABugger->setChecked((machineHasIsa && (bugger_enabled > 0)) ? true : false);
    ui->checkBoxPOSTCard->setChecked(postcard_enabled > 0 ? true : false);
    ui->checkBoxUnitTester->setChecked(unittester_enabled > 0 ? true : false);
    ui->checkBoxKeyCard->setChecked((machineHasIsa && (novell_keycard_enabled > 0)) ? true : false);

    scRTC->removeRows();
    ui->comboBoxRTC->clear();

    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        scIsaMemCard[i]->removeRows();
        if (auto *cb = findChild<QComboBox *>(QString("comboBoxIsaMemCard%1").arg(i + 1)))
            cb->clear();
    }

    for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
        scIsaRomCard[i]->removeRows();
        if (auto *cb = findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1)))
            cb->clear();
    }

    int c           = 0;
    int selectedRow = 0;

    // ISA RTC Cards
    auto *model = ui->comboBoxRTC->model();
    while (true) {
        const QString name = DeviceConfig::DeviceName(isartc_get_device(c), isartc_get_internal_name(c), 0);
        if (name.isEmpty())
            break;

        if (!device_is_valid(isartc_get_device(c), machineId))
            break;

        int row = Models::AddEntry(model, name, c);
        scRTC->addDevice(nullptr, name);
        if (c == isartc_type)
            selectedRow = row;

        ++c;
    }
    ui->comboBoxRTC->setCurrentIndex(selectedRow);
    ui->pushButtonConfigureRTC->setEnabled((isartc_type != 0) && isartc_has_config(isartc_type) && machineHasIsa);

    // ISA Memory Expansion Cards
    QComboBox          *isamem_cbox[ISAMEM_MAX]         = { 0 };
    QAbstractItemModel *isamem_models[ISAMEM_MAX]       = { 0 };
    int                 isamem_removeRows_[ISAMEM_MAX]  = { 0 };
    int                 isamem_selectedRows[ISAMEM_MAX] = { 0 };

    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        isamem_cbox[i]        = findChild<QComboBox *>(QString("comboBoxIsaMemCard%1").arg(i + 1));
        isamem_models[i]      = isamem_cbox[i]->model();
        isamem_removeRows_[i] = isamem_models[i]->rowCount();
    }

    c = 0;
    while (true) {
        const QString name = DeviceConfig::DeviceName(isamem_get_device(c),
                                                      isamem_get_internal_name(c), 0);

        if (name.isEmpty())
            break;

        if (device_is_valid(isamem_get_device(c), machineId)) {
            for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
                int row = Models::AddEntry(isamem_models[i], name, c);
                scIsaMemCard[i]->addDevice(nullptr, name);

                if (c == isamem_type[i])
                    isamem_selectedRows[i] = row - isamem_removeRows_[i];
            }
        }

        c++;
    }

    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        isamem_models[i]->removeRows(0, isamem_removeRows_[i]);
        isamem_cbox[i]->setEnabled(isamem_models[i]->rowCount() > 1);
        isamem_cbox[i]->setCurrentIndex(-1);
        isamem_cbox[i]->setCurrentIndex(isamem_selectedRows[i]);
        findChild<QPushButton *>(QString("pushButtonConfigureIsaMemCard%1").arg(i + 1))->setEnabled((isamem_type[i] != 0) && isamem_has_config(isamem_type[i]) && machineHasIsa);
    }

    // ISA ROM Expansion Cards
    QComboBox          *isarom_cbox[ISAROM_MAX]         = { 0 };
    QAbstractItemModel *isarom_models[ISAROM_MAX]       = { 0 };
    int                 isarom_removeRows_[ISAROM_MAX]  = { 0 };
    int                 isarom_selectedRows[ISAROM_MAX] = { 0 };

    for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
        isarom_cbox[i]        = findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1));
        isarom_models[i]      = isarom_cbox[i]->model();
        isarom_removeRows_[i] = isarom_models[i]->rowCount();
    }

    c = 0;
    while (true) {
        const QString name = DeviceConfig::DeviceName(isarom_get_device(c),
                                                      isarom_get_internal_name(c), 0);

        if (name.isEmpty())
            break;

        if (device_is_valid(isarom_get_device(c), machineId)) {
            for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
                int row = Models::AddEntry(isarom_models[i], name, c);
                scIsaRomCard[i]->addDevice(nullptr, name);

                if (c == isarom_type[i])
                    isarom_selectedRows[i] = row - isarom_removeRows_[i];
            }
        }

        c++;
    }

    for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
        isarom_models[i]->removeRows(0, isarom_removeRows_[i]);
        isarom_cbox[i]->setEnabled(isarom_models[i]->rowCount() > 1);
        isarom_cbox[i]->setCurrentIndex(-1);
        isarom_cbox[i]->setCurrentIndex(isarom_selectedRows[i]);
        findChild<QPushButton *>(QString("pushButtonConfigureIsaRomCard%1").arg(i + 1))->setEnabled((isarom_type[i] != 0) && isarom_has_config(isarom_type[i]) && machineHasIsa);
    }
}

int
SettingsOtherPeripherals::changed()
{
    int has_changed = 0;

    has_changed |= (isartc_type            != ui->comboBoxRTC->currentData().toInt());
    has_changed |= isartc_cfg_changed;
    has_changed |= (bugger_enabled         != (ui->checkBoxISABugger->isChecked() ? 1 : 0));
    has_changed |= (postcard_enabled       != (ui->checkBoxPOSTCard->isChecked() ? 1 : 0));
    has_changed |= (unittester_enabled     != (ui->checkBoxUnitTester->isChecked() ? 1 : 0));
    has_changed |= unittester_cfg_changed;
    has_changed |= (novell_keycard_enabled != (ui->checkBoxKeyCard->isChecked() ? 1 : 0));
    has_changed |= novell_keycard_cfg_changed;

    /* ISA memory boards. */
    for (int i = 0; i < ISAMEM_MAX; i++) {
        auto *cbox     = findChild<QComboBox *>(QString("comboBoxIsaMemCard%1").arg(i + 1));
        has_changed |= (isamem_type[i]         != cbox->currentData().toInt());
        has_changed |= isamem_cfg_changed[i];
    }

    /* ISA ROM boards. */
    for (int i = 0; i < ISAROM_MAX; i++) {
        auto *cbox     = findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1));
        has_changed |= (isarom_type[i]         != cbox->currentData().toInt());
        has_changed |= isarom_cfg_changed[i];
    }

    return has_changed ? (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET) : 0;
}

void
SettingsOtherPeripherals::restore()
{
}

void
SettingsOtherPeripherals::save(int soft)
{
    if (soft)
        return;

    /* Other peripherals category */
    isartc_type            = ui->comboBoxRTC->currentData().toInt();
    bugger_enabled         = ui->checkBoxISABugger->isChecked() ? 1 : 0;
    postcard_enabled       = ui->checkBoxPOSTCard->isChecked() ? 1 : 0;
    unittester_enabled     = ui->checkBoxUnitTester->isChecked() ? 1 : 0;
    novell_keycard_enabled = ui->checkBoxKeyCard->isChecked() ? 1 : 0;

    /* ISA memory boards. */
    for (int i = 0; i < ISAMEM_MAX; i++) {
        auto *cbox     = findChild<QComboBox *>(QString("comboBoxIsaMemCard%1").arg(i + 1));
        isamem_type[i] = cbox->currentData().toInt();
    }

    /* ISA ROM boards. */
    for (int i = 0; i < ISAROM_MAX; i++) {
        auto *cbox     = findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1));
        isarom_type[i] = cbox->currentData().toInt();
    }
}

void
SettingsOtherPeripherals::on_comboBoxRTC_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureRTC->setEnabled((index != 0) && isartc_has_config(index) && machine_has_bus(machineId, MACHINE_BUS_ISA));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureRTC_clicked()
{
    isartc_cfg_changed |= DeviceConfig::ConfigureDevice(isartc_get_device(ui->comboBoxRTC->currentData().toInt()));
}

void
SettingsOtherPeripherals::on_comboBoxIsaMemCard1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaMemCard1->setEnabled((index != 0) && isamem_has_config(index) && machine_has_bus(machineId, MACHINE_BUS_ISA));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaMemCard1_clicked()
{
    isamem_cfg_changed[0] |= DeviceConfig::ConfigureDevice(isamem_get_device(ui->comboBoxIsaMemCard1->currentData().toInt()), 1);
}

void
SettingsOtherPeripherals::on_comboBoxIsaMemCard2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaMemCard2->setEnabled((index != 0) && isamem_has_config(index) && machine_has_bus(machineId, MACHINE_BUS_ISA));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaMemCard2_clicked()
{
    isamem_cfg_changed[1] |= DeviceConfig::ConfigureDevice(isamem_get_device(ui->comboBoxIsaMemCard2->currentData().toInt()), 2);
}

void
SettingsOtherPeripherals::on_comboBoxIsaMemCard3_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaMemCard3->setEnabled((index != 0) && isamem_has_config(index) && machine_has_bus(machineId, MACHINE_BUS_ISA));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaMemCard3_clicked()
{
    isamem_cfg_changed[2] |= DeviceConfig::ConfigureDevice(isamem_get_device(ui->comboBoxIsaMemCard3->currentData().toInt()), 3);
}

void
SettingsOtherPeripherals::on_comboBoxIsaMemCard4_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaMemCard4->setEnabled((index != 0) && isamem_has_config(index) && machine_has_bus(machineId, MACHINE_BUS_ISA));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaMemCard4_clicked()
{
    isamem_cfg_changed[3] |= DeviceConfig::ConfigureDevice(isamem_get_device(ui->comboBoxIsaMemCard4->currentData().toInt()), 4);
}

void
SettingsOtherPeripherals::on_comboBoxIsaRomCard1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaRomCard1->setEnabled((index != 0) && isarom_has_config(index) && machine_has_bus(machineId, MACHINE_BUS_ISA));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaRomCard1_clicked()
{
    isarom_cfg_changed[0] |= DeviceConfig::ConfigureDevice(isarom_get_device(ui->comboBoxIsaRomCard1->currentData().toInt()), 1);
}

void
SettingsOtherPeripherals::on_comboBoxIsaRomCard2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaRomCard2->setEnabled((index != 0) && isarom_has_config(index) && machine_has_bus(machineId, MACHINE_BUS_ISA));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaRomCard2_clicked()
{
    isarom_cfg_changed[1] |= DeviceConfig::ConfigureDevice(isarom_get_device(ui->comboBoxIsaRomCard2->currentData().toInt()), 2);
}

void
SettingsOtherPeripherals::on_comboBoxIsaRomCard3_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaRomCard3->setEnabled((index != 0) && isarom_has_config(index) && machine_has_bus(machineId, MACHINE_BUS_ISA));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaRomCard3_clicked()
{
    isarom_cfg_changed[2] |= DeviceConfig::ConfigureDevice(isarom_get_device(ui->comboBoxIsaRomCard3->currentData().toInt()), 3);
}

void
SettingsOtherPeripherals::on_comboBoxIsaRomCard4_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaRomCard4->setEnabled((index != 0) && isarom_has_config(index) && machine_has_bus(machineId, MACHINE_BUS_ISA));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureIsaRomCard4_clicked()
{
    isarom_cfg_changed[3] |= DeviceConfig::ConfigureDevice(isarom_get_device(ui->comboBoxIsaRomCard4->currentData().toInt()), 4);
}

void
SettingsOtherPeripherals::on_checkBoxUnitTester_stateChanged(int arg1)
{
    ui->pushButtonConfigureUT->setEnabled(arg1 != 0);
}

void
SettingsOtherPeripherals::on_pushButtonConfigureUT_clicked()
{
    unittester_cfg_changed |= DeviceConfig::ConfigureDevice(&unittester_device);
}

void
SettingsOtherPeripherals::on_checkBoxKeyCard_stateChanged(int arg1)
{
    ui->pushButtonConfigureKeyCard->setEnabled(arg1 != 0);
}

void
SettingsOtherPeripherals::on_pushButtonConfigureKeyCard_clicked()
{
    novell_keycard_cfg_changed |= DeviceConfig::ConfigureDevice(&novell_keycard_device);
}
