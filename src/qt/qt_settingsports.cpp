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
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *
 *          Copyright 2022 Cacodemon345
 *          Copyright 2022 Jasmine Iwanek
 *          Copyright 2021 Joakim L. Gilje
 */
#include <cstdint>
#include <cstdio>

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

#include "qt_defs.hpp"

#include "qt_settings_completer.hpp"

#include "qt_settingsports.hpp"
#include "ui_qt_settingsports.h"

SettingsPorts::SettingsPorts(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsPorts)
{
    ui->setupUi(this);

    for (int i = 0; i < PARALLEL_MAX; i++) {
        scLpt[i]                  = new SettingsCompleter(findChild<QComboBox *>(QString("comboBoxLpt%1").arg(i + 1)), nullptr);
        lpt_device_cfg_changed[i] = 0;
    }

    for (int i = 0; i < SERIAL_MAX_UI; i++) {
        scCom[i]                  = new SettingsCompleter(findChild<QComboBox *>(QString("comboBoxCom%1").arg(i + 1)), nullptr);
        com_device_cfg_changed[i] = 0;
    }

    onCurrentMachineChanged(machine);
}

SettingsPorts::~SettingsPorts()
{
    for (int i = 0; i < PARALLEL_MAX; i++)
        delete scLpt[i];
    for (int i = 0; i < SERIAL_MAX_UI; i++)
        delete scCom[i];

    delete ui;
}

int
SettingsPorts::changed()
{
    int has_changed  = 0;
    int soft_changed = 0;

    has_changed |= (jumpered_internal_ecp_dma != ui->comboBoxLptECPDMA->currentData().toInt());

    for (int i = 0; i < PARALLEL_MAX; i++) {
        auto *cbox     = findChild<QComboBox *>(QString("comboBoxLpt%1").arg(i + 1));
        auto *checkBox = findChild<QCheckBox *>(QString("checkBoxParallel%1").arg(i + 1));
        if (cbox != NULL)
            soft_changed |= (lpt_ports[i].device           != cbox->currentData().toInt());
        if (checkBox != NULL)
            has_changed  |= (lpt_ports[i].enabled          != (checkBox->isChecked() ? 1 : 0));
        soft_changed  |= lpt_device_cfg_changed[i];
    }

    for (int i = 0; i < SERIAL_MAX_UI; i++) {
    	auto *cbox     = findChild<QComboBox *>(QString("comboBoxCom%1").arg(i + 1));
        auto *checkBox = findChild<QCheckBox *>(QString("checkBoxSerial%1").arg(i + 1));
        if (cbox != NULL)
            soft_changed |= (serial_passthrough_enabled[i] != cbox->currentData().toInt());
        if (checkBox != NULL)
            has_changed  |= (com_ports[i].enabled          != (checkBox->isChecked() ? 1 : 0));
        soft_changed  |= lpt_device_cfg_changed[i];
    }

    return has_changed ? (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET) :
                         (soft_changed ? SETTINGS_CHANGED : 0);
}

void
SettingsPorts::restore()
{
}

void
SettingsPorts::save()
{
    jumpered_internal_ecp_dma = ui->comboBoxLptECPDMA->currentData().toInt();

    for (int i = 0; i < PARALLEL_MAX; i++) {
        auto *cbox     = findChild<QComboBox *>(QString("comboBoxLpt%1").arg(i + 1));
        auto *checkBox = findChild<QCheckBox *>(QString("checkBoxParallel%1").arg(i + 1));
        if (cbox != NULL)
            lpt_ports[i].device = cbox->currentData().toInt();
        if (checkBox != NULL)
            lpt_ports[i].enabled = checkBox->isChecked() ? 1 : 0;
    }

    for (int i = 0; i < SERIAL_MAX_UI; i++) {
        auto *cbox     = findChild<QComboBox *>(QString("comboBoxCom%1").arg(i + 1));
        auto *checkBox = findChild<QCheckBox *>(QString("checkBoxSerial%1").arg(i + 1));
        if (cbox != NULL)
            serial_passthrough_enabled[i] = cbox->currentData().toInt();
        if (checkBox != NULL)
            com_ports[i].enabled = checkBox->isChecked() ? 1 : 0;
    }
}

