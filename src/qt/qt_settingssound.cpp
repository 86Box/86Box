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
#include <vector>

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
    inMachineChange = true;
    ui->setupUi(this);

    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
        sound_card_cfg_changed[i] = 0;
        scSound[i]                = new SettingsCompleter(findChild<QComboBox *>(QString("comboBoxSoundCard%1").arg(i + 1)), nullptr);
        soundCardCurrent[i] = sound_card_current[i];
    }

    mpu401_cfg_changed             = 0;

    midi_output_device_cfg_changed = 0;
    midi_input_device_cfg_changed  = 0;

    midiOutCurrent = midi_output_device_current;
    midiInCurrent  = midi_input_device_current;
    mpu401Enabled  = mpu401_standalone_enable;

    scMidiOut              = new SettingsCompleter(ui->comboBoxMidiOut, nullptr);
    scMidiIn               = new SettingsCompleter(ui->comboBoxMidiIn, nullptr);

    // FM driver does not need to be changed with machine changes.
    {
        // FM
        auto *modelFM      = ui->comboBoxFM->model();
        auto  removeRowsFM = modelFM->rowCount();
        auto selectedRow   = 0;

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
    }

    populateAudioDevices(sound_backend_current);

    {
        auto *modelInRate      = ui->comboBoxAudioInputRate->model();
        auto  removeRowsInRate = modelInRate->rowCount();
        int   selectedInRate   = 0;
        const int inRates[2]   = { FREQ_44100, FREQ_48000 };

        Models::Batch rateRows(modelInRate);
        for (int i = 0; i < 2; i++) {
            int row = rateRows.add(tr("%1 Hz").arg(inRates[i]), inRates[i]);
            if (inRates[i] == sb_input_rate)
                selectedInRate = row - removeRowsInRate;
        }
        rateRows.commit();
        modelInRate->removeRows(0, removeRowsInRate);
        ui->comboBoxAudioInputRate->setCurrentIndex(-1);
        ui->comboBoxAudioInputRate->setCurrentIndex(selectedInRate);
    }

    ui->comboBoxAudioInputRate->setToolTip(
        tr("Rate the host input capture device is opened at.\n\n"
           "44100 divides evenly by SB16/AWE32 rates of 44.1/22/11 kHz."));

    ui->checkBoxSoundInput->setChecked(sound_input_enabled > 0);

    onCurrentMachineChanged(machine);
}

