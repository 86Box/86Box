/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Storage devices configuration UI module.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *
 *          Copyright 2021 Joakim L. Gilje
 */
#include "qt_settingsstoragecontrollers.hpp"
#include "ui_qt_settingsstoragecontrollers.h"

extern "C" {
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdc_ext.h>
#include <86box/cdrom_interface.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cassette.h>
}

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

SettingsStorageControllers::SettingsStorageControllers(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsStorageControllers)
{
    ui->setupUi(this);

    onCurrentMachineChanged(machine);
}

SettingsStorageControllers::~SettingsStorageControllers()
{
    delete ui;
}

void
SettingsStorageControllers::save()
{
    /* Storage devices category */
    for (uint8_t i = 0; i < SCSI_CARD_MAX; ++i) {
        QComboBox *cbox      = findChild<QComboBox *>(QString("comboBoxSCSI%1").arg(i + 1));
        scsi_card_current[i] = cbox->currentData().toInt();
    }
    hdc_current[0]          = ui->comboBoxHD->currentData().toInt();
    fdc_current[0]          = ui->comboBoxFD->currentData().toInt();
    cdrom_interface_current = ui->comboBoxCDInterface->currentData().toInt();
    ide_ter_enabled         = ui->checkBoxTertiaryIDE->isChecked() ? 1 : 0;
    ide_qua_enabled         = ui->checkBoxQuaternaryIDE->isChecked() ? 1 : 0;
    cassette_enable         = ui->checkBoxCassette->isChecked() ? 1 : 0;
    lba_enhancer_enabled    = ui->checkBoxLbaEnhancer->isChecked() ? 1 : 0;
}

void
SettingsStorageControllers::onCurrentMachineChanged(int machineId)
{
    this->machineId = machineId;

    /*HD controller config*/
    int   c           = 0;
    auto *model       = ui->comboBoxHD->model();
    auto  removeRows  = model->rowCount();
    int   selectedRow = 0;

    while (true) {
        /* Skip "internal" if machine doesn't have it. */
        if ((c == 1) && (machine_has_flags(machineId, MACHINE_HDC) == 0)) {
            c++;
            continue;
        }

        QString name = DeviceConfig::DeviceName(hdc_get_device(c), hdc_get_internal_name(c), 1);
        if (name.isEmpty()) {
            break;
        }

        if (hdc_available(c)) {
            const device_t *hdc_dev = hdc_get_device(c);

            if (device_is_valid(hdc_dev, machineId)) {
                int row = Models::AddEntry(model, name, c);
                if (c == hdc_current[0]) {
                    selectedRow = row - removeRows;
                }
            }
        }
        c++;
    }
    model->removeRows(0, removeRows);
    ui->comboBoxHD->setEnabled(model->rowCount() > 0);
    ui->comboBoxHD->setCurrentIndex(-1);
    ui->comboBoxHD->setCurrentIndex(selectedRow);

    /*FD controller config*/
    model       = ui->comboBoxFD->model();
    removeRows  = model->rowCount();
    c           = 0;
    selectedRow = 0;
    while (true) {
#if 0
        /* Skip "internal" if machine doesn't have it. */
        if ((c == 1) && (machine_has_flags(machineId, MACHINE_FDC) == 0)) {
            c++;
            continue;
        }
#endif

        QString name = DeviceConfig::DeviceName(fdc_card_getdevice(c), fdc_card_get_internal_name(c), 1);
        if (name.isEmpty()) {
            break;
        }

        if (fdc_card_available(c)) {
            const device_t *fdc_dev = fdc_card_getdevice(c);

            if (device_is_valid(fdc_dev, machineId)) {
                int row = Models::AddEntry(model, name, c);
                if (c == fdc_current[0]) {
                    selectedRow = row - removeRows;
                }
            }
        }
        c++;
    }
    model->removeRows(0, removeRows);
    ui->comboBoxFD->setEnabled(model->rowCount() > 0);
    ui->comboBoxFD->setCurrentIndex(-1);
    ui->comboBoxFD->setCurrentIndex(selectedRow);

    /*CD interface controller config*/
    c           = 0;
    model       = ui->comboBoxCDInterface->model();
    removeRows  = model->rowCount();
    selectedRow = 0;

    while (true) {
        /* Skip "internal" if machine doesn't have it. */
        QString name = DeviceConfig::DeviceName(cdrom_interface_get_device(c), cdrom_interface_get_internal_name(c), 1);
        if (name.isEmpty()) {
            break;
        }

        if (cdrom_interface_available(c)) {
            const device_t *cdrom_interface_dev = cdrom_interface_get_device(c);

            if (device_is_valid(cdrom_interface_dev, machineId)) {
                int row = Models::AddEntry(model, name, c);
                if (c == cdrom_interface_current) {
                    selectedRow = row - removeRows;
                }
            }
        }
        c++;
    }
    model->removeRows(0, removeRows);
    ui->comboBoxCDInterface->setEnabled(model->rowCount() > 0);
    ui->comboBoxCDInterface->setCurrentIndex(-1);
    ui->comboBoxCDInterface->setCurrentIndex(selectedRow);

    // SCSI Card
    QComboBox *         cbox[SCSI_CARD_MAX]         = { 0 };
    QAbstractItemModel *models[SCSI_CARD_MAX]       = { 0 };
    int                 removeRows_[SCSI_CARD_MAX]  = { 0 };
    int                 selectedRows[SCSI_CARD_MAX] = { 0 };
    int                 m_has_scsi                  = machine_has_flags(machineId, MACHINE_SCSI);

    for (uint8_t i = 0; i < SCSI_CARD_MAX; ++i) {
        cbox[i]        = findChild<QComboBox *>(QString("comboBoxSCSI%1").arg(i + 1));
        models[i]      = cbox[i]->model();
        removeRows_[i] = models[i]->rowCount();
    }

    c = 0;
    while (true) {
        const QString name = DeviceConfig::DeviceName(scsi_card_getdevice(c),
                                                      scsi_card_get_internal_name(c), 1);

        if (name.isEmpty())
            break;

        if (scsi_card_available(c)) {
            if (device_is_valid(scsi_card_getdevice(c), machineId)) {
                for (uint8_t i = 0; i < SCSI_CARD_MAX; ++i) {
                    if ((c != 1) || ((i == 0) && m_has_scsi)) {
                        int row = Models::AddEntry(models[i], name, c);

                        if (c == scsi_card_current[i])
                            selectedRows[i] = row - removeRows_[i];
                    }
                }
            }
        }

       c++;
    }

    for (uint8_t i = 0; i < SCSI_CARD_MAX; ++i) {
        models[i]->removeRows(0, removeRows_[i]);
        cbox[i]->setEnabled(models[i]->rowCount() > 1);
        cbox[i]->setCurrentIndex(-1);
        cbox[i]->setCurrentIndex(selectedRows[i]);
    }

    int is_at = IS_AT(machineId);
    ui->checkBoxTertiaryIDE->setEnabled(is_at > 0);
    ui->checkBoxQuaternaryIDE->setEnabled(is_at > 0);
    ui->checkBoxTertiaryIDE->setChecked(ui->checkBoxTertiaryIDE->isEnabled() && ide_ter_enabled);
    ui->checkBoxQuaternaryIDE->setChecked(ui->checkBoxQuaternaryIDE->isEnabled() && ide_qua_enabled);

    if (machine_has_bus(machineId, MACHINE_BUS_CASSETTE)) {
        ui->checkBoxCassette->setChecked(cassette_enable > 0);
        ui->checkBoxCassette->setEnabled(true);
    } else {
        ui->checkBoxCassette->setChecked(false);
        ui->checkBoxCassette->setEnabled(false);
    }

    ui->checkBoxLbaEnhancer->setChecked(lba_enhancer_enabled > 0 && device_available(&lba_enhancer_device));
    ui->pushButtonConfigureLbaEnhancer->setEnabled(ui->checkBoxLbaEnhancer->isChecked());
}

