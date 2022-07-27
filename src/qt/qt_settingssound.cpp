/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Sound/MIDI devices configuration UI module.
 *
 *
 *
 * Authors:	Joakim L. Gilje <jgilje@jgilje.net>
 *
 *		Copyright 2021 Joakim L. Gilje
 */
#include "qt_settingssound.hpp"
#include "ui_qt_settingssound.h"

extern "C" {
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_mpu401.h>
#include <86box/snd_opl.h>
}

#include "qt_deviceconfig.hpp"
#include "qt_models_common.hpp"

SettingsSound::SettingsSound(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsSound)
{
    ui->setupUi(this);
    onCurrentMachineChanged(machine);
}

SettingsSound::~SettingsSound()
{
    delete ui;
}

void SettingsSound::save() {
    sound_card_current = ui->comboBoxSoundCard->currentData().toInt();
    midi_output_device_current = ui->comboBoxMidiOut->currentData().toInt();
    midi_input_device_current = ui->comboBoxMidiIn->currentData().toInt();
    mpu401_standalone_enable = ui->checkBoxMPU401->isChecked() ? 1 : 0;
    SSI2001 = ui->checkBoxSSI2001->isChecked() ? 1 : 0;;
    GAMEBLASTER = ui->checkBoxCMS->isChecked() ? 1 : 0;
    GUS = ui->checkBoxGUS->isChecked() ? 1 : 0;;
    sound_is_float = ui->checkBoxFloat32->isChecked() ? 1 : 0;;
    if (ui->radioButtonYMFM->isChecked())
        fm_driver = FM_DRV_YMFM;
    else
        fm_driver = FM_DRV_NUKED;
}

void SettingsSound::onCurrentMachineChanged(int machineId) {
    this->machineId = machineId;

    auto* model = ui->comboBoxSoundCard->model();
    auto removeRows = model->rowCount();
    int c = 0;
    int selectedRow = 0;
    while (true) {
        /* Skip "internal" if machine doesn't have it. */
        if ((c == 1) && (machine_has_flags(machineId, MACHINE_SOUND) == 0)) {
            c++;
            continue;
        }

        auto* sound_dev = sound_card_getdevice(c);
        QString name = DeviceConfig::DeviceName(sound_dev, sound_card_get_internal_name(c), 1);
        if (name.isEmpty()) {
            break;
        }

        if (sound_card_available(c)) {
            if (device_is_valid(sound_dev, machineId)) {
                int row = Models::AddEntry(model, name, c);
                if (c == sound_card_current) {
                    selectedRow = row - removeRows;
                }
            }
        }

        c++;
    }
    model->removeRows(0, removeRows);
    ui->comboBoxSoundCard->setEnabled(model->rowCount() > 0);
    ui->comboBoxSoundCard->setCurrentIndex(-1);
    ui->comboBoxSoundCard->setCurrentIndex(selectedRow);

    model = ui->comboBoxMidiOut->model();
    removeRows = model->rowCount();
    c = 0;
    selectedRow = 0;
    while (true) {
        QString name = DeviceConfig::DeviceName(midi_out_device_getdevice(c), midi_out_device_get_internal_name(c), 0);
        if (name.isEmpty()) {
            break;
        }

        if (midi_out_device_available(c)) {
            int row = Models::AddEntry(model, name, c);
            if (c == midi_output_device_current) {
                selectedRow = row - removeRows;
            }
        }
        c++;
    }
    model->removeRows(0, removeRows);
    ui->comboBoxMidiOut->setEnabled(model->rowCount() > 0);
    ui->comboBoxMidiOut->setCurrentIndex(-1);
    ui->comboBoxMidiOut->setCurrentIndex(selectedRow);

    model = ui->comboBoxMidiIn->model();
    removeRows = model->rowCount();
    c = 0;
    selectedRow = 0;
    while (true) {
        QString name = DeviceConfig::DeviceName(midi_in_device_getdevice(c), midi_in_device_get_internal_name(c), 0);
        if (name.isEmpty()) {
            break;
        }

        if (midi_in_device_available(c)) {
            int row = Models::AddEntry(model, name, c);
            if (c == midi_input_device_current) {
                selectedRow = row - removeRows;
            }
        }

        c++;
    }
    model->removeRows(0, removeRows);
    ui->comboBoxMidiIn->setEnabled(model->rowCount() > 0);
    ui->comboBoxMidiIn->setCurrentIndex(-1);
    ui->comboBoxMidiIn->setCurrentIndex(selectedRow);

    ui->checkBoxMPU401->setChecked(mpu401_standalone_enable > 0);
    ui->checkBoxSSI2001->setChecked(SSI2001 > 0);
    ui->checkBoxCMS->setChecked(GAMEBLASTER > 0);
    ui->checkBoxGUS->setChecked(GUS > 0);
    ui->checkBoxFloat32->setChecked(sound_is_float > 0);

    bool hasIsa = machine_has_bus(machineId, MACHINE_BUS_ISA) > 0;
    bool hasIsa16 = machine_has_bus(machineId, MACHINE_BUS_ISA16) > 0;
    ui->checkBoxCMS->setEnabled(hasIsa);
    ui->pushButtonConfigureCMS->setEnabled((GAMEBLASTER > 0) && hasIsa);
    ui->checkBoxGUS->setEnabled(hasIsa16);
    ui->pushButtonConfigureGUS->setEnabled((GUS > 0) && hasIsa16);
    ui->checkBoxSSI2001->setEnabled(hasIsa);
    ui->pushButtonConfigureSSI2001->setEnabled((SSI2001 > 0) && hasIsa);
    switch (fm_driver) {
    case FM_DRV_YMFM:
        ui->radioButtonYMFM->setChecked(true);
        break;
    case FM_DRV_NUKED:
    default:
        ui->radioButtonNuked->setChecked(true);
        break;
    }
}

