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
#include <86box/mcamem.h>
#include <86box/isamem.h>
#include <86box/isarom.h>
#include <86box/isartc.h>
#include <86box/unittester.h>
#include <86box/softpower.h>
#include <86box/novell_cardkey.h>
}

#include "qt_settings_completer.hpp"

#include "qt_settingsotherperipherals.hpp"
#include "ui_qt_settingsotherperipherals.h"

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

#include "qt_defs.hpp"

static bool
hasIsaOrSidecarBus(int machineId)
{
    return machine_has_bus(machineId, MACHINE_BUS_ISA | MACHINE_BUS_SIDECAR) > 0;
}

SettingsOtherPeripherals::SettingsOtherPeripherals(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsOtherPeripherals)
{
    ui->setupUi(this);

    /* Memory expansion cards: one shared set of four slots, fed from the
       ISA board list (isamem) on ISA machines and from the MCA board list
       (mcamem) on MCA machines. */
    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        scMemExpCard[i] = new SettingsCompleter(findChild<QComboBox *>(QString("comboBoxMemExpCard%1").arg(i + 1)), nullptr);
        memexp_cfg_changed[i] = 0;
    }

    for (uint8_t i = 0; i < ISAROM_MAX; ++i) {
        scIsaRomCard[i] = new SettingsCompleter(findChild<QComboBox *>(QString("comboBoxIsaRomCard%1").arg(i + 1)), nullptr);
        isarom_cfg_changed[i] = 0;
    }

    scRTC           = new SettingsCompleter(ui->comboBoxRTC, nullptr);
    isartc_cfg_changed         = 0;

    unittester_cfg_changed     = 0;
    softpower_cfg_changed      = 0;
    novell_keycard_cfg_changed = 0;

    onCurrentMachineChanged(machine);
}

SettingsOtherPeripherals::~SettingsOtherPeripherals()
{
    for (uint8_t i = 0; i < ISAMEM_MAX; ++i)
        delete scMemExpCard[i];

    for (uint8_t i = 0; i < ISAROM_MAX; ++i)
        delete scIsaRomCard[i];

    delete scRTC;

    delete ui;
}