void
SettingsStorageControllers::on_comboBoxHD_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    ui->pushButtonHD->setEnabled(hdc_has_config(ui->comboBoxHD->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxFD_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    ui->pushButtonFD->setEnabled(hdc_has_config(ui->comboBoxFD->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxCDInterface_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    ui->pushButtonCDInterface->setEnabled(cdrom_interface_has_config(ui->comboBoxCDInterface->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_checkBoxTertiaryIDE_stateChanged(int arg1)
{
    ui->pushButtonTertiaryIDE->setEnabled(arg1 == Qt::Checked);
}

void
SettingsStorageControllers::on_checkBoxQuaternaryIDE_stateChanged(int arg1)
{
    ui->pushButtonQuaternaryIDE->setEnabled(arg1 == Qt::Checked);
}

void
SettingsStorageControllers::on_pushButtonHD_clicked()
{
    DeviceConfig::ConfigureDevice(hdc_get_device(ui->comboBoxHD->currentData().toInt()), 0, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsStorageControllers::on_pushButtonFD_clicked()
{
    DeviceConfig::ConfigureDevice(fdc_card_getdevice(ui->comboBoxFD->currentData().toInt()), 0, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsStorageControllers::on_pushButtonCDInterface_clicked()
{
    DeviceConfig::ConfigureDevice(cdrom_interface_get_device(ui->comboBoxCDInterface->currentData().toInt()), 0, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsStorageControllers::on_pushButtonTertiaryIDE_clicked()
{
    DeviceConfig::ConfigureDevice(&ide_ter_device, 0, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsStorageControllers::on_pushButtonQuaternaryIDE_clicked()
{
    DeviceConfig::ConfigureDevice(&ide_qua_device, 0, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsStorageControllers::on_comboBoxSCSI1_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    ui->pushButtonSCSI1->setEnabled(scsi_card_has_config(ui->comboBoxSCSI1->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxSCSI2_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    ui->pushButtonSCSI2->setEnabled(scsi_card_has_config(ui->comboBoxSCSI2->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxSCSI3_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    ui->pushButtonSCSI3->setEnabled(scsi_card_has_config(ui->comboBoxSCSI3->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxSCSI4_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    ui->pushButtonSCSI4->setEnabled(scsi_card_has_config(ui->comboBoxSCSI4->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_pushButtonSCSI1_clicked()
{
    DeviceConfig::ConfigureDevice(scsi_card_getdevice(ui->comboBoxSCSI1->currentData().toInt()), 1, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsStorageControllers::on_pushButtonSCSI2_clicked()
{
    DeviceConfig::ConfigureDevice(scsi_card_getdevice(ui->comboBoxSCSI2->currentData().toInt()), 2, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsStorageControllers::on_pushButtonSCSI3_clicked()
{
    DeviceConfig::ConfigureDevice(scsi_card_getdevice(ui->comboBoxSCSI3->currentData().toInt()), 3, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsStorageControllers::on_pushButtonSCSI4_clicked()
{
    DeviceConfig::ConfigureDevice(scsi_card_getdevice(ui->comboBoxSCSI4->currentData().toInt()), 4, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsStorageControllers::on_checkBoxLbaEnhancer_stateChanged(int arg1)
{
    ui->pushButtonConfigureLbaEnhancer->setEnabled(arg1 != 0);
}

void
SettingsStorageControllers::on_pushButtonConfigureLbaEnhancer_clicked()
{
    DeviceConfig::ConfigureDevice(&lba_enhancer_device);
}
