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
    for (uint8_t i = 0; i < HDC_MAX; ++i) {
        QComboBox *cbox = findChild<QComboBox *>(QString("comboBoxHD%1").arg(i + 1));
        hdc_current[i]  = cbox->currentData().toInt();
    }
    for (uint8_t i = 0; i < SCSI_CARD_MAX; ++i) {
        QComboBox *cbox      = findChild<QComboBox *>(QString("comboBoxSCSI%1").arg(i + 1));
        scsi_card_current[i] = cbox->currentData().toInt();
    }
    fdc_current[0]          = ui->comboBoxFD->currentData().toInt();
    cdrom_interface_current = ui->comboBoxCDInterface->currentData().toInt();
    cassette_enable         = ui->checkBoxCassette->isChecked() ? 1 : 0;
}

void
SettingsStorageControllers::onCurrentMachineChanged(int machineId)
{
    this->machineId = machineId;

    /* FD controller config */
    int   c           = 0;
    auto *model       = ui->comboBoxFD->model();
    auto  removeRows  = model->rowCount();
    int   selectedRow = 0;

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
    ui->labelCDInterface->setVisible(true);
    ui->comboBoxCDInterface->setVisible(true);
    ui->pushButtonCDInterface->setVisible(true);

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

    // HD Controller
    QComboBox          *hd_cbox[HDC_MAX]         = { 0 };
    QAbstractItemModel *hd_models[HDC_MAX]       = { 0 };
    int                 hd_removeRows_[HDC_MAX]  = { 0 };
    int                 hd_selectedRows[HDC_MAX] = { 0 };

    for (uint8_t i = 0; i < HDC_MAX; ++i) {
        hd_cbox[i]        = findChild<QComboBox *>(QString("comboBoxHD%1").arg(i + 1));
        hd_models[i]      = hd_cbox[i]->model();
        hd_removeRows_[i] = hd_models[i]->rowCount();
    }

    c = 0;
    while (true) {
        const QString name = DeviceConfig::DeviceName(hdc_get_device(c),
                                                      hdc_get_internal_name(c), 1);

        if (name.isEmpty())
            break;

        if (hdc_available(c)) {
            if (device_is_valid(hdc_get_device(c), machineId)) {
                for (uint8_t i = 0; i < HDC_MAX; ++i) {
                    /* Skip "internal" if machine doesn't have it. */
                    if ((c == 1) && ((i > 0) || (machine_has_flags(machineId, MACHINE_HDC) == 0)))
                        continue;

                    int row = Models::AddEntry(hd_models[i], name, c);

                    if (c == hdc_current[i])
                        hd_selectedRows[i] = row - hd_removeRows_[i];
                }
            }
        }

        c++;
    }

    for (uint8_t i = 0; i < HDC_MAX; ++i) {
        hd_models[i]->removeRows(0, hd_removeRows_[i]);
        hd_cbox[i]->setEnabled(hd_models[i]->rowCount() > 1);
        hd_cbox[i]->setCurrentIndex(-1);
        hd_cbox[i]->setCurrentIndex(hd_selectedRows[i]);
    }

    // SCSI Card
    QComboBox          *cbox[SCSI_CARD_MAX]         = { 0 };
    QAbstractItemModel *models[SCSI_CARD_MAX]       = { 0 };
    int                 removeRows_[SCSI_CARD_MAX]  = { 0 };
    int                 selectedRows[SCSI_CARD_MAX] = { 0 };

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
                    int row = Models::AddEntry(models[i], name, c);

                    if (c == scsi_card_current[i])
                        selectedRows[i] = row - removeRows_[i];
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

    if (machine_has_bus(machineId, MACHINE_BUS_CASSETTE)) {
        ui->checkBoxCassette->setChecked(cassette_enable > 0);
        ui->checkBoxCassette->setEnabled(true);
    } else {
        ui->checkBoxCassette->setChecked(false);
        ui->checkBoxCassette->setEnabled(false);
    }
}

void
SettingsStorageControllers::on_comboBoxFD_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonFD->setEnabled(fdc_card_has_config(ui->comboBoxFD->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxHD1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonHD1->setEnabled(hdc_has_config(ui->comboBoxHD1->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxHD2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonHD2->setEnabled(hdc_has_config(ui->comboBoxHD2->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxHD3_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonHD3->setEnabled(hdc_has_config(ui->comboBoxHD3->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxHD4_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonHD4->setEnabled(hdc_has_config(ui->comboBoxHD4->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxCDInterface_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonCDInterface->setEnabled(cdrom_interface_has_config(ui->comboBoxCDInterface->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_pushButtonFD_clicked()
{
    DeviceConfig::ConfigureDevice(fdc_card_getdevice(ui->comboBoxFD->currentData().toInt()));
}

void
SettingsStorageControllers::on_pushButtonHD1_clicked()
{
    DeviceConfig::ConfigureDevice(hdc_get_device(ui->comboBoxHD1->currentData().toInt()), 1);
}

void
SettingsStorageControllers::on_pushButtonHD2_clicked()
{
    DeviceConfig::ConfigureDevice(hdc_get_device(ui->comboBoxHD2->currentData().toInt()), 2);
}

void
SettingsStorageControllers::on_pushButtonHD3_clicked()
{
    DeviceConfig::ConfigureDevice(hdc_get_device(ui->comboBoxHD3->currentData().toInt()), 3);
}

void
SettingsStorageControllers::on_pushButtonHD4_clicked()
{
    DeviceConfig::ConfigureDevice(hdc_get_device(ui->comboBoxHD4->currentData().toInt()), 4);
}

void
SettingsStorageControllers::on_pushButtonCDInterface_clicked()
{
    DeviceConfig::ConfigureDevice(cdrom_interface_get_device(ui->comboBoxCDInterface->currentData().toInt()));
}

void
SettingsStorageControllers::on_comboBoxSCSI1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonSCSI1->setEnabled(scsi_card_has_config(ui->comboBoxSCSI1->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxSCSI2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonSCSI2->setEnabled(scsi_card_has_config(ui->comboBoxSCSI2->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxSCSI3_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonSCSI3->setEnabled(scsi_card_has_config(ui->comboBoxSCSI3->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_comboBoxSCSI4_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonSCSI4->setEnabled(scsi_card_has_config(ui->comboBoxSCSI4->currentData().toInt()) > 0);
}

void
SettingsStorageControllers::on_pushButtonSCSI1_clicked()
{
    DeviceConfig::ConfigureDevice(scsi_card_getdevice(ui->comboBoxSCSI1->currentData().toInt()), 1);
}

void
SettingsStorageControllers::on_pushButtonSCSI2_clicked()
{
    DeviceConfig::ConfigureDevice(scsi_card_getdevice(ui->comboBoxSCSI2->currentData().toInt()), 2);
}

void
SettingsStorageControllers::on_pushButtonSCSI3_clicked()
{
    DeviceConfig::ConfigureDevice(scsi_card_getdevice(ui->comboBoxSCSI3->currentData().toInt()), 3);
}

void
SettingsStorageControllers::on_pushButtonSCSI4_clicked()
{
    DeviceConfig::ConfigureDevice(scsi_card_getdevice(ui->comboBoxSCSI4->currentData().toInt()), 4);
}