static bool allowMpu401(Ui::SettingsSound *ui) {
    QString midiOut = midi_out_device_get_internal_name(ui->comboBoxMidiOut->currentData().toInt());
    QString midiIn  = midi_in_device_get_internal_name(ui->comboBoxMidiIn->currentData().toInt());

    if (midiOut.isEmpty()) {
        return false;
    }

    if (midiOut == QStringLiteral("none") && midiIn == QStringLiteral("none")) {
        return false;
    }

    return true;
}

void SettingsSound::on_comboBoxSoundCard_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }
    ui->pushButtonConfigureSoundCard->setEnabled(sound_card_has_config(ui->comboBoxSoundCard->currentData().toInt()));
}


void SettingsSound::on_pushButtonConfigureSoundCard_clicked() {
    DeviceConfig::ConfigureDevice(sound_card_getdevice(ui->comboBoxSoundCard->currentData().toInt()), 0, qobject_cast<Settings*>(Settings::settings));
}

void SettingsSound::on_comboBoxMidiOut_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }
    ui->pushButtonConfigureMidiOut->setEnabled(midi_out_device_has_config(ui->comboBoxMidiOut->currentData().toInt()));
    ui->checkBoxMPU401->setEnabled(allowMpu401(ui) && (machine_has_bus(machineId, MACHINE_BUS_ISA) || machine_has_bus(machineId, MACHINE_BUS_MCA)));
    ui->pushButtonConfigureMPU401->setEnabled(allowMpu401(ui) && ui->checkBoxMPU401->isChecked());
}

void SettingsSound::on_pushButtonConfigureMidiOut_clicked() {
    DeviceConfig::ConfigureDevice(midi_out_device_getdevice(ui->comboBoxMidiOut->currentData().toInt()), 0, qobject_cast<Settings*>(Settings::settings));
}

void SettingsSound::on_comboBoxMidiIn_currentIndexChanged(int index) {
    if (index < 0) {
        return;
    }
    ui->pushButtonConfigureMidiIn->setEnabled(midi_in_device_has_config(ui->comboBoxMidiIn->currentData().toInt()));
    ui->checkBoxMPU401->setEnabled(allowMpu401(ui) && (machine_has_bus(machineId, MACHINE_BUS_ISA) || machine_has_bus(machineId, MACHINE_BUS_MCA)));
    ui->pushButtonConfigureMPU401->setEnabled(allowMpu401(ui) && ui->checkBoxMPU401->isChecked());
}

void SettingsSound::on_pushButtonConfigureMidiIn_clicked() {
    DeviceConfig::ConfigureDevice(midi_in_device_getdevice(ui->comboBoxMidiIn->currentData().toInt()), 0, qobject_cast<Settings*>(Settings::settings));
}

void SettingsSound::on_checkBoxMPU401_stateChanged(int state) {
    ui->pushButtonConfigureMPU401->setEnabled(state == Qt::Checked);
}

void SettingsSound::on_checkBoxSSI2001_stateChanged(int state) {
    ui->pushButtonConfigureSSI2001->setEnabled(state == Qt::Checked);
}

void SettingsSound::on_checkBoxCMS_stateChanged(int state) {
    ui->pushButtonConfigureCMS->setEnabled(state == Qt::Checked);
}

void SettingsSound::on_checkBoxGUS_stateChanged(int state) {
    ui->pushButtonConfigureGUS->setEnabled(state == Qt::Checked);
}

void SettingsSound::on_pushButtonConfigureMPU401_clicked() {
    if (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0) {
        DeviceConfig::ConfigureDevice(&mpu401_mca_device, 0, qobject_cast<Settings*>(Settings::settings));
    } else {
        DeviceConfig::ConfigureDevice(&mpu401_device, 0, qobject_cast<Settings*>(Settings::settings));
    }
}

void SettingsSound::on_pushButtonConfigureSSI2001_clicked() {
    DeviceConfig::ConfigureDevice(&ssi2001_device, 0, qobject_cast<Settings*>(Settings::settings));
}

void SettingsSound::on_pushButtonConfigureCMS_clicked() {
    DeviceConfig::ConfigureDevice(&cms_device, 0, qobject_cast<Settings*>(Settings::settings));
}

void SettingsSound::on_pushButtonConfigureGUS_clicked() {
    DeviceConfig::ConfigureDevice(&gus_device, 0, qobject_cast<Settings*>(Settings::settings));
}
