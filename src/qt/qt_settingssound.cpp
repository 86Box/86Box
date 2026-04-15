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
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Jasmine Iwanek <jriwanek@gmail.com>
 *
 *          Copyright 2021      Joakim L. Gilje
 *          Copyright 2022-2025 Jasmine Iwanek
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

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

#include "qt_defs.hpp"

#include "qt_settings_completer.hpp"

#include "qt_settingssound.hpp"
#include "ui_qt_settingssound.h"

SettingsSound::SettingsSound(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsSound)
{
    ui->setupUi(this);

    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
        sound_card_cfg_changed[i] = 0;
        scSound[i]                = new SettingsCompleter(findChild<QComboBox *>(QString("comboBoxSoundCard%1").arg(i + 1)), nullptr);
    }

    mpu401_cfg_changed             = 0;

    midi_output_device_cfg_changed = 0;
    midi_input_device_cfg_changed  = 0;

    scMidiOut              = new SettingsCompleter(ui->comboBoxMidiOut, nullptr);
    scMidiIn               = new SettingsCompleter(ui->comboBoxMidiIn, nullptr);

    onCurrentMachineChanged(machine);
}

SettingsSound::~SettingsSound()
{
    delete scMidiIn;
    delete scMidiOut;

    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i)
        delete scSound[i];

    delete ui;
}

int
SettingsSound::changed()
{
    int has_changed  = 0;
    int soft_changed = 0;

    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
        QComboBox *cbox  = findChild<QComboBox *>(QString("comboBoxSoundCard%1").arg(i + 1));
        has_changed     |= (sound_card_current[i]      != cbox->currentData().toInt());
        has_changed     |= sound_card_cfg_changed[i];
    }

    has_changed  |= (fm_driver                  != ui->comboBoxFM->currentData().toInt());
    has_changed  |= (mpu401_standalone_enable   != (ui->checkBoxMPU401->isChecked() ? 1 : 0));
    has_changed  |= mpu401_cfg_changed;
    has_changed  |= (sound_is_float             != (ui->checkBoxFloat32->isChecked() ? 1 : 0));
    has_changed  |= (QString(sound_output_device) != ui->comboBoxAudioOutputDevice->currentData().toString());

    soft_changed |= (midi_output_device_current != ui->comboBoxMidiOut->currentData().toInt());
    soft_changed |= midi_output_device_cfg_changed;
    soft_changed |= (midi_input_device_current  != ui->comboBoxMidiIn->currentData().toInt());
    soft_changed |= midi_input_device_cfg_changed;

    return has_changed ? (SETTINGS_CHANGED | SETTINGS_REQUIRE_HARD_RESET) :
                         (soft_changed ? SETTINGS_CHANGED : 0);
}

void
SettingsSound::restore()
{
}

void
SettingsSound::save()
{
    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
        QComboBox *cbox       = findChild<QComboBox *>(QString("comboBoxSoundCard%1").arg(i + 1));
        sound_card_current[i] = cbox->currentData().toInt();
    }

    fm_driver = ui->comboBoxFM->currentData().toInt();

    midi_output_device_current = ui->comboBoxMidiOut->currentData().toInt();

    midi_input_device_current = ui->comboBoxMidiIn->currentData().toInt();

    mpu401_standalone_enable = ui->checkBoxMPU401->isChecked() ? 1 : 0;

    sound_is_float = ui->checkBoxFloat32->isChecked() ? 1 : 0;

    QByteArray devName = ui->comboBoxAudioOutputDevice->currentData().toString().toUtf8();
    strncpy(sound_output_device, devName.constData(), sizeof(sound_output_device) - 1);
    sound_output_device[sizeof(sound_output_device) - 1] = '\0';
}

