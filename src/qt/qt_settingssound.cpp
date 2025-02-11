/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Sound/MIDI devices configuration UI module.
 *
 *
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Jasmine Iwanek <jriwanek@gmail.com>
 *
 *          Copyright 2021      Joakim L. Gilje
 *          Copyright 2022-2023 Jasmine Iwanek
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

SettingsSound::SettingsSound(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsSound)
{
    ui->setupUi(this);
    onCurrentMachineChanged(machine);
}

SettingsSound::~SettingsSound()
{
    delete ui;
}

void
SettingsSound::save()
{
    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
        auto *cbox = findChild<QComboBox *>(QString("comboBoxSoundCard%1").arg(i + 1));
        sound_card_current[i]         = cbox->currentData().toInt();
    }

    midi_output_device_current = ui->comboBoxMidiOut->currentData().toInt();
    midi_input_device_current  = ui->comboBoxMidiIn->currentData().toInt();
    mpu401_standalone_enable   = ui->checkBoxMPU401->isChecked() ? 1 : 0;

    sound_is_float = ui->checkBoxFloat32->isChecked() ? 1 : 0;

    if (ui->radioButtonYMFM->isChecked())
        fm_driver = FM_DRV_YMFM;
    else
        fm_driver = FM_DRV_NUKED;
}

void
SettingsSound::onCurrentMachineChanged(const int machineId)
{
    this->machineId = machineId;

    int c;
    int selectedRow;

    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
        auto *     cbox          = findChild<QComboBox *>(QString("comboBoxSoundCard%1").arg(i + 1));
        auto *     model         = cbox->model();
        const auto removeRows = model->rowCount();
        c                = 0;
        selectedRow      = 0;

        while (true) {
            /* Skip "internal" if machine doesn't have it or this is not the primary card. */
            if ((c == 1) && ((i > 0) || (machine_has_flags(machineId, MACHINE_SOUND) == 0))) {
                c++;
                continue;
            }

            auto name = DeviceConfig::DeviceName(sound_card_getdevice(c), sound_card_get_internal_name(c), 1);
            if (name.isEmpty()) {
                break;
            }

            if (sound_card_available(c) && device_is_valid(sound_card_getdevice(c), machineId)) {
                int row = Models::AddEntry(model, name, c);
                if (c == sound_card_current[i]) {
                    selectedRow = row - removeRows;
                }
            }
            c++;
        }

        model->removeRows(0, removeRows);
        cbox->setEnabled(model->rowCount() > 0);
        cbox->setCurrentIndex(-1);
        cbox->setCurrentIndex(selectedRow);
    }

    auto model       = ui->comboBoxMidiOut->model();
    auto removeRows  = model->rowCount();
    c           = 0;
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

    model       = ui->comboBoxMidiIn->model();
    removeRows  = model->rowCount();
    c           = 0;
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
    ui->checkBoxFloat32->setChecked(sound_is_float > 0);

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

static bool
allowMpu401(Ui::SettingsSound *ui)
{
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

void
SettingsSound::on_comboBoxSoundCard1_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    int sndCard = ui->comboBoxSoundCard1->currentData().toInt();
    if (sndCard == SOUND_INTERNAL)
        ui->pushButtonConfigureSoundCard1->setEnabled(machine_has_flags(machineId, MACHINE_SOUND) &&
                                            device_has_config(machine_get_snd_device(machineId)));
    else
        ui->pushButtonConfigureSoundCard1->setEnabled(sound_card_has_config(sndCard));
}