void
SettingsPorts::onCurrentMachineChanged(int machineId)
{
    this->machineId = machineId;

    int c = 0;

    auto *lptEcpDmaModel   = ui->comboBoxLptECPDMA->model();
    auto  removeRowsEcpDma = lptEcpDmaModel->rowCount();

    int has_jumpers = !!machine_has_jumpered_ecp_dma(machineId, DMA_ANY);

    int selectedRow = -2;
    int first       = -2;

    for (int i = 0; i < 9; ++i) {
        int j = machine_map_jumpered_ecp_dma(i);

        if ((has_jumpers && ((j == DMA_NONE) || !machine_has_jumpered_ecp_dma(machineId, j))) || (!has_jumpers && (j != DMA_NONE)))
            continue;

        if (first == -2)
            first = j;

        QString name = tr(machine_get_jumpered_ecp_dma_name(i));
        int     row  = lptEcpDmaModel->rowCount();
        lptEcpDmaModel->insertRow(row);
        auto idx = lptEcpDmaModel->index(row, 0);

        lptEcpDmaModel->setData(idx, name, Qt::DisplayRole);
        lptEcpDmaModel->setData(idx, j, Qt::UserRole);

        if (j == jumpered_internal_ecp_dma)
            selectedRow = row - removeRowsEcpDma;

        c++;
    }

    if (selectedRow == -2)
        selectedRow = first;

    lptEcpDmaModel->removeRows(0, removeRowsEcpDma);
    ui->comboBoxLptECPDMA->setCurrentIndex(-1);
    ui->comboBoxLptECPDMA->setCurrentIndex(selectedRow);

    if ((c == 1) || !has_jumpers)
        ui->comboBoxLptECPDMA->setEnabled(false);
    else
        ui->comboBoxLptECPDMA->setEnabled(true);

    c = 0;

    // LPT Device
#define CBOX_MAX MAX(PARALLEL_MAX, SERIAL_MAX_UI)
    QComboBox          *cbox[CBOX_MAX]         = { 0 };
    QAbstractItemModel *models[CBOX_MAX]       = { 0 };
    int                 removeRows_[CBOX_MAX]  = { 0 };
    int                 selectedRows[CBOX_MAX] = { 0 };

    for (uint8_t i = 0; i < PARALLEL_MAX; ++i) {
        scLpt[i]->removeRows();
        cbox[i]        = findChild<QComboBox *>(QString("comboBoxLpt%1").arg(i + 1));
        models[i]      = cbox[i]->model();
        removeRows_[i] = models[i]->rowCount();
    }

    while (true) {
        const QString name = DeviceConfig::DeviceName(lpt_device_getdevice(c),
                                                      lpt_device_get_internal_name(c), -1);

        if (name.isEmpty())
            break;

        if (lpt_device_available(c)) {
            if (name.isEmpty())
                break;

            if (device_is_valid(lpt_device_getdevice(c), machineId)) {
                for (uint8_t i = 0; i < PARALLEL_MAX; ++i) {
                    int row = Models::AddEntry(models[i], name, c);
                    scLpt[i]->addDevice(nullptr, name);

                    if (c == lpt_ports[i].device)
                        selectedRows[i] = row - removeRows_[i];
                }
            }
        }

        c++;
    }

    for (uint8_t i = 0; i < PARALLEL_MAX; ++i) {
        models[i]->removeRows(0, removeRows_[i]);
        cbox[i]->setEnabled(models[i]->rowCount() > 1);
        cbox[i]->setCurrentIndex(-1);
        cbox[i]->setCurrentIndex(selectedRows[i]);

        auto *checkBox  = findChild<QCheckBox *>(QString("checkBoxParallel%1").arg(i + 1));
        auto *buttonCfg = findChild<QPushButton *>(QString("pushButtonConfigureLpt%1").arg(i + 1));
        if (checkBox != NULL)
            checkBox->setChecked(lpt_ports[i].enabled > 0);
        if (cbox[i] != NULL) {
            cbox[i]->setEnabled(lpt_ports[i].enabled > 0);
            if (buttonCfg != NULL) {
                int lptDevice = cbox[i]->currentData().toInt();
                buttonCfg->setEnabled(lpt_device_has_config(lptDevice) && (lpt_ports[i].enabled > 0));
            }
        }
    }

    c = 0;

    // COM Device
    for (uint8_t i = 0; i < SERIAL_MAX_UI; ++i) {
        scCom[i]->removeRows();
        cbox[i]        = findChild<QComboBox *>(QString("comboBoxCom%1").arg(i + 1));
        models[i]      = cbox[i]->model();
        removeRows_[i] = models[i]->rowCount();
    }

    while (true) {
        const device_t *device = (c == 0) ? lpt_device_getdevice(0) : ((c == 1) ? &serial_passthrough_device : nullptr); /* hack to obtain a None device */
        const QString name = DeviceConfig::DeviceName(device,
                                                      device ? device->internal_name : nullptr, -1);

        if (name.isEmpty())
            break;

        if (device_available(device)) {
            if (name.isEmpty())
                break;

            if (device_is_valid(device, machineId)) {
                for (uint8_t i = 0; i < SERIAL_MAX_UI; ++i) {
                    int row = Models::AddEntry(models[i], name, c);
                    scCom[i]->addDevice(nullptr, name);

                    if (c == serial_passthrough_enabled[i])
                        selectedRows[i] = row - removeRows_[i];
                }
            }
        }

        c++;
    }

    for (int i = 0; i < SERIAL_MAX_UI; i++) {
        models[i]->removeRows(0, removeRows_[i]);
        cbox[i]->setEnabled(models[i]->rowCount() > 1);
        cbox[i]->setCurrentIndex(-1);
        cbox[i]->setCurrentIndex(selectedRows[i]);

        auto *checkBox  = findChild<QCheckBox *>(QString("checkBoxSerial%1").arg(i + 1));
        auto *buttonCfg = findChild<QPushButton *>(QString("pushButtonConfigureCom%1").arg(i + 1));
        if (checkBox != NULL)
            checkBox->setChecked(com_ports[i].enabled > 0);
        if (cbox[i] != NULL) {
            cbox[i]->setEnabled(com_ports[i].enabled > 0);
            if (buttonCfg != NULL) {
                int comDevice = cbox[i]->currentData().toInt();
                buttonCfg->setEnabled(comDevice && device_has_config(&serial_passthrough_device) && (com_ports[i].enabled > 0));
            }
        }
    }
}