void
SettingsSound::populateAudioDevices(const int backend)
{
    /* The selected devices only mean something to the backend they were selected for. */
    const bool    current = (backend == sound_backend_current);
    const QString inName  = current ? QString(sound_input_dev_name) : QString();

    {
        // Audio Input Device
        auto *modelAudioIn      = ui->comboBoxAudioInputDevice->model();
        auto  removeRowsAudioIn = modelAudioIn->rowCount();
        int   selectedInputRow  = 0;

        Models::Batch inputRows(modelAudioIn);
        int inputRow = inputRows.add(tr("System Default"), QString(""));
        if (inName.isEmpty())
            selectedInputRow = inputRow - removeRowsAudioIn;

        bool inputDevMatched = (inName.isEmpty());

        const char *inDevList = sound_backend_get_input_devices_of(backend);
        if (inDevList != nullptr) {
            const char *dev = inDevList;
            while (*dev != '\0') {
                QString devName = QString::fromUtf8(dev);
                inputRow        = inputRows.add(devName, devName);
                if (devName == inName) {
                    selectedInputRow = inputRow - removeRowsAudioIn;
                    inputDevMatched  = true;
                }
                dev += strlen(dev) + 1;
            }
        }

        if (!inputDevMatched) {
            inputRow         = inputRows.add(tr("%1 (not detected)").arg(inName),
                                             inName);
            selectedInputRow = inputRow - removeRowsAudioIn;
        }
        inputRows.commit();

        modelAudioIn->removeRows(0, removeRowsAudioIn);
        ui->comboBoxAudioInputDevice->setCurrentIndex(-1);
        ui->comboBoxAudioInputDevice->setCurrentIndex(selectedInputRow);
    }
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

    soft_changed |= (sound_input_enabled         != (ui->checkBoxSoundInput->isChecked() ? 1 : 0));
    soft_changed |= (QString(sound_input_dev_name) != ui->comboBoxAudioInputDevice->currentData().toString());
    soft_changed |= (sb_input_rate != ui->comboBoxAudioInputRate->currentData().toInt());

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

int
SettingsSound::soundCard(int i) const
{
    const QComboBox *cbox = findChild<QComboBox *>(QString("comboBoxSoundCard%1").arg(i + 1));

    return cbox ? cbox->currentData().toInt() : 0;
}

void
SettingsSound::save(int soft)
{
    midi_output_device_current = ui->comboBoxMidiOut->currentData().toInt();

    midi_input_device_current = ui->comboBoxMidiIn->currentData().toInt();

    const bool inputMoved  = (QString(sound_input_dev_name)
                              != ui->comboBoxAudioInputDevice->currentData().toString())
                          || (sound_input_enabled != (ui->checkBoxSoundInput->isChecked() ? 1 : 0))
                          || (sb_input_rate != ui->comboBoxAudioInputRate->currentData().toInt());

    sound_input_enabled = ui->checkBoxSoundInput->isChecked() ? 1 : 0;

    QByteArray inDevName = ui->comboBoxAudioInputDevice->currentData().toString().toUtf8();
    strncpy(sound_input_dev_name, inDevName.constData(), sizeof(sound_input_dev_name) - 1);
    sound_input_dev_name[sizeof(sound_input_dev_name) - 1] = '\0';

    sb_input_rate = ui->comboBoxAudioInputRate->currentData().toInt();
    if (sb_input_rate != FREQ_44100 && sb_input_rate != FREQ_48000)
        sb_input_rate = FREQ_44100;

    if (soft) {
        if (inputMoved)
            sound_reopen_input();
        return;
    }

    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
        QComboBox *cbox       = findChild<QComboBox *>(QString("comboBoxSoundCard%1").arg(i + 1));
        sound_card_current[i] = cbox->currentData().toInt();
    }

    fm_driver = ui->comboBoxFM->currentData().toInt();

    mpu401_standalone_enable = ui->checkBoxMPU401->isChecked() ? 1 : 0;

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
SettingsSound::onCurrentMachineChanged(const int machineId)
{
    inMachineChange = true;
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

    std::vector<Models::Batch> rows(models, models + SOUND_CARD_MAX);
    for (const auto &card : Models::Devices(sound_card_getdevice, sound_card_get_internal_name, sound_card_available, 1)) {
        c = card.id;
        if (card.available && device_is_valid(card.dev, machineId)) {
            QString name = card.name;

            for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
                if ((c != 1) || ((i == 0) && m_has_snd)) {
                    if (i == 0 && c == 1 && m_has_snd && machine_get_snd_device(machineId)) {
                        name += QString(" (%1)").arg(DeviceConfig::DeviceName(machine_get_snd_device(machineId), machine_get_snd_device(machineId)->internal_name, 0));
                    }
                    int row = rows[i].add(name, c);
                    scSound[i]->addDevice(card.dev, name);

                    if (c == soundCardCurrent[i])
                        selectedRows[i] = row - removeRows_[i];
                }
            }
        }
    }

    for (uint8_t i = 0; i < SOUND_CARD_MAX; ++i) {
        rows[i].commit();
        models[i]->removeRows(0, removeRows_[i]);
        cbox[i]->setEnabled(models[i]->rowCount() > 1);
        cbox[i]->setCurrentIndex(-1);
        cbox[i]->setCurrentIndex(selectedRows[i]);
    }

    // Midi Out
    auto *model      = ui->comboBoxMidiOut->model();
    auto  removeRows = model->rowCount();
    selectedRow      = 0;

    Models::Batch midiOutRows(model);
    for (const auto &midi : Models::Devices(midi_out_device_getdevice, midi_out_device_get_internal_name,
                                            midi_out_device_available, 0)) {
        if (midi.available) {
            int row = midiOutRows.add(midi.name, midi.id);
            scMidiOut->addDevice(nullptr, midi.name);
            if (midi.id == midiOutCurrent)
                selectedRow = row - removeRows;
        }
    }
    midiOutRows.commit();

    model->removeRows(0, removeRows);
    ui->comboBoxMidiOut->setEnabled(model->rowCount() > 0);
    ui->comboBoxMidiOut->setCurrentIndex(-1);
    ui->comboBoxMidiOut->setCurrentIndex(selectedRow);

    // Midi In
    model       = ui->comboBoxMidiIn->model();
    removeRows  = model->rowCount();
    selectedRow = 0;

    Models::Batch midiInRows(model);
    for (const auto &midi : Models::Devices(midi_in_device_getdevice, midi_in_device_get_internal_name,
                                            midi_in_device_available, 0)) {
        if (midi.available) {
            int row = midiInRows.add(midi.name, midi.id);
            scMidiIn->addDevice(nullptr, midi.name);
            if (midi.id == midiInCurrent)
                selectedRow = row - removeRows;
        }
    }
    midiInRows.commit();

    model->removeRows(0, removeRows);
    ui->comboBoxMidiIn->setEnabled(model->rowCount() > 0);
    ui->comboBoxMidiIn->setCurrentIndex(-1);
    ui->comboBoxMidiIn->setCurrentIndex(selectedRow);

    // Standalone MPU401
    ui->checkBoxMPU401->setChecked(mpu401Enabled > 0 && allowMpu401(ui));

    updateSoundInputEnabled();

    mpu401Enabled = ui->checkBoxMPU401->isChecked();
    midiInCurrent       = ui->comboBoxMidiIn->currentData().toInt();
    midiOutCurrent      = ui->comboBoxMidiOut->currentData().toInt();
    soundCardCurrent[0] = ui->comboBoxSoundCard1->currentData().toInt();
    soundCardCurrent[1] = ui->comboBoxSoundCard2->currentData().toInt();
    soundCardCurrent[2] = ui->comboBoxSoundCard3->currentData().toInt();
    soundCardCurrent[3] = ui->comboBoxSoundCard4->currentData().toInt();
    inMachineChange = false;
}