void
SettingsSound::onCurrentMachineChanged(const int machineId)
{
    this->machineId = machineId;

    int c;
    int selectedRow;

    // Sound Cards
    QComboBox          *cbox[SOUND_CARD_MAX]         = { 0 };
    QAbstractItemModel *models[SOUND_CARD_MAX]       = { 0 };
    int                 removeRows_[SOUND_CARD_MAX]  = { 0 };
    int                 selectedRows[SOUND_CARD_MAX] = { 0 };
    int                 m_has_snd                    = machine_has_flags(machineId, MACHINE_SOUND);

    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
        scSound[i]->removeRows();
        cbox[i]        = findChild<QComboBox *>(QString("comboBoxSoundCard%1").arg(i + 1));
        models[i]      = cbox[i]->model();
        removeRows_[i] = models[i]->rowCount();
    }

    scMidiOut->removeRows();
    scMidiIn->removeRows();

    c = 0;
    while (true) {
        QString name = DeviceConfig::DeviceName(sound_card_getdevice(c),
                                                sound_card_get_internal_name(c), 1);

        if (name.isEmpty())
            break;

        if (sound_card_available(c)) {
            if (device_is_valid(sound_card_getdevice(c), machineId)) {
                for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
                    if ((c != 1) || ((i == 0) && m_has_snd)) {
                        if (i == 0 && c == 1 && m_has_snd && machine_get_snd_device(machineId)) {
                            name += QString(" (%1)").arg(DeviceConfig::DeviceName(machine_get_snd_device(machineId), machine_get_snd_device(machineId)->internal_name, 0));
                        }
                        int row = Models::AddEntry(models[i], name, c);
                        scSound[i]->addDevice(sound_card_getdevice(c), name);

                        if (c == sound_card_current[i])
                            selectedRows[i] = row - removeRows_[i];
                    }
                }
            }
        }

        c++;
    }

    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
        models[i]->removeRows(0, removeRows_[i]);
        cbox[i]->setEnabled(models[i]->rowCount() > 1);
        cbox[i]->setCurrentIndex(-1);
        cbox[i]->setCurrentIndex(selectedRows[i]);
    }

    // FM
    c                  = 0;
    auto *modelFM      = ui->comboBoxFM->model();
    auto  removeRowsFM = modelFM->rowCount();
    selectedRow        = 0;

    int rowFM = Models::AddEntry(modelFM, tr("Nuked (more accurate)"), 0);
    if (fm_driver != FM_DRV_YMFM)
        selectedRow = rowFM - removeRowsFM;
    rowFM = Models::AddEntry(modelFM, tr("YMFM (faster)"), 1);
    if (fm_driver == FM_DRV_YMFM)
        selectedRow = rowFM - removeRowsFM;

    modelFM->removeRows(0, removeRowsFM);
    ui->comboBoxFM->setEnabled(modelFM->rowCount() > 0);
    ui->comboBoxFM->setCurrentIndex(-1);
    ui->comboBoxFM->setCurrentIndex(selectedRow);

    // Midi Out
    c                = 0;
    auto *model      = ui->comboBoxMidiOut->model();
    auto  removeRows = model->rowCount();
    selectedRow      = 0;

    while (true) {
        const QString name = DeviceConfig::DeviceName(midi_out_device_getdevice(c), midi_out_device_get_internal_name(c), 0);
        if (name.isEmpty())
            break;

        if (midi_out_device_available(c)) {
            int row = Models::AddEntry(model, name, c);
            scMidiOut->addDevice(nullptr, name);
            if (c == midi_output_device_current)
                selectedRow = row - removeRows;
        }

        c++;
    }

    model->removeRows(0, removeRows);
    ui->comboBoxMidiOut->setEnabled(model->rowCount() > 0);
    ui->comboBoxMidiOut->setCurrentIndex(-1);
    ui->comboBoxMidiOut->setCurrentIndex(selectedRow);

    // Midi In
    c           = 0;
    model       = ui->comboBoxMidiIn->model();
    removeRows  = model->rowCount();
    selectedRow = 0;

    while (true) {
        const QString name = DeviceConfig::DeviceName(midi_in_device_getdevice(c), midi_in_device_get_internal_name(c), 0);
        if (name.isEmpty())
            break;

        if (midi_in_device_available(c)) {
            int row = Models::AddEntry(model, name, c);
            scMidiIn->addDevice(nullptr, name);
            if (c == midi_input_device_current)
                selectedRow = row - removeRows;
        }

        c++;
    }

    model->removeRows(0, removeRows);
    ui->comboBoxMidiIn->setEnabled(model->rowCount() > 0);
    ui->comboBoxMidiIn->setCurrentIndex(-1);
    ui->comboBoxMidiIn->setCurrentIndex(selectedRow);

    // Standalone MPU401
    ui->checkBoxMPU401->setChecked(mpu401_standalone_enable > 0);

    // Float32 Sound
    ui->checkBoxFloat32->setChecked(sound_is_float > 0);

    // Audio Output Device
    auto *modelAudioOut      = ui->comboBoxAudioOutputDevice->model();
    auto  removeRowsAudioOut = modelAudioOut->rowCount();
    int   selectedAudioRow   = 0;

    int audioRow = Models::AddEntry(modelAudioOut, tr("System Default"), QString(""));
    if (sound_output_device[0] == '\0')
        selectedAudioRow = audioRow - removeRowsAudioOut;

    const char *devList = sound_get_output_devices();
    if (devList != nullptr) {
        const char *dev = devList;
        while (*dev != '\0') {
            QString devName = QString::fromUtf8(dev);
            audioRow        = Models::AddEntry(modelAudioOut, devName, devName);
            if (devName == QString(sound_output_device))
                selectedAudioRow = audioRow - removeRowsAudioOut;
            dev += strlen(dev) + 1;
        }
    }

    modelAudioOut->removeRows(0, removeRowsAudioOut);
    ui->comboBoxAudioOutputDevice->setCurrentIndex(-1);
    ui->comboBoxAudioOutputDevice->setCurrentIndex(selectedAudioRow);
}