void
SettingsPorts::on_comboBoxLpt1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int lptDevice = ui->comboBoxLpt1->currentData().toInt();

    ui->pushButtonConfigureLpt1->setEnabled(ui->comboBoxLpt1->isEnabled() && lpt_device_has_config(lptDevice));
}

void
SettingsPorts::on_pushButtonConfigureLpt1_clicked()
{
    int   lptDevice = ui->comboBoxLpt1->currentData().toInt();
    auto *device    = lpt_device_getdevice(lptDevice);

    lpt_device_cfg_changed[0] = DeviceConfig::ConfigureDevice(device, 1);
}

void
SettingsPorts::on_comboBoxLpt2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int lptDevice = ui->comboBoxLpt2->currentData().toInt();

    ui->pushButtonConfigureLpt2->setEnabled(ui->comboBoxLpt2->isEnabled() && lpt_device_has_config(lptDevice));
}

void
SettingsPorts::on_pushButtonConfigureLpt2_clicked()
{
    int   lptDevice = ui->comboBoxLpt2->currentData().toInt();
    auto *device    = lpt_device_getdevice(lptDevice);

    lpt_device_cfg_changed[1] = DeviceConfig::ConfigureDevice(device, 1);
}

void
SettingsPorts::on_comboBoxLpt3_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int lptDevice = ui->comboBoxLpt3->currentData().toInt();

    ui->pushButtonConfigureLpt3->setEnabled(ui->comboBoxLpt3->isEnabled() && lpt_device_has_config(lptDevice));
}

void
SettingsPorts::on_pushButtonConfigureLpt3_clicked()
{
    int   lptDevice = ui->comboBoxLpt3->currentData().toInt();
    auto *device    = lpt_device_getdevice(lptDevice);

    lpt_device_cfg_changed[2] = DeviceConfig::ConfigureDevice(device, 1);
}

void
SettingsPorts::on_comboBoxLpt4_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int lptDevice = ui->comboBoxLpt4->currentData().toInt();

    ui->pushButtonConfigureLpt4->setEnabled(ui->comboBoxLpt4->isEnabled() && lpt_device_has_config(lptDevice));
}

void
SettingsPorts::on_pushButtonConfigureLpt4_clicked()
{
    int   lptDevice = ui->comboBoxLpt4->currentData().toInt();
    auto *device    = lpt_device_getdevice(lptDevice);

    lpt_device_cfg_changed[3] = DeviceConfig::ConfigureDevice(device, 1);
}