void
SettingsOtherPeripherals::onCurrentMachineChanged(int machineId)
{
    this->machineId = machineId;

    bool machineHasIsa          = (machine_has_bus(machineId, MACHINE_BUS_ISA) > 0);
    bool machineHasIsaOrSidecar = hasIsaOrSidecarBus(machineId);

    ui->pushButtonConfigureRTC->setEnabled(machineHasIsaOrSidecar);
    ui->comboBoxRTC->setEnabled(machineHasIsaOrSidecar);
    ui->checkBoxISABugger->setEnabled(machineHasIsa);
    ui->pushButtonConfigureUT->setEnabled(unittester_enabled > 0);
    ui->checkBoxKeyCard->setEnabled(machineHasIsa);
    ui->pushButtonConfigureKeyCard->setEnabled(novell_keycard_enabled > 0);
    ui->checkBoxSoftPower->setEnabled(machineHasIsa);
    ui->pushButtonConfigureSoftPower->setEnabled((machineHasIsa && (softpower_enabled > 0)));

    ui->checkBoxISABugger->setChecked((machineHasIsa && (bugger_enabled > 0)) ? true : false);
    ui->checkBoxPOSTCard->setChecked(postcard_enabled > 0 ? true : false);
    ui->checkBoxUnitTester->setChecked(unittester_enabled > 0 ? true : false);
    ui->checkBoxKeyCard->setChecked((machineHasIsa && (novell_keycard_enabled > 0)) ? true : false);
    ui->checkBoxSoftPower->setChecked((machineHasIsa && (softpower_enabled > 0)) ? true : false);

    scRTC->removeRows();
    ui->comboBoxRTC->clear();

    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        scMemExpCard[i]->removeRows();
        if (auto *cb = findChild<QComboBox *>(QString("comboBoxMemExpCard%1").arg(i + 1)))
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

        if (!device_is_valid(isartc_get_device(c), machineId)) {
            ++c;
            continue;
        }

        int row = Models::AddEntry(model, name, c);
        scRTC->addDevice(nullptr, name);
        if (c == isartc_type)
            selectedRow = row;

        ++c;
    }
    ui->comboBoxRTC->setCurrentIndex(selectedRow);
    ui->pushButtonConfigureRTC->setEnabled((isartc_type != 0) && isartc_has_config(isartc_type) && machineHasIsaOrSidecar);

    // Memory Expansion Cards (shared UI: ISA or MCA boards depending on
    // the machine bus).  The isamem/mcamem databases and their config
    // globals remain separate; only the four dropdown slots are shared.
    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    const bool isa_bus = !mca_bus && hasIsaOrSidecarBus(machineId);

    QComboBox          *mem_cbox[ISAMEM_MAX]         = { 0 };
    QAbstractItemModel *mem_models[ISAMEM_MAX]       = { 0 };
    int                 mem_removeRows_[ISAMEM_MAX]  = { 0 };
    int                 mem_selectedRows[ISAMEM_MAX] = { 0 };

    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        mem_cbox[i]        = findChild<QComboBox *>(QString("comboBoxMemExpCard%1").arg(i + 1));
        mem_models[i]      = mem_cbox[i]->model();
        mem_removeRows_[i] = mem_models[i]->rowCount();
    }

    c = 0;
    while (true) {
        const device_t *dev   = NULL;
        const char     *iname = NULL;

        if (mca_bus) {
            dev   = mcamem_get_device(c);
            iname = mcamem_get_internal_name(c);
        } else if (isa_bus) {
            dev   = isamem_get_device(c);
            iname = isamem_get_internal_name(c);
        } else
            break;

        const QString name = DeviceConfig::DeviceName(dev, iname, 0);
        if (name.isEmpty())
            break;

        if (device_is_valid(dev, machineId)) {
            for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
                int cur = mca_bus ? mcamem_type[i] : isamem_type[i];
                int row = Models::AddEntry(mem_models[i], name, c);
                scMemExpCard[i]->addDevice(nullptr, name);

                if (c == cur)
                    mem_selectedRows[i] = row - mem_removeRows_[i];
            }
        }

        c++;
    }

    for (uint8_t i = 0; i < ISAMEM_MAX; ++i) {
        const device_t *seldev = mca_bus ? mcamem_get_device(mcamem_type[i]) : isamem_get_device(isamem_type[i]);
        bool            hascfg = mca_bus ? (mcamem_has_config(mcamem_type[i]) != 0) : (isamem_has_config(isamem_type[i]) != 0);

        mem_models[i]->removeRows(0, mem_removeRows_[i]);
        mem_cbox[i]->setEnabled(mem_models[i]->rowCount() > 1);
        mem_cbox[i]->setCurrentIndex(-1);
        mem_cbox[i]->setCurrentIndex(mem_selectedRows[i]);
        findChild<QPushButton *>(QString("pushButtonConfigureMemExpCard%1").arg(i + 1))->setEnabled(device_is_valid(seldev, machineId) && hascfg);
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
        findChild<QPushButton *>(QString("pushButtonConfigureIsaRomCard%1").arg(i + 1))->setEnabled((isarom_type[i] != 0) && isarom_has_config(isarom_type[i]) && machineHasIsaOrSidecar);
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
    has_changed |= (softpower_enabled       != (ui->checkBoxSoftPower->isChecked() ? 1 : 0));
    has_changed |= softpower_cfg_changed;
    has_changed |= (novell_keycard_enabled != (ui->checkBoxKeyCard->isChecked() ? 1 : 0));
    has_changed |= novell_keycard_cfg_changed;

    /* Memory expansion boards (shared slots; active family by machine). */
    {
        const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);

        if (mca_bus || hasIsaOrSidecarBus(machineId)) {
            for (int i = 0; i < ISAMEM_MAX; i++) {
                auto *cbox    = findChild<QComboBox *>(QString("comboBoxMemExpCard%1").arg(i + 1));
                int   cur     = mca_bus ? mcamem_type[i] : isamem_type[i];
                has_changed  |= (cur != cbox->currentData().toInt());
                has_changed  |= memexp_cfg_changed[i];
            }
        }
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
    softpower_enabled       = ui->checkBoxSoftPower->isChecked() ? 1 : 0;
    novell_keycard_enabled = ui->checkBoxKeyCard->isChecked() ? 1 : 0;

    /* Memory expansion boards (shared slots; write the active family and
       clear the inactive one, matching the single-UI layout). */
    {
        const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
        const bool isa_bus = !mca_bus && hasIsaOrSidecarBus(machineId);

        for (int i = 0; i < ISAMEM_MAX; i++) {
            int val = (mca_bus || isa_bus) ? findChild<QComboBox *>(QString("comboBoxMemExpCard%1").arg(i + 1))->currentData().toInt() : 0;

            if (mca_bus) {
                mcamem_type[i] = val;
                isamem_type[i] = 0;
            } else {
                isamem_type[i] = val;
                mcamem_type[i] = 0;
            }
        }
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

    ui->pushButtonConfigureRTC->setEnabled((index != 0) && isartc_has_config(index) && hasIsaOrSidecarBus(machineId));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureRTC_clicked()
{
    isartc_cfg_changed |= DeviceConfig::ConfigureDevice(isartc_get_device(ui->comboBoxRTC->currentData().toInt()));
}

/* Shared memory-expansion slot helpers: resolve the board device and its
   config capability from the machine's active bus family (MCA or ISA). */
static const device_t *
memexp_slot_device(int idx, bool mca_bus)
{
    return mca_bus ? mcamem_get_device(idx) : isamem_get_device(idx);
}

static int
memexp_slot_has_config(int idx, bool mca_bus)
{
    return mca_bus ? mcamem_has_config(idx) : isamem_has_config(idx);
}

void
SettingsOtherPeripherals::on_comboBoxMemExpCard1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    ui->pushButtonConfigureMemExpCard1->setEnabled(device_is_valid(memexp_slot_device(index, mca_bus), machineId) && memexp_slot_has_config(index, mca_bus));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureMemExpCard1_clicked()
{
    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    memexp_cfg_changed[0] |= DeviceConfig::ConfigureDevice(memexp_slot_device(ui->comboBoxMemExpCard1->currentData().toInt(), mca_bus), 1);
}

void
SettingsOtherPeripherals::on_comboBoxMemExpCard2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    ui->pushButtonConfigureMemExpCard2->setEnabled(device_is_valid(memexp_slot_device(index, mca_bus), machineId) && memexp_slot_has_config(index, mca_bus));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureMemExpCard2_clicked()
{
    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    memexp_cfg_changed[1] |= DeviceConfig::ConfigureDevice(memexp_slot_device(ui->comboBoxMemExpCard2->currentData().toInt(), mca_bus), 2);
}

void
SettingsOtherPeripherals::on_comboBoxMemExpCard3_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    ui->pushButtonConfigureMemExpCard3->setEnabled(device_is_valid(memexp_slot_device(index, mca_bus), machineId) && memexp_slot_has_config(index, mca_bus));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureMemExpCard3_clicked()
{
    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    memexp_cfg_changed[2] |= DeviceConfig::ConfigureDevice(memexp_slot_device(ui->comboBoxMemExpCard3->currentData().toInt(), mca_bus), 3);
}

void
SettingsOtherPeripherals::on_comboBoxMemExpCard4_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    ui->pushButtonConfigureMemExpCard4->setEnabled(device_is_valid(memexp_slot_device(index, mca_bus), machineId) && memexp_slot_has_config(index, mca_bus));
}

void
SettingsOtherPeripherals::on_pushButtonConfigureMemExpCard4_clicked()
{
    const bool mca_bus = (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0);
    memexp_cfg_changed[3] |= DeviceConfig::ConfigureDevice(memexp_slot_device(ui->comboBoxMemExpCard4->currentData().toInt(), mca_bus), 4);
}

void
SettingsOtherPeripherals::on_comboBoxIsaRomCard1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureIsaRomCard1->setEnabled((index != 0) && isarom_has_config(index) && hasIsaOrSidecarBus(machineId));
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

    ui->pushButtonConfigureIsaRomCard2->setEnabled((index != 0) && isarom_has_config(index) && hasIsaOrSidecarBus(machineId));
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

    ui->pushButtonConfigureIsaRomCard3->setEnabled((index != 0) && isarom_has_config(index) && hasIsaOrSidecarBus(machineId));
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

    ui->pushButtonConfigureIsaRomCard4->setEnabled((index != 0) && isarom_has_config(index) && hasIsaOrSidecarBus(machineId));
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
SettingsOtherPeripherals::on_checkBoxSoftPower_stateChanged(int arg1)
{
    ui->pushButtonConfigureSoftPower->setEnabled(arg1 != 0);
}

void
SettingsOtherPeripherals::on_pushButtonConfigureSoftPower_clicked()
{
    softpower_cfg_changed |= DeviceConfig::ConfigureDevice(&softpower_device);
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