static bool
allowMpu401(Ui::SettingsSound *ui)
{
    QString midiOut = midi_out_device_get_internal_name(ui->comboBoxMidiOut->currentData().toInt());
    QString midiIn  = midi_in_device_get_internal_name(ui->comboBoxMidiIn->currentData().toInt());

    if (midiOut.isEmpty())
        return false;

    if (midiOut == QStringLiteral("none") && midiIn == QStringLiteral("none"))
        return false;

    return true;
}

void
SettingsSound::on_comboBoxSoundCard1_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int sndCard = ui->comboBoxSoundCard1->currentData().toInt();

    if (sndCard == SOUND_INTERNAL)
        ui->pushButtonConfigureSoundCard1->setEnabled(machine_has_flags(machineId, MACHINE_SOUND) && device_has_config(machine_get_snd_device(machineId)));
    else
        ui->pushButtonConfigureSoundCard1->setEnabled(sound_card_has_config(sndCard));
}

void
SettingsSound::on_pushButtonConfigureSoundCard1_clicked()
{
    int   sndCard = ui->comboBoxSoundCard1->currentData().toInt();
    auto *device  = sound_card_getdevice(sndCard);

    if (sndCard == SOUND_INTERNAL)
        device = machine_get_snd_device(machineId);
    sound_card_cfg_changed[0] |= DeviceConfig::ConfigureDevice(device, 1);
}

void
SettingsSound::on_comboBoxSoundCard2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int sndCard = ui->comboBoxSoundCard2->currentData().toInt();

    ui->pushButtonConfigureSoundCard2->setEnabled(sound_card_has_config(sndCard));
}

void
SettingsSound::on_pushButtonConfigureSoundCard2_clicked()
{
    int             sndCard = ui->comboBoxSoundCard2->currentData().toInt();
    const device_t *device  = sound_card_getdevice(sndCard);
    sound_card_cfg_changed[1] |= DeviceConfig::ConfigureDevice(device, 2);
}

void
SettingsSound::on_comboBoxSoundCard3_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int sndCard = ui->comboBoxSoundCard3->currentData().toInt();

    ui->pushButtonConfigureSoundCard3->setEnabled(sound_card_has_config(sndCard));
}

void
SettingsSound::on_pushButtonConfigureSoundCard3_clicked()
{
    int             sndCard = ui->comboBoxSoundCard3->currentData().toInt();
    const device_t *device  = sound_card_getdevice(sndCard);

    sound_card_cfg_changed[2] |= DeviceConfig::ConfigureDevice(device, 3);
}

void
SettingsSound::on_comboBoxSoundCard4_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int sndCard = ui->comboBoxSoundCard4->currentData().toInt();

    ui->pushButtonConfigureSoundCard4->setEnabled(sound_card_has_config(sndCard));
}

void
SettingsSound::on_pushButtonConfigureSoundCard4_clicked()
{
    int             sndCard = ui->comboBoxSoundCard4->currentData().toInt();
    const device_t *device  = sound_card_getdevice(sndCard);

    sound_card_cfg_changed[3] |= DeviceConfig::ConfigureDevice(device, 4);
}

void
SettingsSound::on_comboBoxMidiOut_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureMidiOut->setEnabled(midi_out_device_has_config(ui->comboBoxMidiOut->currentData().toInt()));
    ui->checkBoxMPU401->setEnabled(allowMpu401(ui) && (machine_has_bus(machineId, MACHINE_BUS_ISA) || machine_has_bus(machineId, MACHINE_BUS_MCA)));
    ui->pushButtonConfigureMPU401->setEnabled(allowMpu401(ui) && ui->checkBoxMPU401->isChecked());
}

void
SettingsSound::on_pushButtonConfigureMidiOut_clicked()
{
    midi_output_device_cfg_changed |= DeviceConfig::ConfigureDevice(midi_out_device_getdevice(ui->comboBoxMidiOut->currentData().toInt()));
}

void
SettingsSound::on_comboBoxMidiIn_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureMidiIn->setEnabled(midi_in_device_has_config(ui->comboBoxMidiIn->currentData().toInt()));
    ui->checkBoxMPU401->setEnabled(allowMpu401(ui) && (machine_has_bus(machineId, MACHINE_BUS_ISA) || machine_has_bus(machineId, MACHINE_BUS_MCA)));
    ui->pushButtonConfigureMPU401->setEnabled(allowMpu401(ui) && ui->checkBoxMPU401->isChecked());
}

void
SettingsSound::on_pushButtonConfigureMidiIn_clicked()
{
    midi_input_device_cfg_changed |= DeviceConfig::ConfigureDevice(midi_in_device_getdevice(ui->comboBoxMidiIn->currentData().toInt()));
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
        mpu401_cfg_changed |= DeviceConfig::ConfigureDevice(&mpu401_mca_device);
    else
        mpu401_cfg_changed |= DeviceConfig::ConfigureDevice(&mpu401_device);
}
