/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Sound preferences UI module, shared by the emulator and the
 *          VM manager.
 */
#include "qt_preferencessound.hpp"
#include "ui_qt_preferencessound.h"

#include <cstring>

extern "C" {
#include <86box/86box.h>
#include <86box/sound.h>
}

PreferencesSound::PreferencesSound(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesSound)
{
    ui->setupUi(this);

    populatingBackend = true;
    for (int i = 0; i < sound_backend_get_count(); i++) {
        ui->comboBoxSoundBackend->addItem(QString::fromUtf8(sound_backend_get_name(i)), i);
        if (i == sound_backend_current)
            ui->comboBoxSoundBackend->setCurrentIndex(ui->comboBoxSoundBackend->count() - 1);
    }
    populatingBackend = false;
    populateAudioOutputDevices(sound_backend_current);
}

PreferencesSound::~PreferencesSound()
{
    delete ui;
}

void
PreferencesSound::save()
{
    const int      backend   = ui->comboBoxSoundBackend->currentData().toInt();
    const QString  outDev    = ui->comboBoxAudioOutputDevice->currentData().toString();
    const bool     outMoved  = (backend != sound_backend_current) || (outDev != QString(sound_output_device));

    sound_backend_current = backend;
    qstrncpy(sound_output_device, outDev.toUtf8().constData(), sizeof(sound_output_device));

    /* The manager has no sound running, only the emulator needs to switch. */
    if (outMoved && sound_output_is_open())
        sound_reopen_output();
}

void
PreferencesSound::populateAudioOutputDevices(const int backend)
{
    /* The selected device only means something to the backend it was selected for. */
    const QString outName = (backend == sound_backend_current) ? QString(sound_output_device) : QString();

    ui->comboBoxAudioOutputDevice->clear();
    ui->comboBoxAudioOutputDevice->addItem(tr("System Default"), QString(""));
    int selected = 0;

    const char *devList = sound_backend_get_output_devices_of(backend);
    if (devList != nullptr) {
        for (const char *dev = devList; *dev != '\0';) {
            const size_t devLen = strnlen(dev, 1024);
            const QString devName = QString::fromUtf8(dev, static_cast<qsizetype>(devLen));
            ui->comboBoxAudioOutputDevice->addItem(devName, devName);
            if (devName == outName)
                selected = ui->comboBoxAudioOutputDevice->count() - 1;
            dev += devLen + 1;
        }
    }

    ui->comboBoxAudioOutputDevice->setCurrentIndex(selected);
}

void
PreferencesSound::on_comboBoxSoundBackend_currentIndexChanged(int index)
{
    if ((index < 0) || populatingBackend)
        return;

    populateAudioOutputDevices(ui->comboBoxSoundBackend->currentData().toInt());
}