void
SettingsSound::on_pushButtonConfigureSoundCard1_clicked()
{
    int sndCard = ui->comboBoxSoundCard1->currentData().toInt();
    auto *device = sound_card_getdevice(sndCard);
    if (sndCard == SOUND_INTERNAL)
        device = machine_get_snd_device(machineId);
    DeviceConfig::ConfigureDevice(device, 1, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsSound::on_comboBoxSoundCard2_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    int sndCard = ui->comboBoxSoundCard2->currentData().toInt();
    ui->pushButtonConfigureSoundCard2->setEnabled(sound_card_has_config(sndCard));
}

void
SettingsSound::on_pushButtonConfigureSoundCard2_clicked()
{
    int sndCard = ui->comboBoxSoundCard2->currentData().toInt();
    auto *device = sound_card_getdevice(sndCard);
    DeviceConfig::ConfigureDevice(device, 2, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsSound::on_comboBoxSoundCard3_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    int sndCard = ui->comboBoxSoundCard3->currentData().toInt();
    ui->pushButtonConfigureSoundCard3->setEnabled(sound_card_has_config(sndCard));
}

void
SettingsSound::on_pushButtonConfigureSoundCard3_clicked()
{
    int sndCard = ui->comboBoxSoundCard3->currentData().toInt();
    auto *device = sound_card_getdevice(sndCard);
    DeviceConfig::ConfigureDevice(device, 3, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsSound::on_comboBoxSoundCard4_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    int sndCard = ui->comboBoxSoundCard4->currentData().toInt();
    ui->pushButtonConfigureSoundCard4->setEnabled(sound_card_has_config(sndCard));
}

void
SettingsSound::on_pushButtonConfigureSoundCard4_clicked()
{
    int sndCard = ui->comboBoxSoundCard4->currentData().toInt();
    auto *device = sound_card_getdevice(sndCard);
    DeviceConfig::ConfigureDevice(device, 4, qobject_cast<Settings *>(Settings::settings));
}

void
SettingsSound::on_comboBoxMidiOut_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    ui->pushButtonConfigureMidiOut->setEnabled(midi_out_device_has_config(ui->comboBoxMidiOut->currentData().toInt()));
    ui->checkBoxMPU401->setEnabled(allowMpu401(ui) && (machine_has_bus(machineId, MACHINE_BUS_ISA) || machine_has_bus(machineId, MACHINE_BUS_MCA)));
    ui->pushButtonConfigureMPU401->setEnabled(allowMpu401(ui) && ui->checkBoxMPU401->isChecked());
}

void
SettingsSound::on_pushButtonConfigureMidiOut_clicked()
{
    DeviceConfig::ConfigureDevice(midi_out_device_getdevice(ui->comboBoxMidiOut->currentData().toInt()), 0,
                                  qobject_cast<Settings *>(Settings::settings));
}

void
SettingsSound::on_comboBoxMidiIn_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }
    ui->pushButtonConfigureMidiIn->setEnabled(midi_in_device_has_config(ui->comboBoxMidiIn->currentData().toInt()));
    ui->checkBoxMPU401->setEnabled(allowMpu401(ui) && (machine_has_bus(machineId, MACHINE_BUS_ISA) || machine_has_bus(machineId, MACHINE_BUS_MCA)));
    ui->pushButtonConfigureMPU401->setEnabled(allowMpu401(ui) && ui->checkBoxMPU401->isChecked());
}

void
SettingsSound::on_pushButtonConfigureMidiIn_clicked()
{
    DeviceConfig::ConfigureDevice(midi_in_device_getdevice(ui->comboBoxMidiIn->currentData().toInt()), 0,
                                  qobject_cast<Settings *>(Settings::settings));
}

void
SettingsSound::on_checkBoxMPU401_stateChanged(int state)
{
    ui->pushButtonConfigureMPU401->setEnabled(state == Qt::Checked);
}

void
SettingsSound::on_pushButtonConfigureMPU401_clicked()
{
    if (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0)
        DeviceConfig::ConfigureDevice(&mpu401_mca_device, 0, qobject_cast<Settings *>(Settings::settings));
    else
        DeviceConfig::ConfigureDevice(&mpu401_device, 0, qobject_cast<Settings *>(Settings::settings));
}