void
SettingsPorts::on_checkBoxParallel1_stateChanged(int state)
{
    ui->comboBoxLpt1->setEnabled(state == Qt::Checked);
    on_comboBoxLpt1_currentIndexChanged(0);
}

void
SettingsPorts::on_checkBoxParallel2_stateChanged(int state)
{
    ui->comboBoxLpt2->setEnabled(state == Qt::Checked);
    on_comboBoxLpt2_currentIndexChanged(0);
}

void
SettingsPorts::on_checkBoxParallel3_stateChanged(int state)
{
    ui->comboBoxLpt3->setEnabled(state == Qt::Checked);
    on_comboBoxLpt3_currentIndexChanged(0);
}

void
SettingsPorts::on_checkBoxParallel4_stateChanged(int state)
{
    ui->comboBoxLpt4->setEnabled(state == Qt::Checked);
    on_comboBoxLpt4_currentIndexChanged(0);
}

void
SettingsPorts::on_comboBoxCom1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int comDevice = ui->comboBoxCom1->currentData().toInt();

    ui->pushButtonConfigureCom1->setEnabled(ui->comboBoxCom1->isEnabled() && comDevice && device_has_config(&serial_passthrough_device));
}

void
SettingsPorts::on_pushButtonConfigureCom1_clicked()
{
    int   comDevice = ui->comboBoxCom1->currentData().toInt();
    auto *device    = &serial_passthrough_device; (void) comDevice;

    com_device_cfg_changed[0] = DeviceConfig::ConfigureDevice(device, 1);
}

void
SettingsPorts::on_comboBoxCom2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int comDevice = ui->comboBoxCom2->currentData().toInt();

    ui->pushButtonConfigureCom2->setEnabled(ui->comboBoxCom2->isEnabled() && comDevice && device_has_config(&serial_passthrough_device));
}

void
SettingsPorts::on_pushButtonConfigureCom2_clicked()
{
    int   comDevice = ui->comboBoxCom2->currentData().toInt();
    auto *device    = &serial_passthrough_device; (void) comDevice;

    com_device_cfg_changed[1] = DeviceConfig::ConfigureDevice(device, 1);
}

void
SettingsPorts::on_comboBoxCom3_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int comDevice = ui->comboBoxCom3->currentData().toInt();

    ui->pushButtonConfigureCom3->setEnabled(ui->comboBoxCom3->isEnabled() && comDevice && device_has_config(&serial_passthrough_device));
}

void
SettingsPorts::on_pushButtonConfigureCom3_clicked()
{
    int   comDevice = ui->comboBoxCom3->currentData().toInt();
    auto *device    = &serial_passthrough_device; (void) comDevice;

    com_device_cfg_changed[2] = DeviceConfig::ConfigureDevice(device, 1);
}

void
SettingsPorts::on_comboBoxCom4_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int comDevice = ui->comboBoxCom4->currentData().toInt();

    ui->pushButtonConfigureCom4->setEnabled(ui->comboBoxCom4->isEnabled() && comDevice && device_has_config(&serial_passthrough_device));
}

void
SettingsPorts::on_pushButtonConfigureCom4_clicked()
{
    int   comDevice = ui->comboBoxCom4->currentData().toInt();
    auto *device    = &serial_passthrough_device; (void) comDevice;

    com_device_cfg_changed[3] = DeviceConfig::ConfigureDevice(device, 1);
}

void
SettingsPorts::on_checkBoxSerial1_stateChanged(int state)
{
    ui->comboBoxCom1->setEnabled(state == Qt::Checked);
    on_comboBoxCom1_currentIndexChanged(0);
}

void
SettingsPorts::on_checkBoxSerial2_stateChanged(int state)
{
    ui->comboBoxCom2->setEnabled(state == Qt::Checked);
    on_comboBoxCom2_currentIndexChanged(0);
}

void
SettingsPorts::on_checkBoxSerial3_stateChanged(int state)
{
    ui->comboBoxCom3->setEnabled(state == Qt::Checked);
    on_comboBoxCom3_currentIndexChanged(0);
}

void
SettingsPorts::on_checkBoxSerial4_stateChanged(int state)
{
    ui->comboBoxCom4->setEnabled(state == Qt::Checked);
    on_comboBoxCom4_currentIndexChanged(0);
}