void
SettingsSound::updateSoundInputEnabled()
{
    bool hasInput = false;

    for (int i = 1; i <= 4; i++) {
        auto *cbox = findChild<QComboBox *>(QString("comboBoxSoundCard%1").arg(i));
        if (cbox == nullptr)
            continue;

        const int sndCard = cbox->currentData().toInt();
        if (sndCard == SOUND_INTERNAL) {
            const device_t *dev = machine_get_snd_device(machineId);
            if (machine_has_flags(machineId, MACHINE_SOUND) && (dev != nullptr) &&
                (dev->flags & DEVICE_AUDIO_IN))
                hasInput = true;
        } else if (sound_card_has_input(sndCard)) {
            hasInput = true;
        }
    }

    ui->checkBoxSoundInput->setEnabled(hasInput);
    ui->labelAudioInputDevice->setEnabled(hasInput);
    ui->labelAudioInputRate->setEnabled(hasInput);

    const bool on = hasInput && ui->checkBoxSoundInput->isChecked();
    ui->comboBoxAudioInputDevice->setEnabled(on);
    ui->comboBoxAudioInputRate->setEnabled(on);
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
    if (!inMachineChange)
        soundCardCurrent[0] = sndCard;
    updateSoundInputEnabled();
}

void
SettingsSound::on_pushButtonConfigureSoundCard1_clicked()
{
    int   sndCard = ui->comboBoxSoundCard1->currentData().toInt();
    auto *device  = sound_card_getdevice(sndCard);

    if (sndCard == SOUND_INTERNAL) {
        device = machine_get_snd_device(machineId);
        sound_card_cfg_changed[0] |= DeviceConfig::ConfigureDevice(device);
    } else
        sound_card_cfg_changed[0] |= DeviceConfig::ConfigureDevice(device, 1);

}

void
SettingsSound::on_comboBoxSoundCard2_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    int sndCard = ui->comboBoxSoundCard2->currentData().toInt();

    ui->pushButtonConfigureSoundCard2->setEnabled(sound_card_has_config(sndCard));
    if (!inMachineChange)
        soundCardCurrent[1] = sndCard;
    updateSoundInputEnabled();
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
    if (!inMachineChange)
        soundCardCurrent[2] = sndCard;
    updateSoundInputEnabled();
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
    if (!inMachineChange)
        soundCardCurrent[3] = sndCard;
    updateSoundInputEnabled();
}

void
SettingsSound::on_pushButtonConfigureSoundCard4_clicked()
{
    int             sndCard = ui->comboBoxSoundCard4->currentData().toInt();
    const device_t *device  = sound_card_getdevice(sndCard);

    sound_card_cfg_changed[3] |= DeviceConfig::ConfigureDevice(device, 4);
}

void
SettingsSound::on_checkBoxSoundInput_stateChanged(int state)
{
    (void) state;
    updateSoundInputEnabled();
}

void
SettingsSound::on_comboBoxMidiOut_currentIndexChanged(int index)
{
    if (index < 0)
        return;

    ui->pushButtonConfigureMidiOut->setEnabled(midi_out_device_has_config(ui->comboBoxMidiOut->currentData().toInt()));
    ui->checkBoxMPU401->setEnabled(allowMpu401(ui) && (machine_has_bus(machineId, MACHINE_BUS_ISA) || machine_has_bus(machineId, MACHINE_BUS_MCA) || machine_has_bus(machineId, MACHINE_PC98)));
    ui->pushButtonConfigureMPU401->setEnabled(allowMpu401(ui) && ui->checkBoxMPU401->isChecked());

    if (!inMachineChange)
        midiOutCurrent = ui->comboBoxMidiOut->currentData().toInt();
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
    ui->checkBoxMPU401->setEnabled(allowMpu401(ui) && (machine_has_bus(machineId, MACHINE_BUS_ISA) || machine_has_bus(machineId, MACHINE_BUS_MCA) || machine_has_bus(machineId, MACHINE_PC98)));
    ui->pushButtonConfigureMPU401->setEnabled(allowMpu401(ui) && ui->checkBoxMPU401->isChecked());

    if (!inMachineChange)
        midiInCurrent = ui->comboBoxMidiIn->currentData().toInt();
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
    mpu401Enabled = (state == Qt::Checked);
}

void
SettingsSound::on_pushButtonConfigureMPU401_clicked()
{
    if (machine_has_bus(machineId, MACHINE_PC98) > 0)
        mpu401_cfg_changed |= DeviceConfig::ConfigureDevice(&mpu401_pc98_device);
    else if (machine_has_bus(machineId, MACHINE_BUS_MCA) > 0)
        mpu401_cfg_changed |= DeviceConfig::ConfigureDevice(&mpu401_mca_device);
    else
        mpu401_cfg_changed |= DeviceConfig::ConfigureDevice(&mpu401_device);
}
